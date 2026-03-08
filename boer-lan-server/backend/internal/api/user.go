package api

import (
	"boer-lan-server/internal/model"
	"boer-lan-server/pkg/utils"
	"net/http"
	"strconv"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

type UserHandler struct {
	db *gorm.DB
}

func NewUserHandler(db *gorm.DB) *UserHandler {
	return &UserHandler{db: db}
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
	var users []model.User

	if err := h.db.Preload("Group.Parent").Find(&users).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": users,
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
		Username    *string `json:"username"`
		Password    *string `json:"password"`
		Nickname    *string `json:"nickname"`
		Email       *string `json:"email"`
		Phone       *string `json:"phone"`
		Role        *string `json:"role"`
		Disabled    *bool   `json:"disabled"`
		Permissions *string `json:"permissions"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	updates := make(map[string]interface{})

	if req.Username != nil {
		// 检查用户名是否已被其他用户使用
		var count int64
		h.db.Model(&model.User{}).Where("username = ? AND id != ?", *req.Username, id).Count(&count)
		if count > 0 {
			c.JSON(http.StatusBadRequest, gin.H{"error": "用户名已存在"})
			return
		}
		updates["username"] = *req.Username
	}

	if req.Password != nil && *req.Password != "" {
		hashedPassword, err := utils.HashPassword(*req.Password)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{"error": "密码加密失败"})
			return
		}
		updates["password"] = hashedPassword
	}

	if req.Nickname != nil {
		updates["nickname"] = *req.Nickname
	}

	if req.Email != nil {
		updates["email"] = *req.Email
	}

	if req.Phone != nil {
		updates["phone"] = *req.Phone
	}

	if req.Role != nil {
		updates["role"] = *req.Role
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
