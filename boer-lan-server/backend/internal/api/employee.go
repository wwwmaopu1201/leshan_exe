package api

import (
	"boer-lan-server/internal/model"
	"net/http"
	"regexp"
	"strconv"
	"strings"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

type EmployeeHandler struct {
	db *gorm.DB
}

func NewEmployeeHandler(db *gorm.DB) *EmployeeHandler {
	return &EmployeeHandler{db: db}
}

func isValidEmployeeCode(code string) bool {
	code = strings.TrimSpace(code)
	if code == "" {
		return false
	}
	return len([]rune(code)) <= 11
}

func isValidEmployeePhone(phone string) bool {
	if phone == "" {
		return false
	}
	matched, _ := regexp.MatchString(`^1[3-9]\d{9}$`, phone)
	return matched
}

func (h *EmployeeHandler) GetEmployeeList(c *gin.Context) {
	var employees []model.Employee
	query := h.db.Model(&model.Employee{})

	if keyword := c.Query("keyword"); keyword != "" {
		like := "%" + strings.TrimSpace(keyword) + "%"
		query = query.Where("name LIKE ? OR code LIKE ? OR phone LIKE ? OR remark LIKE ?", like, like, like, like)
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
			"remark":     e.Remark,
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
	var req struct {
		Code       string `json:"code" binding:"required"`
		Name       string `json:"name" binding:"required"`
		Department string `json:"department"`
		Position   string `json:"position"`
		Phone      string `json:"phone"`
		Remark     string `json:"remark"`
	}
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}

	req.Code = strings.TrimSpace(req.Code)
	req.Name = strings.TrimSpace(req.Name)
	req.Phone = strings.TrimSpace(req.Phone)
	req.Remark = strings.TrimSpace(req.Remark)
	if !isValidEmployeeCode(req.Code) || req.Name == "" {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "员工工号不能为空且不能超过11位，姓名不能为空",
		})
		return
	}
	if !isValidEmployeePhone(req.Phone) {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "手机号不能为空且格式需正确",
		})
		return
	}

	var count int64
	h.db.Model(&model.Employee{}).Where("code = ?", req.Code).Count(&count)
	if count > 0 {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "员工工号已存在",
		})
		return
	}

	employee := model.Employee{
		Code:       req.Code,
		Name:       req.Name,
		Department: strings.TrimSpace(req.Department),
		Position:   strings.TrimSpace(req.Position),
		Phone:      req.Phone,
		Remark:     req.Remark,
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

	var req struct {
		Name       *string `json:"name"`
		Department *string `json:"department"`
		Position   *string `json:"position"`
		Phone      *string `json:"phone"`
		Remark     *string `json:"remark"`
	}
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}

	updates := map[string]interface{}{}

	if req.Department != nil || req.Position != nil || req.Remark != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "仅支持修改员工姓名和手机号",
		})
		return
	}

	if req.Name != nil {
		name := strings.TrimSpace(*req.Name)
		if name == "" {
			c.JSON(http.StatusBadRequest, gin.H{
				"code":    400,
				"message": "员工姓名不能为空",
			})
			return
		}
		updates["name"] = name
	}
	if req.Phone != nil {
		phone := strings.TrimSpace(*req.Phone)
		if !isValidEmployeePhone(phone) {
			c.JSON(http.StatusBadRequest, gin.H{
				"code":    400,
				"message": "手机号不能为空且格式需正确",
			})
			return
		}
		updates["phone"] = phone
	}
	if len(updates) == 0 {
		c.JSON(http.StatusOK, gin.H{
			"code":    0,
			"message": "success",
		})
		return
	}

	if err := h.db.Model(&employee).Updates(updates).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "更新失败",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
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

func (h *EmployeeHandler) ImportEmployees(c *gin.Context) {
	var req struct {
		Employees []struct {
			Code       string `json:"code" binding:"required"`
			Name       string `json:"name" binding:"required"`
			Department string `json:"department"`
			Position   string `json:"position"`
			Phone      string `json:"phone"`
			Remark     string `json:"remark"`
		} `json:"employees" binding:"required"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}

	tx := h.db.Begin()
	successCount := 0
	errorsList := make([]string, 0)

	for _, item := range req.Employees {
		code := strings.TrimSpace(item.Code)
		name := strings.TrimSpace(item.Name)
		phone := strings.TrimSpace(item.Phone)
		if !isValidEmployeeCode(code) || name == "" {
			errorsList = append(errorsList, "存在工号为空/超11位或姓名为空记录")
			continue
		}
		if !isValidEmployeePhone(phone) {
			errorsList = append(errorsList, code+" 手机号不能为空且格式需正确")
			continue
		}

		var count int64
		tx.Model(&model.Employee{}).Where("code = ?", code).Count(&count)
		if count > 0 {
			errorsList = append(errorsList, code+" 工号已存在")
			continue
		}

		employee := model.Employee{
			Code:       code,
			Name:       name,
			Department: strings.TrimSpace(item.Department),
			Position:   strings.TrimSpace(item.Position),
			Phone:      phone,
			Remark:     strings.TrimSpace(item.Remark),
		}

		if err := tx.Create(&employee).Error; err != nil {
			errorsList = append(errorsList, code+" 导入失败: "+err.Error())
			continue
		}
		successCount++
	}

	tx.Commit()

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"successCount": successCount,
			"errors":       errorsList,
		},
		"message": "success",
	})
}

func (h *EmployeeHandler) ExportEmployees(c *gin.Context) {
	var employees []model.Employee
	query := h.db.Model(&model.Employee{})

	if keyword := c.Query("keyword"); keyword != "" {
		like := "%" + strings.TrimSpace(keyword) + "%"
		query = query.Where("name LIKE ? OR code LIKE ? OR phone LIKE ? OR remark LIKE ?", like, like, like, like)
	}
	if department := c.Query("department"); department != "" {
		query = query.Where("department = ?", department)
	}

	if err := query.Order("id DESC").Find(&employees).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "导出失败",
		})
		return
	}

	list := make([]gin.H, 0, len(employees))
	for _, e := range employees {
		list = append(list, gin.H{
			"id":         e.ID,
			"code":       e.Code,
			"name":       e.Name,
			"department": e.Department,
			"position":   e.Position,
			"phone":      e.Phone,
			"remark":     e.Remark,
			"createTime": e.CreatedAt.Format("2006-01-02 15:04:05"),
		})
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": list,
	})
}
