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
		return true
	}
	matched, _ := regexp.MatchString(`^1[3-9]\d{9}$`, phone)
	return matched
}

func isValidUsername(username string) bool {
	if username == "" || len(username) > 11 {
		return false
	}
	matched, _ := regexp.MatchString(`^[a-zA-Z0-9_]+$`, username)
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

	list := make([]gin.H, 0, len(users))
	for _, user := range users {
		list = append(list, gin.H{
			"id":          user.ID,
			"username":    user.Username,
			"nickname":    user.Nickname,
			"email":       user.Email,
			"phone":       user.Phone,
			"role":        user.Role,
			"groupId":     user.GroupID,
			"group":       user.Group,
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
		c.JSON(http.StatusBadRequest, gin.H{"error": "账号仅支持字母数字下划线，且不超过11位"})
		return
	}
	if len(req.Password) < 6 || len(req.Password) > 32 {
		c.JSON(http.StatusBadRequest, gin.H{"error": "密码长度需在6-32位"})
		return
	}
	if !isValidUserPhone(req.Phone) {
		c.JSON(http.StatusBadRequest, gin.H{"error": "手机号格式不正确"})
		return
	}

	// 检查用户名是否已存在
	var count int64
	h.db.Model(&model.User{}).Where("username = ?", req.Username).Count(&count)
	if count > 0 {
		c.JSON(http.StatusBadRequest, gin.H{"error": "用户名已存在"})
		return
	}

	// 加密密码
	hashedPassword, err := utils.HashPassword(req.Password)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "密码加密失败"})
		return
	}

	// 默认角色为user
	if req.Role == "" {
		req.Role = "user"
	}

	// 默认权限
	if req.Permissions == "" {
		req.Permissions = `{"fileManagement":true,"remoteMonitoring":true,"statistics":true,"deviceManagement":true}`
	}

	user := model.User{
		Username:    req.Username,
		Password:    hashedPassword,
		Nickname:    req.Nickname,
		Email:       req.Email,
		Phone:       req.Phone,
		Role:        req.Role,
		GroupID:     req.GroupID,
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

	var req struct {
		Username    *string         `json:"username"`
		Password    *string         `json:"password"`
		Nickname    *string         `json:"nickname"`
		Email       *string         `json:"email"`
		Phone       *string         `json:"phone"`
		Role        *string         `json:"role"`
		GroupID     json.RawMessage `json:"groupId"`
		Disabled    *bool           `json:"disabled"`
		Permissions *string         `json:"permissions"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	updates := make(map[string]interface{})

	if req.Username != nil {
		username := strings.TrimSpace(*req.Username)
		if !isValidUsername(username) {
			c.JSON(http.StatusBadRequest, gin.H{"error": "账号仅支持字母数字下划线，且不超过11位"})
			return
		}
		// 检查用户名是否已被其他用户使用
		var count int64
		h.db.Model(&model.User{}).Where("username = ? AND id != ?", username, id).Count(&count)
		if count > 0 {
			c.JSON(http.StatusBadRequest, gin.H{"error": "用户名已存在"})
			return
		}
		updates["username"] = username
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
		updates["nickname"] = strings.TrimSpace(*req.Nickname)
	}

	if req.Email != nil {
		updates["email"] = strings.TrimSpace(*req.Email)
	}

	if req.Phone != nil {
		phone := strings.TrimSpace(*req.Phone)
		if !isValidUserPhone(phone) {
			c.JSON(http.StatusBadRequest, gin.H{"error": "手机号格式不正确"})
			return
		}
		updates["phone"] = phone
	}

	if req.Role != nil {
		updates["role"] = strings.TrimSpace(*req.Role)
	}

	if len(req.GroupID) > 0 {
		raw := strings.TrimSpace(string(req.GroupID))
		if raw == "" || raw == "null" {
			updates["group_id"] = nil
		} else {
			var groupID uint
			if err := json.Unmarshal(req.GroupID, &groupID); err != nil || groupID == 0 {
				c.JSON(http.StatusBadRequest, gin.H{"error": "分组参数错误"})
				return
			}
			updates["group_id"] = groupID
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

	if err := h.db.Model(&model.User{}).Where("id IN ?", req.UserIDs).
		Update("group_id", req.GroupID).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"msg":  "移动成功",
	})
}
