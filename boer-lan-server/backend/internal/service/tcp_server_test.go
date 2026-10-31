package service

import (
	"context"
	"path/filepath"
	"testing"
	"time"

	"boer-lan-server/internal/model"

	"gorm.io/driver/sqlite"
	"gorm.io/gorm"
)

func TestResetStaleDeviceStatuses(t *testing.T) {
	db, err := gorm.Open(sqlite.Open("file:reset_stale_device_statuses?mode=memory&cache=shared"), &gorm.Config{})
	if err != nil {
		t.Fatalf("open sqlite: %v", err)
	}
	if err := db.AutoMigrate(&model.Device{}, &model.DeviceRuntimeSession{}); err != nil {
		t.Fatalf("migrate devices: %v", err)
	}

	devices := []model.Device{
		{Code: "idle", Name: "idle", Status: "idle"},
		{Code: "working", Name: "working", Status: "working"},
		{Code: "offline", Name: "offline", Status: "offline"},
	}
	if err := db.Create(&devices).Error; err != nil {
		t.Fatalf("create devices: %v", err)
	}

	server := NewTCPServer(db)
	if err := server.ResetStaleDeviceStatuses(); err != nil {
		t.Fatalf("reset stale statuses: %v", err)
	}

	var count int64
	if err := db.Model(&model.Device{}).Where("status <> ?", "offline").Count(&count).Error; err != nil {
		t.Fatalf("count non-offline devices: %v", err)
	}
	if count != 0 {
		t.Fatalf("expected all devices offline, got %d non-offline devices", count)
	}
}

func TestReconcileOfflineRetriesAndPreservesLiveDevices(t *testing.T) {
	db, err := gorm.Open(sqlite.Open(filepath.Join(t.TempDir(), "reconcile.db")), &gorm.Config{})
	if err != nil {
		t.Fatal(err)
	}
	if err := db.AutoMigrate(&model.Device{}, &model.DeviceRuntimeSession{}); err != nil {
		t.Fatal(err)
	}
	old := time.Now().Add(-time.Hour)
	devices := []model.Device{{Code: "stale", Name: "stale", Model: gorm.Model{UpdatedAt: old}, Status: "idle", LastProtocolAt: old}, {Code: "live", Name: "live", Model: gorm.Model{UpdatedAt: old}, Status: "working", LastProtocolAt: old}, {Code: "recent", Name: "recent", Status: "idle"}}
	if err := db.Create(&devices).Error; err != nil {
		t.Fatal(err)
	}
	server := NewTCPServer(db)
	server.connMgr.Register("live", &DeviceConnection{deviceID: devices[1].ID, deviceCode: "live"})
	// A transient write failure must leave the device eligible for the next scan.
	db.Exec("CREATE TRIGGER block_offline BEFORE UPDATE OF status ON devices BEGIN SELECT RAISE(ABORT, 'injected write failure'); END")
	if err := server.reconcileOfflineStatuses(time.Now()); err == nil {
		t.Fatal("expected injected write failure")
	}
	db.Exec("DROP TRIGGER block_offline")
	if err := server.reconcileOfflineStatuses(time.Now()); err != nil {
		t.Fatal(err)
	}
	for i, want := range []string{"offline", "working", "idle"} {
		var got model.Device
		db.First(&got, devices[i].ID)
		if got.Status != want {
			t.Fatalf("%s got %s want %s", got.Code, got.Status, want)
		}
	}
}

func TestMergeBindingRunsAfterCommit(t *testing.T) {
	db, err := gorm.Open(sqlite.Open(filepath.Join(t.TempDir(), "merge.db")), &gorm.Config{})
	if err != nil {
		t.Fatal(err)
	}
	pool, _ := db.DB()
	pool.SetMaxOpenConns(1)
	defer pool.Close()
	if err := db.AutoMigrate(&model.Device{}, &model.DeviceRuntimeSession{}, &model.DebugLog{}, &model.ServerConfig{}, &model.EmployeeDevice{}, &model.DevicePatternFile{}, &model.UploadTask{}, &model.DownloadTask{}, &model.ProductionRecord{}, &model.AlarmRecord{}, &model.SalaryRecord{}); err != nil {
		t.Fatal(err)
	}
	source := model.Device{Code: "old", Name: "old"}
	target := model.Device{Code: "new", Name: "new"}
	db.Create(&source)
	db.Create(&target)
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	dc := &DeviceConnection{db: db.WithContext(ctx)}
	if err := dc.mergeDeviceRecord(source, target, "test"); err != nil {
		t.Fatal(err)
	}
	if ctx.Err() != nil {
		t.Fatal("merge attempted to acquire another connection before commit")
	}
	var count int64
	db.Model(&model.DeviceRuntimeSession{}).Where("device_id=?", target.ID).Count(&count)
	if count != 1 {
		t.Fatalf("missing post-commit runtime session: %d", count)
	}
}

func TestDisconnectedStatusRetriesAfterWriteFailure(t *testing.T) {
	db, err := gorm.Open(sqlite.Open(filepath.Join(t.TempDir(), "disconnect.db")), &gorm.Config{})
	if err != nil {
		t.Fatal(err)
	}
	pool, _ := db.DB()
	defer pool.Close()
	if err := db.AutoMigrate(&model.Device{}, &model.DeviceRuntimeSession{}, &model.DebugLog{}, &model.ServerConfig{}); err != nil {
		t.Fatal(err)
	}
	device := model.Device{Code: "retry", Name: "retry", Status: "idle"}
	db.Create(&device)
	db.Create(&model.ServerConfig{Key: "debug_output_enabled", Value: "false"})
	if err := db.Exec("CREATE TRIGGER block_offline BEFORE UPDATE OF status ON devices BEGIN SELECT RAISE(ABORT, 'injected write failure'); END").Error; err != nil {
		t.Fatal(err)
	}
	dc := &DeviceConnection{db: db, deviceID: device.ID, deviceCode: device.Code, connMgr: NewConnectionManager()}
	dc.scheduleOfflineTransition()
	time.Sleep(ReconnectGracePeriod + time.Second)
	var got model.Device
	db.First(&got, device.ID)
	if got.Status != "idle" {
		t.Fatal("fixture did not block offline write")
	}
	db.Exec("DROP TRIGGER block_offline")
	deadline := time.Now().Add(ReconnectGracePeriod + 2*time.Second)
	for time.Now().Before(deadline) {
		db.First(&got, device.ID)
		if got.Status == "offline" {
			return
		}
		time.Sleep(50 * time.Millisecond)
	}
	t.Fatal("offline update did not retry after failure")
}
