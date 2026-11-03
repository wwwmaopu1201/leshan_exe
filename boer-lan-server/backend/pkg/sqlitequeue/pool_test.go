package sqlitequeue

import (
	"context"
	"database/sql"
	"errors"
	"path/filepath"
	"testing"
	"time"

	"gorm.io/driver/sqlite"
	"gorm.io/gorm"
)

func TestPoolReadersSurviveQueuedWriters(t *testing.T) {
	p, err := OpenPool(filepath.Join(t.TempDir(), "pool.db") + "?_journal_mode=WAL&_txlock=immediate&_busy_timeout=5000")
	if err != nil {
		t.Fatal(err)
	}
	defer p.Close()
	db, err := gorm.Open(sqlite.Dialector{Conn: p}, &gorm.Config{})
	if err != nil {
		t.Fatal(err)
	}
	if err := db.Exec("CREATE TABLE items(id INTEGER)").Error; err != nil {
		t.Fatal(err)
	}
	tx := db.Begin()
	if tx.Error != nil {
		t.Fatal(tx.Error)
	}
	defer tx.Rollback()
	if err := tx.Exec("INSERT INTO items VALUES(0)").Error; err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	done := make(chan error, 32)
	for i := 0; i < 32; i++ {
		go func() {
			done <- db.WithContext(ctx).Transaction(func(tx *gorm.DB) error {
				return tx.Exec("INSERT INTO items VALUES(1)").Error
			})
		}()
	}
	deadline := time.Now().Add(time.Second)
	for {
		p.queue.mu.Lock()
		waiting := len(p.queue.waiting)
		p.queue.mu.Unlock()
		if waiting == 32 {
			break
		}
		if time.Now().After(deadline) {
			t.Fatal("writers did not queue")
		}
		time.Sleep(time.Millisecond)
	}
	if n := p.writers.Stats().InUse; n != 1 {
		t.Fatalf("waiting writers took connections: %d", n)
	}
	readCtx, stop := context.WithTimeout(context.Background(), 500*time.Millisecond)
	defer stop()
	var count int
	if err := db.WithContext(readCtx).Raw("SELECT COUNT(*) FROM items").Scan(&count).Error; err != nil {
		t.Fatal(err)
	}
	if count != 0 {
		t.Fatalf("read uncommitted write: %d", count)
	}
	if err := tx.Commit().Error; err != nil {
		t.Fatal(err)
	}
	for i := 0; i < 32; i++ {
		if err := <-done; err != nil {
			t.Fatal(err)
		}
	}
	if err := db.Raw("SELECT COUNT(*) FROM items").Scan(&count).Error; err != nil || count != 33 {
		t.Fatalf("lost writes: count=%d err=%v", count, err)
	}
}

func TestPoolCancelledTransactionReleasesWriter(t *testing.T) {
	p, err := OpenPool(filepath.Join(t.TempDir(), "cancel.db") + "?_journal_mode=WAL&_txlock=immediate")
	if err != nil {
		t.Fatal(err)
	}
	defer p.Close()
	ctx, cancel := context.WithCancel(context.Background())
	if _, err := p.BeginTx(ctx, nil); err != nil {
		t.Fatal(err)
	}
	cancel()
	next, stop := context.WithTimeout(context.Background(), time.Second)
	defer stop()
	tx, err := p.BeginTx(next, &sql.TxOptions{})
	if err != nil {
		t.Fatalf("cancelled writer blocked next transaction: %v", err)
	}
	if err := tx.(gorm.TxCommitter).Rollback(); err != nil && !errors.Is(err, sql.ErrTxDone) {
		t.Fatal(err)
	}
}
