package service

import (
	"encoding/json"
	"log"
	"sync"
	"time"

	"boer-lan-server/internal/model"
	"gorm.io/gorm"
)

// UploadTaskWorker keeps device transfers out of HTTP requests and persists the
// queued name/conflict choices so pause/resume and app restarts do not lose them.
type UploadTaskWorker struct {
	db       *gorm.DB
	transfer *PatternTransferService
	stop     chan struct{}
	once     sync.Once
	mu       sync.Mutex
	active   map[uint]bool
}

func NewUploadTaskWorker(db *gorm.DB, transfer *PatternTransferService) *UploadTaskWorker {
	return &UploadTaskWorker{db: db, transfer: transfer, stop: make(chan struct{}), active: map[uint]bool{}}
}

func (w *UploadTaskWorker) Start() {
	// An interrupted upload must not be reported as completed or silently overwrite a file.
	w.db.Model(&model.UploadTask{}).Where("status = ?", "uploading").Updates(map[string]interface{}{"status": "failed", "message": "服务重启中断回传，请重新提交"})
	go func() {
		ticker := time.NewTicker(time.Second)
		defer ticker.Stop()
		for {
			select {
			case <-w.stop:
				return
			case <-ticker.C:
				w.processOnce()
			}
		}
	}()
}

func (w *UploadTaskWorker) Stop() { w.once.Do(func() { close(w.stop) }) }

func (w *UploadTaskWorker) processOnce() {
	var tasks []model.UploadTask
	if err := w.db.Where("status = ?", "waiting").Order("created_at ASC, id ASC").Find(&tasks).Error; err != nil {
		return
	}
	w.mu.Lock()
	defer w.mu.Unlock()
	for _, task := range tasks {
		if w.active[task.DeviceID] {
			continue
		}
		result := w.db.Model(&model.UploadTask{}).Where("id = ? AND status = ?", task.ID, "waiting").Updates(map[string]interface{}{"status": "uploading", "progress": 1, "message": "正在从设备回传"})
		if result.Error != nil || result.RowsAffected == 0 {
			continue
		}
		w.active[task.DeviceID] = true
		go w.runTask(task)
	}
}

func (w *UploadTaskWorker) runTask(task model.UploadTask) {
	defer func() { w.mu.Lock(); delete(w.active, task.DeviceID); w.mu.Unlock() }()
	var device model.Device
	var file model.DevicePatternFile
	err := w.db.First(&device, task.DeviceID).Error
	if task.FileSnapshot != "" {
		if decodeErr := json.Unmarshal([]byte(task.FileSnapshot), &file); decodeErr != nil {
			err = decodeErr
		}
	} else if err == nil {
		err = w.db.First(&file, task.DeviceFileID).Error
	}
	var pattern *model.Pattern
	if err == nil {
		pattern, err = w.transfer.uploadPatternWithProgress(device, file, task.OperatorID, NormalizeUploadConflictMode(task.ConflictMode), task.TargetName, func(progress int) {
			w.db.Model(&model.UploadTask{}).Where("id = ? AND status = ?", task.ID, "uploading").Update("progress", progress)
		})
	}
	updates := map[string]interface{}{"status": "completed", "progress": 100, "message": "回传完成"}
	if err != nil {
		log.Printf("upload task %d (%s) failed: %v", task.ID, file.FileName, err)
		updates = map[string]interface{}{"status": "failed", "message": "回传失败，请检查设备连接和花型文件后重试"}
	} else {
		updates["pattern_id"] = pattern.ID
	}
	w.db.Model(&model.UploadTask{}).Where("id = ? AND status = ?", task.ID, "uploading").Updates(updates)
}
