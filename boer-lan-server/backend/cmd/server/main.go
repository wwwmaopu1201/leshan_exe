package main

import (
	"fmt"
	"log"
	"os"

	"boer-lan-server/internal/api"
	"boer-lan-server/internal/model"
	"boer-lan-server/internal/service"
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
	// Load config
	loadConfig()

	// Initialize database
	initDB()

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

func loadConfig() {
	// 支持从环境变量读取配置文件路径
	configPath := os.Getenv("CONFIG_PATH")
	if configPath == "" {
		configPath = "configs/config.yaml"
	}

	data, err := os.ReadFile(configPath)
	if err != nil {
		log.Fatalf("Failed to read config file: %v", err)
	}

	if err := yaml.Unmarshal(data, &config); err != nil {
		log.Fatalf("Failed to parse config file: %v", err)
	}

	utils.JWTSecret = config.JWT.Secret
	utils.JWTExpire = config.JWT.Expire
	log.Printf("Config loaded from: %s", configPath)
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

		db.Create(&model.User{
			Username:    "admin",
			Password:    hashedPassword,
			Nickname:    "管理员",
			Role:        "admin",
			GroupID:     &firstGroup.ID,
			Permissions: `{"fileManagement":true,"remoteMonitoring":true,"statistics":true,"deviceManagement":true}`,
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
