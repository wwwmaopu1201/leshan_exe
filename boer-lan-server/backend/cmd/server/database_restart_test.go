package main

import (
	"bytes"
	"log"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"boer-lan-server/internal/model"
	"gorm.io/gorm"
	"gorm.io/gorm/logger"
)

func TestSQLiteRestartDoesNotRebuildUnchangedPriceTables(t *testing.T) {
	var output bytes.Buffer
	db, err := openSQLiteDatabase(filepath.Join(t.TempDir(), "restart.db"), &gorm.Config{Logger: logger.New(log.New(&output, "", 0), logger.Config{LogLevel: logger.Info, SlowThreshold: time.Second})})
	if err != nil {
		t.Fatal(err)
	}
	defer closeDatabase(db)
	models := []interface{}{&model.Pattern{}, &model.DevicePatternFile{}, &model.ProductionRecord{}, &model.SalaryRecord{}}
	for _, m := range models {
		if err = db.AutoMigrate(m); err != nil {
			t.Fatal(err)
		}
	}
	output.Reset()
	for _, m := range models {
		if err = db.AutoMigrate(m); err != nil {
			t.Fatal(err)
		}
	}
	if strings.Contains(output.String(), "__temp") {
		t.Fatal("unchanged tables were rebuilt on restart")
	}
}
