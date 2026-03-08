package api

import (
	"boer-lan-server/internal/model"
	"net/http"
	"strconv"
	"strings"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

type GroupHandler struct {
	db *gorm.DB
}

func NewGroupHandler(db *gorm.DB) *GroupHandler {
	return &GroupHandler{db: db}
}

type groupTreeNode struct {
	ID        uint            `json:"id"`
	Name      string          `json:"name"`
	ParentID  *uint           `json:"parentId"`
	SortOrder int             `json:"sortOrder"`
	Children  []groupTreeNode `json:"children,omitempty"`
}

func applyParentScope(query *gorm.DB, parentID *uint) *gorm.DB {
	if parentID == nil {
		return query.Where("parent_id IS NULL")
	}
	return query.Where("parent_id = ?", *parentID)
}

// GetGroupTree 获取分组树
func (h *GroupHandler) GetGroupTree(c *gin.Context) {
	var groups []model.Group
	if err := h.db.Select("id", "name", "parent_id", "sort_order").
		Order("sort_order, id").
		Find(&groups).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	nodeByID := make(map[uint]*groupTreeNode, len(groups))
	for _, group := range groups {
		nodeByID[group.ID] = &groupTreeNode{
			ID:        group.ID,
			Name:      group.Name,
			ParentID:  group.ParentID,
			SortOrder: group.SortOrder,
			Children:  make([]groupTreeNode, 0),
		}
	}

	roots := make([]groupTreeNode, 0)
	for _, group := range groups {
		current := nodeByID[group.ID]
		if group.ParentID == nil {
			roots = append(roots, *current)
			continue
		}

		parent, ok := nodeByID[*group.ParentID]
		if !ok {
			roots = append(roots, *current)
			continue
		}
		parent.Children = append(parent.Children, *current)
	}

	// 二次回填子节点，保证树结构引用一致
	var attachChildren func(nodes []groupTreeNode) []groupTreeNode
	attachChildren = func(nodes []groupTreeNode) []groupTreeNode {
		result := make([]groupTreeNode, 0, len(nodes))
		for _, node := range nodes {
			copied := node
			if ptr, ok := nodeByID[node.ID]; ok && len(ptr.Children) > 0 {
				copied.Children = attachChildren(ptr.Children)
			}
			result = append(result, copied)
		}
		return result
	}
	roots = attachChildren(roots)

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": roots,
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

	name := strings.TrimSpace(req.Name)
	if name == "" {
		c.JSON(http.StatusBadRequest, gin.H{"error": "分组名称不能为空"})
		return
	}
	if len([]rune(name)) > 50 {
		c.JSON(http.StatusBadRequest, gin.H{"error": "分组名称不能超过50个字符"})
		return
	}

	if req.ParentID != nil {
		var parentCount int64
		if err := h.db.Model(&model.Group{}).Where("id = ?", *req.ParentID).Count(&parentCount).Error; err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
			return
		}
		if parentCount == 0 {
			c.JSON(http.StatusBadRequest, gin.H{"error": "父分组不存在"})
			return
		}
	}

	var siblingDuplicateCount int64
	dupQuery := applyParentScope(h.db.Model(&model.Group{}), req.ParentID).Where("name = ?", name)
	if err := dupQuery.Count(&siblingDuplicateCount).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	if siblingDuplicateCount > 0 {
		c.JSON(http.StatusBadRequest, gin.H{"error": "同级分组名称已存在"})
		return
	}

	sortOrder := 1
	var lastSibling model.Group
	if err := applyParentScope(h.db.Model(&model.Group{}), req.ParentID).
		Order("sort_order DESC, id DESC").
		First(&lastSibling).Error; err == nil {
		sortOrder = lastSibling.SortOrder + 1
	}

	group := model.Group{
		Name:      name,
		ParentID:  req.ParentID,
		SortOrder: sortOrder,
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
	var group model.Group
	if err := h.db.First(&group, id).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{"error": "分组不存在"})
		return
	}

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
		name := strings.TrimSpace(req.Name)
		if name == "" {
			c.JSON(http.StatusBadRequest, gin.H{"error": "分组名称不能为空"})
			return
		}
		if len([]rune(name)) > 50 {
			c.JSON(http.StatusBadRequest, gin.H{"error": "分组名称不能超过50个字符"})
			return
		}

		var duplicateCount int64
		dupQuery := applyParentScope(h.db.Model(&model.Group{}), group.ParentID).
			Where("id <> ? AND name = ?", group.ID, name)
		if err := dupQuery.Count(&duplicateCount).Error; err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
			return
		}
		if duplicateCount > 0 {
			c.JSON(http.StatusBadRequest, gin.H{"error": "同级分组名称已存在"})
			return
		}
		updates["name"] = name
	}
	if req.SortOrder != nil {
		updates["sort_order"] = *req.SortOrder
	}
	if len(updates) == 0 {
		c.JSON(http.StatusOK, gin.H{
			"code": 0,
			"msg":  "无需更新",
		})
		return
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
	var groupCount int64
	h.db.Model(&model.Group{}).Count(&groupCount)
	if groupCount <= 1 {
		c.JSON(http.StatusBadRequest, gin.H{"error": "至少保留一个分组，无法删除"})
		return
	}

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
	if len(req) == 0 {
		c.JSON(http.StatusBadRequest, gin.H{"error": "排序数据不能为空"})
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
