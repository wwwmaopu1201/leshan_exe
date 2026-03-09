package api

import (
	"errors"
	"math"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"boer-lan-server/internal/model"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

type PatternHandler struct {
	db *gorm.DB
}

var (
	errPatternNameFormat = errors.New("pattern name format invalid")
	errPatternFieldValue = errors.New("pattern field invalid")
)

type PatternUpdateRequest struct {
	Name        *string  `json:"name"`
	PatternType *string  `json:"patternType"`
	Stitches    *int     `json:"stitches"`
	UnitPrice   *float64 `json:"unitPrice"`
	OrderNo     *string  `json:"orderNo"`
}

type PatternBatchUpdateRequest struct {
	IDs []uint `json:"ids" binding:"required"`
	PatternUpdateRequest
}

func NewPatternHandler(db *gorm.DB) *PatternHandler {
	return &PatternHandler{db: db}
}

func roundTo3(v float64) float64 {
	return math.Round(v*1000) / 1000
}

func parseDateFilter(dateStr string, endOfDay bool) (*time.Time, error) {
	dateStr = strings.TrimSpace(dateStr)
	if dateStr == "" {
		return nil, nil
	}

	t, err := time.ParseInLocation("2006-01-02", dateStr, time.Local)
	if err != nil {
		return nil, err
	}

	if endOfDay {
		end := t.Add(24*time.Hour - time.Nanosecond)
		return &end, nil
	}

	return &t, nil
}

func parsePagination(c *gin.Context) (int, int) {
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("pageSize", "10"))
	if page < 1 {
		page = 1
	}
	if pageSize < 1 {
		pageSize = 10
	}
	if pageSize > 200 {
		pageSize = 200
	}
	return page, pageSize
}

func formatFileSize(size int64) string {
	const (
		KB = 1024
		MB = 1024 * KB
	)
	if size >= MB {
		return strconv.FormatFloat(float64(size)/float64(MB), 'f', 1, 64) + "MB"
	}
	if size >= KB {
		return strconv.FormatFloat(float64(size)/float64(KB), 'f', 1, 64) + "KB"
	}
	return strconv.FormatInt(size, 10) + "B"
}

func isValidPatternName(name string) bool {
	name = strings.TrimSpace(name)
	if name == "" {
		return false
	}
	parts := strings.FieldsFunc(name, func(r rune) bool {
		return r == '+' || r == '＋'
	})
	if len(parts) != 3 {
		return false
	}
	for _, part := range parts {
		if strings.TrimSpace(part) == "" {
			return false
		}
	}
	return true
}

func (h *PatternHandler) validateAndBuildPatternUpdates(req PatternUpdateRequest) (map[string]interface{}, error) {
	updates := make(map[string]interface{})

	if req.Name != nil {
		name := strings.TrimSpace(*req.Name)
		if !isValidPatternName(name) {
			return nil, errPatternNameFormat
		}
		updates["name"] = name
	}

	if req.PatternType != nil {
		updates["pattern_type"] = strings.TrimSpace(*req.PatternType)
	}

	if req.Stitches != nil {
		if *req.Stitches < 0 {
			return nil, errPatternFieldValue
		}
		updates["stitches"] = *req.Stitches
	}

	if req.UnitPrice != nil {
		if *req.UnitPrice < 0 {
			return nil, errPatternFieldValue
		}
		updates["unit_price"] = roundTo3(*req.UnitPrice)
	}

	if req.OrderNo != nil {
		updates["order_no"] = strings.TrimSpace(*req.OrderNo)
	}

	return updates, nil
}

func (h *PatternHandler) GetPatternList(c *gin.Context) {
	var patterns []model.Pattern
	query := h.db.Model(&model.Pattern{})

	if keyword := strings.TrimSpace(c.Query("keyword")); keyword != "" {
		like := "%" + keyword + "%"
		query = query.Where(
			"name LIKE ? OR file_name LIKE ? OR pattern_type LIKE ? OR order_no LIKE ?",
			like,
			like,
			like,
			like,
		)
	}

	if patternType := strings.TrimSpace(c.Query("patternType")); patternType != "" {
		query = query.Where("pattern_type = ?", patternType)
	}

	if orderNo := strings.TrimSpace(c.Query("orderNo")); orderNo != "" {
		query = query.Where("order_no LIKE ?", "%"+orderNo+"%")
	}

	startDate, err := parseDateFilter(c.Query("startDate"), false)
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "开始日期格式错误，应为 yyyy-MM-dd",
		})
		return
	}
	endDate, err := parseDateFilter(c.Query("endDate"), true)
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "结束日期格式错误，应为 yyyy-MM-dd",
		})
		return
	}

	if startDate != nil {
		query = query.Where("created_at >= ?", *startDate)
	}
	if endDate != nil {
		query = query.Where("created_at <= ?", *endDate)
	}

	page, pageSize := parsePagination(c)
	offset := (page - 1) * pageSize

	var total int64
	if err := query.Count(&total).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "查询花型总数失败",
		})
		return
	}

	if err := query.Offset(offset).Limit(pageSize).Order("created_at DESC").Find(&patterns).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "查询花型列表失败",
		})
		return
	}

	list := make([]gin.H, 0, len(patterns))
	for _, p := range patterns {
		list = append(list, gin.H{
			"id":          p.ID,
			"name":        p.Name,
			"patternType": p.PatternType,
			"fileName":    p.FileName,
			"size":        formatFileSize(p.FileSize),
			"fileSize":    p.FileSize,
			"stitches":    p.Stitches,
			"colors":      p.Colors,
			"width":       p.Width,
			"height":      p.Height,
			"unitPrice":   roundTo3(p.UnitPrice),
			"orderNo":     p.OrderNo,
			"uploadTime":  p.CreatedAt.Format("2006-01-02 15:04:05"),
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

func (h *PatternHandler) GetPatternTypes(c *gin.Context) {
	types := make([]string, 0)
	if err := h.db.Model(&model.Pattern{}).
		Where("pattern_type <> ''").
		Distinct("pattern_type").
		Order("pattern_type ASC").
		Pluck("pattern_type", &types).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "查询花型类型失败",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": types,
	})
}

func (h *PatternHandler) UploadPattern(c *gin.Context) {
	file, err := c.FormFile("file")
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "请选择文件",
		})
		return
	}

	if err := os.MkdirAll(filepath.Join("uploads", "patterns"), 0755); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "创建上传目录失败",
		})
		return
	}

	filename := time.Now().Format("20060102150405.000") + "_" + file.Filename
	savePath := filepath.Join("uploads", "patterns", filename)

	if err := c.SaveUploadedFile(file, savePath); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "文件保存失败",
		})
		return
	}

	name := strings.TrimSpace(c.PostForm("name"))
	if name == "" {
		name = file.Filename
	} else if !isValidPatternName(name) {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "花型名称需为“款式+部位+尺码”格式",
		})
		return
	}
	patternType := strings.TrimSpace(c.PostForm("patternType"))
	orderNo := strings.TrimSpace(c.PostForm("orderNo"))

	stitches := 0
	if stitchesStr := strings.TrimSpace(c.PostForm("stitches")); stitchesStr != "" {
		parsed, err := strconv.Atoi(stitchesStr)
		if err != nil || parsed < 0 {
			c.JSON(http.StatusBadRequest, gin.H{
				"code":    400,
				"message": "针数必须是非负整数",
			})
			return
		}
		stitches = parsed
	}

	unitPrice := 0.0
	if unitPriceStr := strings.TrimSpace(c.PostForm("unitPrice")); unitPriceStr != "" {
		parsed, err := strconv.ParseFloat(unitPriceStr, 64)
		if err != nil || parsed < 0 {
			c.JSON(http.StatusBadRequest, gin.H{
				"code":    400,
				"message": "工价必须是非负数字",
			})
			return
		}
		unitPrice = parsed
	}

	userID := c.GetUint("userId")
	pattern := model.Pattern{
		Name:        name,
		PatternType: patternType,
		FileName:    filename,
		FilePath:    savePath,
		FileSize:    file.Size,
		Stitches:    stitches,
		UnitPrice:   roundTo3(unitPrice),
		OrderNo:     orderNo,
		UploadedBy:  userID,
	}

	if err := h.db.Create(&pattern).Error; err != nil {
		_ = os.Remove(savePath)
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "保存花型记录失败",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"id":          pattern.ID,
			"name":        pattern.Name,
			"patternType": pattern.PatternType,
			"fileName":    pattern.FileName,
			"fileSize":    pattern.FileSize,
			"size":        formatFileSize(pattern.FileSize),
			"stitches":    pattern.Stitches,
			"unitPrice":   roundTo3(pattern.UnitPrice),
			"orderNo":     pattern.OrderNo,
			"uploadTime":  pattern.CreatedAt.Format("2006-01-02 15:04:05"),
		},
		"message": "success",
	})
}

func (h *PatternHandler) UpdatePattern(c *gin.Context) {
	id := c.Param("id")

	var pattern model.Pattern
	if err := h.db.First(&pattern, id).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "花型不存在",
		})
		return
	}

	var req PatternUpdateRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}

	updates, err := h.validateAndBuildPatternUpdates(req)
	if err != nil {
		message := "提交字段不合法（名称不能为空，针数/工价不能为负数）"
		if errors.Is(err, errPatternNameFormat) {
			message = "花型名称需为“款式+部位+尺码”格式"
		}
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": message,
		})
		return
	}

	if len(updates) == 0 {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "没有可更新的字段",
		})
		return
	}

	if err := h.db.Model(&pattern).Updates(updates).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "更新花型失败",
		})
		return
	}

	if err := h.db.First(&pattern, pattern.ID).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "读取更新后的花型失败",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"id":          pattern.ID,
			"name":        pattern.Name,
			"patternType": pattern.PatternType,
			"fileName":    pattern.FileName,
			"fileSize":    pattern.FileSize,
			"size":        formatFileSize(pattern.FileSize),
			"stitches":    pattern.Stitches,
			"unitPrice":   roundTo3(pattern.UnitPrice),
			"orderNo":     pattern.OrderNo,
			"uploadTime":  pattern.CreatedAt.Format("2006-01-02 15:04:05"),
		},
		"message": "success",
	})
}

func (h *PatternHandler) BatchUpdatePatterns(c *gin.Context) {
	var req PatternBatchUpdateRequest
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
			"message": "请选择至少一条花型记录",
		})
		return
	}

	updates, err := h.validateAndBuildPatternUpdates(req.PatternUpdateRequest)
	if err != nil {
		message := "提交字段不合法（名称不能为空，针数/工价不能为负数）"
		if errors.Is(err, errPatternNameFormat) {
			message = "花型名称需为“款式+部位+尺码”格式"
		}
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": message,
		})
		return
	}

	if len(updates) == 0 {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "请至少填写一个需要批量修改的字段",
		})
		return
	}

	result := h.db.Model(&model.Pattern{}).Where("id IN ?", req.IDs).Updates(updates)
	if result.Error != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "批量更新失败",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"affected": result.RowsAffected,
		},
		"message": "success",
	})
}

func (h *PatternHandler) DeletePattern(c *gin.Context) {
	id := c.Param("id")

	var pattern model.Pattern
	if err := h.db.First(&pattern, id).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "花型不存在",
		})
		return
	}

	if err := h.db.Delete(&pattern).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "删除花型失败",
		})
		return
	}

	if pattern.FilePath != "" {
		_ = os.Remove(pattern.FilePath)
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "success",
	})
}

func (h *PatternHandler) DownloadToDevice(c *gin.Context) {
	var req struct {
		PatternID uint   `json:"patternId"`
		DeviceIDs []uint `json:"deviceIds"`
	}
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}
	if req.PatternID == 0 || len(req.DeviceIDs) == 0 {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "请选择花型和目标设备",
		})
		return
	}

	userID := c.GetUint("userId")
	for _, deviceID := range req.DeviceIDs {
		task := model.DownloadTask{
			PatternID:  req.PatternID,
			DeviceID:   deviceID,
			Status:     "waiting",
			Progress:   0,
			Message:    "等待下发",
			OperatorID: userID,
		}
		if err := h.db.Create(&task).Error; err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"code":    500,
				"message": "创建下发任务失败",
			})
			return
		}
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "success",
	})
}

func (h *PatternHandler) BatchDownload(c *gin.Context) {
	var req struct {
		PatternIDs []uint `json:"patternIds"`
		DeviceIDs  []uint `json:"deviceIds"`
	}
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}
	if len(req.PatternIDs) == 0 || len(req.DeviceIDs) == 0 {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "请选择花型和目标设备",
		})
		return
	}

	userID := c.GetUint("userId")
	for _, patternID := range req.PatternIDs {
		for _, deviceID := range req.DeviceIDs {
			task := model.DownloadTask{
				PatternID:  patternID,
				DeviceID:   deviceID,
				Status:     "waiting",
				Progress:   0,
				Message:    "等待下发",
				OperatorID: userID,
			}
			if err := h.db.Create(&task).Error; err != nil {
				c.JSON(http.StatusInternalServerError, gin.H{
					"code":    500,
					"message": "创建下发任务失败",
				})
				return
			}
		}
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "success",
	})
}

func (h *PatternHandler) queryPatternIDs(patternName, patternType, orderNo string) ([]uint, error) {
	query := h.db.Model(&model.Pattern{})
	needFilter := false

	if patternName != "" {
		query = query.Where("name LIKE ?", "%"+patternName+"%")
		needFilter = true
	}
	if patternType != "" {
		query = query.Where("pattern_type = ?", patternType)
		needFilter = true
	}
	if orderNo != "" {
		query = query.Where("order_no LIKE ?", "%"+orderNo+"%")
		needFilter = true
	}
	if !needFilter {
		return nil, nil
	}

	ids := make([]uint, 0)
	if err := query.Pluck("id", &ids).Error; err != nil {
		return nil, err
	}
	return ids, nil
}

func (h *PatternHandler) queryDeviceIDs(deviceName string) ([]uint, error) {
	deviceName = strings.TrimSpace(deviceName)
	if deviceName == "" {
		return nil, nil
	}

	ids := make([]uint, 0)
	if err := h.db.Model(&model.Device{}).
		Where("name LIKE ?", "%"+deviceName+"%").
		Pluck("id", &ids).Error; err != nil {
		return nil, err
	}
	return ids, nil
}

func uniqueUint(ids []uint) []uint {
	if len(ids) == 0 {
		return ids
	}

	set := make(map[uint]struct{}, len(ids))
	list := make([]uint, 0, len(ids))
	for _, id := range ids {
		if _, exists := set[id]; exists {
			continue
		}
		set[id] = struct{}{}
		list = append(list, id)
	}
	return list
}

func (h *PatternHandler) buildDownloadTaskList(tasks []model.DownloadTask) []gin.H {
	if len(tasks) == 0 {
		return []gin.H{}
	}

	patternIDs := make([]uint, 0, len(tasks))
	deviceIDs := make([]uint, 0, len(tasks))
	for _, t := range tasks {
		patternIDs = append(patternIDs, t.PatternID)
		deviceIDs = append(deviceIDs, t.DeviceID)
	}
	patternIDs = uniqueUint(patternIDs)
	deviceIDs = uniqueUint(deviceIDs)

	patternMap := make(map[uint]model.Pattern, len(patternIDs))
	deviceMap := make(map[uint]model.Device, len(deviceIDs))

	if len(patternIDs) > 0 {
		var patterns []model.Pattern
		if err := h.db.Where("id IN ?", patternIDs).Find(&patterns).Error; err == nil {
			for _, p := range patterns {
				patternMap[p.ID] = p
			}
		}
	}

	if len(deviceIDs) > 0 {
		var devices []model.Device
		if err := h.db.Where("id IN ?", deviceIDs).Find(&devices).Error; err == nil {
			for _, d := range devices {
				deviceMap[d.ID] = d
			}
		}
	}

	list := make([]gin.H, 0, len(tasks))
	for _, t := range tasks {
		p := patternMap[t.PatternID]
		d := deviceMap[t.DeviceID]

		list = append(list, gin.H{
			"id":          t.ID,
			"patternId":   t.PatternID,
			"patternName": p.Name,
			"patternType": p.PatternType,
			"stitches":    p.Stitches,
			"fileSize":    p.FileSize,
			"size":        formatFileSize(p.FileSize),
			"unitPrice":   roundTo3(p.UnitPrice),
			"orderNo":     p.OrderNo,
			"deviceId":    t.DeviceID,
			"deviceName":  d.Name,
			"status":      t.Status,
			"progress":    t.Progress,
			"message":     t.Message,
			"createTime":  t.CreatedAt.Format("2006-01-02 15:04:05"),
			"updateTime":  t.UpdatedAt.Format("2006-01-02 15:04:05"),
		})
	}
	return list
}

func (h *PatternHandler) GetDownloadQueue(c *gin.Context) {
	query := h.db.Model(&model.DownloadTask{})

	status := strings.TrimSpace(c.Query("status"))
	if status != "" {
		query = query.Where("status = ?", status)
	} else {
		query = query.Where("status IN ?", []string{"waiting", "downloading", "paused", "failed"})
	}

	patternIDs, err := h.queryPatternIDs(
		strings.TrimSpace(c.Query("patternName")),
		strings.TrimSpace(c.Query("patternType")),
		strings.TrimSpace(c.Query("orderNo")),
	)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "查询花型条件失败",
		})
		return
	}
	if patternIDs != nil {
		if len(patternIDs) == 0 {
			c.JSON(http.StatusOK, gin.H{"code": 0, "data": gin.H{"list": []gin.H{}, "total": 0}, "message": "success"})
			return
		}
		query = query.Where("pattern_id IN ?", patternIDs)
	}

	deviceIDs, err := h.queryDeviceIDs(c.Query("deviceName"))
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "查询设备条件失败",
		})
		return
	}
	if deviceIDs != nil {
		if len(deviceIDs) == 0 {
			c.JSON(http.StatusOK, gin.H{"code": 0, "data": gin.H{"list": []gin.H{}, "total": 0}, "message": "success"})
			return
		}
		query = query.Where("device_id IN ?", deviceIDs)
	}

	var tasks []model.DownloadTask
	if err := query.Order("created_at DESC").Find(&tasks).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "查询下发队列失败",
		})
		return
	}

	list := h.buildDownloadTaskList(tasks)
	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"list":  list,
			"total": len(list),
		},
		"message": "success",
	})
}

func (h *PatternHandler) GetDownloadLog(c *gin.Context) {
	query := h.db.Model(&model.DownloadTask{})

	if status := strings.TrimSpace(c.Query("status")); status != "" {
		query = query.Where("status = ?", status)
	} else {
		query = query.Where("status IN ?", []string{"completed", "failed"})
	}

	startDate, err := parseDateFilter(c.Query("startDate"), false)
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "开始日期格式错误，应为 yyyy-MM-dd",
		})
		return
	}
	endDate, err := parseDateFilter(c.Query("endDate"), true)
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "结束日期格式错误，应为 yyyy-MM-dd",
		})
		return
	}

	if startDate != nil {
		query = query.Where("created_at >= ?", *startDate)
	}
	if endDate != nil {
		query = query.Where("created_at <= ?", *endDate)
	}

	patternIDs, err := h.queryPatternIDs(
		strings.TrimSpace(c.Query("patternName")),
		strings.TrimSpace(c.Query("patternType")),
		strings.TrimSpace(c.Query("orderNo")),
	)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "查询花型条件失败",
		})
		return
	}
	if patternIDs != nil {
		if len(patternIDs) == 0 {
			c.JSON(http.StatusOK, gin.H{"code": 0, "data": gin.H{"list": []gin.H{}, "total": 0}, "message": "success"})
			return
		}
		query = query.Where("pattern_id IN ?", patternIDs)
	}

	deviceIDs, err := h.queryDeviceIDs(c.Query("deviceName"))
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "查询设备条件失败",
		})
		return
	}
	if deviceIDs != nil {
		if len(deviceIDs) == 0 {
			c.JSON(http.StatusOK, gin.H{"code": 0, "data": gin.H{"list": []gin.H{}, "total": 0}, "message": "success"})
			return
		}
		query = query.Where("device_id IN ?", deviceIDs)
	}

	page, pageSize := parsePagination(c)
	offset := (page - 1) * pageSize

	var total int64
	if err := query.Count(&total).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "统计下发日志失败",
		})
		return
	}

	var tasks []model.DownloadTask
	if err := query.Order("created_at DESC").Offset(offset).Limit(pageSize).Find(&tasks).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "查询下发日志失败",
		})
		return
	}

	list := h.buildDownloadTaskList(tasks)
	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"list":  list,
			"total": total,
		},
		"message": "success",
	})
}

func containsString(list []string, target string) bool {
	for _, item := range list {
		if item == target {
			return true
		}
	}
	return false
}

func (h *PatternHandler) updateDownloadTaskStatus(id string, allowedCurrent []string, nextStatus, message string) (int, string) {
	var task model.DownloadTask
	if err := h.db.First(&task, id).Error; err != nil {
		if err == gorm.ErrRecordNotFound {
			return http.StatusNotFound, "任务不存在"
		}
		return http.StatusInternalServerError, "查询任务失败"
	}

	if !containsString(allowedCurrent, task.Status) {
		return http.StatusBadRequest, "当前状态不支持此操作"
	}

	updates := map[string]interface{}{
		"status":  nextStatus,
		"message": message,
	}
	if nextStatus == "waiting" && task.Progress < 100 {
		updates["progress"] = 0
	}

	if err := h.db.Model(&task).Updates(updates).Error; err != nil {
		return http.StatusInternalServerError, "更新任务状态失败"
	}
	return http.StatusOK, "success"
}

func (h *PatternHandler) PauseDownload(c *gin.Context) {
	statusCode, message := h.updateDownloadTaskStatus(c.Param("id"), []string{"waiting", "downloading"}, "paused", "任务已暂停")
	if statusCode != http.StatusOK {
		c.JSON(statusCode, gin.H{"code": statusCode, "message": message})
		return
	}
	c.JSON(http.StatusOK, gin.H{"code": 0, "message": "success"})
}

func (h *PatternHandler) ResumeDownload(c *gin.Context) {
	statusCode, message := h.updateDownloadTaskStatus(c.Param("id"), []string{"paused"}, "waiting", "等待下发")
	if statusCode != http.StatusOK {
		c.JSON(statusCode, gin.H{"code": statusCode, "message": message})
		return
	}
	c.JSON(http.StatusOK, gin.H{"code": 0, "message": "success"})
}

func (h *PatternHandler) PauseAllDownloads(c *gin.Context) {
	result := h.db.Model(&model.DownloadTask{}).
		Where("status IN ?", []string{"waiting", "downloading"}).
		Updates(map[string]interface{}{
			"status":  "paused",
			"message": "任务已暂停",
		})
	if result.Error != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "批量暂停失败",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"affected": result.RowsAffected,
		},
		"message": "success",
	})
}

func (h *PatternHandler) ResumeAllDownloads(c *gin.Context) {
	result := h.db.Model(&model.DownloadTask{}).
		Where("status = ?", "paused").
		Updates(map[string]interface{}{
			"status":   "waiting",
			"progress": 0,
			"message":  "等待下发",
		})
	if result.Error != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "批量继续失败",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"affected": result.RowsAffected,
		},
		"message": "success",
	})
}

func (h *PatternHandler) ClearCompletedDownloads(c *gin.Context) {
	result := h.db.Where("status IN ?", []string{"completed", "failed"}).Delete(&model.DownloadTask{})
	if result.Error != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "清理队列失败",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"affected": result.RowsAffected,
		},
		"message": "success",
	})
}

func (h *PatternHandler) CancelDownload(c *gin.Context) {
	id := c.Param("id")

	var task model.DownloadTask
	if err := h.db.First(&task, id).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "任务不存在",
		})
		return
	}

	if err := h.db.Delete(&task).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "取消任务失败",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "success",
	})
}
