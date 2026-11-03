package sqlitequeue

import (
	"context"
	"database/sql"
	"errors"
	"sync"

	"gorm.io/gorm"
)

// Pool keeps waiting writers outside database/sql connections. Reads have their
// own pool, so a busy device or history repair cannot exhaust query connections.
type Pool struct {
	readers *sql.DB
	writers *sql.DB
	queue   gate
}

func OpenPool(dsn string) (*Pool, error) {
	writers, err := sql.Open("sqlite3", dsn)
	if err != nil {
		return nil, err
	}
	writers.SetMaxOpenConns(1)
	writers.SetMaxIdleConns(1)
	// Initialize WAL before opening independent readers.
	if err := writers.Ping(); err != nil {
		writers.Close()
		return nil, err
	}
	readers, err := sql.Open("sqlite3", dsn)
	if err != nil {
		writers.Close()
		return nil, err
	}
	readers.SetMaxOpenConns(8)
	readers.SetMaxIdleConns(8)
	return &Pool{readers: readers, writers: writers}, nil
}

func (p *Pool) Close() error {
	return errors.Join(p.readers.Close(), p.writers.Close())
}

// Raw DB access uses the single writer pool; normal GORM reads use readers.
func (p *Pool) GetDBConn() (*sql.DB, error) { return p.writers, nil }
func (p *Pool) Ping() error                 { return p.writers.Ping() }

func (p *Pool) target(query string) *sql.DB {
	if readOnly(query) {
		return p.readers
	}
	return p.writers
}

func (p *Pool) PrepareContext(ctx context.Context, query string) (*sql.Stmt, error) {
	return p.target(query).PrepareContext(ctx, query)
}

func (p *Pool) ExecContext(ctx context.Context, query string, args ...interface{}) (sql.Result, error) {
	if readOnly(query) {
		return p.readers.ExecContext(ctx, query, args...)
	}
	release, err := p.queue.acquire(ctx)
	if err != nil {
		return nil, err
	}
	defer release()
	return p.writers.ExecContext(ctx, query, args...)
}

func (p *Pool) QueryContext(ctx context.Context, query string, args ...interface{}) (*sql.Rows, error) {
	// RETURNING and PRAGMA stay on the one writer connection until rows close.
	return p.target(query).QueryContext(ctx, query, args...)
}

func (p *Pool) QueryRowContext(ctx context.Context, query string, args ...interface{}) *sql.Row {
	return p.target(query).QueryRowContext(ctx, query, args...)
}

func (p *Pool) BeginTx(ctx context.Context, opts *sql.TxOptions) (gorm.ConnPool, error) {
	release, err := p.queue.acquire(ctx)
	if err != nil {
		return nil, err
	}
	tx, err := p.writers.BeginTx(ctx, opts)
	if err != nil {
		release()
		return nil, err
	}
	result := &poolTransaction{Tx: tx, release: release, done: make(chan struct{})}
	if ctx.Done() != nil {
		go func() {
			select {
			case <-ctx.Done():
				result.Rollback()
			case <-result.done:
			}
		}()
	}
	return result, nil
}

type poolTransaction struct {
	*sql.Tx
	release func()
	once    sync.Once
	done    chan struct{}
}

func (t *poolTransaction) finish() {
	t.once.Do(func() { t.release(); close(t.done) })
}

func (t *poolTransaction) Commit() error   { defer t.finish(); return t.Tx.Commit() }
func (t *poolTransaction) Rollback() error { defer t.finish(); return t.Tx.Rollback() }
