package api

import (
	"fmt"
	"net/http"
	"strconv"
	"strings"

	"boer-lan-server/internal/model"
	"boer-lan-server/internal/service"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

type UploadDeviceFilesRequest struct {
	DeviceID      uint                       `json:"deviceId" binding:"required"`
	FileIDs       []uint                     `json:"fileIds" binding:"required"`
	SelectedFiles []UploadDeviceFileSnapshot `json:"selectedFiles"`
	ConflictMode  string                     `json:"conflictMode"`
	RenameNames   map[string]string          `json:"renameNames"`
}

type UploadDeviceFileSnapshot struct {
	ID        uint   `json:"id"`
	PatternNo uint   `json:"patternNo"`
	FileName  string `json:"fileName"`
}

type uploadPatternConflictItem struct {
	FileID            uint   `json:"fileId"`
	FileName          string `json:"fileName"`
	PatternName       string `json:"patternName"`
	ExistingPatternID uint   `json:"existingPatternId"`
}

func (h *PatternHandler) loadAccessibleDeviceForPatternFiles(c *gin.Context, rawDeviceID string) (*model.Device, bool) {
	deviceIDStr := strings.TrimSpace(rawDeviceID)
	if deviceIDStr == "" {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "deviceId 不能为空",
		})
		return nil, false
	}

	deviceID, err := strconv.ParseUint(deviceIDStr, 10, 64)
	if err != nil || deviceID == 0 {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "deviceId 不合法",
		})
		return nil, false
	}

	var device model.Device
	if err := h.db.First(&device, deviceID).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "设备不存在",
		})
		return nil, false
	}
	scope := h.getCurrentUserScope(c)
	allowed, err := h.canAccessDeviceID(scope, device.ID)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "校验设备权限失败",
		})
		return nil, false
	}
	if !allowed {
		c.JSON(http.StatusForbidden, gin.H{
			"code":    403,
			"message": "无权访问该设备文件",
		})
		return nil, false
	}

	return &device, true
}

func (h *PatternHandler) GetDevicePatternFiles(c *gin.Context) {
	device, ok := h.loadAccessibleDeviceForPatternFiles(c, c.Query("deviceId"))
	if !ok {
		return
	}

	var files []model.DevicePatternFile
	if err := h.db.
		Where("device_id = ?", device.ID).
		Order("updated_at DESC").
		Find(&files).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "查询设备文件失败",
		})
		return
	}

	serverPatterns, err := h.loadServerPatternMatches(device.ID, files)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "匹配服务器花型失败",
		})
		return
	}

	keyword := strings.TrimSpace(c.Query("keyword"))
	patternTypeFilter := strings.TrimSpace(c.Query("patternType"))
	rows := make([]gin.H, 0, len(files))
	for _, item := range files {
		row := h.buildDevicePatternFileRow(device.ID, item, serverPatterns)
		if !matchesDevicePatternFileFilter(row, keyword, patternTypeFilter) {
			continue
		}
		rows = append(rows, row)
	}

	page, pageSize := parsePagination(c)
	total := int64(len(rows))
	start := (page - 1) * pageSize
	if start > len(rows) {
		start = len(rows)
	}
	end := start + pageSize
	if end > len(rows) {
		end = len(rows)
	}
	list := rows[start:end]

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"list":  list,
			"total": total,
		},
		"message": "success",
	})
}

func (h *PatternHandler) buildDevicePatternFileRow(deviceID uint, item model.DevicePatternFile, serverPatterns map[string]model.Pattern) gin.H {
	patternName := service.TrimKnownPatternFileExtension(item.FileName)
	patternType := item.PatternType
	stitches := item.Stitches
	unitPrice := item.UnitPrice
	orderNo := item.OrderNo
	fileSize := item.FileSize
	var serverPatternID uint

	if matched, ok := findMatchedServerPattern(deviceID, item, serverPatterns); ok {
		serverPatternID = matched.ID
		if strings.TrimSpace(matched.Name) != "" {
			patternName = strings.TrimSpace(matched.Name)
		}
		patternType = matched.PatternType
		stitches = matched.Stitches
		unitPrice = matched.UnitPrice
		orderNo = matched.OrderNo
		fileSize = matched.FileSize
	}

	return gin.H{
		"id":                   item.ID,
		"deviceId":             item.DeviceID,
		"patternNo":            item.PatternNo,
		"fileName":             item.FileName,
		"patternName":          patternName,
		"matchedServerPattern": serverPatternID > 0,
		"serverPatternId":      serverPatternID,
		"patternType":          patternType,
		"fileSize":             fileSize,
		"size":                 formatFileSize(fileSize),
		"stitches":             stitches,
		"unitPrice":            roundTo3(unitPrice),
		"orderNo":              orderNo,
		"filePath":             item.FilePath,
		"createTime":           item.CreatedAt.Format("2006-01-02 15:04:05"),
		"updateTime":           item.UpdatedAt.Format("2006-01-02 15:04:05"),
	}
}

func (h *PatternHandler) loadServerPatternMatches(deviceID uint, files []model.DevicePatternFile) (map[string]model.Pattern, error) {
	candidateSet := make(map[string]struct{})
	candidates := make([]string, 0, len(files)*3)
	for _, file := range files {
		for _, candidate := range buildDevicePatternMatchCandidates(deviceID, file) {
			key := normalizePatternMatchKey(candidate)
			if key == "" {
				continue
			}
			if _, exists := candidateSet[key]; exists {
				continue
			}
			candidateSet[key] = struct{}{}
			candidates = append(candidates, strings.TrimSpace(candidate))
		}
	}
	if len(candidates) == 0 {
		return map[string]model.Pattern{}, nil
	}

	var patterns []model.Pattern
	if err := h.db.Where("name IN ? OR file_name IN ?", candidates, candidates).Find(&patterns).Error; err != nil {
		return nil, err
	}

	result := make(map[string]model.Pattern, len(patterns)*3)
	for _, pattern := range patterns {
		for _, candidate := range []string{pattern.Name, pattern.FileName, service.TrimKnownPatternFileExtension(pattern.FileName)} {
			key := normalizePatternMatchKey(candidate)
			if key == "" {
				continue
			}
			if _, exists := result[key]; !exists {
				result[key] = pattern
			}
		}
	}
	return result, nil
}

func findMatchedServerPattern(deviceID uint, file model.DevicePatternFile, patterns map[string]model.Pattern) (model.Pattern, bool) {
	for _, candidate := range buildDevicePatternMatchCandidates(deviceID, file) {
		if pattern, ok := patterns[normalizePatternMatchKey(candidate)]; ok {
			return pattern, true
		}
	}
	return model.Pattern{}, false
}

func buildDevicePatternMatchCandidates(deviceID uint, file model.DevicePatternFile) []string {
	fileName := strings.TrimSpace(file.FileName)
	baseName := service.TrimKnownPatternFileExtension(fileName)
	return []string{
		service.ResolveUploadedPatternName(file, deviceID),
		fileName,
		baseName,
	}
}

func normalizePatternMatchKey(value string) string {
	return strings.ToLower(strings.TrimSpace(value))
}

func matchesDevicePatternFileFilter(row gin.H, keyword, patternType string) bool {
	if patternType != "" && strings.TrimSpace(toString(row["patternType"])) != patternType {
		return false
	}
	if keyword == "" {
		return true
	}
	needle := strings.ToLower(keyword)
	for _, field := range []string{"fileName", "patternName", "patternType", "orderNo"} {
		if strings.Contains(strings.ToLower(toString(row[field])), needle) {
			return true
		}
	}
	return false
}

func toString(value interface{}) string {
	if value == nil {
		return ""
	}
	if s, ok := value.(string); ok {
		return s
	}
	return strings.TrimSpace(fmt.Sprint(value))
}

func (h *PatternHandler) RefreshDevicePatternFiles(c *gin.Context) {
	device, ok := h.loadAccessibleDeviceForPatternFiles(c, c.Query("deviceId"))
	if !ok {
		return
	}

	if h.transfer == nil {
		c.JSON(http.StatusServiceUnavailable, gin.H{
			"code":    503,
			"message": "设备传输服务未初始化",
		})
		return
	}
	if !h.transfer.IsDeviceConnected(*device) {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "设备未开机连接，无法实时读取",
		})
		return
	}

	if _, err := h.transfer.RefreshDevicePatternFiles(*device); err != nil {
		statusCode := http.StatusInternalServerError
		message := "实时读取设备文件失败: " + err.Error()
		if strings.Contains(strings.ToLower(err.Error()), "busy") {
			statusCode = http.StatusConflict
			message = "设备当前忙碌，请稍后再试"
		}
		c.JSON(statusCode, gin.H{
			"code":    statusCode,
			"message": message,
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "设备花型列表已刷新",
	})
}

func (h *PatternHandler) DeleteDevicePatternFile(c *gin.Context) {
	id := c.Param("id")

	var record model.DevicePatternFile
	if err := h.db.First(&record, id).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "设备文件不存在",
		})
		return
	}

	scope := h.getCurrentUserScope(c)
	allowed, err := h.canAccessDeviceID(scope, record.DeviceID)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "校验设备权限失败",
		})
		return
	}
	if !allowed {
		c.JSON(http.StatusForbidden, gin.H{
			"code":    403,
			"message": "无权删除该设备文件",
		})
		return
	}

	var device model.Device
	if err := h.db.First(&device, record.DeviceID).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "设备不存在",
		})
		return
	}

	if h.transfer != nil && h.transfer.IsDeviceConnected(device) {
		if err := h.transfer.DeleteDevicePatternFile(device, record); err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"code":    500,
				"message": "删除设备文件失败: " + err.Error(),
			})
			return
		}
	}

	if err := h.db.Delete(&record).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "删除设备文件失败",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "success",
	})
}

func (h *PatternHandler) UploadDeviceFilesToServer(c *gin.Context) {
	var req UploadDeviceFilesRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}
	if len(req.FileIDs) == 0 {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "请至少选择一个设备文件",
		})
		return
	}
	req.FileIDs = normalizeGroupIDs(req.FileIDs)
	if len(req.FileIDs) == 0 {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "请至少选择一个设备文件",
		})
		return
	}

	var device model.Device
	if err := h.db.First(&device, req.DeviceID).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "设备不存在",
		})
		return
	}
	scope := h.getCurrentUserScope(c)
	allowed, err := h.canAccessDeviceID(scope, device.ID)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "校验设备权限失败",
		})
		return
	}
	if !allowed {
		c.JSON(http.StatusForbidden, gin.H{
			"code":    403,
			"message": "无权回传该设备文件",
		})
		return
	}
	if h.transfer == nil {
		c.JSON(http.StatusServiceUnavailable, gin.H{
			"code":    503,
			"message": "设备传输服务未初始化",
		})
		return
	}
	if !h.transfer.IsDeviceConnected(device) {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "设备未开机连接，无法回传文件",
		})
		return
	}

	var files []model.DevicePatternFile
	if err := h.db.Where("device_id = ? AND id IN ?", req.DeviceID, req.FileIDs).Find(&files).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "查询设备文件失败",
		})
		return
	}
	if len(files) != len(req.FileIDs) {
		resolvedFiles, err := h.resolveUploadDeviceFiles(req.DeviceID, req.FileIDs, req.SelectedFiles)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"code":    500,
				"message": "查询设备文件失败",
			})
			return
		}
		files = resolvedFiles
	}
	if len(files) == 0 {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "未找到可回传的设备文件",
		})
		return
	}
	if len(files) != len(req.FileIDs) {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "包含不存在的设备文件",
		})
		return
	}

	conflictMode := service.NormalizeUploadConflictMode(req.ConflictMode)
	if conflictMode == service.UploadConflictModeAsk {
		conflicts, err := h.findUploadPatternConflicts(device.ID, files)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"code":    500,
				"message": "检测花型重名失败",
			})
			return
		}
		if len(conflicts) > 0 {
			c.JSON(http.StatusConflict, gin.H{
				"code":    http.StatusConflict,
				"message": "检测到同名花型，请选择覆盖或重命名",
				"data": gin.H{
					"duplicates": conflicts,
				},
			})
			return
		}
	}

	userID := c.GetUint("userId")
	successCount := 0
	failedCount := 0

	for _, file := range files {
		task := model.UploadTask{
			DeviceFileID: file.ID,
			DeviceID:     req.DeviceID,
			Status:       "uploading",
			Progress:     5,
			Message:      "正在从设备回传",
			OperatorID:   userID,
		}
		if err := h.db.Create(&task).Error; err != nil {
			failedCount++
			continue
		}

		renameName := strings.TrimSpace(req.RenameNames[strconv.FormatUint(uint64(file.ID), 10)])
		pattern, err := h.transfer.UploadPatternFromDeviceWithOptions(device, file, userID, conflictMode, renameName)
		if err != nil {
			_ = h.db.Model(&task).Updates(map[string]interface{}{
				"status":   "failed",
				"progress": 0,
				"message":  "回传失败: " + err.Error(),
			}).Error
			failedCount++
			continue
		}

		patternID := pattern.ID
		if err := h.db.Model(&task).Updates(map[string]interface{}{
			"status":     "completed",
			"progress":   100,
			"pattern_id": patternID,
			"message":    "回传完成",
		}).Error; err != nil {
			failedCount++
			continue
		}

		successCount++
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"total":   len(files),
			"success": successCount,
			"failed":  failedCount,
		},
		"message": "success",
	})
}

func (h *PatternHandler) resolveUploadDeviceFiles(deviceID uint, fileIDs []uint, snapshots []UploadDeviceFileSnapshot) ([]model.DevicePatternFile, error) {
	fileIDs = normalizeGroupIDs(fileIDs)
	files := make([]model.DevicePatternFile, 0, len(fileIDs))
	seen := make(map[uint]struct{}, len(fileIDs))

	if len(fileIDs) > 0 {
		var byID []model.DevicePatternFile
		if err := h.db.Where("device_id = ? AND id IN ?", deviceID, fileIDs).Find(&byID).Error; err != nil {
			return nil, err
		}
		for _, file := range byID {
			if _, ok := seen[file.ID]; ok {
				continue
			}
			files = append(files, file)
			seen[file.ID] = struct{}{}
		}
	}

	for _, snapshot := range snapshots {
		if len(files) >= len(fileIDs) {
			break
		}

		var file model.DevicePatternFile
		query := h.db.Where("device_id = ?", deviceID)
		if snapshot.PatternNo > 0 {
			query = query.Where("pattern_no = ?", snapshot.PatternNo)
		} else if fileName := strings.TrimSpace(snapshot.FileName); fileName != "" {
			query = query.Where("file_name = ?", fileName)
		} else {
			continue
		}
		if err := query.Order("updated_at DESC").First(&file).Error; err != nil {
			if err == gorm.ErrRecordNotFound {
				continue
			}
			return nil, err
		}
		if _, ok := seen[file.ID]; ok {
			continue
		}
		files = append(files, file)
		seen[file.ID] = struct{}{}
	}

	return files, nil
}

func (h *PatternHandler) findUploadPatternConflicts(deviceID uint, files []model.DevicePatternFile) ([]uploadPatternConflictItem, error) {
	if h == nil || h.db == nil || len(files) == 0 {
		return nil, nil
	}

	nameSet := make(map[string]struct{}, len(files))
	names := make([]string, 0, len(files))
	for _, file := range files {
		patternName := service.ResolveUploadedPatternName(file, deviceID)
		if patternName == "" {
			continue
		}
		if _, exists := nameSet[patternName]; exists {
			continue
		}
		nameSet[patternName] = struct{}{}
		names = append(names, patternName)
	}
	if len(names) == 0 {
		return nil, nil
	}

	var patterns []model.Pattern
	if err := h.db.Where("name IN ?", names).Find(&patterns).Error; err != nil {
		return nil, err
	}

	existingByName := make(map[string]model.Pattern, len(patterns))
	for _, pattern := range patterns {
		if _, exists := existingByName[pattern.Name]; !exists {
			existingByName[pattern.Name] = pattern
		}
	}

	conflicts := make([]uploadPatternConflictItem, 0)
	for _, file := range files {
		patternName := service.ResolveUploadedPatternName(file, deviceID)
		existing, exists := existingByName[patternName]
		if !exists {
			continue
		}
		conflicts = append(conflicts, uploadPatternConflictItem{
			FileID:            file.ID,
			FileName:          file.FileName,
			PatternName:       patternName,
			ExistingPatternID: existing.ID,
		})
	}
	return conflicts, nil
}

func (h *PatternHandler) GetUploadQueue(c *gin.Context) {
	query := h.db.Model(&model.UploadTask{})
	scope := h.getCurrentUserScope(c)

	if !scope.All {
		allowedDeviceIDs, err := h.queryScopedDeviceIDs(scope)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"code":    500,
				"message": "查询设备范围失败",
			})
			return
		}
		if len(allowedDeviceIDs) == 0 {
			c.JSON(http.StatusOK, gin.H{"code": 0, "data": gin.H{"list": []gin.H{}, "total": 0}, "message": "success"})
			return
		}
		query = query.Where("device_id IN ?", allowedDeviceIDs)
	}

	if status := strings.TrimSpace(c.Query("status")); status != "" {
		query = query.Where("status = ?", status)
	} else {
		query = query.Where("status IN ?", []string{"waiting", "uploading", "paused", "completed", "failed"})
	}

	if deviceIDStr := strings.TrimSpace(c.Query("deviceId")); deviceIDStr != "" {
		deviceID, err := strconv.ParseUint(deviceIDStr, 10, 64)
		if err != nil || deviceID == 0 {
			c.JSON(http.StatusBadRequest, gin.H{
				"code":    400,
				"message": "deviceId 不合法",
			})
			return
		}
		query = query.Where("device_id = ?", uint(deviceID))
	}

	page, pageSize := parsePagination(c)
	offset := (page - 1) * pageSize

	var total int64
	if err := query.Count(&total).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "统计上传队列失败",
		})
		return
	}

	var tasks []model.UploadTask
	if err := query.Order("created_at DESC").Offset(offset).Limit(pageSize).Find(&tasks).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "查询上传队列失败",
		})
		return
	}

	fileIDs := make([]uint, 0, len(tasks))
	deviceIDs := make([]uint, 0, len(tasks))
	patternIDs := make([]uint, 0, len(tasks))

	for _, t := range tasks {
		fileIDs = append(fileIDs, t.DeviceFileID)
		deviceIDs = append(deviceIDs, t.DeviceID)
		if t.PatternID != nil {
			patternIDs = append(patternIDs, *t.PatternID)
		}
	}

	fileIDs = uniqueUint(fileIDs)
	deviceIDs = uniqueUint(deviceIDs)
	patternIDs = uniqueUint(patternIDs)

	fileMap := make(map[uint]model.DevicePatternFile)
	deviceMap := make(map[uint]model.Device)
	patternMap := make(map[uint]model.Pattern)

	if len(fileIDs) > 0 {
		var files []model.DevicePatternFile
		if err := h.db.Where("id IN ?", fileIDs).Find(&files).Error; err == nil {
			for _, f := range files {
				fileMap[f.ID] = f
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
	if len(patternIDs) > 0 {
		var patterns []model.Pattern
		if err := h.db.Where("id IN ?", patternIDs).Find(&patterns).Error; err == nil {
			for _, p := range patterns {
				patternMap[p.ID] = p
			}
		}
	}

	list := make([]gin.H, 0, len(tasks))
	for _, t := range tasks {
		file := fileMap[t.DeviceFileID]
		device := deviceMap[t.DeviceID]

		var patternID uint
		var patternName string
		if t.PatternID != nil {
			patternID = *t.PatternID
			patternName = patternMap[*t.PatternID].Name
		}

		list = append(list, gin.H{
			"id":           t.ID,
			"deviceId":     t.DeviceID,
			"deviceName":   device.Name,
			"deviceFileId": t.DeviceFileID,
			"fileName":     file.FileName,
			"patternType":  file.PatternType,
			"fileSize":     file.FileSize,
			"size":         formatFileSize(file.FileSize),
			"stitches":     file.Stitches,
			"unitPrice":    roundTo3(file.UnitPrice),
			"orderNo":      file.OrderNo,
			"patternId":    patternID,
			"patternName":  patternName,
			"status":       t.Status,
			"progress":     t.Progress,
			"message":      t.Message,
			"createTime":   t.CreatedAt.Format("2006-01-02 15:04:05"),
			"updateTime":   t.UpdatedAt.Format("2006-01-02 15:04:05"),
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

func (h *PatternHandler) updateUploadTaskStatus(c *gin.Context, id string, allowedCurrent []string, nextStatus, message string) (int, string) {
	var task model.UploadTask
	if err := h.db.First(&task, id).Error; err != nil {
		if err == gorm.ErrRecordNotFound {
			return http.StatusNotFound, "上传任务不存在"
		}
		return http.StatusInternalServerError, "查询上传任务失败"
	}

	scope := h.getCurrentUserScope(c)
	allowed, err := h.canAccessDeviceID(scope, task.DeviceID)
	if err != nil {
		return http.StatusInternalServerError, "校验任务权限失败"
	}
	if !allowed {
		return http.StatusForbidden, "无权操作该任务"
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
		return http.StatusInternalServerError, "更新上传任务失败"
	}

	return http.StatusOK, "success"
}

func (h *PatternHandler) PauseUploadTask(c *gin.Context) {
	statusCode, message := h.updateUploadTaskStatus(c, c.Param("id"), []string{"waiting", "uploading"}, "paused", "任务已暂停")
	if statusCode != http.StatusOK {
		c.JSON(statusCode, gin.H{"code": statusCode, "message": message})
		return
	}
	c.JSON(http.StatusOK, gin.H{"code": 0, "message": "success"})
}

func (h *PatternHandler) ResumeUploadTask(c *gin.Context) {
	statusCode, message := h.updateUploadTaskStatus(c, c.Param("id"), []string{"paused"}, "waiting", "等待回传")
	if statusCode != http.StatusOK {
		c.JSON(statusCode, gin.H{"code": statusCode, "message": message})
		return
	}
	c.JSON(http.StatusOK, gin.H{"code": 0, "message": "success"})
}

func (h *PatternHandler) CancelUploadTask(c *gin.Context) {
	statusCode, message := h.updateUploadTaskStatus(c, c.Param("id"), []string{"waiting", "uploading", "paused"}, "canceled", "任务已取消")
	if statusCode != http.StatusOK {
		c.JSON(statusCode, gin.H{"code": statusCode, "message": message})
		return
	}
	c.JSON(http.StatusOK, gin.H{"code": 0, "message": "success"})
}

func (h *PatternHandler) ClearCompletedUploads(c *gin.Context) {
	scope := h.getCurrentUserScope(c)
	query := h.db.Model(&model.UploadTask{}).Where("status IN ?", []string{"completed", "failed", "canceled"})
	if !scope.All {
		allowedDeviceIDs, err := h.queryScopedDeviceIDs(scope)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"code":    500,
				"message": "查询设备范围失败",
			})
			return
		}
		if len(allowedDeviceIDs) == 0 {
			c.JSON(http.StatusOK, gin.H{"code": 0, "data": gin.H{"affected": 0}, "message": "success"})
			return
		}
		query = query.Where("device_id IN ?", allowedDeviceIDs)
	}

	result := query.Delete(&model.UploadTask{})
	if result.Error != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "清理上传队列失败",
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
