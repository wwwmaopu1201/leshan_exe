package main

import (
	"boer-lan-server/pkg/sqlitequeue"
	"gorm.io/driver/sqlite"
	"gorm.io/gorm"
	"strings"
)

// Driver parameters apply to every pooled connection. Immediate transactions
// acquire the write reservation before reading instead of failing on promotion.
func openSQLiteDatabase(path string, config *gorm.Config) (*gorm.DB, error) {
	separator := "?"
	if strings.Contains(path, "?") {
		separator = "&"
	}
	pool, err := sqlitequeue.OpenPool(path + separator + "_busy_timeout=5000&_journal_mode=WAL&_txlock=immediate")
	if err != nil {
		return nil, err
	}
	db, err := gorm.Open(sqlite.Dialector{Conn: pool}, config)
	if err != nil {
		pool.Close()
		return nil, err
	}
	return db, nil
}

func closeDatabase(database *gorm.DB) error {
	if pool, ok := database.ConnPool.(*sqlitequeue.Pool); ok {
		return pool.Close()
	}
	pool, err := database.DB()
	if err != nil {
		return err
	}
	return pool.Close()
}
