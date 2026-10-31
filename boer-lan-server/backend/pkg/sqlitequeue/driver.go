// Package sqlitequeue serializes SQLite writers while leaving readers concurrent.
package sqlitequeue

import (
	"context"
	"database/sql"
	"database/sql/driver"
	"errors"
	"io"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/mattn/go-sqlite3"
)

type priorityKey struct{}

func Priority(ctx context.Context, priority int) context.Context {
	return context.WithValue(ctx, priorityKey{}, priority)
}

type waiter struct {
	ready    chan struct{}
	priority int
}
type gate struct {
	mu      sync.Mutex
	active  bool
	burst   int
	waiting []*waiter
}

func (g *gate) acquire(ctx context.Context) (func(), error) {
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	waiting, cancel := context.WithTimeout(ctx, 10*time.Second)
	defer cancel()
	release, err := g.admit(waiting)
	if err != nil && ctx.Err() == nil && errors.Is(err, context.DeadlineExceeded) {
		err = sqlite3.Error{Code: sqlite3.ErrBusy}
	}
	return release, err
}
func (g *gate) admit(ctx context.Context) (func(), error) {
	g.mu.Lock()
	if !g.active {
		g.active = true
		g.mu.Unlock()
		return g.release, nil
	}
	if len(g.waiting) >= 1024 {
		g.mu.Unlock()
		return nil, sqlite3.Error{Code: sqlite3.ErrBusy}
	}
	priority, _ := ctx.Value(priorityKey{}).(int)
	w := &waiter{make(chan struct{}), priority}
	g.waiting = append(g.waiting, w)
	g.mu.Unlock()
	select {
	case <-w.ready:
		return g.release, nil
	case <-ctx.Done():
		g.mu.Lock()
		for i, v := range g.waiting {
			if v == w {
				g.waiting = append(g.waiting[:i], g.waiting[i+1:]...)
				g.mu.Unlock()
				return nil, ctx.Err()
			}
		}
		g.mu.Unlock()
		g.release()
		return nil, ctx.Err()
	}
}
func (g *gate) release() {
	g.mu.Lock()
	defer g.mu.Unlock()
	if len(g.waiting) == 0 {
		g.active = false
		return
	}
	best := 0
	for i, w := range g.waiting {
		if w.priority > g.waiting[best].priority {
			best = i
		}
	}
	if g.burst >= 16 {
		for i, w := range g.waiting {
			if w.priority < g.waiting[best].priority {
				best = i
				break
			}
		}
	}
	if g.waiting[best].priority > 0 {
		g.burst++
	} else {
		g.burst = 0
	}
	w := g.waiting[best]
	g.waiting = append(g.waiting[:best], g.waiting[best+1:]...)
	close(w.ready)
}

var sequence atomic.Uint64
var gates sync.Map

// DriverName shares admission across pools opening the same database file.
func DriverName(path string) string {
	key := strings.SplitN(path, "?", 2)[0]
	v, _ := gates.LoadOrStore(key, &gate{})
	name := "boer-sqlite-writer-" + strconv.FormatUint(sequence.Add(1), 10)
	sql.Register(name, &queuedDriver{gate: v.(*gate)})
	return name
}

type queuedDriver struct{ gate *gate }

func (d *queuedDriver) Open(name string) (driver.Conn, error) {
	release, err := d.gate.acquire(context.Background())
	if err != nil {
		return nil, err
	}
	defer release()
	c, err := (&sqlite3.SQLiteDriver{}).Open(name)
	if err != nil {
		return nil, err
	}
	return &conn{SQLiteConn: c.(*sqlite3.SQLiteConn), gate: d.gate}, nil
}

type conn struct {
	*sqlite3.SQLiteConn
	gate    *gate
	tx      bool
	release func()
}

func (c *conn) lock(ctx context.Context, query string) (func(), error) {
	if c.tx || readOnly(query) {
		return func() {}, nil
	}
	return c.gate.acquire(ctx)
}
func readOnly(query string) bool {
	q := strings.ToUpper(strings.TrimSpace(query))
	return strings.HasPrefix(q, "SELECT ") || strings.HasPrefix(q, "SELECT\n") || strings.HasPrefix(q, "EXPLAIN ")
}
func (c *conn) Begin() (driver.Tx, error) { return c.BeginTx(context.Background(), driver.TxOptions{}) }
func (c *conn) BeginTx(ctx context.Context, opts driver.TxOptions) (driver.Tx, error) {
	release, err := c.gate.acquire(ctx)
	if err != nil {
		return nil, err
	}
	tx, err := c.SQLiteConn.BeginTx(ctx, opts)
	if err != nil {
		release()
		return nil, err
	}
	c.tx = true
	c.release = release
	return &transaction{Tx: tx, conn: c}, nil
}
func (c *conn) Close() error {
	err := c.SQLiteConn.Close()
	if c.release != nil {
		c.release()
		c.release = nil
		c.tx = false
	}
	return err
}

type transaction struct {
	driver.Tx
	conn *conn
}

func (t *transaction) finish() {
	if t.conn.release != nil {
		t.conn.release()
		t.conn.release = nil
		t.conn.tx = false
	}
}
func (t *transaction) Commit() error   { defer t.finish(); return t.Tx.Commit() }
func (t *transaction) Rollback() error { defer t.finish(); return t.Tx.Rollback() }
func (c *conn) ExecContext(ctx context.Context, q string, args []driver.NamedValue) (driver.Result, error) {
	release, err := c.lock(ctx, q)
	if err != nil {
		return nil, err
	}
	result, err := c.SQLiteConn.ExecContext(ctx, q, args)
	command := strings.ToUpper(strings.TrimSpace(q))
	if err == nil && !c.tx && (command == "BEGIN" || strings.HasPrefix(command, "BEGIN ")) {
		c.tx = true
		c.release = release
		return result, nil
	}
	release()
	if err == nil && c.tx && (command == "COMMIT" || command == "COMMIT;" || command == "END" || command == "ROLLBACK" || command == "ROLLBACK;") {
		c.release()
		c.release = nil
		c.tx = false
	}
	return result, err
}

func (c *conn) QueryContext(ctx context.Context, q string, args []driver.NamedValue) (driver.Rows, error) {
	release, err := c.lock(ctx, q)
	if err != nil {
		return nil, err
	}
	rows, err := c.SQLiteConn.QueryContext(ctx, q, args)
	if err != nil {
		release()
		return nil, err
	}
	return &resultRows{Rows: rows, release: release}, nil
}
func (c *conn) Prepare(q string) (driver.Stmt, error) {
	return c.PrepareContext(context.Background(), q)
}
func (c *conn) PrepareContext(ctx context.Context, q string) (driver.Stmt, error) {
	s, err := c.SQLiteConn.PrepareContext(ctx, q)
	if err != nil {
		return nil, err
	}
	return &statement{Stmt: s, conn: c, query: q}, nil
}

type statement struct {
	driver.Stmt
	conn  *conn
	query string
}

func (s *statement) Exec(args []driver.Value) (driver.Result, error) {
	release, err := s.conn.lock(context.Background(), s.query)
	if err != nil {
		return nil, err
	}
	defer release()
	return s.Stmt.Exec(args)
}
func (s *statement) Query(args []driver.Value) (driver.Rows, error) {
	release, err := s.conn.lock(context.Background(), s.query)
	if err != nil {
		return nil, err
	}
	r, err := s.Stmt.Query(args)
	if err != nil {
		release()
		return nil, err
	}
	return &resultRows{Rows: r, release: release}, nil
}
func (s *statement) ExecContext(ctx context.Context, args []driver.NamedValue) (driver.Result, error) {
	release, err := s.conn.lock(ctx, s.query)
	if err != nil {
		return nil, err
	}
	defer release()
	return s.Stmt.(driver.StmtExecContext).ExecContext(ctx, args)
}
func (s *statement) QueryContext(ctx context.Context, args []driver.NamedValue) (driver.Rows, error) {
	release, err := s.conn.lock(ctx, s.query)
	if err != nil {
		return nil, err
	}
	r, err := s.Stmt.(driver.StmtQueryContext).QueryContext(ctx, args)
	if err != nil {
		release()
		return nil, err
	}
	return &resultRows{Rows: r, release: release}, nil
}

type resultRows struct {
	driver.Rows
	release func()
}

func (r *resultRows) Close() error {
	err := r.Rows.Close()
	if r.release != nil {
		r.release()
		r.release = nil
	}
	return err
}
func (r *resultRows) Next(values []driver.Value) error {
	err := r.Rows.Next(values)
	if errors.Is(err, io.EOF) {
		r.Close()
	}
	return err
}

// Preserve column metadata used by GORM migrations and type inference.
func (r *resultRows) ColumnTypeDatabaseTypeName(i int) string {
	if v, ok := r.Rows.(driver.RowsColumnTypeDatabaseTypeName); ok {
		return v.ColumnTypeDatabaseTypeName(i)
	}
	return ""
}
func (r *resultRows) ColumnTypeNullable(i int) (bool, bool) {
	if v, ok := r.Rows.(driver.RowsColumnTypeNullable); ok {
		return v.ColumnTypeNullable(i)
	}
	return false, false
}
