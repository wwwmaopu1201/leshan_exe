package api

import (
	"net/http"
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
		c.JSON(http.StatusUnauthorized, gin.H{
			"code":    401,
			"message": "用户名或密码错误",
		})
		return
	}

	if !utils.CheckPassword(req.Password, user.Password) {
		c.JSON(http.StatusUnauthorized, gin.H{
			"code":    401,
			"message": "用户名或密码错误",
		})
		return
	}

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
	h.db.Create(&model.LoginLog{
		UserID:    user.ID,
		IP:        c.ClientIP(),
		Device:    c.GetHeader("User-Agent"),
		Status:    "成功",
		LoginTime: time.Now(),
	})

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"token": token,
			"user": gin.H{
				"id":       user.ID,
				"username": user.Username,
				"nickname": user.Nickname,
				"role":     user.Role,
				"email":    user.Email,
				"phone":    user.Phone,
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
			"message": "用户不存在",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"id":       user.ID,
			"username": user.Username,
			"nickname": user.Nickname,
			"role":     user.Role,
			"email":    user.Email,
			"phone":    user.Phone,
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

	userId := c.GetUint("userId")

	var user model.User
	if err := h.db.First(&user, userId).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "用户不存在",
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
	userId := c.GetUint("userId")

	var logs []model.LoginLog
	h.db.Where("user_id = ?", userId).
		Order("login_time DESC").
		Limit(10).
		Find(&logs)

	list := make([]gin.H, 0)
	for _, log := range logs {
		list = append(list, gin.H{
			"time":   log.LoginTime.Format("2006-01-02 15:04:05"),
			"ip":     log.IP,
			"device": log.Device,
			"status": log.Status,
		})
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"data":    list,
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
			"message": "用户不存在",
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
