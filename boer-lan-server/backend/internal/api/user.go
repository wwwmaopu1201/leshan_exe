package api

import (
	"boer-lan-server/internal/model"
	"boer-lan-server/pkg/utils"
	"encoding/json"
	"net/http"
	"regexp"
	"strconv"
	"strings"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

type UserHandler struct {
	db *gorm.DB
}

func NewUserHandler(db *gorm.DB) *UserHandler {
	return &UserHandler{db: db}
}

func isValidUserPhone(phone string) bool {
	if phone == "" {
		return false
	}
	matched, _ := regexp.MatchString(`^1[3-9]\d{9}$`, phone)
	return matched
}

func isValidUsername(username string) bool {
	if username == "" || len(username) > 11 {
		return false
	}
	matched, _ := regexp.MatchString(`^[a-zA-Z0-9]+$`, username)
	return matched
}

// GetUserList 获取用户列表
func (h *UserHandler) GetUserList(c *gin.Context) {
	groupID := c.Query("groupId")
	var users []model.User

	query := h.db.Preload("Group")

	if groupID != "" {
		query = query.Where("group_id = ?", groupID)
	}

	if err := query.Find(&users).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": users,
	})
}

// GetAllUsers 加载全部用户
func (h *UserHandler) GetAllUsers(c *gin.Context) {
	keyword := strings.TrimSpace(c.Query("keyword"))
	startDate := strings.TrimSpace(c.Query("startDate"))
	endDate := strings.TrimSpace(c.Query("endDate"))
	role := strings.TrimSpace(c.Query("role"))

	query := h.db.Preload("Group.Parent").Model(&model.User{})
	if keyword != "" {
		like := "%" + keyword + "%"
		query = query.Where("username LIKE ? OR nickname LIKE ? OR phone LIKE ?", like, like, like)
	}
	if role != "" {
		query = query.Where("role = ?", role)
	}
	if startDate != "" {
		query = query.Where("DATE(created_at) >= ?", startDate)
	}
	if endDate != "" {
		query = query.Where("DATE(created_at) <= ?", endDate)
	}

	var users []model.User
	if err := query.Order("created_at DESC").Find(&users).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	userGroupIDs := make(map[uint][]uint, len(users))
	groupIDSet := make(map[uint]struct{})
	for _, user := range users {
		ids := collectUserGroupIDs(user)
		userGroupIDs[user.ID] = ids
		for _, groupID := range ids {
			groupIDSet[groupID] = struct{}{}
		}
	}

	groupInfoMap := make(map[uint]gin.H)
	if len(groupIDSet) > 0 {
		groupIDs := make([]uint, 0, len(groupIDSet))
		for groupID := range groupIDSet {
			groupIDs = append(groupIDs, groupID)
		}

		var relatedGroups []model.Group
		if err := h.db.Preload("Parent").Where("id IN ?", groupIDs).Find(&relatedGroups).Error; err == nil {
			for _, group := range relatedGroups {
				groupInfoMap[group.ID] = gin.H{
					"id":       group.ID,
					"name":     group.Name,
					"parentId": group.ParentID,
					"parent":   group.Parent,
				}
			}
		}
	}

	list := make([]gin.H, 0, len(users))
	for _, user := range users {
		groupIDs := userGroupIDs[user.ID]
		groups := make([]gin.H, 0, len(groupIDs))
		for _, groupID := range groupIDs {
			if groupInfo, ok := groupInfoMap[groupID]; ok {
				groups = append(groups, groupInfo)
			}
		}
		list = append(list, gin.H{
			"id":          user.ID,
			"username":    user.Username,
			"nickname":    user.Nickname,
			"email":       user.Email,
			"phone":       user.Phone,
			"role":        user.Role,
			"groupId":     user.GroupID,
			"groupIds":    groupIDs,
			"group":       user.Group,
			"groups":      groups,
			"disabled":    user.Disabled,
			"permissions": user.Permissions,
			"createTime":  user.CreatedAt.Format("2006-01-02 15:04:05"),
		})
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": list,
	})
}

// CreateUser 创建用户
func (h *UserHandler) CreateUser(c *gin.Context) {
	var req struct {
		Username    string `json:"username" binding:"required"`
		Password    string `json:"password" binding:"required"`
		Nickname    string `json:"nickname"`
		Email       string `json:"email"`
		Phone       string `json:"phone"`
		Role        string `json:"role"`
		GroupID     *uint  `json:"groupId"`
		GroupIDs    []uint `json:"groupIds"`
		Permissions string `json:"permissions"` // JSON字符串
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	req.Username = strings.TrimSpace(req.Username)
	req.Password = strings.TrimSpace(req.Password)
	req.Nickname = strings.TrimSpace(req.Nickname)
	req.Phone = strings.TrimSpace(req.Phone)
	req.Email = strings.TrimSpace(req.Email)

	if !isValidUsername(req.Username) {
		c.JSON(http.StatusBadRequest, gin.H{"error": "账号仅支持字母数字，且不超过11位"})
		return
	}
	if len(req.Password) < 6 || len(req.Password) > 32 {
		c.JSON(http.StatusBadRequest, gin.H{"error": "密码长度需在6-32位"})
		return
	}
	if req.Nickname == "" {
		c.JSON(http.StatusBadRequest, gin.H{"error": "账号姓名不能为空"})
		return
	}
	if !isValidUserPhone(req.Phone) {
		c.JSON(http.StatusBadRequest, gin.H{"error": "手机号不能为空且格式需正确"})
		return
	}

	// 检查用户名是否已存在
	var count int64
	h.db.Model(&model.User{}).Where("username = ?", req.Username).Count(&count)
	if count > 0 {
		c.JSON(http.StatusBadRequest, gin.H{"error": "账号已存在"})
		return
	}

	// 加密密码
	hashedPassword, err := utils.HashPassword(req.Password)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "密码加密失败"})
		return
	}

	req.Role = strings.TrimSpace(req.Role)
	if req.Role == "" {
		c.JSON(http.StatusBadRequest, gin.H{"error": "角色不能为空"})
		return
	}
	if err := ensureRoleExistsByName(h.db, req.Role); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": "角色不存在，请先在权限角色中创建"})
		return
	}

	groupIDs := append([]uint{}, req.GroupIDs...)
	if req.GroupID != nil && *req.GroupID > 0 {
		groupIDs = append(groupIDs, *req.GroupID)
	}
	groupIDs = normalizeGroupIDs(groupIDs)
	if !isAdminRole(req.Role) && len(groupIDs) > 1 {
		c.JSON(http.StatusBadRequest, gin.H{"error": "非管理员仅允许选择一个分组"})
		return
	}
	if err := ensureGroupIDsExist(h.db, groupIDs); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": "分组参数错误"})
		return
	}

	// 管理员未显式配置可见分组时，默认可查看全部分组。
	if isAdminRole(req.Role) && req.GroupID == nil && len(req.GroupIDs) == 0 {
		groupIDs = nil
	}

	var primaryGroupID *uint
	if len(groupIDs) > 0 {
		gid := groupIDs[0]
		primaryGroupID = &gid
	}

	// 默认权限：优先使用角色配置
	if req.Permissions == "" {
		var role model.Role
		if err := h.db.Where("name = ?", req.Role).First(&role).Error; err == nil {
			req.Permissions = role.Permissions
		} else {
			req.Permissions = defaultRolePermissionsJSON()
		}
	}

	user := model.User{
		Username:    req.Username,
		Password:    hashedPassword,
		Nickname:    req.Nickname,
		Email:       req.Email,
		Phone:       req.Phone,
		Role:        req.Role,
		GroupID:     primaryGroupID,
		GroupIDs:    encodeGroupIDs(groupIDs),
		Permissions: req.Permissions,
	}

	if err := h.db.Create(&user).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": user,
	})
}

// UpdateUser 更新用户
func (h *UserHandler) UpdateUser(c *gin.Context) {
	id, _ := strconv.Atoi(c.Param("id"))
	var user model.User
	if err := h.db.First(&user, id).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{"error": "用户不存在"})
		return
	}

	var req struct {
		Username    *string         `json:"username"`
		Password    *string         `json:"password"`
		Nickname    *string         `json:"nickname"`
		Email       *string         `json:"email"`
		Phone       *string         `json:"phone"`
		Role        *string         `json:"role"`
		GroupID     json.RawMessage `json:"groupId"`
		GroupIDs    json.RawMessage `json:"groupIds"`
		Disabled    *bool           `json:"disabled"`
		Permissions *string         `json:"permissions"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	updates := make(map[string]interface{})

	if req.Username != nil {
		if strings.TrimSpace(*req.Username) != user.Username {
			c.JSON(http.StatusBadRequest, gin.H{"error": "账号不允许修改"})
			return
		}
	}

	if req.Password != nil && *req.Password != "" {
		password := strings.TrimSpace(*req.Password)
		if len(password) < 6 || len(password) > 32 {
			c.JSON(http.StatusBadRequest, gin.H{"error": "密码长度需在6-32位"})
			return
		}
		hashedPassword, err := utils.HashPassword(password)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{"error": "密码加密失败"})
			return
		}
		updates["password"] = hashedPassword
	}

	if req.Nickname != nil {
		nickname := strings.TrimSpace(*req.Nickname)
		if nickname == "" {
			c.JSON(http.StatusBadRequest, gin.H{"error": "账号姓名不能为空"})
			return
		}
		updates["nickname"] = nickname
	}

	if req.Email != nil {
		updates["email"] = strings.TrimSpace(*req.Email)
	}

	if req.Phone != nil {
		phone := strings.TrimSpace(*req.Phone)
		if !isValidUserPhone(phone) {
			c.JSON(http.StatusBadRequest, gin.H{"error": "手机号不能为空且格式需正确"})
			return
		}
		updates["phone"] = phone
	}

	effectiveRole := strings.TrimSpace(user.Role)
	if req.Role != nil {
		roleName := strings.TrimSpace(*req.Role)
		if err := ensureRoleExistsByName(h.db, roleName); err != nil {
			c.JSON(http.StatusBadRequest, gin.H{"error": "角色不存在，请先在权限角色中创建"})
			return
		}
		effectiveRole = roleName
		updates["role"] = roleName

		// 如果仅切换角色，未显式传权限，则同步到角色默认权限
		if req.Permissions == nil {
			var role model.Role
			if err := h.db.Where("name = ?", roleName).First(&role).Error; err == nil {
				updates["permissions"] = role.Permissions
			}
		}
	}

	nextGroupIDs := collectUserGroupIDs(user)
	groupConfigProvided := false

	if len(req.GroupIDs) > 0 {
		groupConfigProvided = true
		raw := strings.TrimSpace(string(req.GroupIDs))
		if raw == "" || raw == "null" {
			nextGroupIDs = nil
		} else {
			var groupIDs []uint
			if err := json.Unmarshal(req.GroupIDs, &groupIDs); err != nil {
				c.JSON(http.StatusBadRequest, gin.H{"error": "分组参数错误"})
				return
			}
			nextGroupIDs = normalizeGroupIDs(groupIDs)
		}
	}

	if len(req.GroupID) > 0 {
		groupConfigProvided = true
		raw := strings.TrimSpace(string(req.GroupID))
		if raw == "" || raw == "null" {
			if len(req.GroupIDs) == 0 {
				nextGroupIDs = nil
			}
		} else {
			var groupID uint
			if err := json.Unmarshal(req.GroupID, &groupID); err != nil || groupID == 0 {
				c.JSON(http.StatusBadRequest, gin.H{"error": "分组参数错误"})
				return
			}
			if len(req.GroupIDs) > 0 {
				nextGroupIDs = normalizeGroupIDs(append(nextGroupIDs, groupID))
			} else {
				nextGroupIDs = []uint{groupID}
			}
		}
	}

	if !isAdminRole(effectiveRole) {
		if groupConfigProvided && len(nextGroupIDs) > 1 {
			c.JSON(http.StatusBadRequest, gin.H{"error": "非管理员仅允许选择一个分组"})
			return
		}
		if !groupConfigProvided && len(nextGroupIDs) > 1 {
			nextGroupIDs = nextGroupIDs[:1]
			groupConfigProvided = true
		}
	}

	if groupConfigProvided {
		if err := ensureGroupIDsExist(h.db, nextGroupIDs); err != nil {
			c.JSON(http.StatusBadRequest, gin.H{"error": "分组参数错误"})
			return
		}
		updates["group_ids"] = encodeGroupIDs(nextGroupIDs)
		if len(nextGroupIDs) > 0 {
			updates["group_id"] = nextGroupIDs[0]
		} else {
			updates["group_id"] = nil
		}
	}

	if req.Disabled != nil {
		updates["disabled"] = *req.Disabled
	}

	if req.Permissions != nil {
		updates["permissions"] = *req.Permissions
	}

	if err := h.db.Model(&model.User{}).Where("id = ?", id).Updates(updates).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"msg":  "更新成功",
	})
}

// DeleteUser 删除用户
func (h *UserHandler) DeleteUser(c *gin.Context) {
	var req struct {
		IDs []uint `json:"ids" binding:"required"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	// 不允许删除admin用户
	var adminCount int64
	h.db.Model(&model.User{}).Where("id IN ? AND username = ?", req.IDs, "admin").Count(&adminCount)
	if adminCount > 0 {
		c.JSON(http.StatusBadRequest, gin.H{"error": "不能删除管理员账户"})
		return
	}

	if err := h.db.Delete(&model.User{}, req.IDs).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"msg":  "删除成功",
	})
}

// MoveUsersToGroup 移动用户到其他分组
func (h *UserHandler) MoveUsersToGroup(c *gin.Context) {
	var req struct {
		UserIDs []uint `json:"userIds" binding:"required"`
		GroupID *uint  `json:"groupId" binding:"required"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	groupIDs := []uint{}
	if req.GroupID != nil && *req.GroupID > 0 {
		groupIDs = []uint{*req.GroupID}
	}
	if err := h.db.Model(&model.User{}).Where("id IN ?", req.UserIDs).
		Updates(map[string]interface{}{
			"group_id":  req.GroupID,
			"group_ids": encodeGroupIDs(groupIDs),
		}).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"msg":  "移动成功",
	})
}
