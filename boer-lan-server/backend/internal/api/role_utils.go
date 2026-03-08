package api

import (
	"encoding/json"
	"errors"
	"strings"
	"unicode/utf8"

	"boer-lan-server/internal/model"

	"gorm.io/gorm"
)

var defaultRolePermissionMap = map[string]bool{
	"home":               true,
	"dashboard":          true,
	"deviceManagement":   true,
	"fileManagement":     true,
	"statistics":         true,
	"employeeManagement": true,
	"remoteMonitoring":   true,
}

func defaultRolePermissionsJSON() string {
	encoded, _ := json.Marshal(defaultRolePermissionMap)
	return string(encoded)
}

func isValidRoleName(name string) bool {
	trimmed := strings.TrimSpace(name)
	if trimmed == "" {
		return false
	}
	return utf8.RuneCountInString(trimmed) <= 10
}

func normalizePermissionsJSON(raw string) (string, error) {
	trimmed := strings.TrimSpace(raw)
	if trimmed == "" {
		return defaultRolePermissionsJSON(), nil
	}

	var decoded interface{}
	if err := json.Unmarshal([]byte(trimmed), &decoded); err != nil {
		return "", errors.New("权限数据格式错误")
	}

	encoded, err := json.Marshal(decoded)
	if err != nil {
		return "", errors.New("权限数据序列化失败")
	}
	return string(encoded), nil
}

func ensureRoleExistsByName(db *gorm.DB, roleName string) error {
	name := strings.TrimSpace(roleName)
	if name == "" {
		return errors.New("角色不能为空")
	}

	var count int64
	if err := db.Model(&model.Role{}).Where("name = ?", name).Count(&count).Error; err != nil {
		return err
	}
	if count == 0 {
		return errors.New("角色不存在")
	}
	return nil
}
