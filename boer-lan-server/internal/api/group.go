package api

import (
	"boer-lan-server/internal/model"
	"net/http"
	"strconv"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

type GroupHandler struct {
	db *gorm.DB
}

func NewGroupHandler(db *gorm.DB) *GroupHandler {
	return &GroupHandler{db: db}
}

// GetGroupTree 获取分组树
func (h *GroupHandler) GetGroupTree(c *gin.Context) {
	var groups []model.Group

	// 只获取顶层分组
	if err := h.db.Preload("Children.Users").
		Preload("Children.Operators").
		Preload("Children.Devices").
		Preload("Users").
		Preload("Operators").
		Preload("Devices").
		Where("parent_id IS NULL").
		Order("sort_order, id").
		Find(&groups).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": groups,
	})
}

// GetGroupList 获取所有分组列表（扁平）
func (h *GroupHandler) GetGroupList(c *gin.Context) {
	var groups []model.Group

	if err := h.db.Preload("Parent").
		Order("parent_id, sort_order, id").
		Find(&groups).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": groups,
	})
}

// CreateGroup 创建分组
func (h *GroupHandler) CreateGroup(c *gin.Context) {
	var req struct {
		Name     string `json:"name" binding:"required"`
		ParentID *uint  `json:"parentId"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	group := model.Group{
		Name:     req.Name,
		ParentID: req.ParentID,
	}

	if err := h.db.Create(&group).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": group,
	})
}

// UpdateGroup 更新分组
func (h *GroupHandler) UpdateGroup(c *gin.Context) {
	id, _ := strconv.Atoi(c.Param("id"))

	var req struct {
		Name      string `json:"name"`
		SortOrder *int   `json:"sortOrder"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	updates := make(map[string]interface{})
	if req.Name != "" {
		updates["name"] = req.Name
	}
	if req.SortOrder != nil {
		updates["sort_order"] = *req.SortOrder
	}

	if err := h.db.Model(&model.Group{}).Where("id = ?", id).Updates(updates).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"msg":  "更新成功",
	})
}

// DeleteGroup 删除分组
func (h *GroupHandler) DeleteGroup(c *gin.Context) {
	id, _ := strconv.Atoi(c.Param("id"))

	// 检查是否有子分组
	var childCount int64
	h.db.Model(&model.Group{}).Where("parent_id = ?", id).Count(&childCount)
	if childCount > 0 {
		c.JSON(http.StatusBadRequest, gin.H{"error": "该分组下存在子分组，无法删除"})
		return
	}

	// 检查是否有用户、设备、操作员
	var userCount, deviceCount, operatorCount int64
	h.db.Model(&model.User{}).Where("group_id = ?", id).Count(&userCount)
	h.db.Model(&model.Device{}).Where("group_id = ?", id).Count(&deviceCount)
	h.db.Model(&model.Operator{}).Where("group_id = ?", id).Count(&operatorCount)

	if userCount > 0 || deviceCount > 0 || operatorCount > 0 {
		c.JSON(http.StatusBadRequest, gin.H{
			"error": "该分组下存在用户、设备或操作员，无法删除",
		})
		return
	}

	if err := h.db.Delete(&model.Group{}, id).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"msg":  "删除成功",
	})
}

// SortGroups 批量更新分组排序
func (h *GroupHandler) SortGroups(c *gin.Context) {
	var req []struct {
		ID        uint `json:"id" binding:"required"`
		SortOrder int  `json:"sortOrder"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	tx := h.db.Begin()
	for _, item := range req {
		if err := tx.Model(&model.Group{}).Where("id = ?", item.ID).
			Update("sort_order", item.SortOrder).Error; err != nil {
			tx.Rollback()
			c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
			return
		}
	}
	tx.Commit()

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"msg":  "排序成功",
	})
}
