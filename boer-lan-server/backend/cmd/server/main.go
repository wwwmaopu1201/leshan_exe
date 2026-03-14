package main

import (
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"boer-lan-server/internal/api"
	"boer-lan-server/internal/model"
	"boer-lan-server/internal/service"
	"boer-lan-server/pkg/trial"
	"boer-lan-server/pkg/utils"

	"github.com/gin-gonic/gin"
	"gopkg.in/yaml.v3"
	"gorm.io/driver/mysql"
	"gorm.io/driver/sqlite"
	"gorm.io/gorm"
	"gorm.io/gorm/logger"
)

type Config struct {
	Server struct {
		Port int    `yaml:"port"`
		Mode string `yaml:"mode"`
	} `yaml:"server"`
	Database struct {
		Type string `yaml:"type"` // sqlite, mysql
		Path string `yaml:"path"` // SQLite数据库文件路径
		// MySQL配置（保留以便需要时可切换）
		Host     string `yaml:"host"`
		Port     int    `yaml:"port"`
		Username string `yaml:"username"`
		Password string `yaml:"password"`
		Database string `yaml:"database"`
		Charset  string `yaml:"charset"`
	} `yaml:"database"`
	JWT struct {
		Secret string `yaml:"secret"`
		Expire int    `yaml:"expire"`
	} `yaml:"jwt"`
}

var (
	config Config
	db     *gorm.DB
)

func main() {
	trialStatus, err := trial.Ensure()
	if err != nil {
		log.Fatalf("Trial validation failed: %s", trialStatus.Message)
	}
	trial.StartExpiryWatcher(trialStatus)
	log.Printf("Trial valid until %s", trialStatus.ExpiresAt.Format(time.RFC3339))

	// Load config
	loadConfig()

	// Initialize database
	initDB()
	applyServerConfigOverrides()
	persistRuntimePort()

	// Initialize Gin
	if config.Server.Mode == "release" {
		gin.SetMode(gin.ReleaseMode)
	}

	r := gin.Default()

	// CORS middleware
	r.Use(corsMiddleware())

	// Setup routes
	api.SetupRouter(r, db, config.JWT.Secret, config.JWT.Expire, config.Server.Port)

	// Start background workers
	downloadWorker := service.NewDownloadTaskWorker(db)
	downloadWorker.Start()
	defer downloadWorker.Stop()

	externalDBSyncWorker := service.NewExternalDBSyncService(db)
	externalDBSyncWorker.Start()
	defer externalDBSyncWorker.Stop()

	// Start server
	addr := fmt.Sprintf(":%d", config.Server.Port)
	log.Printf("Server starting on %s", addr)
	if err := r.Run(addr); err != nil {
		log.Fatalf("Failed to start server: %v", err)
	}
}

func applyServerConfigOverrides() {
	if config.Server.Port <= 0 {
		config.Server.Port = 8088
	}

	var record model.ServerConfig
	if err := db.Where("key = ?", "server_port").First(&record).Error; err != nil {
		return
	}

	port, err := strconv.Atoi(strings.TrimSpace(record.Value))
	if err != nil || port < 1 || port > 65535 {
		log.Printf("Ignore invalid server_port from server_config: %q", record.Value)
		return
	}

	if config.Server.Port != port {
		log.Printf("Server port overridden by server_config: %d -> %d", config.Server.Port, port)
	}
	config.Server.Port = port
}

func persistRuntimePort() {
	portFile := strings.TrimSpace(os.Getenv("PORT_FILE"))
	if portFile == "" {
		dataDir := strings.TrimSpace(os.Getenv("DATA_DIR"))
		if dataDir == "" {
			return
		}
		portFile = filepath.Join(dataDir, "backend-port.txt")
	}

	if err := os.MkdirAll(filepath.Dir(portFile), 0755); err != nil {
		log.Printf("Failed to create backend port directory: %v", err)
		return
	}

	if err := os.WriteFile(portFile, []byte(strconv.Itoa(config.Server.Port)), 0644); err != nil {
		log.Printf("Failed to persist backend port: %v", err)
		return
	}

	log.Printf("Backend runtime port written to: %s", portFile)
}

func defaultConfig() Config {
	var cfg Config
	cfg.Server.Port = 8088
	cfg.Server.Mode = "release"
	cfg.Database.Type = "sqlite"
	cfg.Database.Path = ""
	cfg.JWT.Secret = "boer-lan-secret-key-2024"
	cfg.JWT.Expire = 24
	return cfg
}

func loadConfig() {
	config = defaultConfig()

	// 开发环境下支持从环境变量读取配置文件路径；打包后若文件不存在则使用内置默认配置
	configPath := os.Getenv("CONFIG_PATH")
	if configPath == "" {
		configPath = "configs/config.yaml"
	}

	data, err := os.ReadFile(configPath)
	if err == nil {
		if err := yaml.Unmarshal(data, &config); err != nil {
			log.Fatalf("Failed to parse config file: %v", err)
		}
		log.Printf("Config loaded from: %s", configPath)
	} else if os.IsNotExist(err) {
		log.Printf("Config file not found, using embedded defaults")
	} else {
		log.Fatalf("Failed to read config file: %v", err)
	}

	utils.JWTSecret = config.JWT.Secret
	utils.JWTExpire = config.JWT.Expire
}

func initDB() {
	var err error

	// 默认使用SQLite
	if config.Database.Type == "" {
		config.Database.Type = "sqlite"
	}

	// 支持从环境变量读取数据目录
	if config.Database.Path == "" {
		dataDir := os.Getenv("DATA_DIR")
		if dataDir == "" {
			dataDir = "./data"
		}
		config.Database.Path = fmt.Sprintf("%s/boer-lan.db", dataDir)
	}

	switch config.Database.Type {
	case "sqlite":
		// 确保数据目录存在
		dbDir := config.Database.Path[:len(config.Database.Path)-len("/boer-lan.db")]
		if err := os.MkdirAll(dbDir, 0755); err != nil {
			log.Fatalf("Failed to create data directory: %v", err)
		}

		db, err = gorm.Open(sqlite.Open(config.Database.Path), &gorm.Config{
			Logger: logger.Default.LogMode(logger.Info),
		})
		if err != nil {
			log.Fatalf("Failed to connect to SQLite database: %v", err)
		}
		log.Printf("Using SQLite database: %s", config.Database.Path)

	case "mysql":
		dsn := fmt.Sprintf("%s:%s@tcp(%s:%d)/%s?charset=%s&parseTime=True&loc=Local",
			config.Database.Username,
			config.Database.Password,
			config.Database.Host,
			config.Database.Port,
			config.Database.Database,
			config.Database.Charset,
		)
		db, err = gorm.Open(mysql.Open(dsn), &gorm.Config{
			Logger: logger.Default.LogMode(logger.Info),
		})
		if err != nil {
			log.Fatalf("Failed to connect to MySQL database: %v", err)
		}
		log.Printf("Using MySQL database: %s", config.Database.Database)

	default:
		log.Fatalf("Unsupported database type: %s", config.Database.Type)
	}

	// Auto migrate
	if err := db.AutoMigrate(
		&model.Group{},
		&model.Role{},
		&model.User{},
		&model.Operator{},
		&model.Device{},
		&model.Pattern{},
		&model.DownloadTask{},
		&model.DevicePatternFile{},
		&model.UploadTask{},
		&model.Employee{},
		&model.EmployeeDevice{},
		&model.ProductionRecord{},
		&model.AlarmRecord{},
		&model.SalaryRecord{},
		&model.LoginLog{},
		&model.ServerConfig{},
		&model.DebugLog{},
	); err != nil {
		log.Fatalf("Failed to auto migrate: %v", err)
	}

	// Create default data if not exists
	initDefaultData(db)

	log.Println("Database connected and migrated successfully")
}

func initDefaultData(db *gorm.DB) {
	// 创建默认权限角色
	defaultPermissions := `{"home":true,"dashboard":true,"deviceManagement":true,"fileManagement":true,"statistics":true,"employeeManagement":true,"remoteMonitoring":true}`
	ensureDefaultRole := func(name string, remark string) {
		var count int64
		db.Model(&model.Role{}).Where("name = ?", name).Count(&count)
		if count == 0 {
			db.Create(&model.Role{
				Name:            name,
				Remark:          remark,
				Permissions:     defaultPermissions,
				ParentChildLink: true,
			})
		}
	}
	ensureDefaultRole("admin", "系统默认管理员角色")
	ensureDefaultRole("user", "系统默认普通角色")

	// 创建默认分组
	var groupCount int64
	db.Model(&model.Group{}).Count(&groupCount)
	if groupCount == 0 {
		// 创建一级分组
		group1 := model.Group{Name: "工厂一"}
		db.Create(&group1)

		// 创建二级分组
		db.Create(&model.Group{Name: "车间一", ParentID: &group1.ID})
		db.Create(&model.Group{Name: "车间二", ParentID: &group1.ID})

		log.Println("Default groups created")
	}

	// 创建默认管理员用户
	var userCount int64
	db.Model(&model.User{}).Count(&userCount)
	if userCount == 0 {
		hashedPassword, _ := utils.HashPassword("admin123")

		// 获取第一个分组
		var firstGroup model.Group
		db.First(&firstGroup)

		adminPermissions := `{"home":true,"dashboard":true,"deviceManagement":true,"fileManagement":true,"statistics":true,"employeeManagement":true,"remoteMonitoring":true}`
		var adminRole model.Role
		if err := db.Where("name = ?", "admin").First(&adminRole).Error; err == nil {
			adminPermissions = adminRole.Permissions
		}

		db.Create(&model.User{
			Username:    "admin",
			Password:    hashedPassword,
			Nickname:    "管理员",
			Role:        "admin",
			GroupID:     &firstGroup.ID,
			Permissions: adminPermissions,
		})
		log.Println("Default admin user created (admin/admin123)")
	}

	// 创建默认操作员
	var operatorCount int64
	db.Model(&model.Operator{}).Count(&operatorCount)
	if operatorCount == 0 {
		hashedPassword, _ := utils.HashPassword("123")

		// 获取第一个分组
		var firstGroup model.Group
		db.First(&firstGroup)

		db.Create(&model.Operator{
			Username: "operator",
			Password: hashedPassword,
			Nickname: "操作员",
			GroupID:  &firstGroup.ID,
		})
		log.Println("Default operator created (operator/123)")
	}
}

func corsMiddleware() gin.HandlerFunc {
	return func(c *gin.Context) {
		c.Header("Access-Control-Allow-Origin", "*")
		c.Header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
		c.Header("Access-Control-Allow-Headers", "Origin, Content-Type, Authorization")
		c.Header("Access-Control-Max-Age", "86400")

		if c.Request.Method == "OPTIONS" {
			c.AbortWithStatus(204)
			return
		}

		c.Next()
	}
}
