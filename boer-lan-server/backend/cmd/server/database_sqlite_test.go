package main

import (
	"fmt"
	"gorm.io/gorm"
	"path/filepath"
	"sync"
	"testing"
)

func TestSQLiteConcurrentReadModifyWrite(t *testing.T) {
	db, err := openSQLiteDatabase(filepath.Join(t.TempDir(), "concurrent.db"), &gorm.Config{})
	if err != nil {
		t.Fatal(err)
	}
	pool, _ := db.DB()
	defer pool.Close()
	if err := db.Exec("CREATE TABLE counter(value INTEGER NOT NULL)").Error; err != nil {
		t.Fatal(err)
	}
	if err := db.Exec("INSERT INTO counter VALUES(0)").Error; err != nil {
		t.Fatal(err)
	}
	var journal string
	db.Raw("PRAGMA journal_mode").Scan(&journal)
	if journal != "wal" {
		t.Fatalf("journal mode: %s", journal)
	}
	var wg sync.WaitGroup
	errors := make(chan error, 8)
	for worker := 0; worker < 8; worker++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for n := 0; n < 30; n++ {
				err := db.Transaction(func(tx *gorm.DB) error {
					var value int
					if err := tx.Raw("SELECT value FROM counter").Scan(&value).Error; err != nil {
						return err
					}
					return tx.Exec("UPDATE counter SET value=?", value+1).Error
				})
				if err != nil {
					errors <- fmt.Errorf("transaction: %w", err)
					return
				}
			}
		}()
	}
	wg.Wait()
	close(errors)
	for err := range errors {
		t.Error(err)
	}
	var value int
	db.Raw("SELECT value FROM counter").Scan(&value)
	if value != 240 {
		t.Fatalf("lost updates: got %d want 240", value)
	}
}
