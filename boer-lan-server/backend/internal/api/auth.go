package api

import (
	"encoding/json"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"boer-lan-server/internal/model"
	"boer-lan-server/pkg/utils"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

type AuthHandler struct {
	db        *gorm.DB
	jwtSecret string
	jwtExpire int
}

func NewAuthHandler(db *gorm.DB, jwtSecret string, jwtExpire int) *AuthHandler {
	return &AuthHandler{
		db:        db,
		jwtSecret: jwtSecret,
		jwtExpire: jwtExpire,
	}
}

type LoginRequest struct {
	Username string `json:"username" binding:"required"`
	Password string `json:"password" binding:"required"`
}

func encodeEffectivePermissionsJSON(db *gorm.DB, user model.User) string {
	permissionMap, err := loadUserPermissionMap(db, user.ID, user.Role)
	if err != nil || len(permissionMap) == 0 {
		return user.Permissions
	}

	encoded, err := json.Marshal(permissionMap)
	if err != nil {
		return user.Permissions
	}

	return string(encoded)
}

func (h *AuthHandler) recordLoginLog(c *gin.Context, userID uint, username, status string) {
	if h == nil || h.db == nil {
		return
	}
	username = strings.TrimSpace(username)
	status = strings.TrimSpace(status)
	if status == "" {
		status = "失败"
	}
	_ = h.db.Create(&model.LoginLog{
		UserID:    userID,
		Username:  username,
		IP:        c.ClientIP(),
		Device:    c.GetHeader("User-Agent"),
		Status:    status,
		LoginTime: time.Now(),
	}).Error
}

func (h *AuthHandler) Login(c *gin.Context) {
	var req LoginRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}

	var user model.User
	if err := h.db.Where("username = ?", req.Username).First(&user).Error; err != nil {
		h.recordLoginLog(c, 0, req.Username, "失败")
		c.JSON(http.StatusUnauthorized, gin.H{
			"code":    401,
			"message": "账号或密码错误",
		})
		return
	}

	if !utils.CheckPassword(req.Password, user.Password) {
		h.recordLoginLog(c, user.ID, user.Username, "失败")
		c.JSON(http.StatusUnauthorized, gin.H{
			"code":    401,
			"message": "账号或密码错误",
		})
		return
	}
	if user.Disabled {
		h.recordLoginLog(c, user.ID, user.Username, "失败")
		c.JSON(http.StatusForbidden, gin.H{
			"code":    403,
			"message": "账号已被禁用",
		})
		return
	}

	groupIDs := collectUserGroupIDs(user)
	effectivePermissions := encodeEffectivePermissionsJSON(h.db, user)

	// Generate token
	token, err := utils.GenerateToken(user.ID, user.Username, user.Role)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "生成Token失败",
		})
		return
	}

	// 记录登录日志
	h.recordLoginLog(c, user.ID, user.Username, "成功")

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"token": token,
			"user": gin.H{
				"id":          user.ID,
				"username":    user.Username,
				"nickname":    user.Nickname,
				"avatar":      user.Avatar,
				"role":        user.Role,
				"email":       user.Email,
				"phone":       user.Phone,
				"groupId":     user.GroupID,
				"groupIds":    groupIDs,
				"disabled":    user.Disabled,
				"permissions": effectivePermissions,
				"createTime":  user.CreatedAt.Format("2006-01-02 15:04:05"),
			},
		},
		"message": "success",
	})
}

func (h *AuthHandler) Logout(c *gin.Context) {
	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "success",
	})
}

func (h *AuthHandler) GetUserInfo(c *gin.Context) {
	userId := c.GetUint("userId")

	var user model.User
	if err := h.db.First(&user, userId).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "账号不存在",
		})
		return
	}

	groupIDs := collectUserGroupIDs(user)
	effectivePermissions := encodeEffectivePermissionsJSON(h.db, user)

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"id":          user.ID,
			"username":    user.Username,
			"nickname":    user.Nickname,
			"avatar":      user.Avatar,
			"role":        user.Role,
			"email":       user.Email,
			"phone":       user.Phone,
			"groupId":     user.GroupID,
			"groupIds":    groupIDs,
			"disabled":    user.Disabled,
			"permissions": effectivePermissions,
			"createTime":  user.CreatedAt.Format("2006-01-02 15:04:05"),
		},
		"message": "success",
	})
}

type ChangePasswordRequest struct {
	OldPassword string `json:"oldPassword" binding:"required"`
	NewPassword string `json:"newPassword" binding:"required,min=6"`
}

func (h *AuthHandler) ChangePassword(c *gin.Context) {
	var req ChangePasswordRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}
	req.OldPassword = strings.TrimSpace(req.OldPassword)
	req.NewPassword = strings.TrimSpace(req.NewPassword)
	if len(req.NewPassword) < 6 || len(req.NewPassword) > 32 {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "新密码长度需在6-32位",
		})
		return
	}
	if req.NewPassword == req.OldPassword {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "新密码不能与原密码相同",
		})
		return
	}

	userId := c.GetUint("userId")

	var user model.User
	if err := h.db.First(&user, userId).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "账号不存在",
		})
		return
	}

	if !utils.CheckPassword(req.OldPassword, user.Password) {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "原密码错误",
		})
		return
	}

	hashedPassword, err := utils.HashPassword(req.NewPassword)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "密码加密失败",
		})
		return
	}

	h.db.Model(&user).Update("password", hashedPassword)

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "密码修改成功",
	})
}

func (h *AuthHandler) GetLoginLogs(c *gin.Context) {
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("pageSize", "20"))
	if page < 1 {
		page = 1
	}
	if pageSize < 1 {
		pageSize = 20
	}
	if pageSize > 200 {
		pageSize = 200
	}

	keyword := strings.TrimSpace(c.Query("keyword"))
	status := strings.TrimSpace(c.Query("status"))
	startDate := strings.TrimSpace(c.Query("startDate"))
	endDate := strings.TrimSpace(c.Query("endDate"))

	query := h.db.Model(&model.LoginLog{}).
		Joins("LEFT JOIN users ON users.id = login_logs.user_id")
	if keyword != "" {
		like := "%" + keyword + "%"
		query = query.Where("(login_logs.username LIKE ? OR users.username LIKE ? OR login_logs.ip LIKE ?)", like, like, like)
	}
	if status != "" {
		query = query.Where("login_logs.status = ?", status)
	}
	if startDate != "" {
		query = query.Where("login_logs.login_time >= ?", startDate+" 00:00:00")
	}
	if endDate != "" {
		query = query.Where("login_logs.login_time <= ?", endDate+" 23:59:59")
	}

	var total int64
	query.Session(&gorm.Session{}).Count(&total)

	var logs []struct {
		ID        uint
		UserID    uint
		Username  string
		IP        string
		Device    string
		Status    string
		LoginTime time.Time
	}
	query.Session(&gorm.Session{}).
		Select("login_logs.id, login_logs.user_id, COALESCE(NULLIF(login_logs.username, ''), users.username, '-') as username, login_logs.ip, login_logs.device, login_logs.status, login_logs.login_time").
		Order("login_logs.login_time DESC, login_logs.id DESC").
		Offset((page - 1) * pageSize).
		Limit(pageSize).
		Scan(&logs)

	list := make([]gin.H, 0)
	for _, log := range logs {
		list = append(list, gin.H{
			"id":        log.ID,
			"userId":    log.UserID,
			"username":  log.Username,
			"ip":        log.IP,
			"device":    log.Device,
			"status":    log.Status,
			"loginTime": log.LoginTime.Format("2006-01-02 15:04:05"),
		})
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"list":     list,
			"total":    total,
			"page":     page,
			"pageSize": pageSize,
		},
		"message": "success",
	})
}

type UpdateProfileRequest struct {
	Nickname string `json:"nickname"`
	Email    string `json:"email"`
	Phone    string `json:"phone"`
}

func (h *AuthHandler) UpdateProfile(c *gin.Context) {
	var req UpdateProfileRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}

	userId := c.GetUint("userId")

	var user model.User
	if err := h.db.First(&user, userId).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "账号不存在",
		})
		return
	}

	updates := map[string]interface{}{}
	if req.Nickname != "" {
		updates["nickname"] = req.Nickname
	}
	if req.Email != "" {
		updates["email"] = req.Email
	}
	if req.Phone != "" {
		updates["phone"] = req.Phone
	}

	h.db.Model(&user).Updates(updates)

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "信息更新成功",
	})
}

func (h *AuthHandler) UploadAvatar(c *gin.Context) {
	userId := c.GetUint("userId")

	var user model.User
	if err := h.db.First(&user, userId).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "账号不存在",
		})
		return
	}

	file, err := c.FormFile("file")
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "请选择头像文件",
		})
		return
	}

	ext := strings.ToLower(filepath.Ext(file.Filename))
	switch ext {
	case ".png", ".jpg", ".jpeg", ".webp":
	default:
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "头像仅支持 png/jpg/jpeg/webp 格式",
		})
		return
	}

	if file.Size > 2*1024*1024 {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "头像文件不能超过 2MB",
		})
		return
	}

	avatarDir := filepath.Join("uploads", "avatars")
	if err := os.MkdirAll(avatarDir, 0755); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "创建头像目录失败",
		})
		return
	}

	fileName := time.Now().Format("20060102150405.000") + "_user_" + strings.TrimSpace(c.GetString("username")) + ext
	savePath := filepath.Join(avatarDir, fileName)
	oldAvatar := strings.TrimSpace(user.Avatar)
	if err := c.SaveUploadedFile(file, savePath); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "头像保存失败",
		})
		return
	}

	avatarURL := "/uploads/avatars/" + fileName

	if err := h.db.Model(&user).Update("avatar", avatarURL).Error; err != nil {
		_ = os.Remove(savePath)
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "头像更新失败",
		})
		return
	}

	if strings.HasPrefix(oldAvatar, "/uploads/avatars/") {
		oldPath := filepath.Clean("." + oldAvatar)
		if oldPath != filepath.Clean(savePath) {
			_ = os.Remove(oldPath)
		}
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"avatar": avatarURL,
		},
		"message": "头像更新成功",
	})
}
