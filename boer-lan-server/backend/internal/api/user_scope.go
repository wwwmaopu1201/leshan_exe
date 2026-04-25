package api

import (
	"boer-lan-server/internal/model"
	"encoding/json"
	"errors"
	"sort"
	"strings"

	"gorm.io/gorm"
)

type userGroupScope struct {
	All      bool
	GroupIDs []uint
}

func isAdminRole(roleName string) bool {
	return strings.EqualFold(strings.TrimSpace(roleName), "admin")
}

func normalizeGroupIDs(ids []uint) []uint {
	if len(ids) == 0 {
		return nil
	}
	set := make(map[uint]struct{}, len(ids))
	for _, id := range ids {
		if id == 0 {
			continue
		}
		set[id] = struct{}{}
	}
	if len(set) == 0 {
		return nil
	}
	result := make([]uint, 0, len(set))
	for id := range set {
		result = append(result, id)
	}
	sort.Slice(result, func(i, j int) bool { return result[i] < result[j] })
	return result
}

func parseGroupIDsJSON(raw string) []uint {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return nil
	}

	var ids []uint
	if err := json.Unmarshal([]byte(raw), &ids); err != nil {
		return nil
	}
	return normalizeGroupIDs(ids)
}

func encodeGroupIDs(ids []uint) string {
	normalized := normalizeGroupIDs(ids)
	if len(normalized) == 0 {
		return ""
	}
	bytes, err := json.Marshal(normalized)
	if err != nil {
		return ""
	}
	return string(bytes)
}

func collectUserGroupIDs(user model.User) []uint {
	ids := parseGroupIDsJSON(user.GroupIDs)
	if user.GroupID != nil {
		ids = append(ids, *user.GroupID)
	}
	return normalizeGroupIDs(ids)
}

func ensureGroupIDsExist(db *gorm.DB, ids []uint) error {
	normalized := normalizeGroupIDs(ids)
	if len(normalized) == 0 {
		return nil
	}

	var count int64
	if err := db.Model(&model.Group{}).Where("id IN ?", normalized).Count(&count).Error; err != nil {
		return err
	}
	if int(count) != len(normalized) {
		return errors.New("存在无效分组")
	}
	return nil
}

func resolveUserGroupScope(user model.User, roleName string) userGroupScope {
	_ = roleName

	// 账号分组仅用于组织管理，不再参与业务数据的可见范围控制。
	// 功能可见性由角色/权限决定，拥有模块权限的账号应看到一致的数据。
	return userGroupScope{All: true}
}

func loadUserGroupScope(db *gorm.DB, userID uint, roleName string) (userGroupScope, error) {
	var user model.User
	if err := db.Select("id", "role", "group_id", "group_ids").First(&user, userID).Error; err != nil {
		return userGroupScope{}, err
	}
	return resolveUserGroupScope(user, roleName), nil
}

func containsGroupID(groupIDs []uint, groupID uint) bool {
	for _, id := range groupIDs {
		if id == groupID {
			return true
		}
	}
	return false
}
