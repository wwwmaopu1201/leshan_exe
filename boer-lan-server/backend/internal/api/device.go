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

func (h *DeviceHandler) GetDeviceTree(c *gin.Context) {
	var groups []model.Group
	h.db.Preload("Devices").Where("parent_id IS NULL").Find(&groups)

	tree := h.buildTree(groups)

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"data":    tree,
		"message": "success",
	})
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
		h.db.Preload("Devices").Where("parent_id = ?", g.ID).Find(&children)

		if len(children) > 0 {
			node["children"] = h.buildTree(children)
		} else {
			// Add devices as children
			deviceNodes := make([]gin.H, 0)
			for _, d := range g.Devices {
				label := d.Name
				if strings.TrimSpace(d.EmployeeName) != "" {
					label = d.Name + "（" + strings.TrimSpace(d.EmployeeName) + "）"
				}
				deviceNodes = append(deviceNodes, gin.H{
					"id":           d.ID,
					"label":        label,
					"type":         "device",
					"status":       d.Status,
					"model":        d.ModelName,
					"employeeCode": d.EmployeeCode,
					"employeeName": d.EmployeeName,
				})
			}
			if len(deviceNodes) > 0 {
				node["children"] = deviceNodes
			}
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
	if err := h.db.Delete(&model.Device{}, id).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "删除失败",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "success",
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

	h.db.Delete(&model.Device{}, req.IDs)

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "success",
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

	h.db.Model(&model.Device{}).Where("id IN ?", req.DeviceIDs).Update("group_id", req.GroupID)

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "success",
	})
}

func (h *DeviceHandler) GetDeviceGroups(c *gin.Context) {
	var groups []model.Group
	h.db.Find(&groups)

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"data":    groups,
		"message": "success",
	})
}

func (h *DeviceHandler) CreateDeviceGroup(c *gin.Context) {
	var group model.Group
	if err := c.ShouldBindJSON(&group); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}

	if group.SortOrder == 0 {
		var maxSort int
		h.db.Model(&model.Group{}).
			Where("parent_id IS ?", group.ParentID).
			Select("COALESCE(MAX(sort_order), 0)").
			Scan(&maxSort)
		group.SortOrder = maxSort + 1
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

	originalParentID := group.ParentID

	if err := c.ShouldBindJSON(&group); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}

	if group.SortOrder == 0 && (originalParentID == nil && group.ParentID != nil || originalParentID != nil && group.ParentID == nil || (originalParentID != nil && group.ParentID != nil && *originalParentID != *group.ParentID)) {
		var maxSort int
		h.db.Model(&model.Group{}).
			Where("parent_id IS ?", group.ParentID).
			Select("COALESCE(MAX(sort_order), 0)").
			Scan(&maxSort)
		group.SortOrder = maxSort + 1
	}

	h.db.Save(&group)

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
