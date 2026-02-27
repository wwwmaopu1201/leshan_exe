package api

import (
	"net/http"
	"strconv"

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
	var groups []model.DeviceGroup
	h.db.Preload("Devices").Where("parent_id IS NULL").Find(&groups)

	tree := h.buildTree(groups)

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"data":    tree,
		"message": "success",
	})
}

func (h *DeviceHandler) buildTree(groups []model.DeviceGroup) []gin.H {
	result := make([]gin.H, 0)
	for _, g := range groups {
		node := gin.H{
			"id":    g.ID,
			"label": g.Name,
		}

		// Get children
		var children []model.DeviceGroup
		h.db.Preload("Devices").Where("parent_id = ?", g.ID).Find(&children)

		if len(children) > 0 {
			node["children"] = h.buildTree(children)
		} else {
			// Add devices as children
			deviceNodes := make([]gin.H, 0)
			for _, d := range g.Devices {
				deviceNodes = append(deviceNodes, gin.H{
					"id":     d.ID,
					"label":  d.Name,
					"type":   "device",
					"status": d.Status,
					"model":  d.Model,
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
		query = query.Where("name LIKE ? OR code LIKE ?", "%"+keyword+"%", "%"+keyword+"%")
	}

	// Filter by status
	if status := c.Query("status"); status != "" {
		query = query.Where("status = ?", status)
	}

	// Filter by group
	if groupId := c.Query("groupId"); groupId != "" {
		query = query.Where("group_id = ?", groupId)
	}

	// Pagination
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("pageSize", "10"))
	offset := (page - 1) * pageSize

	var total int64
	query.Model(&model.Device{}).Count(&total)
	query.Offset(offset).Limit(pageSize).Find(&devices)

	list := make([]gin.H, 0)
	for _, d := range devices {
		item := gin.H{
			"id":     d.ID,
			"code":   d.Code,
			"name":   d.Name,
			"type":   d.Type,
			"model":  d.Model,
			"ip":     d.IP,
			"status": d.Status,
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

	if err := c.ShouldBindJSON(&device); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}

	h.db.Save(&device)

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
		GroupID   uint   `json:"groupId"`
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
	var groups []model.DeviceGroup
	h.db.Find(&groups)

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"data":    groups,
		"message": "success",
	})
}

func (h *DeviceHandler) CreateDeviceGroup(c *gin.Context) {
	var group model.DeviceGroup
	if err := c.ShouldBindJSON(&group); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
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
	var group model.DeviceGroup
	if err := h.db.First(&group, id).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "分组不存在",
		})
		return
	}

	if err := c.ShouldBindJSON(&group); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
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
	h.db.Delete(&model.DeviceGroup{}, id)

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "success",
	})
}
