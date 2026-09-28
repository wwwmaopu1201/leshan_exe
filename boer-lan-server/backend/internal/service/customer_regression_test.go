package service

import (
	"bytes"
	"context"
	"errors"
	"net"
	"os"
	"path/filepath"
	"testing"
	"time"

	"boer-lan-server/internal/model"
	"gorm.io/driver/sqlite"
	"gorm.io/gorm"
)

func customerServiceDB(t *testing.T) *gorm.DB {
	t.Helper()
	db, err := gorm.Open(sqlite.Open(filepath.Join(t.TempDir(), "test.db")), &gorm.Config{})
	if err != nil {
		t.Fatal(err)
	}
	if err := db.AutoMigrate(&model.Pattern{}, &model.DownloadTask{}, &model.UploadTask{}, &model.ServerConfig{}, &model.Group{}); err != nil {
		t.Fatal(err)
	}
	sqlDB, _ := db.DB()
	t.Cleanup(func() { sqlDB.Close() })
	return db
}

func TestLargePatternFramesReassembleWithoutWrapping(t *testing.T) {
	for _, size := range []int{1, 256, 255 * 256, 255*256 + 1, 354 * 1024, 1024 * 1024} {
		original := bytes.Repeat([]byte{0x15, 0x26, 0x37, 0x48}, (size+3)/4)[:size]
		frames := buildDownloadPatternFrames(original, 7, 9)
		if len(frames) > 255 {
			t.Fatalf("%d bytes produced %d frames", size, len(frames))
		}
		var result []byte
		for i, frame := range frames {
			if int(frame.FrameNo) != i+1 || int(frame.TotalFrames) != len(frames) {
				t.Fatalf("invalid frame %d/%d", frame.FrameNo, frame.TotalFrames)
			}
			if frame.Addr1 != 7 || frame.Addr2 != 9 {
				t.Fatal("lost device address")
			}
			result = append(result, frame.Data...)
		}
		if !bytes.Equal(original, result) {
			t.Fatalf("payload corrupted at %d bytes", size)
		}
	}
}

func TestOverwritePreservesPatternBusinessMetadata(t *testing.T) {
	db := customerServiceDB(t)
	original := model.Pattern{Name: "test", PatternType: "shirt", FileName: "test.ntp", Stitches: 123, UnitPrice: 1.25, OrderNo: "order-1", FileSize: 2}
	if err := db.Create(&original).Error; err != nil {
		t.Fatal(err)
	}
	result, err := NewPatternTransferService(db, nil).saveUploadedPattern(model.Device{Model: gorm.Model{ID: 1}}, model.DevicePatternFile{FileName: "test.ntp"}, "", []byte{1, 2, 3}, 1, UploadConflictModeOverwrite, "")
	if err != nil {
		t.Fatal(err)
	}
	if result.ID != original.ID || result.PatternType != original.PatternType || result.Stitches != original.Stitches || result.UnitPrice != original.UnitPrice || result.OrderNo != original.OrderNo || result.FileSize != 3 {
		t.Fatalf("metadata lost: %+v", result)
	}
}

func TestPausedDownloadCannotBeCompletedByOldTransfer(t *testing.T) {
	db := customerServiceDB(t)
	task := model.DownloadTask{Status: "paused"}
	if err := db.Create(&task).Error; err != nil {
		t.Fatal(err)
	}
	transfer := NewPatternTransferService(db, nil)
	if err := transfer.updateDownloadTask(task.ID, map[string]interface{}{"status": "completed", "progress": 100}); !errors.Is(err, context.Canceled) {
		t.Fatalf("expected cancellation, got %v", err)
	}
	var saved model.DownloadTask
	db.First(&saved, task.ID)
	if saved.Status != "paused" {
		t.Fatalf("status overwritten: %s", saved.Status)
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go transfer.watchDownloadTask(ctx, cancel, task.ID)
	select {
	case <-ctx.Done():
	case <-time.After(time.Second):
		t.Fatal("pause did not interrupt waiter")
	}
}

func TestManualSyncDisabledReturnsError(t *testing.T) {
	db := customerServiceDB(t)
	if err := db.Create(&model.ServerConfig{Key: externalDBConfigKey, Value: `{"enabled":false}`}).Error; err != nil {
		t.Fatal(err)
	}
	if err := RunExternalDBSyncOnce(db); err == nil {
		t.Fatal("disabled sync incorrectly succeeded")
	}
}

func TestSyncIncludesGroupsAndDeletionTombstones(t *testing.T) {
	source, destination := customerServiceDB(t), customerServiceDB(t)
	group := model.Group{Name: "车间一"}
	source.Create(&group)
	if err := syncModelInBatches[model.Group](source, destination, 1, time.Time{}); err != nil {
		t.Fatal(err)
	}
	source.Delete(&group)
	if err := syncModelInBatches[model.Group](source, destination, 1, time.Time{}); err != nil {
		t.Fatal(err)
	}
	var total int64
	destination.Model(&model.Group{}).Count(&total)
	if total != 0 {
		t.Fatal("deleted group remained active in destination")
	}
	var saved model.Group
	destination.Unscoped().First(&saved, group.ID)
	if saved.Name != group.Name || !saved.DeletedAt.Valid {
		t.Fatal("group sync lost data or deletion")
	}
}

func TestPauseInterruptsDownloadWhileWaitingForDevice(t *testing.T) {
	db := customerServiceDB(t)
	if err := db.AutoMigrate(&model.Device{}); err != nil {
		t.Fatal(err)
	}
	device := model.Device{Code: "test-device", Name: "test-device", Status: "idle"}
	db.Create(&device)
	path := filepath.Join(t.TempDir(), "sample.ntp")
	if err := os.WriteFile(path, []byte{1, 2, 3}, 0600); err != nil {
		t.Fatal(err)
	}
	pattern := model.Pattern{Name: "sample", FileName: "sample.ntp", FilePath: path, FileSize: 3}
	db.Create(&pattern)
	task := model.DownloadTask{DeviceID: device.ID, PatternID: pattern.ID, Status: "downloading"}
	db.Create(&task)
	local, remote := net.Pipe()
	defer local.Close()
	defer remote.Close()
	manager := NewConnectionManager()
	connection := NewDeviceConnection(local, db, manager)
	connection.deviceID = device.ID
	connection.deviceCode = device.Code
	manager.Register(device.Code, connection)
	transfer := NewPatternTransferService(db, manager)
	finished := make(chan error, 1)
	go func() { finished <- transfer.ExecuteDownloadTask(task.ID) }()
	remote.SetReadDeadline(time.Now().Add(time.Second))
	packet, err := ParsePacket(remote)
	if err != nil || packet.ParamNo != PNDownloadPatternCommand {
		t.Fatalf("missing download command: %v", err)
	}
	db.Model(&task).Update("status", "paused")
	transfer.InterruptDownload(task.ID)
	select {
	case err := <-finished:
		if !errors.Is(err, context.Canceled) {
			t.Fatalf("expected interrupted transfer, got %v", err)
		}
	case <-time.After(time.Second):
		t.Fatal("pause left transfer waiting for device")
	}
}
