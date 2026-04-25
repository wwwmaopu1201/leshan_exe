package api

import (
	"boer-lan-server/internal/model"
	"encoding/json"
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

func (h *EmployeeHandler) getCurrentUserScope(c *gin.Context) userGroupScope {
	userID := c.GetUint("userId")
	if userID == 0 {
		return userGroupScope{All: true}
	}

	scope, err := loadUserGroupScope(h.db, userID, c.GetString("role"))
	if err != nil {
		return userGroupScope{All: false, GroupIDs: nil}
	}
	return scope
}

func (h *EmployeeHandler) queryScopedEmployeeIDs(scope userGroupScope) ([]uint, error) {
	if scope.All {
		return nil, nil
	}
	if len(scope.GroupIDs) == 0 {
		return []uint{}, nil
	}

	allowedDeviceIDs := make([]uint, 0)
	if err := h.db.Model(&model.Device{}).
		Where("group_id IN ?", scope.GroupIDs).
		Pluck("id", &allowedDeviceIDs).Error; err != nil {
		return nil, err
	}
	allowedDeviceIDs = normalizeGroupIDs(allowedDeviceIDs)
	if len(allowedDeviceIDs) == 0 {
		employeeIDs := make([]uint, 0)
		if err := h.db.Model(&model.Employee{}).
			Where("group_id IN ?", scope.GroupIDs).
			Pluck("id", &employeeIDs).Error; err != nil {
			return nil, err
		}
		return normalizeGroupIDs(employeeIDs), nil
	}

	employeeIDs := make([]uint, 0)
	directEmployeeIDs := make([]uint, 0)
	if err := h.db.Model(&model.Employee{}).
		Where("group_id IN ?", scope.GroupIDs).
		Pluck("id", &directEmployeeIDs).Error; err != nil {
		return nil, err
	}
	employeeIDs = append(employeeIDs, directEmployeeIDs...)

	appendIDs := func(model interface{}, column string, extra func(*gorm.DB) *gorm.DB) error {
		ids := make([]uint, 0)
		query := h.db.Model(model).Where("device_id IN ?", allowedDeviceIDs)
		if extra != nil {
			query = extra(query)
		}
		if err := query.Distinct(column).Pluck(column, &ids).Error; err != nil {
			return err
		}
		employeeIDs = append(employeeIDs, ids...)
		return nil
	}

	if err := appendIDs(&model.EmployeeDevice{}, "employee_id", nil); err != nil {
		return nil, err
	}
	if err := appendIDs(&model.ProductionRecord{}, "employee_id", func(query *gorm.DB) *gorm.DB {
		return query.Where("employee_id > 0")
	}); err != nil {
		return nil, err
	}
	if err := appendIDs(&model.SalaryRecord{}, "employee_id", func(query *gorm.DB) *gorm.DB {
		return query.Where("employee_id > 0")
	}); err != nil {
		return nil, err
	}

	return normalizeGroupIDs(employeeIDs), nil
}

func (h *EmployeeHandler) ensureWritableEmployeeGroup(scope userGroupScope, groupID *uint) (*uint, int, string) {
	if scope.All {
		if groupID == nil || *groupID == 0 {
			return nil, http.StatusBadRequest, "请选择所属分组"
		}
		var count int64
		if err := h.db.Model(&model.Group{}).Where("id = ?", *groupID).Count(&count).Error; err != nil {
			return nil, http.StatusInternalServerError, "分组校验失败"
		}
		if count == 0 {
			return nil, http.StatusBadRequest, "所属分组不存在"
		}
		return groupID, http.StatusOK, ""
	}

	allowedGroupIDs := normalizeGroupIDs(scope.GroupIDs)
	if len(allowedGroupIDs) == 0 {
		return nil, http.StatusForbidden, "当前账号没有可用分组"
	}

	if groupID == nil || *groupID == 0 {
		if len(allowedGroupIDs) == 1 {
			resolved := allowedGroupIDs[0]
			return &resolved, http.StatusOK, ""
		}
		return nil, http.StatusBadRequest, "当前账号可管理多个分组，请先选择所属分组"
	}

	if !containsGroupID(allowedGroupIDs, *groupID) {
		return nil, http.StatusForbidden, "无权使用该所属分组"
	}

	var count int64
	if err := h.db.Model(&model.Group{}).Where("id = ?", *groupID).Count(&count).Error; err != nil {
		return nil, http.StatusInternalServerError, "分组校验失败"
	}
	if count == 0 {
		return nil, http.StatusBadRequest, "所属分组不存在"
	}
	return groupID, http.StatusOK, ""
}

func (h *EmployeeHandler) canAccessEmployee(scope userGroupScope, employeeID uint) (bool, error) {
	if scope.All {
		return true, nil
	}

	employeeIDs, err := h.queryScopedEmployeeIDs(scope)
	if err != nil {
		return false, err
	}
	for _, id := range employeeIDs {
		if id == employeeID {
			return true, nil
		}
	}
	return false, nil
}

func (h *EmployeeHandler) applyEmployeeFilters(query *gorm.DB, c *gin.Context) *gorm.DB {
	if keyword := strings.TrimSpace(c.Query("keyword")); keyword != "" {
		like := "%" + keyword + "%"
		query = query.Where("name LIKE ? OR remark LIKE ?", like, like)
	}

	if code := strings.TrimSpace(c.Query("code")); code != "" {
		query = query.Where("code LIKE ?", "%"+code+"%")
	}

	if phone := strings.TrimSpace(c.Query("phone")); phone != "" {
		query = query.Where("phone LIKE ?", "%"+phone+"%")
	}

	if startDate := strings.TrimSpace(c.Query("startDate")); startDate != "" {
		if startTime, err := parseDateFilter(startDate, false); err == nil && startTime != nil {
			query = query.Where("created_at >= ?", *startTime)
		}
	}

	if endDate := strings.TrimSpace(c.Query("endDate")); endDate != "" {
		if endTime, err := parseDateFilter(endDate, true); err == nil && endTime != nil {
			query = query.Where("created_at <= ?", *endTime)
		}
	}

	if groupID := strings.TrimSpace(c.Query("groupId")); groupID != "" {
		query = query.Where("group_id = ?", groupID)
	}

	if department := strings.TrimSpace(c.Query("department")); department != "" {
		query = query.Where("department = ?", department)
	}

	return query
}

func (h *EmployeeHandler) GetEmployeeGroups(c *gin.Context) {
	scope := h.getCurrentUserScope(c)
	var groups []model.Group
	if err := h.db.Order("parent_id IS NOT NULL, parent_id, sort_order, id").Find(&groups).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "查询分组失败",
		})
		return
	}

	if !scope.All {
		visibleSet := buildVisibleGroupSet(groups, scope.GroupIDs)
		filtered := make([]model.Group, 0, len(groups))
		for _, group := range groups {
			if _, ok := visibleSet[group.ID]; ok {
				filtered = append(filtered, group)
			}
		}
		groups = filtered
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"data":    groups,
		"message": "success",
	})
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
	query := h.db.Model(&model.Employee{}).Preload("Group")
	scope := h.getCurrentUserScope(c)

	if !scope.All {
		employeeIDs, err := h.queryScopedEmployeeIDs(scope)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"code":    500,
				"message": "查询员工范围失败",
			})
			return
		}
		if len(employeeIDs) == 0 {
			c.JSON(http.StatusOK, gin.H{
				"code": 0,
				"data": gin.H{
					"list":  []gin.H{},
					"total": 0,
				},
				"message": "success",
			})
			return
		}
		query = query.Where("id IN ?", employeeIDs)
	}
	query = h.applyEmployeeFilters(query, c)

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
			"groupId":    e.GroupID,
			"createTime": e.CreatedAt.Format("2006-01-02 15:04:05"),
		})
		if e.Group != nil {
			list[len(list)-1]["groupName"] = e.Group.Name
		}
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
	if err := h.db.Preload("Group").First(&employee, id).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "员工不存在",
		})
		return
	}

	scope := h.getCurrentUserScope(c)
	allowed, err := h.canAccessEmployee(scope, employee.ID)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "校验员工权限失败",
		})
		return
	}
	if !allowed {
		c.JSON(http.StatusForbidden, gin.H{
			"code":    403,
			"message": "无权访问该员工",
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
		GroupID    *uint  `json:"groupId"`
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

	scope := h.getCurrentUserScope(c)
	resolvedGroupID, statusCode, message := h.ensureWritableEmployeeGroup(scope, req.GroupID)
	if statusCode != http.StatusOK {
		c.JSON(statusCode, gin.H{
			"code":    statusCode,
			"message": message,
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
		GroupID:    resolvedGroupID,
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

	scope := h.getCurrentUserScope(c)
	allowed, err := h.canAccessEmployee(scope, employee.ID)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "校验员工权限失败",
		})
		return
	}
	if !allowed {
		c.JSON(http.StatusForbidden, gin.H{
			"code":    403,
			"message": "无权修改该员工",
		})
		return
	}

	var req struct {
		Name       *string         `json:"name"`
		Department *string         `json:"department"`
		Position   *string         `json:"position"`
		Phone      *string         `json:"phone"`
		Remark     *string         `json:"remark"`
		GroupID    json.RawMessage `json:"groupId"`
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
			"message": "仅支持修改员工姓名、手机号和所属分组",
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
	if len(req.GroupID) > 0 {
		raw := strings.TrimSpace(string(req.GroupID))
		if raw == "" || raw == "null" {
			c.JSON(http.StatusBadRequest, gin.H{
				"code":    400,
				"message": "所属分组不能为空",
			})
			return
		}

		var groupID uint
		if err := json.Unmarshal(req.GroupID, &groupID); err != nil || groupID == 0 {
			c.JSON(http.StatusBadRequest, gin.H{
				"code":    400,
				"message": "所属分组不合法",
			})
			return
		}

		resolvedGroupID, statusCode, message := h.ensureWritableEmployeeGroup(scope, &groupID)
		if statusCode != http.StatusOK {
			c.JSON(statusCode, gin.H{
				"code":    statusCode,
				"message": message,
			})
			return
		}
		updates["group_id"] = *resolvedGroupID
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
	var employee model.Employee
	if err := h.db.First(&employee, id).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "员工不存在",
		})
		return
	}

	scope := h.getCurrentUserScope(c)
	allowed, err := h.canAccessEmployee(scope, employee.ID)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "校验员工权限失败",
		})
		return
	}
	if !allowed {
		c.JSON(http.StatusForbidden, gin.H{
			"code":    403,
			"message": "无权删除该员工",
		})
		return
	}

	if err := h.db.Delete(&employee).Error; err != nil {
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
		GroupID   *uint `json:"groupId"`
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

	scope := h.getCurrentUserScope(c)
	resolvedGroupID, statusCode, message := h.ensureWritableEmployeeGroup(scope, req.GroupID)
	if statusCode != http.StatusOK {
		c.JSON(statusCode, gin.H{
			"code":    statusCode,
			"message": message,
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
			GroupID:    resolvedGroupID,
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
	query := h.db.Model(&model.Employee{}).Preload("Group")
	scope := h.getCurrentUserScope(c)

	if !scope.All {
		employeeIDs, err := h.queryScopedEmployeeIDs(scope)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"code":    500,
				"message": "查询员工范围失败",
			})
			return
		}
		if len(employeeIDs) == 0 {
			c.JSON(http.StatusOK, gin.H{
				"code": 0,
				"data": []gin.H{},
			})
			return
		}
		query = query.Where("id IN ?", employeeIDs)
	}
	query = h.applyEmployeeFilters(query, c)

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
			"groupId":    e.GroupID,
			"createTime": e.CreatedAt.Format("2006-01-02 15:04:05"),
		})
		if e.Group != nil {
			list[len(list)-1]["groupName"] = e.Group.Name
		}
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": list,
	})
}
