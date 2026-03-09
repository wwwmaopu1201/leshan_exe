package api

import (
	"encoding/json"
	"net/http"
	"strings"

	"boer-lan-server/internal/model"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

func parsePermissionMap(raw string) map[string]bool {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return nil
	}

	var boolMap map[string]bool
	if err := json.Unmarshal([]byte(raw), &boolMap); err == nil {
		result := make(map[string]bool, len(boolMap))
		for key, enabled := range boolMap {
			if enabled {
				result[key] = true
			}
		}
		return result
	}

	var list []string
	if err := json.Unmarshal([]byte(raw), &list); err == nil {
		result := make(map[string]bool, len(list))
		for _, key := range list {
			key = strings.TrimSpace(key)
			if key != "" {
				result[key] = true
			}
		}
		return result
	}

	var anyMap map[string]interface{}
	if err := json.Unmarshal([]byte(raw), &anyMap); err != nil {
		return nil
	}
	result := make(map[string]bool, len(anyMap))
	for key, value := range anyMap {
		if enabled, ok := value.(bool); ok && enabled {
			result[key] = true
		}
	}
	return result
}

func loadUserPermissionMap(db *gorm.DB, userID uint, roleName string) (map[string]bool, error) {
	var user model.User
	if err := db.Select("id", "role", "permissions").First(&user, userID).Error; err != nil {
		return nil, err
	}

	permissions := parsePermissionMap(user.Permissions)
	if len(permissions) > 0 {
		return permissions, nil
	}

	effectiveRoleName := strings.TrimSpace(roleName)
	if effectiveRoleName == "" {
		effectiveRoleName = strings.TrimSpace(user.Role)
	}
	if effectiveRoleName != "" {
		var role model.Role
		if err := db.Select("permissions").Where("name = ?", effectiveRoleName).First(&role).Error; err == nil {
			permissions = parsePermissionMap(role.Permissions)
			if len(permissions) > 0 {
				return permissions, nil
			}
		}
	}

	// 兼容历史数据：权限字段为空时使用默认模块权限。
	permissions = make(map[string]bool, len(defaultRolePermissionMap))
	for key, enabled := range defaultRolePermissionMap {
		if enabled {
			permissions[key] = true
		}
	}
	return permissions, nil
}

func hasAnyPermission(permissionMap map[string]bool, required ...string) bool {
	if len(required) == 0 {
		return true
	}
	for _, key := range required {
		key = strings.TrimSpace(key)
		if key == "" {
			continue
		}
		if permissionMap[key] {
			return true
		}
	}
	return false
}

func hasPermissionForUser(db *gorm.DB, userID uint, roleName string, required ...string) (bool, error) {
	if isAdminRole(roleName) {
		return true, nil
	}
	permissionMap, err := loadUserPermissionMap(db, userID, roleName)
	if err != nil {
		return false, err
	}
	return hasAnyPermission(permissionMap, required...), nil
}

func RequirePermission(db *gorm.DB, required ...string) gin.HandlerFunc {
	return func(c *gin.Context) {
		roleName := c.GetString("role")
		if isAdminRole(roleName) {
			c.Next()
			return
		}

		userID := c.GetUint("userId")
		if userID == 0 {
			c.JSON(http.StatusUnauthorized, gin.H{
				"code":    401,
				"message": "账号认证失效，请重新登录",
			})
			c.Abort()
			return
		}

		allowed, err := hasPermissionForUser(db, userID, roleName, required...)
		if err != nil {
			c.JSON(http.StatusUnauthorized, gin.H{
				"code":    401,
				"message": "账号认证失效，请重新登录",
			})
			c.Abort()
			return
		}
		if !allowed {
			c.JSON(http.StatusForbidden, gin.H{
				"code":    403,
				"message": "当前账号无权访问该功能",
			})
			c.Abort()
			return
		}
		c.Next()
	}
}
