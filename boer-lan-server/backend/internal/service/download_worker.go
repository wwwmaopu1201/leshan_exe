package service

import (
	"log"
	"sync"
	"time"

	"boer-lan-server/internal/model"

	"gorm.io/gorm"
)

// DownloadTaskWorker 驱动下发任务状态推进（waiting -> downloading -> completed）
// 说明：当前版本不直接对接设备传输协议，先保证队列行为完整可观测。
type DownloadTaskWorker struct {
	db       *gorm.DB
	transfer *PatternTransferService
	stopCh   chan struct{}
	once     sync.Once
	mu       sync.Mutex
	active   map[uint]uint
}

func NewDownloadTaskWorker(db *gorm.DB, transfer *PatternTransferService) *DownloadTaskWorker {
	return &DownloadTaskWorker{
		db:       db,
		transfer: transfer,
		stopCh:   make(chan struct{}),
		active:   make(map[uint]uint),
	}
}

func (w *DownloadTaskWorker) Start() {
	go w.loop()
}

func (w *DownloadTaskWorker) Stop() {
	w.once.Do(func() {
		close(w.stopCh)
	})
}

func (w *DownloadTaskWorker) loop() {
	ticker := time.NewTicker(2 * time.Second)
	defer ticker.Stop()

	_ = w.db.Model(&model.DownloadTask{}).
		Where("status = ?", "downloading").
		Updates(map[string]interface{}{
			"status":   "waiting",
			"progress": 0,
			"message":  "等待下发",
		}).Error

	_ = w.processOnce()

	for {
		select {
		case <-w.stopCh:
			return
		case <-ticker.C:
			if err := w.processOnce(); err != nil {
				log.Printf("download task worker process error: %v", err)
			}
		}
	}
}

func (w *DownloadTaskWorker) processOnce() error {
	w.mu.Lock()
	defer w.mu.Unlock()

	return w.startWaitingTasks()
}

func (w *DownloadTaskWorker) startWaitingTasks() error {
	if w.transfer == nil {
		return nil
	}

	var waitingTasks []model.DownloadTask
	if err := w.db.
		Where("status = ?", "waiting").
		Order("created_at ASC").
		Find(&waitingTasks).Error; err != nil {
		return err
	}

	for _, task := range waitingTasks {
		if _, exists := w.active[task.DeviceID]; exists {
			continue
		}

		var device model.Device
		if err := w.db.Select("id", "code", "status").First(&device, task.DeviceID).Error; err != nil {
			continue
		}
		if device.Status == "working" || device.Status == "offline" {
			continue
		}
		if !w.transfer.IsDeviceConnected(device) {
			continue
		}

		if err := w.db.Model(&model.DownloadTask{}).
			Where("id = ? AND status = ?", task.ID, "waiting").
			Updates(map[string]interface{}{
				"status":   "downloading",
				"progress": 0,
				"message":  "准备下发",
			}).Error; err != nil {
			return err
		}

		w.active[task.DeviceID] = task.ID
		go w.runTask(task.ID, task.DeviceID)
	}
	return nil
}

func (w *DownloadTaskWorker) runTask(taskID, deviceID uint) {
	defer func() {
		w.mu.Lock()
		delete(w.active, deviceID)
		w.mu.Unlock()
	}()

	if err := w.transfer.ExecuteDownloadTask(taskID); err != nil {
		log.Printf("download task %d failed: %v", taskID, err)
		if updateErr := w.db.Model(&model.DownloadTask{}).
			Where("id = ?", taskID).
			Updates(map[string]interface{}{
				"status":  "failed",
				"message": err.Error(),
			}).Error; updateErr != nil {
			log.Printf("download task %d update failed status error: %v", taskID, updateErr)
		}
	}
}
