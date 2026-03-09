package api

import (
	"boer-lan-server/internal/model"
	"boer-lan-server/internal/service"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"runtime"
	"strconv"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
	"gorm.io/driver/mysql"
	"gorm.io/driver/sqlserver"
	"gorm.io/gorm"
)

type SystemHandler struct {
	db         *gorm.DB
	serverPort int
}

type ExternalDBConfig struct {
	DBType              string `json:"dbType"` // mysql, mssql
	Host                string `json:"host"`
	Port                int    `json:"port"`
	Username            string `json:"username"`
	Password            string `json:"password"`
	Database            string `json:"database"`
	Charset             string `json:"charset"`
	SyncIntervalMinutes int    `json:"syncIntervalMinutes"`
	Enabled             bool   `json:"enabled"`
	UpdatedAt           int64  `json:"updatedAt"`
}

const externalDBConfigKey = "external_db_config"
const externalDBLastSyncAtKey = "external_db_last_sync_at"
const debugOutputEnabledConfigKey = "debug_output_enabled"

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
			"ips":     ips,
			"port":    h.serverPort,
			"workDir": workDir,
			"dataDir": dataDir,
			"os":      runtime.GOOS,
			"arch":    runtime.GOARCH,
			"version": "1.0.0",
			"uptime":  time.Now().Unix(),
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
	if !h.isDebugOutputEnabled() {
		c.JSON(http.StatusOK, gin.H{
			"code":    0,
			"message": "调试输出已关闭",
		})
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

func (h *SystemHandler) isDebugOutputEnabled() bool {
	var config model.ServerConfig
	if err := h.db.Where("key = ?", debugOutputEnabledConfigKey).First(&config).Error; err != nil {
		// 默认开启调试输出
		return true
	}

	value := strings.ToLower(strings.TrimSpace(config.Value))
	if value == "" {
		return true
	}
	switch value {
	case "1", "true", "yes", "on":
		return true
	case "0", "false", "no", "off":
		return false
	default:
		return true
	}
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
		"ipconfig": true,
		"ifconfig": true,
		"arp":      true,
		"ping":     true,
		"netstat":  true,
		"hostname": true,
		"netsh":    true,
		"control":  true,
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
			"name":      iface.Name,
			"mac":       iface.HardwareAddr.String(),
			"flags":     iface.Flags.String(),
			"mtu":       iface.MTU,
			"addresses": ips,
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
	var deviceCount, userCount, operatorCount, employeeCount, groupCount int64
	h.db.Model(&model.Device{}).Count(&deviceCount)
	h.db.Model(&model.User{}).Count(&userCount)
	h.db.Model(&model.Operator{}).Count(&operatorCount)
	h.db.Model(&model.Employee{}).Count(&employeeCount)
	h.db.Model(&model.Group{}).Count(&groupCount)

	var onlineDeviceCount int64
	h.db.Model(&model.Device{}).Where("status IN ?", []string{"online", "working", "idle"}).Count(&onlineDeviceCount)

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"deviceCount":       deviceCount,
			"onlineDeviceCount": onlineDeviceCount,
			"userCount":         userCount,
			"employeeCount":     employeeCount,
			"operatorCount":     operatorCount,
			"groupCount":        groupCount,
		},
	})
}

func defaultExternalDBConfig() ExternalDBConfig {
	return ExternalDBConfig{
		DBType:              "mysql",
		Host:                "127.0.0.1",
		Port:                3306,
		Username:            "",
		Password:            "",
		Database:            "",
		Charset:             "utf8mb4",
		SyncIntervalMinutes: 30,
		Enabled:             false,
		UpdatedAt:           0,
	}
}

func normalizeExternalDBConfig(cfg *ExternalDBConfig) {
	cfg.DBType = strings.ToLower(strings.TrimSpace(cfg.DBType))
	cfg.Host = strings.TrimSpace(cfg.Host)
	cfg.Username = strings.TrimSpace(cfg.Username)
	cfg.Database = strings.TrimSpace(cfg.Database)
	cfg.Charset = strings.TrimSpace(cfg.Charset)

	if cfg.DBType == "" {
		cfg.DBType = "mysql"
	}
	if cfg.Port <= 0 {
		if cfg.DBType == "mssql" {
			cfg.Port = 1433
		} else {
			cfg.Port = 3306
		}
	}
	if cfg.Charset == "" {
		cfg.Charset = "utf8mb4"
	}
	if cfg.SyncIntervalMinutes <= 0 {
		cfg.SyncIntervalMinutes = 30
	}
}

func validateExternalDBConfig(cfg ExternalDBConfig, requireCredential bool) error {
	if cfg.DBType != "mysql" && cfg.DBType != "mssql" {
		return errors.New("数据库类型仅支持 mysql 或 mssql")
	}
	if cfg.Host == "" {
		return errors.New("数据库地址不能为空")
	}
	if cfg.Port < 1 || cfg.Port > 65535 {
		return errors.New("数据库端口不合法")
	}
	if requireCredential {
		if cfg.Username == "" {
			return errors.New("登录名不能为空")
		}
		if cfg.Database == "" {
			return errors.New("数据库名不能为空")
		}
	}
	return nil
}

func (h *SystemHandler) loadExternalDBConfig() (ExternalDBConfig, error) {
	cfg := defaultExternalDBConfig()

	var record model.ServerConfig
	if err := h.db.Where("key = ?", externalDBConfigKey).First(&record).Error; err != nil {
		if err == gorm.ErrRecordNotFound {
			return cfg, nil
		}
		return cfg, err
	}

	if err := json.Unmarshal([]byte(record.Value), &cfg); err != nil {
		return defaultExternalDBConfig(), err
	}
	normalizeExternalDBConfig(&cfg)
	return cfg, nil
}

func (h *SystemHandler) saveExternalDBConfig(cfg ExternalDBConfig) error {
	payload, err := json.Marshal(cfg)
	if err != nil {
		return err
	}

	record := model.ServerConfig{
		Key:   externalDBConfigKey,
		Value: string(payload),
		Desc:  "外部数据库连接配置",
	}

	var existing model.ServerConfig
	err = h.db.Where("key = ?", externalDBConfigKey).First(&existing).Error
	if err == gorm.ErrRecordNotFound {
		return h.db.Create(&record).Error
	}
	if err != nil {
		return err
	}
	return h.db.Model(&existing).Updates(record).Error
}

// GetExternalDBConfig 获取外部数据库配置
func (h *SystemHandler) GetExternalDBConfig(c *gin.Context) {
	cfg, err := h.loadExternalDBConfig()
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "读取数据库配置失败",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": cfg,
	})
}

// GetExternalDBSyncStatus 获取外部数据库同步状态
func (h *SystemHandler) GetExternalDBSyncStatus(c *gin.Context) {
	cfg, err := h.loadExternalDBConfig()
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "读取数据库配置失败",
		})
		return
	}

	lastSyncUnix := int64(0)
	nextSyncUnix := int64(0)

	var record model.ServerConfig
	if err := h.db.Where("key = ?", externalDBLastSyncAtKey).First(&record).Error; err == nil {
		raw := strings.TrimSpace(record.Value)
		if raw != "" {
			if parsed, parseErr := time.Parse(time.RFC3339, raw); parseErr == nil {
				lastSyncUnix = parsed.Unix()
			} else {
				var ts int64
				if _, scanErr := fmt.Sscanf(raw, "%d", &ts); scanErr == nil && ts > 0 {
					lastSyncUnix = ts
				}
			}
		}
	}

	if cfg.SyncIntervalMinutes <= 0 {
		cfg.SyncIntervalMinutes = 30
	}
	if lastSyncUnix > 0 {
		nextSyncUnix = lastSyncUnix + int64(cfg.SyncIntervalMinutes*60)
	}

	status := "disabled"
	if cfg.Enabled {
		status = "waiting_first_sync"
		if lastSyncUnix > 0 {
			if time.Now().Unix() >= nextSyncUnix {
				status = "due"
			} else {
				status = "scheduled"
			}
		}
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"enabled":             cfg.Enabled,
			"dbType":              cfg.DBType,
			"syncIntervalMinutes": cfg.SyncIntervalMinutes,
			"lastSyncAt":          lastSyncUnix,
			"nextSyncAt":          nextSyncUnix,
			"status":              status,
		},
	})
}

func buildMSSQLDSN(cfg ExternalDBConfig) string {
	u := &url.URL{
		Scheme: "sqlserver",
		User:   url.UserPassword(cfg.Username, cfg.Password),
		Host:   fmt.Sprintf("%s:%d", cfg.Host, cfg.Port),
	}
	q := url.Values{}
	q.Set("database", cfg.Database)
	u.RawQuery = q.Encode()
	return u.String()
}

// SetExternalDBConfig 设置外部数据库配置
func (h *SystemHandler) SetExternalDBConfig(c *gin.Context) {
	var req ExternalDBConfig
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}

	normalizeExternalDBConfig(&req)
	if err := validateExternalDBConfig(req, req.Enabled); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": err.Error(),
		})
		return
	}

	req.UpdatedAt = time.Now().Unix()
	if err := h.saveExternalDBConfig(req); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "保存数据库配置失败",
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "配置已保存",
		"data":    req,
	})
}

// SyncExternalDBNow 手动触发一次外部数据库同步
func (h *SystemHandler) SyncExternalDBNow(c *gin.Context) {
	if err := service.RunExternalDBSyncOnce(h.db); err != nil {
		c.JSON(http.StatusOK, gin.H{
			"code":    1,
			"message": "同步失败: " + err.Error(),
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "同步任务已执行",
	})
}

// TestExternalDBConnection 测试外部数据库连接
func (h *SystemHandler) TestExternalDBConnection(c *gin.Context) {
	var req ExternalDBConfig
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}

	normalizeExternalDBConfig(&req)
	if err := validateExternalDBConfig(req, true); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": err.Error(),
		})
		return
	}

	switch req.DBType {
	case "mysql":
		dsn := fmt.Sprintf("%s:%s@tcp(%s:%d)/%s?charset=%s&parseTime=True&loc=Local",
			req.Username,
			req.Password,
			req.Host,
			req.Port,
			req.Database,
			req.Charset,
		)
		db, err := gorm.Open(mysql.Open(dsn), &gorm.Config{})
		if err != nil {
			c.JSON(http.StatusOK, gin.H{
				"code":    1,
				"message": "MySQL连接失败: " + err.Error(),
				"data": gin.H{
					"success": false,
				},
			})
			return
		}

		sqlDB, err := db.DB()
		if err != nil {
			c.JSON(http.StatusOK, gin.H{
				"code":    1,
				"message": "获取MySQL连接失败: " + err.Error(),
				"data": gin.H{
					"success": false,
				},
			})
			return
		}
		defer sqlDB.Close()

		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()

		if err := sqlDB.PingContext(ctx); err != nil {
			c.JSON(http.StatusOK, gin.H{
				"code":    1,
				"message": "MySQL连通性验证失败: " + err.Error(),
				"data": gin.H{
					"success": false,
				},
			})
			return
		}

		c.JSON(http.StatusOK, gin.H{
			"code":    0,
			"message": "MySQL连接成功",
			"data": gin.H{
				"success": true,
			},
		})
		return

	case "mssql":
		dsn := buildMSSQLDSN(req)
		db, err := gorm.Open(sqlserver.Open(dsn), &gorm.Config{})
		if err != nil {
			c.JSON(http.StatusOK, gin.H{
				"code":    1,
				"message": "MSSQL连接失败: " + err.Error(),
				"data": gin.H{
					"success": false,
				},
			})
			return
		}

		sqlDB, err := db.DB()
		if err != nil {
			c.JSON(http.StatusOK, gin.H{
				"code":    1,
				"message": "获取MSSQL连接失败: " + err.Error(),
				"data": gin.H{
					"success": false,
				},
			})
			return
		}
		defer sqlDB.Close()

		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()

		if err := sqlDB.PingContext(ctx); err != nil {
			c.JSON(http.StatusOK, gin.H{
				"code":    1,
				"message": "MSSQL连通性验证失败: " + err.Error(),
				"data": gin.H{
					"success": false,
				},
			})
			return
		}

		c.JSON(http.StatusOK, gin.H{
			"code":    0,
			"message": "MSSQL连接成功",
			"data": gin.H{
				"success": true,
			},
		})
		return
	default:
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "不支持的数据库类型",
		})
	}
}
