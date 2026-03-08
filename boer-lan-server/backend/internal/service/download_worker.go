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
	db     *gorm.DB
	stopCh chan struct{}
	once   sync.Once
	mu     sync.Mutex
}

func NewDownloadTaskWorker(db *gorm.DB) *DownloadTaskWorker {
	return &DownloadTaskWorker{
		db:     db,
		stopCh: make(chan struct{}),
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

	if err := w.startWaitingTasks(); err != nil {
		return err
	}
	if err := w.advanceDownloadingTasks(); err != nil {
		return err
	}
	return nil
}

func (w *DownloadTaskWorker) startWaitingTasks() error {
	var waitingTasks []model.DownloadTask
	if err := w.db.
		Where("status = ?", "waiting").
		Order("created_at ASC").
		Find(&waitingTasks).Error; err != nil {
		return err
	}

	deviceHasDownloading := make(map[uint]bool)
	for _, task := range waitingTasks {
		hasDownloading, exists := deviceHasDownloading[task.DeviceID]
		if !exists {
			var count int64
			if err := w.db.Model(&model.DownloadTask{}).
				Where("device_id = ? AND status = ?", task.DeviceID, "downloading").
				Count(&count).Error; err != nil {
				return err
			}
			hasDownloading = count > 0
			deviceHasDownloading[task.DeviceID] = hasDownloading
		}
		if hasDownloading {
			continue
		}

		var device model.Device
		if err := w.db.Select("id", "status").First(&device, task.DeviceID).Error; err != nil {
			continue
		}
		// 设备缝纫中或离线时，不启动新下发任务
		if device.Status == "working" || device.Status == "offline" {
			continue
		}

		if err := w.db.Model(&model.DownloadTask{}).
			Where("id = ? AND status = ?", task.ID, "waiting").
			Updates(map[string]interface{}{
				"status":   "downloading",
				"progress": 5,
				"message":  "正在下发",
			}).Error; err != nil {
			return err
		}
		deviceHasDownloading[task.DeviceID] = true
	}
	return nil
}

func (w *DownloadTaskWorker) advanceDownloadingTasks() error {
	var downloadingTasks []model.DownloadTask
	if err := w.db.
		Where("status = ?", "downloading").
		Order("updated_at ASC").
		Find(&downloadingTasks).Error; err != nil {
		return err
	}

	for _, task := range downloadingTasks {
		next := task.Progress + 20
		if next >= 100 {
			next = 100
			if err := w.db.Model(&model.DownloadTask{}).
				Where("id = ? AND status = ?", task.ID, "downloading").
				Updates(map[string]interface{}{
					"status":   "completed",
					"progress": next,
					"message":  "下发完成",
				}).Error; err != nil {
				return err
			}
			continue
		}

		if err := w.db.Model(&model.DownloadTask{}).
			Where("id = ? AND status = ?", task.ID, "downloading").
			Updates(map[string]interface{}{
				"progress": next,
				"message":  "正在下发",
			}).Error; err != nil {
			return err
		}
	}
	return nil
}
