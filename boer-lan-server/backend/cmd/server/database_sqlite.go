package main

import (
	"boer-lan-server/pkg/sqlitequeue"
	"gorm.io/driver/sqlite"
	"gorm.io/gorm"
	"path/filepath"
	"strings"
)

// Driver parameters apply to every pooled connection. Immediate transactions
// acquire the write reservation before reading instead of failing on promotion.
func openSQLiteDatabase(path string, config *gorm.Config) (*gorm.DB, error) {
	separator := "?"
	if strings.Contains(path, "?") {
		separator = "&"
	}
	db, err := gorm.Open(sqlite.Dialector{DriverName: sqlitequeue.DriverName(filepath.Clean(path)), DSN: path + separator + "_busy_timeout=5000&_journal_mode=WAL&_txlock=immediate"}, config)
	if err != nil {
		return nil, err
	}
	pool, err := db.DB()
	if err != nil {
		return nil, err
	}
	// Bound the number of waiting writers without starving independent readers.
	pool.SetMaxOpenConns(8)
	pool.SetMaxIdleConns(8)
	return db, nil
}
