package sqlitequeue

import (
	"context"
	"database/sql"
	"path/filepath"
	"testing"
	"time"
)

func TestWritersQueuedReadersIndependent(t *testing.T) {
	path := filepath.Join(t.TempDir(), "queue.db")
	db, err := sql.Open(DriverName(path), path+"?_journal_mode=WAL&_txlock=immediate&_busy_timeout=0")
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	db.SetMaxOpenConns(4)
	if _, err = db.Exec("CREATE TABLE items(id INTEGER PRIMARY KEY,value INTEGER)"); err != nil {
		t.Fatal(err)
	}
	// Warm a separate reader before reserving the writer.
	reader, err := db.Conn(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	defer reader.Close()
	tx, err := db.Begin()
	if err != nil {
		t.Fatal(err)
	}
	if _, err = tx.Exec("INSERT INTO items(value) VALUES(1)"); err != nil {
		t.Fatal(err)
	}
	var count int
	if err = reader.QueryRowContext(context.Background(), "SELECT COUNT(*) FROM items").Scan(&count); err != nil || count != 0 {
		t.Fatal(count, err)
	}
	result := make(chan error, 1)
	go func() { _, err := db.Exec("INSERT INTO items(value) VALUES(2)"); result <- err }()
	select {
	case err := <-result:
		t.Fatalf("writer did not wait: %v", err)
	case <-time.After(50 * time.Millisecond):
	}
	if err = tx.Rollback(); err != nil {
		t.Fatal(err)
	}
	select {
	case err := <-result:
		if err != nil {
			t.Fatal(err)
		}
	case <-time.After(time.Second):
		t.Fatal("writer never released")
	}
	var value int
	if err = db.QueryRow("SELECT value FROM items").Scan(&value); err != nil || value != 2 {
		t.Fatal(value, err)
	}
	dedicated, err := db.Conn(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if _, err = dedicated.ExecContext(context.Background(), "BEGIN IMMEDIATE"); err != nil {
		t.Fatal(err)
	}
	blocked, stop := context.WithTimeout(context.Background(), 50*time.Millisecond)
	if _, err = db.ExecContext(blocked, "INSERT INTO items(value) VALUES(9)"); err == nil {
		t.Fatal("manual transaction did not retain admission")
	}
	stop()
	if _, err = dedicated.ExecContext(context.Background(), "ROLLBACK"); err != nil {
		t.Fatal(err)
	}
	dedicated.Close()
	// Prepared RETURNING must retain its reservation until the returned rows close.
	stmt, err := db.Prepare("INSERT INTO items(value) VALUES(?) RETURNING id")
	if err != nil {
		t.Fatal(err)
	}
	defer stmt.Close()
	rows, err := stmt.Query(3)
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
	defer cancel()
	if _, err = db.ExecContext(ctx, "INSERT INTO items(value) VALUES(4)"); err == nil {
		t.Fatal("writer bypassed open RETURNING rows")
	}
	rows.Close()
	if _, err = db.Exec("INSERT INTO items(value) VALUES(5)"); err != nil {
		t.Fatal(err)
	}
}

func TestPriorityAndCancellation(t *testing.T) {
	g := &gate{}
	release, err := g.acquire(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if _, err := g.acquire(ctx); err == nil {
		t.Fatal("cancelled admission accepted")
	}
	order := make(chan int, 2)
	for _, priority := range []int{-10, 10} {
		go func(p int) {
			release, err := g.acquire(Priority(context.Background(), p))
			if err != nil {
				order <- 0
				return
			}
			order <- p
			release()
		}(priority)
	}
	deadline := time.Now().Add(time.Second)
	for {
		g.mu.Lock()
		n := len(g.waiting)
		g.mu.Unlock()
		if n == 2 {
			break
		}
		if time.Now().After(deadline) {
			t.Fatal("waiters not queued")
		}
		time.Sleep(time.Millisecond)
	}
	release()
	if first := <-order; first != 10 {
		t.Fatalf("priority order: %d", first)
	}
	if second := <-order; second != -10 {
		t.Fatal(second)
	}
}
