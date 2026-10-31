package service

import (
	"context"
	"log"
	"time"

	"boer-lan-server/internal/model"
	"boer-lan-server/pkg/dbretry"

	"gorm.io/gorm"
)

func ensureDeviceRuntimeSession(db *gorm.DB, deviceID uint, seenAt time.Time) {
	if db == nil || deviceID == 0 {
		return
	}
	if seenAt.IsZero() {
		seenAt = time.Now()
	}

	err := dbretry.Do(context.Background(), func() error {
		return db.Transaction(func(tx *gorm.DB) error {
			var session model.DeviceRuntimeSession
			err := tx.Where("device_id = ? AND ended_at IS NULL", deviceID).Order("started_at DESC, id DESC").First(&session).Error
			if err == nil {
				// A heartbeat per second is enough; retain the exact disconnect time on close.
				if !seenAt.Before(session.StartedAt) && seenAt.Sub(session.LastSeenAt) < time.Second {
					return nil
				}
				updates := map[string]interface{}{"last_seen_at": seenAt}
				if seenAt.Before(session.StartedAt) {
					updates["started_at"] = seenAt
				}
				return tx.Model(&session).Updates(updates).Error
			}
			if err != gorm.ErrRecordNotFound {
				return err
			}
			return tx.Create(&model.DeviceRuntimeSession{DeviceID: deviceID, StartedAt: seenAt, LastSeenAt: seenAt}).Error
		})
	})
	if err != nil {
		log.Printf("Runtime session write failed: device=%d err=%v", deviceID, err)
	}
}

func closeDeviceRuntimeSessions(db *gorm.DB, deviceID uint, endedAt time.Time, reason string) {
	if db == nil {
		return
	}
	if endedAt.IsZero() {
		endedAt = time.Now()
	}

	query := db.Model(&model.DeviceRuntimeSession{}).Where("ended_at IS NULL")
	if deviceID > 0 {
		query = query.Where("device_id = ?", deviceID)
	}

	var sessions []model.DeviceRuntimeSession
	if err := query.Find(&sessions).Error; err != nil {
		return
	}
	for _, session := range sessions {
		end := endedAt
		if end.Before(session.StartedAt) {
			end = session.StartedAt
		}
		duration := int64(end.Sub(session.StartedAt).Seconds())
		if duration < 0 {
			duration = 0
		}
		if err := dbretry.Do(context.Background(), func() error {
			return db.Model(&session).Updates(map[string]interface{}{
				"ended_at":         end,
				"last_seen_at":     end,
				"duration_seconds": duration,
				"end_reason":       reason,
			}).Error
		}); err != nil {
			log.Printf("Runtime session close failed: device=%d err=%v", deviceID, err)
		}
	}
}
