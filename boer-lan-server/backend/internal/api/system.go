package api

import (
	"boer-lan-server/internal/model"
	"net"
	"net/http"
	"os"
	"os/exec"
	"runtime"
	"strconv"
	"time"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

type SystemHandler struct {
	db         *gorm.DB
	serverPort int
}

func NewSystemHandler(db *gorm.DB, serverPort int) *SystemHandler {
	return &SystemHandler{
		db:         db,
		serverPort: serverPort,
	}
}

// GetServerInfo 获取服务器信息
func (h *SystemHandler) GetServerInfo(c *gin.Context) {
	// 获取所有网卡IP地址
	addrs, _ := net.InterfaceAddrs()
	var ips []string
	for _, addr := range addrs {
		if ipnet, ok := addr.(*net.IPNet); ok && !ipnet.IP.IsLoopback() {
			if ipnet.IP.To4() != nil {
				ips = append(ips, ipnet.IP.String())
			}
		}
	}

	// 获取工作目录
	workDir, _ := os.Getwd()

	// 获取数据目录
	dataDir := "./data"

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"ips":      ips,
			"port":     h.serverPort,
			"workDir":  workDir,
			"dataDir":  dataDir,
			"os":       runtime.GOOS,
			"arch":     runtime.GOARCH,
			"version":  "1.0.0",
			"uptime":   time.Now().Unix(),
		},
	})
}

// GetDebugLogs 获取调试日志
func (h *SystemHandler) GetDebugLogs(c *gin.Context) {
	limit, _ := strconv.Atoi(c.DefaultQuery("limit", "100"))
	level := c.Query("level")

	var logs []model.DebugLog
	query := h.db.Order("created_at DESC").Limit(limit)

	if level != "" {
		query = query.Where("level = ?", level)
	}

	if err := query.Find(&logs).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": logs,
	})
}

// AddDebugLog 添加调试日志
func (h *SystemHandler) AddDebugLog(c *gin.Context) {
	var req struct {
		Level   string `json:"level" binding:"required"`
		Message string `json:"message" binding:"required"`
		Source  string `json:"source"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	log := model.DebugLog{
		Level:   req.Level,
		Message: req.Message,
		Source:  req.Source,
	}

	if err := h.db.Create(&log).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": log,
	})
}

// ClearDebugLogs 清空调试日志
func (h *SystemHandler) ClearDebugLogs(c *gin.Context) {
	if err := h.db.Exec("DELETE FROM debug_logs").Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"msg":  "日志已清空",
	})
}

// GetServerConfig 获取服务器配置
func (h *SystemHandler) GetServerConfig(c *gin.Context) {
	key := c.Query("key")

	if key != "" {
		var config model.ServerConfig
		if err := h.db.Where("key = ?", key).First(&config).Error; err != nil {
			if err == gorm.ErrRecordNotFound {
				c.JSON(http.StatusNotFound, gin.H{"error": "配置项不存在"})
			} else {
				c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
			}
			return
		}

		c.JSON(http.StatusOK, gin.H{
			"code": 0,
			"data": config,
		})
		return
	}

	var configs []model.ServerConfig
	if err := h.db.Find(&configs).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": configs,
	})
}

// SetServerConfig 设置服务器配置
func (h *SystemHandler) SetServerConfig(c *gin.Context) {
	var req struct {
		Key   string `json:"key" binding:"required"`
		Value string `json:"value" binding:"required"`
		Desc  string `json:"desc"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	config := model.ServerConfig{
		Key:   req.Key,
		Value: req.Value,
		Desc:  req.Desc,
	}

	// 如果配置已存在则更新，否则创建
	var existing model.ServerConfig
	err := h.db.Where("key = ?", req.Key).First(&existing).Error
	if err == gorm.ErrRecordNotFound {
		// 创建
		if err := h.db.Create(&config).Error; err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
			return
		}
	} else if err == nil {
		// 更新
		if err := h.db.Model(&existing).Updates(config).Error; err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
			return
		}
	} else {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"msg":  "配置已保存",
	})
}

// ExecuteCommand 执行系统命令（用于辅助工具）
func (h *SystemHandler) ExecuteCommand(c *gin.Context) {
	var req struct {
		Command string   `json:"command" binding:"required"`
		Args    []string `json:"args"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	// 安全检查：只允许特定的命令
	allowedCommands := map[string]bool{
		"ipconfig":  true,
		"ifconfig":  true,
		"arp":       true,
		"ping":      true,
		"netstat":   true,
		"hostname":  true,
	}

	if !allowedCommands[req.Command] {
		c.JSON(http.StatusForbidden, gin.H{"error": "不允许执行该命令"})
		return
	}

	cmd := exec.Command(req.Command, req.Args...)
	output, err := cmd.CombinedOutput()

	if err != nil {
		c.JSON(http.StatusOK, gin.H{
			"code": 1,
			"data": gin.H{
				"output": string(output),
				"error":  err.Error(),
			},
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"output": string(output),
		},
	})
}

// GetNetworkInfo 获取网络信息
func (h *SystemHandler) GetNetworkInfo(c *gin.Context) {
	interfaces, err := net.Interfaces()
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	var networkInfo []gin.H
	for _, iface := range interfaces {
		addrs, err := iface.Addrs()
		if err != nil {
			continue
		}

		var ips []string
		for _, addr := range addrs {
			if ipnet, ok := addr.(*net.IPNet); ok {
				ips = append(ips, ipnet.String())
			}
		}

		networkInfo = append(networkInfo, gin.H{
			"name":       iface.Name,
			"mac":        iface.HardwareAddr.String(),
			"flags":      iface.Flags.String(),
			"mtu":        iface.MTU,
			"addresses":  ips,
		})
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": networkInfo,
	})
}

// PingDevice 测试设备连通性
func (h *SystemHandler) PingDevice(c *gin.Context) {
	ip := c.Query("ip")
	if ip == "" {
		c.JSON(http.StatusBadRequest, gin.H{"error": "IP地址不能为空"})
		return
	}

	var cmd *exec.Cmd
	if runtime.GOOS == "windows" {
		cmd = exec.Command("ping", "-n", "4", ip)
	} else {
		cmd = exec.Command("ping", "-c", "4", ip)
	}

	output, err := cmd.CombinedOutput()

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"output":  string(output),
			"success": err == nil,
		},
	})
}

// GetSystemStats 获取系统统计信息
func (h *SystemHandler) GetSystemStats(c *gin.Context) {
	var deviceCount, userCount, operatorCount, groupCount int64
	h.db.Model(&model.Device{}).Count(&deviceCount)
	h.db.Model(&model.User{}).Count(&userCount)
	h.db.Model(&model.Operator{}).Count(&operatorCount)
	h.db.Model(&model.Group{}).Count(&groupCount)

	var onlineDeviceCount int64
	h.db.Model(&model.Device{}).Where("status = ?", "online").Count(&onlineDeviceCount)

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"deviceCount":       deviceCount,
			"onlineDeviceCount": onlineDeviceCount,
			"userCount":         userCount,
			"operatorCount":     operatorCount,
			"groupCount":        groupCount,
		},
	})
}
