package service

import (
	"errors"
	"log"
	"sort"
	"strings"
	"sync"
	"time"

	"boer-lan-server/internal/model"

	"gorm.io/gorm"
)

const devicePatternSyncInterval = 1 * time.Minute

type DevicePatternSyncWorker struct {
	db       *gorm.DB
	transfer *PatternTransferService
	stopCh   chan struct{}
	once     sync.Once
	mu       sync.Mutex
}

func NewDevicePatternSyncWorker(db *gorm.DB, transfer *PatternTransferService) *DevicePatternSyncWorker {
	return &DevicePatternSyncWorker{
		db:       db,
		transfer: transfer,
		stopCh:   make(chan struct{}),
	}
}

func (w *DevicePatternSyncWorker) Start() {
	go w.loop()
}

func (w *DevicePatternSyncWorker) Stop() {
	w.once.Do(func() {
		close(w.stopCh)
	})
}

func (w *DevicePatternSyncWorker) loop() {
	ticker := time.NewTicker(devicePatternSyncInterval)
	defer ticker.Stop()

	if err := w.processOnce(); err != nil {
		log.Printf("device pattern sync worker initial process error: %v", err)
	}

	for {
		select {
		case <-w.stopCh:
			return
		case <-ticker.C:
			if err := w.processOnce(); err != nil {
				log.Printf("device pattern sync worker process error: %v", err)
			}
		}
	}
}

func (w *DevicePatternSyncWorker) processOnce() error {
	if w == nil || w.transfer == nil || w.transfer.connMgr == nil {
		return nil
	}

	w.mu.Lock()
	defer w.mu.Unlock()

	deviceIDs := w.collectConnectedDeviceIDs()
	if len(deviceIDs) == 0 {
		return nil
	}

	var devices []model.Device
	if err := w.db.Where("id IN ?", deviceIDs).Find(&devices).Error; err != nil {
		return err
	}

	sort.Slice(devices, func(i, j int) bool { return devices[i].ID < devices[j].ID })
	for _, device := range devices {
		if !w.transfer.IsDeviceConnected(device) {
			continue
		}
		if _, err := w.transfer.RefreshDevicePatternFilesIfIdle(device); err != nil {
			if isPatternTransferBusy(err) {
				continue
			}
			log.Printf("device pattern sync skipped device=%s id=%d err=%v", strings.TrimSpace(device.Code), device.ID, err)
		}
	}
	return nil
}

func (w *DevicePatternSyncWorker) collectConnectedDeviceIDs() []uint {
	connections := w.transfer.connMgr.GetAll()
	if len(connections) == 0 {
		return nil
	}

	seen := make(map[uint]struct{}, len(connections))
	ids := make([]uint, 0, len(connections))
	for _, dc := range connections {
		if dc == nil || dc.deviceID == 0 {
			continue
		}
		if _, exists := seen[dc.deviceID]; exists {
			continue
		}
		seen[dc.deviceID] = struct{}{}
		ids = append(ids, dc.deviceID)
	}
	return ids
}

func isPatternTransferBusy(err error) bool {
	if err == nil {
		return false
	}
	return errors.Is(err, ErrPatternTransferBusy) || strings.Contains(strings.ToLower(err.Error()), "pattern transfer busy")
}
