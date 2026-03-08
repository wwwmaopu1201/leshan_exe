package api

import (
	"net/http"
	"path/filepath"
	"strconv"
	"time"

	"boer-lan-server/internal/model"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

type PatternHandler struct {
	db *gorm.DB
}

func NewPatternHandler(db *gorm.DB) *PatternHandler {
	return &PatternHandler{db: db}
}

func (h *PatternHandler) GetPatternList(c *gin.Context) {
	var patterns []model.Pattern
	query := h.db.Model(&model.Pattern{})

	if keyword := c.Query("keyword"); keyword != "" {
		query = query.Where("name LIKE ?", "%"+keyword+"%")
	}

	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("pageSize", "10"))
	offset := (page - 1) * pageSize

	var total int64
	query.Count(&total)
	query.Offset(offset).Limit(pageSize).Order("created_at DESC").Find(&patterns)

	list := make([]gin.H, 0)
	for _, p := range patterns {
		list = append(list, gin.H{
			"id":         p.ID,
			"name":       p.Name,
			"fileName":   p.FileName,
			"size":       formatFileSize(p.FileSize),
			"stitches":   p.Stitches,
			"colors":     p.Colors,
			"uploadTime": p.CreatedAt.Format("2006-01-02 15:04:05"),
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

func (h *PatternHandler) UploadPattern(c *gin.Context) {
	file, err := c.FormFile("file")
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "请选择文件",
		})
		return
	}

	// Save file
	filename := time.Now().Format("20060102150405") + "_" + file.Filename
	savePath := filepath.Join("uploads", "patterns", filename)

	if err := c.SaveUploadedFile(file, savePath); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "文件保存失败",
		})
		return
	}

	// Create record
	userId := c.GetUint("userId")
	pattern := model.Pattern{
		Name:       file.Filename,
		FileName:   filename,
		FilePath:   savePath,
		FileSize:   file.Size,
		UploadedBy: userId,
	}

	h.db.Create(&pattern)

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"data":    pattern,
		"message": "success",
	})
}

func (h *PatternHandler) DeletePattern(c *gin.Context) {
	id := c.Param("id")
	h.db.Delete(&model.Pattern{}, id)

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

	userId := c.GetUint("userId")

	for _, deviceId := range req.DeviceIDs {
		task := model.DownloadTask{
			PatternID:  req.PatternID,
			DeviceID:   deviceId,
			Status:     "waiting",
			Progress:   0,
			OperatorID: userId,
		}
		h.db.Create(&task)
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

	userId := c.GetUint("userId")

	for _, patternId := range req.PatternIDs {
		for _, deviceId := range req.DeviceIDs {
			task := model.DownloadTask{
				PatternID:  patternId,
				DeviceID:   deviceId,
				Status:     "waiting",
				Progress:   0,
				OperatorID: userId,
			}
			h.db.Create(&task)
		}
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "success",
	})
}

func (h *PatternHandler) GetDownloadQueue(c *gin.Context) {
	var tasks []model.DownloadTask
	h.db.Where("status IN ?", []string{"waiting", "downloading"}).
		Order("created_at DESC").
		Find(&tasks)

	list := make([]gin.H, 0)
	for _, t := range tasks {
		var pattern model.Pattern
		var device model.Device
		h.db.First(&pattern, t.PatternID)
		h.db.First(&device, t.DeviceID)

		list = append(list, gin.H{
			"id":          t.ID,
			"patternName": pattern.Name,
			"deviceName":  device.Name,
			"status":      t.Status,
			"progress":    t.Progress,
			"createTime":  t.CreatedAt.Format("2006-01-02 15:04:05"),
		})
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"list": list,
		},
		"message": "success",
	})
}

func (h *PatternHandler) GetDownloadLog(c *gin.Context) {
	var tasks []model.DownloadTask
	query := h.db.Model(&model.DownloadTask{}).
		Where("status IN ?", []string{"completed", "failed"})

	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("pageSize", "10"))
	offset := (page - 1) * pageSize

	var total int64
	query.Count(&total)
	query.Offset(offset).Limit(pageSize).Order("created_at DESC").Find(&tasks)

	list := make([]gin.H, 0)
	for _, t := range tasks {
		var pattern model.Pattern
		var device model.Device
		h.db.First(&pattern, t.PatternID)
		h.db.First(&device, t.DeviceID)

		list = append(list, gin.H{
			"id":          t.ID,
			"patternName": pattern.Name,
			"deviceName":  device.Name,
			"status":      t.Status,
			"message":     t.Message,
			"createTime":  t.CreatedAt.Format("2006-01-02 15:04:05"),
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

	h.db.Delete(&task)

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "success",
	})
}
