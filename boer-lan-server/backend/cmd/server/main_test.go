package main

import (
	"bytes"
	"log"
	"net"
	"net/http"
	"path/filepath"
	"sync"
	"testing"
	"time"

	"boer-lan-server/internal/model"

	"gorm.io/driver/sqlite"
	"gorm.io/gorm"
	"gorm.io/gorm/logger"
)

func TestEnsureDeviceIdentityIndexesOnRestart(t *testing.T) {
	for _, schema := range []string{"current", "legacy unique code", "missing mainboard index"} {
		t.Run(schema, func(t *testing.T) {
			var output bytes.Buffer
			db, err := gorm.Open(sqlite.Open(filepath.Join(t.TempDir(), "devices.db")), &gorm.Config{
				Logger: logger.New(log.New(&output, "", 0), logger.Config{LogLevel: logger.Error, SlowThreshold: time.Second}),
			})
			if err != nil {
				t.Fatal(err)
			}
			sqlDB, err := db.DB()
			if err != nil {
				t.Fatal(err)
			}
			t.Cleanup(func() { _ = sqlDB.Close() })
			if err := db.AutoMigrate(&model.Device{}); err != nil {
				t.Fatal(err)
			}
			switch schema {
			case "legacy unique code":
				if err := db.Exec("DROP INDEX idx_devices_code").Error; err != nil {
					t.Fatal(err)
				}
				if err := db.Exec("CREATE UNIQUE INDEX idx_devices_code ON devices(code)").Error; err != nil {
					t.Fatal(err)
				}
			case "missing mainboard index":
				if err := db.Migrator().DropIndex(&model.Device{}, "MainboardSN"); err != nil {
					t.Fatal(err)
				}
			}
			for restart := 0; restart < 2; restart++ {
				if err := ensureDeviceIdentityIndexes(db); err != nil {
					t.Fatalf("restart %d: %v", restart, err)
				}
			}
			for _, field := range []string{"Code", "MainboardSN"} {
				if !db.Migrator().HasIndex(&model.Device{}, field) {
					t.Fatalf("missing index for %s", field)
				}
			}
			// 旧版唯一索引必须被替换，允许多块主板使用相同设备编号。
			for _, sn := range []string{"board-1", "board-2"} {
				if err := db.Create(&model.Device{Code: "shared-code", Name: "device", MainboardSN: sn}).Error; err != nil {
					t.Fatalf("insert duplicate device code: %v", err)
				}
			}
			if output.Len() != 0 {
				t.Fatalf("unexpected database errors: %s", output.String())
			}
		})
	}
}

func TestHTTPReadyWhileProductionBackfillIsBlocked(t *testing.T) {
	database, err := gorm.Open(sqlite.Open(filepath.Join(t.TempDir(), "history.db")), &gorm.Config{})
	if err != nil {
		t.Fatal(err)
	}
	sqlDB, err := database.DB()
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = sqlDB.Close() })
	if err := database.AutoMigrate(&model.ProductionRecord{}); err != nil {
		t.Fatal(err)
	}
	blocked, release := make(chan struct{}), make(chan struct{})
	var once sync.Once
	if err := database.Callback().Query().Before("gorm:query").Register("test:slow_history", func(tx *gorm.DB) {
		if tx.Statement.Table == "production_records" {
			once.Do(func() {
				close(blocked)
				<-release
			})
		}
	}); err != nil {
		t.Fatal(err)
	}
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	server := &http.Server{Handler: http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
	})}
	finished := make(chan error, 1)
	go func() { finished <- serveHTTPWithProductionBackfill(server, listener, database) }()
	t.Cleanup(func() {
		close(release)
		_ = server.Close()
		select {
		case err := <-finished:
			if err != http.ErrServerClosed {
				t.Errorf("serve: %v", err)
			}
		case <-time.After(5 * time.Second):
			t.Error("background backfill did not stop with server")
		}
	})
	select {
	case <-blocked:
	case <-time.After(5 * time.Second):
		t.Fatal("backfill did not start")
	}
	client := &http.Client{Timeout: time.Second}
	response, err := client.Get("http://" + listener.Addr().String() + "/api/healthz")
	if err != nil {
		t.Fatalf("HTTP must be ready before history repair completes: %v", err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		t.Fatalf("health status: %d", response.StatusCode)
	}
}

func TestInitDBOnRestart(t *testing.T) {
	previousConfig, previousDB, previousSettings := config, db, runtimeLogSettings
	t.Cleanup(func() {
		config, db, runtimeLogSettings = previousConfig, previousDB, previousSettings
	})
	t.Setenv("DB_PATH", filepath.Join(t.TempDir(), "custom.db"))
	config = defaultConfig()
	runtimeLogSettings.GORMLogLevel = logger.Error
	for restart := 0; restart < 2; restart++ {
		initDB()
		var count int64
		if err := db.Model(&model.User{}).Where("username = ?", "admin").Count(&count).Error; err != nil {
			t.Fatal(err)
		}
		if count != 1 {
			t.Fatalf("restart %d: expected one admin, got %d", restart, count)
		}
		sqlDB, err := db.DB()
		if err != nil {
			t.Fatal(err)
		}
		if err := sqlDB.Close(); err != nil {
			t.Fatal(err)
		}
	}
}
