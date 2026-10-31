package dbretry

import (
	"context"
	"database/sql"
	"errors"
	_ "github.com/mattn/go-sqlite3"
	"path/filepath"
	"testing"
	"time"
)

func TestRealSQLiteLockRecovers(t *testing.T) {
	db, err := sql.Open("sqlite3", filepath.Join(t.TempDir(), "retry.db")+"?_busy_timeout=0&_journal_mode=WAL")
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	if _, err = db.Exec("CREATE TABLE values_test(value INTEGER)"); err != nil {
		t.Fatal(err)
	}
	blocker, err := db.Begin()
	if err != nil {
		t.Fatal(err)
	}
	if _, err = blocker.Exec("INSERT INTO values_test VALUES(1)"); err != nil {
		t.Fatal(err)
	}
	unlocked := make(chan struct{})
	go func() { time.Sleep(150 * time.Millisecond); blocker.Rollback(); close(unlocked) }()
	attempts := 0
	err = Do(context.Background(), func() error { attempts++; _, err := db.Exec("INSERT INTO values_test VALUES(2)"); return err })
	<-unlocked
	if err != nil || attempts < 2 {
		t.Fatalf("attempts=%d err=%v", attempts, err)
	}
	var count int
	db.QueryRow("SELECT COUNT(*) FROM values_test").Scan(&count)
	if count != 1 {
		t.Fatalf("count=%d", count)
	}
}

func TestPermanentFailureAndCancellation(t *testing.T) {
	permanent := errors.New("disk full")
	attempts := 0
	if err := Do(context.Background(), func() error { attempts++; return permanent }); !errors.Is(err, permanent) || attempts != 1 {
		t.Fatal(err, attempts)
	}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if err := Do(ctx, func() error { t.Fatal("operation after cancellation"); return nil }); !errors.Is(err, context.Canceled) {
		t.Fatal(err)
	}
}
