package api

import (
	"errors"
	"fmt"
	"sort"
	"strings"
	"time"
	"unicode/utf8"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

// Catalog table names are internal constants, never request input.
type catalogEntry struct {
	gorm.Model
	Value string
}

func saveCatalogEntry(db *gorm.DB, table, value string, limit int, rejectExisting bool) error {
	value = strings.TrimSpace(value)
	if value == "" || utf8.RuneCountInString(value) > limit {
		return fmt.Errorf("名称不能为空且不能超过%d个字符", limit)
	}
	return db.Transaction(func(tx *gorm.DB) error {
		var entry catalogEntry
		err := tx.Unscoped().Table(table).Where("value = ?", value).First(&entry).Error
		if err == nil {
			if !entry.DeletedAt.Valid {
				if rejectExisting {
					return errors.New("名称已存在")
				}
				return nil
			}
			return tx.Unscoped().Table(table).Where("id = ?", entry.ID).Updates(map[string]interface{}{"deleted_at": nil, "updated_at": time.Now()}).Error
		}
		if !errors.Is(err, gorm.ErrRecordNotFound) {
			return err
		}
		return tx.Table(table).Create(&catalogEntry{Value: value}).Error
	})
}

func sortCatalogSummary(db *gorm.DB, table string, list []gin.H) {
	var entries []catalogEntry
	db.Table(table).Find(&entries)
	timestamps := make(map[string]string)
	for _, entry := range entries {
		timestamps[entry.Value] = entry.UpdatedAt.Local().Format("2006-01-02 15:04:05")
	}
	for _, item := range list {
		value, _ := item["value"].(string)
		stamp, _ := item["updateTime"].(string)
		// Aggregate timestamps vary by SQL driver; the calendar prefix is shared.
		stamp = strings.ReplaceAll(stamp, "T", " ")
		if len(stamp) >= 19 {
			stamp = stamp[:19]
		}
		if candidate := timestamps[value]; candidate != "" && (stamp == "-" || candidate > stamp) {
			stamp = candidate
		}
		item["updateTime"] = stamp
	}
	sort.SliceStable(list, func(i, j int) bool {
		a, _ := list[i]["updateTime"].(string)
		b, _ := list[j]["updateTime"].(string)
		if a != b {
			return a > b
		}
		return fmt.Sprint(list[i]["value"]) < fmt.Sprint(list[j]["value"])
	})
}
