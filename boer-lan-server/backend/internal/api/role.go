package api

import (
	"net/http"
	"strconv"
	"strings"

	"boer-lan-server/internal/model"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

type RoleHandler struct {
	db *gorm.DB
}

func NewRoleHandler(db *gorm.DB) *RoleHandler {
	return &RoleHandler{db: db}
}

// GetRoleList 角色列表（支持名称和创建时间筛选）
func (h *RoleHandler) GetRoleList(c *gin.Context) {
	keyword := strings.TrimSpace(c.Query("keyword"))
	startDate := strings.TrimSpace(c.Query("startDate"))
	endDate := strings.TrimSpace(c.Query("endDate"))

	query := h.db.Model(&model.Role{})
	if keyword != "" {
		like := "%" + keyword + "%"
		query = query.Where("name LIKE ? OR remark LIKE ?", like, like)
	}
	if startDate != "" {
		query = query.Where("DATE(created_at) >= ?", startDate)
	}
	if endDate != "" {
		query = query.Where("DATE(created_at) <= ?", endDate)
	}

	var roles []model.Role
	if err := query.Order("created_at DESC").Find(&roles).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	list := make([]gin.H, 0, len(roles))
	for _, role := range roles {
		list = append(list, gin.H{
			"id":              role.ID,
			"name":            role.Name,
			"remark":          role.Remark,
			"permissions":     role.Permissions,
			"parentChildLink": role.ParentChildLink,
			"createTime":      role.CreatedAt.Format("2006-01-02 15:04:05"),
		})
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": list,
	})
}

// CreateRole 新建角色
func (h *RoleHandler) CreateRole(c *gin.Context) {
	var req struct {
		Name            string `json:"name" binding:"required"`
		Remark          string `json:"remark"`
		Permissions     string `json:"permissions"`
		ParentChildLink *bool  `json:"parentChildLink"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	req.Name = strings.TrimSpace(req.Name)
	req.Remark = strings.TrimSpace(req.Remark)
	if !isValidRoleName(req.Name) {
		c.JSON(http.StatusBadRequest, gin.H{"error": "角色名称不能为空且不超过10个字"})
		return
	}

	permissions, err := normalizePermissionsJSON(req.Permissions)
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	var count int64
	if err := h.db.Model(&model.Role{}).Where("name = ?", req.Name).Count(&count).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	if count > 0 {
		c.JSON(http.StatusBadRequest, gin.H{"error": "角色名称已存在"})
		return
	}

	parentChildLink := true
	if req.ParentChildLink != nil {
		parentChildLink = *req.ParentChildLink
	}

	role := model.Role{
		Name:            req.Name,
		Remark:          req.Remark,
		Permissions:     permissions,
		ParentChildLink: parentChildLink,
	}

	if err := h.db.Create(&role).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"id":              role.ID,
			"name":            role.Name,
			"remark":          role.Remark,
			"permissions":     role.Permissions,
			"parentChildLink": role.ParentChildLink,
			"createTime":      role.CreatedAt.Format("2006-01-02 15:04:05"),
		},
	})
}

// UpdateRole 更新角色
func (h *RoleHandler) UpdateRole(c *gin.Context) {
	id, _ := strconv.Atoi(c.Param("id"))
	var role model.Role
	if err := h.db.First(&role, id).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{"error": "角色不存在"})
		return
	}

	var req struct {
		Name            *string `json:"name"`
		Remark          *string `json:"remark"`
		Permissions     *string `json:"permissions"`
		ParentChildLink *bool   `json:"parentChildLink"`
	}
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	updates := map[string]interface{}{}

	if req.Name != nil {
		name := strings.TrimSpace(*req.Name)
		if !isValidRoleName(name) {
			c.JSON(http.StatusBadRequest, gin.H{"error": "角色名称不能为空且不超过10个字"})
			return
		}

		var count int64
		if err := h.db.Model(&model.Role{}).Where("name = ? AND id <> ?", name, role.ID).Count(&count).Error; err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
			return
		}
		if count > 0 {
			c.JSON(http.StatusBadRequest, gin.H{"error": "角色名称已存在"})
			return
		}
		updates["name"] = name
	}

	if req.Remark != nil {
		updates["remark"] = strings.TrimSpace(*req.Remark)
	}

	if req.Permissions != nil {
		permissions, err := normalizePermissionsJSON(*req.Permissions)
		if err != nil {
			c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
			return
		}
		updates["permissions"] = permissions
	}

	if req.ParentChildLink != nil {
		updates["parent_child_link"] = *req.ParentChildLink
	}

	if len(updates) == 0 {
		c.JSON(http.StatusOK, gin.H{"code": 0, "msg": "无需更新"})
		return
	}

	if err := h.db.Model(&role).Updates(updates).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	// 角色名称变更时，同步用户表中的角色名
	if nameAny, ok := updates["name"]; ok {
		newName := nameAny.(string)
		if newName != role.Name {
			if err := h.db.Model(&model.User{}).Where("role = ?", role.Name).Update("role", newName).Error; err != nil {
				c.JSON(http.StatusInternalServerError, gin.H{"error": "更新用户角色失败: " + err.Error()})
				return
			}
		}
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"msg":  "更新成功",
	})
}

// DeleteRole 删除角色（有账号使用时禁止删除）
func (h *RoleHandler) DeleteRole(c *gin.Context) {
	id, _ := strconv.Atoi(c.Param("id"))
	var role model.Role
	if err := h.db.First(&role, id).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{"error": "角色不存在"})
		return
	}

	var userCount int64
	if err := h.db.Model(&model.User{}).Where("role = ?", role.Name).Count(&userCount).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	if userCount > 0 {
		c.JSON(http.StatusBadRequest, gin.H{"error": "该角色下仍有关联账号，无法删除"})
		return
	}

	if err := h.db.Delete(&role).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"msg":  "删除成功",
	})
}
