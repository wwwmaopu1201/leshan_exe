package service

import (
	"fmt"
	"log"
	"strings"
	"sync"
	"time"

	"boer-lan-server/internal/model"

	"gorm.io/gorm"
)

const DownloadTaskQueueTimeout = 10 * time.Minute

var runnableDownloadTaskStatuses = []string{"waiting", "failed"}

// DownloadTaskWorker 驱动下发任务状态推进（waiting/failed -> downloading -> completed）
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

	return w.startRunnableTasks()
}

func (w *DownloadTaskWorker) startRunnableTasks() error {
	if w.transfer == nil {
		return nil
	}

	var tasks []model.DownloadTask
	if err := w.db.
		Where("status IN ?", runnableDownloadTaskStatuses).
		Order("device_id ASC, created_at ASC").
		Find(&tasks).Error; err != nil {
		return err
	}

	now := time.Now()
	seenDevices := make(map[uint]struct{})
	for _, task := range tasks {
		if _, seen := seenDevices[task.DeviceID]; seen {
			continue
		}
		seenDevices[task.DeviceID] = struct{}{}

		if _, exists := w.active[task.DeviceID]; exists {
			continue
		}

		var device model.Device
		if err := w.db.Select("id", "code", "name", "status", "ip", "mainboard_sn").First(&device, task.DeviceID).Error; err != nil {
			continue
		}
		if !w.transfer.IsDeviceConnected(device) {
			if task.Status == "waiting" && w.hasQueueSlotExpired(task, now) {
				if err := w.failWaitingTask(task, "设备未连接，等待超过10分钟，任务失败"); err != nil {
					return err
				}
				continue
			}
			message := "设备未连接，等待开机"
			if task.Status == "failed" {
				message = "设备未连接，恢复后自动重试"
			}
			_ = w.updateTaskMessage(task.ID, task.Status, message)
			continue
		}
		if device.Status == "working" {
			if task.Status == "waiting" && w.hasQueueSlotExpired(task, now) {
				if err := w.failWaitingTask(task, "设备缝纫中，等待超过10分钟，任务失败"); err != nil {
					return err
				}
				continue
			}
			message := "设备缝纫中，等待空闲"
			if task.Status == "failed" {
				message = "设备缝纫中，空闲后自动重试"
			}
			_ = w.updateTaskMessage(task.ID, task.Status, message)
			continue
		}

		message := "准备下发"
		if task.Status == "failed" {
			message = "准备重新下发"
		}
		if err := w.db.Model(&model.DownloadTask{}).
			Where("id = ? AND status IN ?", task.ID, runnableDownloadTaskStatuses).
			Updates(map[string]interface{}{
				"status":   "downloading",
				"progress": 0,
				"message":  message,
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
		var task model.DownloadTask
		queryErr := w.db.First(&task, taskID).Error
		expired := queryErr == nil && w.hasQueueSlotExpired(task, time.Now())
		if queryErr == nil && task.Status == "downloading" && !expired && isRetryableDownloadError(err) {
			if updateErr := w.db.Model(&model.DownloadTask{}).
				Where("id = ? AND status = ?", taskID, "downloading").
				UpdateColumns(map[string]interface{}{
					"status":   "waiting",
					"progress": 0,
					"message":  fmt.Sprintf("设备暂不可下发，等待重试：%v", err),
				}).Error; updateErr != nil {
				log.Printf("download task %d requeue error: %v", taskID, updateErr)
			}
			return
		}
		message := err.Error()
		if expired && isRetryableDownloadError(err) {
			message = "下发超过10分钟未完成，任务失败"
		}
		if updateErr := w.db.Model(&model.DownloadTask{}).
			Where("id = ?", taskID).
			Updates(map[string]interface{}{
				"status":  "failed",
				"message": message,
			}).Error; updateErr != nil {
			log.Printf("download task %d update failed status error: %v", taskID, updateErr)
		}
	}

	if err := w.activateNextWaitingSlot(deviceID); err != nil {
		log.Printf("download task worker activate next slot error: %v", err)
	}
}

func (w *DownloadTaskWorker) hasQueueSlotExpired(task model.DownloadTask, now time.Time) bool {
	slotStartedAt := task.UpdatedAt
	if slotStartedAt.IsZero() {
		slotStartedAt = task.CreatedAt
	}
	return now.Sub(slotStartedAt) > DownloadTaskQueueTimeout
}

func (w *DownloadTaskWorker) updateTaskMessage(taskID uint, status, message string) error {
	return w.db.Model(&model.DownloadTask{}).
		Where("id = ? AND status = ?", taskID, status).
		UpdateColumn("message", message).Error
}

func (w *DownloadTaskWorker) failWaitingTask(task model.DownloadTask, message string) error {
	if err := w.db.Model(&model.DownloadTask{}).
		Where("id = ? AND status = ?", task.ID, "waiting").
		Updates(map[string]interface{}{
			"status":  "failed",
			"message": message,
		}).Error; err != nil {
		return err
	}
	return w.activateNextWaitingSlot(task.DeviceID)
}

func (w *DownloadTaskWorker) activateNextWaitingSlot(deviceID uint) error {
	var next model.DownloadTask
	if err := w.db.
		Where("device_id = ? AND status = ?", deviceID, "waiting").
		Order("created_at ASC").
		First(&next).Error; err != nil {
		if err == gorm.ErrRecordNotFound {
			return nil
		}
		return err
	}

	return w.db.Model(&model.DownloadTask{}).
		Where("id = ? AND status = ?", next.ID, "waiting").
		Updates(map[string]interface{}{
			"message": "等待下发",
		}).Error
}

func isRetryableDownloadError(err error) bool {
	if err == nil {
		return false
	}
	message := strings.ToLower(err.Error())
	retryableParts := []string{
		"timeout waiting for device response",
		"device session closed",
		"is not connected",
		"broken pipe",
		"connection reset",
		"use of closed network connection",
		"device rejected",
		"device final download result",
	}
	for _, part := range retryableParts {
		if strings.Contains(message, part) {
			return true
		}
	}
	return false
}
