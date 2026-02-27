package main

import (
	"fmt"
	"log"
	"os"

	"boer-lan-server/internal/api"
	"boer-lan-server/internal/model"
	"boer-lan-server/pkg/utils"

	"github.com/gin-gonic/gin"
	"gopkg.in/yaml.v3"
	"gorm.io/driver/mysql"
	"gorm.io/gorm"
	"gorm.io/gorm/logger"
)

type Config struct {
	Server struct {
		Port int    `yaml:"port"`
		Mode string `yaml:"mode"`
	} `yaml:"server"`
	Database struct {
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
	api.SetupRouter(r, db, config.JWT.Secret, config.JWT.Expire)

	// Start server
	addr := fmt.Sprintf(":%d", config.Server.Port)
	log.Printf("Server starting on %s", addr)
	if err := r.Run(addr); err != nil {
		log.Fatalf("Failed to start server: %v", err)
	}
}

func loadConfig() {
	data, err := os.ReadFile("configs/config.yaml")
	if err != nil {
		log.Fatalf("Failed to read config file: %v", err)
	}

	if err := yaml.Unmarshal(data, &config); err != nil {
		log.Fatalf("Failed to parse config file: %v", err)
	}

	utils.JWTSecret = config.JWT.Secret
	utils.JWTExpire = config.JWT.Expire
}

func initDB() {
	dsn := fmt.Sprintf("%s:%s@tcp(%s:%d)/%s?charset=%s&parseTime=True&loc=Local",
		config.Database.Username,
		config.Database.Password,
		config.Database.Host,
		config.Database.Port,
		config.Database.Database,
		config.Database.Charset,
	)

	var err error
	db, err = gorm.Open(mysql.Open(dsn), &gorm.Config{
		Logger: logger.Default.LogMode(logger.Info),
	})
	if err != nil {
		log.Fatalf("Failed to connect to database: %v", err)
	}

	// Auto migrate
	if err := db.AutoMigrate(
		&model.User{},
		&model.Device{},
		&model.DeviceGroup{},
		&model.Pattern{},
		&model.DownloadTask{},
		&model.Employee{},
		&model.EmployeeDevice{},
		&model.ProductionRecord{},
		&model.AlarmRecord{},
		&model.SalaryRecord{},
		&model.LoginLog{},
	); err != nil {
		log.Fatalf("Failed to auto migrate: %v", err)
	}

	// Create default admin user if not exists
	var count int64
	db.Model(&model.User{}).Count(&count)
	if count == 0 {
		hashedPassword, _ := utils.HashPassword("admin123")
		db.Create(&model.User{
			Username: "admin",
			Password: hashedPassword,
			Nickname: "管理员",
			Role:     "admin",
		})
	}

	log.Println("Database connected and migrated successfully")
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
