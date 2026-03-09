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
	role := strings.ToLower(strings.TrimSpace(roleName))
	if role == "" {
		role = strings.ToLower(strings.TrimSpace(user.Role))
	}

	// 管理员默认可查看所有分组；仅当显式配置了 group_ids 时按配置限制。
	explicitAdminGroupIDs := parseGroupIDsJSON(user.GroupIDs)
	if role == "admin" {
		if len(explicitAdminGroupIDs) == 0 {
			return userGroupScope{All: true}
		}
		return userGroupScope{All: false, GroupIDs: explicitAdminGroupIDs}
	}

	return userGroupScope{
		All:      false,
		GroupIDs: collectUserGroupIDs(user),
	}
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
