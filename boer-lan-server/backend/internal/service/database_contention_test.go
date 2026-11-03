package service

import (
	"context"
	"fmt"
	"path/filepath"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"boer-lan-server/internal/model"
	"boer-lan-server/pkg/sqlitequeue"
	"gorm.io/driver/sqlite"
	"gorm.io/gorm"
	"gorm.io/gorm/logger"
)

func contentionDB(t *testing.T) *gorm.DB {
	t.Helper()
	p, err := sqlitequeue.OpenPool(filepath.Join(t.TempDir(), "load.db") + "?_journal_mode=WAL&_txlock=immediate&_busy_timeout=5000")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { p.Close() })
	db, err := gorm.Open(sqlite.Dialector{Conn: p}, &gorm.Config{Logger: logger.Default.LogMode(logger.Silent)})
	if err != nil {
		t.Fatal(err)
	}
	if err := db.AutoMigrate(&model.Device{}, &model.Employee{}, &model.Pattern{}, &model.DevicePatternFile{}, &model.ProductionRecord{}, &model.SalaryRecord{}, &model.ServerConfig{}, &model.DebugLog{}); err != nil {
		t.Fatal(err)
	}
	return db
}

func TestBackfillCheckpointAndUnchangedSalary(t *testing.T) {
	db := contentionDB(t)
	device := model.Device{Code: "D1", Name: "D1"}
	employee := model.Employee{Code: "E1", Name: "E1"}
	if err := db.Create(&device).Error; err != nil {
		t.Fatal(err)
	}
	if err := db.Create(&employee).Error; err != nil {
		t.Fatal(err)
	}
	record := model.ProductionRecord{DeviceID: device.ID, EmployeeID: employee.ID, PatternName: "test", Pieces: 2, UnitPrice: 3, RecordDate: time.Now(), SourceKey: "history"}
	if err := db.Create(&record).Error; err != nil {
		t.Fatal(err)
	}
	BackfillProductionDerivedData(db)
	var salary model.SalaryRecord
	if err := db.Where("source_production_id = ?", record.ID).First(&salary).Error; err != nil {
		t.Fatal(err)
	}
	var writes atomic.Int64
	db.Callback().Update().Before("gorm:update").Register("test:salary_writes", func(tx *gorm.DB) {
		if tx.Statement.Table == "salary_records" {
			writes.Add(1)
		}
	})
	if _, err := upsertSalaryRecordFromProduction(db, &record, true); err != nil {
		t.Fatal(err)
	}
	if writes.Load() != 0 {
		t.Fatal("unchanged salary was rewritten")
	}
	var repaired atomic.Int64
	db.Callback().Query().Before("gorm:query").Register("test:history_queries", func(tx *gorm.DB) {
		if tx.Statement.Table == "production_records" {
			repaired.Add(1)
		}
	})
	BackfillProductionDerivedData(db)
	// One empty range query, no record replay or salary UPDATE.
	if repaired.Load() != 1 || writes.Load() != 0 {
		t.Fatalf("completed history replayed: reads=%d writes=%d", repaired.Load(), writes.Load())
	}
	var cursor model.ServerConfig
	if err := db.Where("key = ?", "production_backfill_cursor_v1").First(&cursor).Error; err != nil || cursor.Value != fmt.Sprint(record.ID) {
		t.Fatalf("checkpoint: %+v err=%v", cursor, err)
	}
}

func TestBackfillHonorsCancellation(t *testing.T) {
	db := contentionDB(t)
	ctx, cancel := context.WithCancel(context.Background())
	var queried atomic.Bool
	db.Callback().Query().Before("gorm:query").Register("test:cancelled", func(tx *gorm.DB) { queried.Store(true) })
	cancel()
	BackfillProductionDerivedData(db.WithContext(ctx))
	if queried.Load() {
		t.Fatal("cancelled repair still queried database")
	}
}

func TestDebugLoggingDoesNotWaitForWriter(t *testing.T) {
	db := contentionDB(t)
	tx := db.Begin()
	if tx.Error != nil {
		t.Fatal(tx.Error)
	}
	defer tx.Rollback()
	done := make(chan struct{})
	go func() {
		for i := 0; i < 100; i++ {
			emitTCPLog(db, "info", true, "[TCP] contention-test packet=%d", i)
		}
		close(done)
	}()
	select {
	case <-done:
	case <-time.After(500 * time.Millisecond):
		t.Fatal("device logging waited for database writer")
	}
	if err := tx.Rollback().Error; err != nil {
		t.Fatal(err)
	}
	deadline := time.Now().Add(3 * time.Second)
	for {
		var count int64
		if err := db.Model(&model.DebugLog{}).Count(&count).Error; err != nil {
			t.Fatal(err)
		}
		if count == 100 {
			break
		}
		if time.Now().After(deadline) {
			t.Fatalf("queued logs not persisted: %d", count)
		}
		time.Sleep(10 * time.Millisecond)
	}
}

func TestProductionAndDuplicateUploadsUnderConcurrentReads(t *testing.T) {
	db := contentionDB(t)
	device := model.Device{Code: "D1", Name: "D1"}
	employee := model.Employee{Code: "E1", Name: "E1"}
	if err := db.Create(&employee).Error; err != nil {
		t.Fatal(err)
	}
	device.EmployeeCode = employee.Code
	if err := db.Create(&device).Error; err != nil {
		t.Fatal(err)
	}
	pattern := model.Pattern{Name: "load-test", FileName: "load-test.dst", UnitPrice: 2}
	if err := db.Create(&pattern).Error; err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	var wg sync.WaitGroup
	errors := make(chan error, 20)
	for worker := 0; worker < 16; worker++ {
		wg.Add(1)
		go func(worker int) {
			defer wg.Done()
			dc := &DeviceConnection{db: db, deviceID: device.ID}
			for i := 0; i < 8; i++ {
				snapshot := productionSnapshot{PatternNo: 1, PatternName: pattern.Name, ProtocolUserID: employee.Code, StartNeedle: uint32(worker*100 + i), EndNeedle: uint32(worker*100 + i + 1), StartTime: time.Date(2026, 11, 3, 1, 0, 0, 0, time.UTC), EndTime: time.Date(2026, 11, 3, 1, 1, 0, 0, time.UTC)}
				for repeat := 0; repeat < 2; repeat++ {
					if _, _, err := dc.syncProductionSnapshot(snapshot); err != nil {
						errors <- err
						return
					}
				}
			}
		}(worker)
	}
	for reader := 0; reader < 4; reader++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for i := 0; i < 50; i++ {
				readCtx, stop := context.WithTimeout(ctx, time.Second)
				var count int64
				err := db.WithContext(readCtx).Model(&model.ProductionRecord{}).Count(&count).Error
				stop()
				if err != nil {
					errors <- err
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
	var production, salary int64
	if err := db.Model(&model.ProductionRecord{}).Count(&production).Error; err != nil {
		t.Fatal(err)
	}
	if err := db.Model(&model.SalaryRecord{}).Count(&salary).Error; err != nil {
		t.Fatal(err)
	}
	if production != 128 || salary != 128 {
		t.Fatalf("lost or duplicated data: production=%d salary=%d", production, salary)
	}
}
