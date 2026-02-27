package api

import (
	"net/http"
	"strconv"

	"boer-lan-server/internal/model"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

type EmployeeHandler struct {
	db *gorm.DB
}

func NewEmployeeHandler(db *gorm.DB) *EmployeeHandler {
	return &EmployeeHandler{db: db}
}

func (h *EmployeeHandler) GetEmployeeList(c *gin.Context) {
	var employees []model.Employee
	query := h.db.Model(&model.Employee{})

	if keyword := c.Query("keyword"); keyword != "" {
		query = query.Where("name LIKE ? OR code LIKE ?", "%"+keyword+"%", "%"+keyword+"%")
	}

	if department := c.Query("department"); department != "" {
		query = query.Where("department = ?", department)
	}

	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("pageSize", "10"))
	offset := (page - 1) * pageSize

	var total int64
	query.Count(&total)
	query.Offset(offset).Limit(pageSize).Find(&employees)

	list := make([]gin.H, 0)
	for _, e := range employees {
		list = append(list, gin.H{
			"id":         e.ID,
			"code":       e.Code,
			"name":       e.Name,
			"department": e.Department,
			"position":   e.Position,
			"phone":      e.Phone,
			"createTime": e.CreatedAt.Format("2006-01-02 15:04:05"),
		})
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

func (h *EmployeeHandler) GetEmployee(c *gin.Context) {
	id := c.Param("id")
	var employee model.Employee
	if err := h.db.First(&employee, id).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "员工不存在",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"data":    employee,
		"message": "success",
	})
}

func (h *EmployeeHandler) CreateEmployee(c *gin.Context) {
	var employee model.Employee
	if err := c.ShouldBindJSON(&employee); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}

	if err := h.db.Create(&employee).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "创建失败",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"data":    employee,
		"message": "success",
	})
}

func (h *EmployeeHandler) UpdateEmployee(c *gin.Context) {
	id := c.Param("id")
	var employee model.Employee
	if err := h.db.First(&employee, id).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "员工不存在",
		})
		return
	}

	if err := c.ShouldBindJSON(&employee); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}

	h.db.Save(&employee)

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"data":    employee,
		"message": "success",
	})
}

func (h *EmployeeHandler) DeleteEmployee(c *gin.Context) {
	id := c.Param("id")
	if err := h.db.Delete(&model.Employee{}, id).Error; err != nil {
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
