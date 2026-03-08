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

type OperatorHandler struct {
	db *gorm.DB
}

func NewOperatorHandler(db *gorm.DB) *OperatorHandler {
	return &OperatorHandler{db: db}
}

func isValidOperatorUsername(username string) bool {
	if username == "" || len(username) > 11 {
		return false
	}
	matched, _ := regexp.MatchString(`^[a-zA-Z0-9_]+$`, username)
	return matched
}

// GetOperatorList 获取操作员列表
func (h *OperatorHandler) GetOperatorList(c *gin.Context) {
	groupID := c.Query("groupId")
	var operators []model.Operator

	query := h.db.Preload("Group")

	if groupID != "" {
		query = query.Where("group_id = ?", groupID)
	}

	if err := query.Find(&operators).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": operators,
	})
}

// GetAllOperators 加载全部操作员
func (h *OperatorHandler) GetAllOperators(c *gin.Context) {
	keyword := strings.TrimSpace(c.Query("keyword"))
	startDate := strings.TrimSpace(c.Query("startDate"))
	endDate := strings.TrimSpace(c.Query("endDate"))

	query := h.db.Preload("Group.Parent").Model(&model.Operator{})
	if keyword != "" {
		like := "%" + keyword + "%"
		query = query.Where("username LIKE ? OR nickname LIKE ?", like, like)
	}
	if startDate != "" {
		query = query.Where("DATE(created_at) >= ?", startDate)
	}
	if endDate != "" {
		query = query.Where("DATE(created_at) <= ?", endDate)
	}

	var operators []model.Operator
	if err := query.Order("created_at DESC").Find(&operators).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	list := make([]gin.H, 0, len(operators))
	for _, op := range operators {
		list = append(list, gin.H{
			"id":         op.ID,
			"username":   op.Username,
			"nickname":   op.Nickname,
			"groupId":    op.GroupID,
			"group":      op.Group,
			"disabled":   op.Disabled,
			"createTime": op.CreatedAt.Format("2006-01-02 15:04:05"),
		})
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": list,
	})
}

// CreateOperator 创建操作员
func (h *OperatorHandler) CreateOperator(c *gin.Context) {
	var req struct {
		Username string `json:"username" binding:"required"`
		Password string `json:"password" binding:"required"`
		Nickname string `json:"nickname"`
		GroupID  *uint  `json:"groupId"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	req.Username = strings.TrimSpace(req.Username)
	req.Password = strings.TrimSpace(req.Password)
	req.Nickname = strings.TrimSpace(req.Nickname)

	if !isValidOperatorUsername(req.Username) {
		c.JSON(http.StatusBadRequest, gin.H{"error": "账号仅支持字母数字下划线，且不超过11位"})
		return
	}
	if len(req.Password) < 6 || len(req.Password) > 32 {
		c.JSON(http.StatusBadRequest, gin.H{"error": "密码长度需在6-32位"})
		return
	}

	// 检查用户名是否已存在
	var count int64
	h.db.Model(&model.Operator{}).Where("username = ?", req.Username).Count(&count)
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

	operator := model.Operator{
		Username: req.Username,
		Password: hashedPassword,
		Nickname: req.Nickname,
		GroupID:  req.GroupID,
	}

	if err := h.db.Create(&operator).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": operator,
	})
}

// UpdateOperator 更新操作员
func (h *OperatorHandler) UpdateOperator(c *gin.Context) {
	id, _ := strconv.Atoi(c.Param("id"))

	var req struct {
		Username *string         `json:"username"`
		Password *string         `json:"password"`
		Nickname *string         `json:"nickname"`
		GroupID  json.RawMessage `json:"groupId"`
		Disabled *bool           `json:"disabled"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	updates := make(map[string]interface{})

	if req.Username != nil {
		username := strings.TrimSpace(*req.Username)
		if !isValidOperatorUsername(username) {
			c.JSON(http.StatusBadRequest, gin.H{"error": "账号仅支持字母数字下划线，且不超过11位"})
			return
		}
		// 检查用户名是否已被其他操作员使用
		var count int64
		h.db.Model(&model.Operator{}).Where("username = ? AND id != ?", username, id).Count(&count)
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

	if err := h.db.Model(&model.Operator{}).Where("id = ?", id).Updates(updates).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"msg":  "更新成功",
	})
}

// DeleteOperator 删除操作员
func (h *OperatorHandler) DeleteOperator(c *gin.Context) {
	var req struct {
		IDs []uint `json:"ids" binding:"required"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	if err := h.db.Delete(&model.Operator{}, req.IDs).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"msg":  "删除成功",
	})
}

// MoveOperatorsToGroup 移动操作员到其他分组
func (h *OperatorHandler) MoveOperatorsToGroup(c *gin.Context) {
	var req struct {
		OperatorIDs []uint `json:"operatorIds" binding:"required"`
		GroupID     *uint  `json:"groupId" binding:"required"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	if err := h.db.Model(&model.Operator{}).Where("id IN ?", req.OperatorIDs).
		Update("group_id", req.GroupID).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"msg":  "移动成功",
	})
}

// ImportOperators 批量导入操作员
func (h *OperatorHandler) ImportOperators(c *gin.Context) {
	var req struct {
		Operators []struct {
			Username string `json:"username" binding:"required"`
			Password string `json:"password" binding:"required"`
			Nickname string `json:"nickname"`
			GroupID  *uint  `json:"groupId"`
		} `json:"operators" binding:"required"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	tx := h.db.Begin()
	successCount := 0
	var errors []string

	for _, op := range req.Operators {
		username := strings.TrimSpace(op.Username)
		password := strings.TrimSpace(op.Password)
		nickname := strings.TrimSpace(op.Nickname)
		if nickname == "" {
			nickname = username
		}
		if !isValidOperatorUsername(username) {
			errors = append(errors, op.Username+" 账号格式错误")
			continue
		}
		if len(password) < 6 || len(password) > 32 {
			errors = append(errors, op.Username+" 密码长度需在6-32位")
			continue
		}

		// 检查用户名是否已存在
		var count int64
		tx.Model(&model.Operator{}).Where("username = ?", username).Count(&count)
		if count > 0 {
			errors = append(errors, username+" 已存在")
			continue
		}

		hashedPassword, err := utils.HashPassword(password)
		if err != nil {
			errors = append(errors, username+" 密码加密失败")
			continue
		}

		operator := model.Operator{
			Username: username,
			Password: hashedPassword,
			Nickname: nickname,
			GroupID:  op.GroupID,
		}

		if err := tx.Create(&operator).Error; err != nil {
			errors = append(errors, username+" 创建失败: "+err.Error())
			continue
		}

		successCount++
	}

	tx.Commit()

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"successCount": successCount,
			"errors":       errors,
		},
	})
}

// ExportOperators 导出操作员列表
func (h *OperatorHandler) ExportOperators(c *gin.Context) {
	groupID := c.Query("groupId")
	keyword := strings.TrimSpace(c.Query("keyword"))
	startDate := strings.TrimSpace(c.Query("startDate"))
	endDate := strings.TrimSpace(c.Query("endDate"))

	var operators []model.Operator
	query := h.db.Preload("Group")

	if groupID != "" {
		query = query.Where("group_id = ?", groupID)
	}
	if keyword != "" {
		like := "%" + keyword + "%"
		query = query.Where("username LIKE ? OR nickname LIKE ?", like, like)
	}
	if startDate != "" {
		query = query.Where("DATE(created_at) >= ?", startDate)
	}
	if endDate != "" {
		query = query.Where("DATE(created_at) <= ?", endDate)
	}

	if err := query.Order("created_at DESC").Find(&operators).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	// 转换为导出格式（不包含密码）
	exportData := make([]gin.H, len(operators))
	for i, op := range operators {
		exportData[i] = gin.H{
			"username":   op.Username,
			"nickname":   op.Nickname,
			"disabled":   op.Disabled,
			"groupId":    op.GroupID,
			"createTime": op.CreatedAt.Format("2006-01-02 15:04:05"),
		}
		if op.Group != nil {
			exportData[i]["groupName"] = op.Group.Name
		}
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": exportData,
	})
}
