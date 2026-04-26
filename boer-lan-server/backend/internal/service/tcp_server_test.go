package service

import (
	"testing"

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
		{Code: "online", Name: "online", Status: "online"},
		{Code: "idle", Name: "idle", Status: "idle"},
		{Code: "working", Name: "working", Status: "working"},
		{Code: "alarm", Name: "alarm", Status: "alarm"},
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
