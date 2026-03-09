package api

import (
	"encoding/json"
	"net/http"
	"strconv"
	"strings"

	"boer-lan-server/internal/model"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

type DeviceHandler struct {
	db *gorm.DB
}

func NewDeviceHandler(db *gorm.DB) *DeviceHandler {
	return &DeviceHandler{db: db}
}

func applyDeviceGroupParentScope(query *gorm.DB, parentID *uint) *gorm.DB {
	if parentID == nil {
		return query.Where("parent_id IS NULL")
	}
	return query.Where("parent_id = ?", *parentID)
}

func (h *DeviceHandler) isDescendantGroup(groupID uint, potentialDescendantID uint) bool {
	var groups []model.Group
	if err := h.db.Select("id", "parent_id").Find(&groups).Error; err != nil {
		return false
	}

	childMap := make(map[uint][]uint)
	for _, group := range groups {
		if group.ParentID == nil {
			continue
		}
		childMap[*group.ParentID] = append(childMap[*group.ParentID], group.ID)
	}

	queue := []uint{groupID}
	visited := map[uint]bool{groupID: true}
	for len(queue) > 0 {
		current := queue[0]
		queue = queue[1:]
		for _, child := range childMap[current] {
			if child == potentialDescendantID {
				return true
			}
			if !visited[child] {
				visited[child] = true
				queue = append(queue, child)
			}
		}
	}
	return false
}

func (h *DeviceHandler) GetDeviceTree(c *gin.Context) {
	var groups []model.Group
	h.db.
		Preload("Devices", func(db *gorm.DB) *gorm.DB { return db.Order("sort_order ASC, code ASC, id ASC") }).
		Where("parent_id IS NULL").
		Order("sort_order ASC, id ASC").
		Find(&groups)

	tree := h.buildTree(groups)
	var ungroupedDevices []model.Device
	if err := h.db.Where("group_id IS NULL").Order("code ASC").Find(&ungroupedDevices).Error; err == nil && len(ungroupedDevices) > 0 {
		tree = append(tree, gin.H{
			"id":       "ungrouped",
			"label":    "未分组设备",
			"children": h.buildDeviceNodes(ungroupedDevices),
		})
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"data":    tree,
		"message": "success",
	})
}

func (h *DeviceHandler) buildDeviceNodes(devices []model.Device) []gin.H {
	nodes := make([]gin.H, 0, len(devices))
	for _, d := range devices {
		label := d.Name
		if strings.TrimSpace(d.EmployeeName) != "" {
			label = d.Name + "（" + strings.TrimSpace(d.EmployeeName) + "）"
		}
		nodes = append(nodes, gin.H{
			"id":           d.ID,
			"label":        label,
			"type":         "device",
			"status":       d.Status,
			"model":        d.ModelName,
			"employeeCode": d.EmployeeCode,
			"employeeName": d.EmployeeName,
		})
	}
	return nodes
}

func (h *DeviceHandler) buildTree(groups []model.Group) []gin.H {
	result := make([]gin.H, 0)
	for _, g := range groups {
		node := gin.H{
			"id":    g.ID,
			"label": g.Name,
		}

		// Get children
		var children []model.Group
		h.db.
			Preload("Devices", func(db *gorm.DB) *gorm.DB { return db.Order("sort_order ASC, code ASC, id ASC") }).
			Where("parent_id = ?", g.ID).
			Order("sort_order ASC, id ASC").
			Find(&children)
		childNodes := make([]gin.H, 0, len(children)+len(g.Devices))
		if len(children) > 0 {
			childNodes = append(childNodes, h.buildTree(children)...)
		}
		if len(g.Devices) > 0 {
			childNodes = append(childNodes, h.buildDeviceNodes(g.Devices)...)
		}
		if len(childNodes) > 0 {
			node["children"] = childNodes
		}

		result = append(result, node)
	}
	return result
}

func (h *DeviceHandler) GetDeviceList(c *gin.Context) {
	var devices []model.Device
	query := h.db.Preload("Group")

	// Search
	if keyword := c.Query("keyword"); keyword != "" {
		like := "%" + strings.TrimSpace(keyword) + "%"
		query = query.Where(
			"name LIKE ? OR initial_name LIKE ? OR code LIKE ? OR type LIKE ? OR model_name LIKE ? OR ip LIKE ? OR employee_code LIKE ? OR employee_name LIKE ? OR mainboard_sn LIKE ? OR remark LIKE ?",
			like,
			like,
			like,
			like,
			like,
			like,
			like,
			like,
			like,
			like,
		)
	}

	// Filter by status
	if status := c.Query("status"); status != "" {
		query = query.Where("status = ?", status)
	}

	// Filter by group
	if groupId := c.Query("groupId"); groupId != "" {
		query = query.Where("group_id = ?", groupId)
	}

	// Filter by create date
	if startDate := strings.TrimSpace(c.Query("startDate")); startDate != "" {
		query = query.Where("DATE(created_at) >= ?", startDate)
	}
	if endDate := strings.TrimSpace(c.Query("endDate")); endDate != "" {
		query = query.Where("DATE(created_at) <= ?", endDate)
	}

	// Pagination
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("pageSize", "10"))
	if page < 1 {
		page = 1
	}
	if pageSize < 1 {
		pageSize = 10
	}
	if pageSize > 2000 {
		pageSize = 2000
	}
	offset := (page - 1) * pageSize

	var total int64
	query.Model(&model.Device{}).Count(&total)
	query.Order("group_id IS NULL DESC").
		Order("created_at DESC").
		Offset(offset).
		Limit(pageSize).
		Find(&devices)

	list := make([]gin.H, 0)
	for _, d := range devices {
		item := gin.H{
			"id":           d.ID,
			"code":         d.Code,
			"name":         d.Name,
			"initialName":  d.InitialName,
			"type":         d.Type,
			"model":        d.ModelName,
			"employeeCode": d.EmployeeCode,
			"employeeName": d.EmployeeName,
			"mainboardSn":  d.MainboardSN,
			"remark":       d.Remark,
			"ip":           d.IP,
			"status":       d.Status,
			"groupId":      d.GroupID,
			"sortOrder":    d.SortOrder,
			"createTime":   d.CreatedAt.Format("2006-01-02 15:04:05"),
		}
		if d.Group != nil {
			item["group"] = d.Group.Name
		}
		list = append(list, item)
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"list":  list,
			"total": total,
		},
		"message": "success",
	})
}

func (h *DeviceHandler) GetDevice(c *gin.Context) {
	id := c.Param("id")
	var device model.Device
	if err := h.db.Preload("Group").First(&device, id).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "设备不存在",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"data":    device,
		"message": "success",
	})
}

func (h *DeviceHandler) CreateDevice(c *gin.Context) {
	var device model.Device
	if err := c.ShouldBindJSON(&device); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}

	device.Code = strings.TrimSpace(device.Code)
	device.Name = strings.TrimSpace(device.Name)
	device.InitialName = strings.TrimSpace(device.InitialName)
	device.Type = strings.TrimSpace(device.Type)
	device.ModelName = strings.TrimSpace(device.ModelName)
	device.EmployeeCode = strings.TrimSpace(device.EmployeeCode)
	device.EmployeeName = strings.TrimSpace(device.EmployeeName)
	device.MainboardSN = strings.TrimSpace(device.MainboardSN)
	device.Remark = strings.TrimSpace(device.Remark)
	device.IP = strings.TrimSpace(device.IP)

	if device.Code == "" || device.Name == "" {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "设备编码和名称不能为空",
		})
		return
	}
	if device.InitialName == "" {
		device.InitialName = device.Name
	}

	if err := h.db.Create(&device).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "创建失败",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"data":    device,
		"message": "success",
	})
}

func (h *DeviceHandler) UpdateDevice(c *gin.Context) {
	id := c.Param("id")
	var device model.Device
	if err := h.db.First(&device, id).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "设备不存在",
		})
		return
	}

	var req struct {
		Code         *string         `json:"code"`
		Name         *string         `json:"name"`
		InitialName  *string         `json:"initialName"`
		SortOrder    *int            `json:"sortOrder"`
		Type         *string         `json:"type"`
		ModelName    *string         `json:"model"`
		IP           *string         `json:"ip"`
		Status       *string         `json:"status"`
		GroupID      json.RawMessage `json:"groupId"`
		EmployeeCode *string         `json:"employeeCode"`
		EmployeeName *string         `json:"employeeName"`
		MainboardSN  *string         `json:"mainboardSn"`
		Remark       *string         `json:"remark"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}

	updates := map[string]interface{}{}
	if req.Code != nil {
		code := strings.TrimSpace(*req.Code)
		if code == "" {
			c.JSON(http.StatusBadRequest, gin.H{
				"code":    400,
				"message": "设备编码不能为空",
			})
			return
		}
		updates["code"] = code
	}
	if req.Name != nil {
		name := strings.TrimSpace(*req.Name)
		if name == "" {
			c.JSON(http.StatusBadRequest, gin.H{
				"code":    400,
				"message": "设备名称不能为空",
			})
			return
		}
		updates["name"] = name
	}
	if req.InitialName != nil {
		updates["initial_name"] = strings.TrimSpace(*req.InitialName)
	}
	if req.SortOrder != nil {
		updates["sort_order"] = *req.SortOrder
	}
	if req.Type != nil {
		updates["type"] = strings.TrimSpace(*req.Type)
	}
	if req.ModelName != nil {
		updates["model_name"] = strings.TrimSpace(*req.ModelName)
	}
	if req.IP != nil {
		updates["ip"] = strings.TrimSpace(*req.IP)
	}
	if req.Status != nil {
		updates["status"] = strings.TrimSpace(*req.Status)
	}
	if len(req.GroupID) > 0 {
		groupRaw := strings.TrimSpace(string(req.GroupID))
		if groupRaw == "" || groupRaw == "null" {
			updates["group_id"] = nil
		} else {
			var groupID uint
			if err := json.Unmarshal(req.GroupID, &groupID); err != nil || groupID == 0 {
				c.JSON(http.StatusBadRequest, gin.H{
					"code":    400,
					"message": "分组参数错误",
				})
				return
			}
			updates["group_id"] = groupID
		}
	}
	if req.EmployeeCode != nil {
		updates["employee_code"] = strings.TrimSpace(*req.EmployeeCode)
	}
	if req.EmployeeName != nil {
		updates["employee_name"] = strings.TrimSpace(*req.EmployeeName)
	}
	if req.MainboardSN != nil {
		updates["mainboard_sn"] = strings.TrimSpace(*req.MainboardSN)
	}
	if req.Remark != nil {
		updates["remark"] = strings.TrimSpace(*req.Remark)
	}

	if len(updates) > 0 {
		if err := h.db.Model(&device).Updates(updates).Error; err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"code":    500,
				"message": "更新失败",
			})
			return
		}
	}

	_ = h.db.First(&device, device.ID).Error

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"data":    device,
		"message": "success",
	})
}

func (h *DeviceHandler) DeleteDevice(c *gin.Context) {
	id := c.Param("id")
	var device model.Device
	if err := h.db.First(&device, id).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "设备不存在",
		})
		return
	}

	updates := map[string]interface{}{
		"group_id":   nil,
		"sort_order": 0,
		// 删除设备仅移出分组并清空备注名，显示名回退为初始名（无初始名则回退到设备编码）
		"name": gorm.Expr("COALESCE(NULLIF(initial_name, ''), code)"),
	}

	if err := h.db.Model(&device).Updates(updates).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "移出分组失败",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "设备已移出分组",
	})
}

func (h *DeviceHandler) BatchDeleteDevices(c *gin.Context) {
	var req struct {
		IDs []uint `json:"ids"`
	}
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}

	if len(req.IDs) == 0 {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "设备ID不能为空",
		})
		return
	}

	updates := map[string]interface{}{
		"group_id":   nil,
		"sort_order": 0,
		// 批量删除与单个删除语义一致：移出分组并恢复为初始名/设备编码
		"name": gorm.Expr("COALESCE(NULLIF(initial_name, ''), code)"),
	}
	result := h.db.Model(&model.Device{}).Where("id IN ?", req.IDs).Updates(updates)
	if result.Error != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "批量移出分组失败",
		})
		return
	}
	if result.RowsAffected == 0 {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "设备不存在",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "设备已批量移出分组",
	})
}

func (h *DeviceHandler) MoveToGroup(c *gin.Context) {
	var req struct {
		DeviceIDs []uint `json:"deviceIds"`
		GroupID   *uint  `json:"groupId"`
	}
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}

	if len(req.DeviceIDs) == 0 {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "设备ID不能为空",
		})
		return
	}

	if req.GroupID != nil {
		var groupCount int64
		if err := h.db.Model(&model.Group{}).Where("id = ?", *req.GroupID).Count(&groupCount).Error; err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"code":    500,
				"message": "分组校验失败",
			})
			return
		}
		if groupCount == 0 {
			c.JSON(http.StatusBadRequest, gin.H{
				"code":    400,
				"message": "目标分组不存在",
			})
			return
		}
	}

	tx := h.db.Begin()
	if tx.Error != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "移动失败",
		})
		return
	}

	if req.GroupID == nil {
		if err := tx.Model(&model.Device{}).
			Where("id IN ?", req.DeviceIDs).
			Updates(map[string]interface{}{
				"group_id":   nil,
				"sort_order": 0,
			}).Error; err != nil {
			tx.Rollback()
			c.JSON(http.StatusInternalServerError, gin.H{
				"code":    500,
				"message": "移出分组失败",
			})
			return
		}
	} else {
		var maxSort int
		if err := tx.Model(&model.Device{}).
			Where("group_id = ?", *req.GroupID).
			Select("COALESCE(MAX(sort_order), 0)").
			Scan(&maxSort).Error; err != nil {
			tx.Rollback()
			c.JSON(http.StatusInternalServerError, gin.H{
				"code":    500,
				"message": "读取分组排序失败",
			})
			return
		}

		for index, deviceID := range req.DeviceIDs {
			if err := tx.Model(&model.Device{}).
				Where("id = ?", deviceID).
				Updates(map[string]interface{}{
					"group_id":   req.GroupID,
					"sort_order": maxSort + index + 1,
				}).Error; err != nil {
				tx.Rollback()
				c.JSON(http.StatusInternalServerError, gin.H{
					"code":    500,
					"message": "移动设备失败",
				})
				return
			}
		}
	}

	if err := tx.Commit().Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "移动失败",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "success",
	})
}

func (h *DeviceHandler) GetDeviceGroups(c *gin.Context) {
	var groups []model.Group
	h.db.Order("parent_id IS NOT NULL, parent_id, sort_order, id").Find(&groups)

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"data":    groups,
		"message": "success",
	})
}

func (h *DeviceHandler) CreateDeviceGroup(c *gin.Context) {
	var req struct {
		Name      string `json:"name" binding:"required"`
		ParentID  *uint  `json:"parentId"`
		SortOrder int    `json:"sortOrder"`
	}
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}

	name := strings.TrimSpace(req.Name)
	if name == "" {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "分组名称不能为空",
		})
		return
	}
	if len([]rune(name)) > 50 {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "分组名称不能超过50个字符",
		})
		return
	}

	if req.ParentID != nil {
		var parentCount int64
		if err := h.db.Model(&model.Group{}).Where("id = ?", *req.ParentID).Count(&parentCount).Error; err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"code":    500,
				"message": "创建分组失败",
			})
			return
		}
		if parentCount == 0 {
			c.JSON(http.StatusBadRequest, gin.H{
				"code":    400,
				"message": "父分组不存在",
			})
			return
		}
	}

	var duplicateCount int64
	dupQuery := applyDeviceGroupParentScope(h.db.Model(&model.Group{}), req.ParentID).
		Where("name = ?", name)
	if err := dupQuery.Count(&duplicateCount).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "创建分组失败",
		})
		return
	}
	if duplicateCount > 0 {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "同级分组名称已存在",
		})
		return
	}

	sortOrder := req.SortOrder
	if sortOrder <= 0 {
		var maxSort int
		h.db.Model(&model.Group{}).
			Where("parent_id IS ?", req.ParentID).
			Select("COALESCE(MAX(sort_order), 0)").
			Scan(&maxSort)
		sortOrder = maxSort + 1
	}

	group := model.Group{
		Name:      name,
		ParentID:  req.ParentID,
		SortOrder: sortOrder,
	}

	h.db.Create(&group)

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"data":    group,
		"message": "success",
	})
}

func (h *DeviceHandler) UpdateDeviceGroup(c *gin.Context) {
	id := c.Param("id")
	var group model.Group
	if err := h.db.First(&group, id).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "分组不存在",
		})
		return
	}

	var req struct {
		Name      *string         `json:"name"`
		ParentID  json.RawMessage `json:"parentId"`
		SortOrder *int            `json:"sortOrder"`
	}
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}

	updates := map[string]interface{}{}
	targetParentID := group.ParentID

	if len(req.ParentID) > 0 {
		raw := strings.TrimSpace(string(req.ParentID))
		if raw == "" || raw == "null" {
			targetParentID = nil
			updates["parent_id"] = nil
		} else {
			var parsedParentID uint
			if err := json.Unmarshal(req.ParentID, &parsedParentID); err != nil || parsedParentID == 0 {
				c.JSON(http.StatusBadRequest, gin.H{
					"code":    400,
					"message": "父分组参数错误",
				})
				return
			}
			if parsedParentID == group.ID {
				c.JSON(http.StatusBadRequest, gin.H{
					"code":    400,
					"message": "分组不能设置自己为父分组",
				})
				return
			}
			if h.isDescendantGroup(group.ID, parsedParentID) {
				c.JSON(http.StatusBadRequest, gin.H{
					"code":    400,
					"message": "不能移动到当前分组的子分组下",
				})
				return
			}

			var parentCount int64
			if err := h.db.Model(&model.Group{}).Where("id = ?", parsedParentID).Count(&parentCount).Error; err != nil {
				c.JSON(http.StatusInternalServerError, gin.H{
					"code":    500,
					"message": "更新分组失败",
				})
				return
			}
			if parentCount == 0 {
				c.JSON(http.StatusBadRequest, gin.H{
					"code":    400,
					"message": "父分组不存在",
				})
				return
			}
			targetParentID = &parsedParentID
			updates["parent_id"] = parsedParentID
		}
	}

	targetName := group.Name
	if req.Name != nil {
		name := strings.TrimSpace(*req.Name)
		if name == "" {
			c.JSON(http.StatusBadRequest, gin.H{
				"code":    400,
				"message": "分组名称不能为空",
			})
			return
		}
		if len([]rune(name)) > 50 {
			c.JSON(http.StatusBadRequest, gin.H{
				"code":    400,
				"message": "分组名称不能超过50个字符",
			})
			return
		}
		targetName = name
		updates["name"] = name
	}

	var duplicateCount int64
	dupQuery := applyDeviceGroupParentScope(h.db.Model(&model.Group{}), targetParentID).
		Where("id <> ? AND name = ?", group.ID, targetName)
	if err := dupQuery.Count(&duplicateCount).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "更新分组失败",
		})
		return
	}
	if duplicateCount > 0 {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "同级分组名称已存在",
		})
		return
	}

	if req.SortOrder != nil && *req.SortOrder > 0 {
		updates["sort_order"] = *req.SortOrder
	}

	parentChanged := false
	if len(req.ParentID) > 0 {
		switch {
		case group.ParentID == nil && targetParentID != nil:
			parentChanged = true
		case group.ParentID != nil && targetParentID == nil:
			parentChanged = true
		case group.ParentID != nil && targetParentID != nil && *group.ParentID != *targetParentID:
			parentChanged = true
		}
	}

	if parentChanged && req.SortOrder == nil {
		var maxSort int
		h.db.Model(&model.Group{}).
			Where("parent_id IS ?", targetParentID).
			Select("COALESCE(MAX(sort_order), 0)").
			Scan(&maxSort)
		updates["sort_order"] = maxSort + 1
	}

	if len(updates) == 0 {
		c.JSON(http.StatusOK, gin.H{
			"code":    0,
			"data":    group,
			"message": "success",
		})
		return
	}

	if err := h.db.Model(&group).Updates(updates).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "更新分组失败",
		})
		return
	}

	_ = h.db.First(&group, group.ID).Error

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"data":    group,
		"message": "success",
	})
}

func (h *DeviceHandler) DeleteDeviceGroup(c *gin.Context) {
	id := c.Param("id")
	var group model.Group
	if err := h.db.First(&group, id).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "分组不存在",
		})
		return
	}

	var groupCount int64
	h.db.Model(&model.Group{}).Count(&groupCount)
	if groupCount <= 1 {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "至少保留一个设备分组",
		})
		return
	}

	tx := h.db.Begin()
	if tx.Error != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "删除分组失败",
		})
		return
	}

	if err := tx.Model(&model.Device{}).Where("group_id = ?", group.ID).Update("group_id", nil).Error; err != nil {
		tx.Rollback()
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "清理设备分组失败",
		})
		return
	}

	if err := tx.Model(&model.Group{}).Where("parent_id = ?", group.ID).Update("parent_id", group.ParentID).Error; err != nil {
		tx.Rollback()
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "迁移子分组失败",
		})
		return
	}

	if err := tx.Delete(&model.Group{}, group.ID).Error; err != nil {
		tx.Rollback()
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "删除分组失败",
		})
		return
	}

	if err := tx.Commit().Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "删除分组失败",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "success",
	})
}
