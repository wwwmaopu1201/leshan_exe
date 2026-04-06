package main

import (
	"fmt"
	"io"
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
	config             Config
	db                 *gorm.DB
	runtimeLogSettings LogSettings
)

type LogSettings struct {
	LogToStdout         bool
	EnableHTTPAccessLog bool
	GORMLogLevel        logger.LogLevel
}

func setupLogging(settings LogSettings) *os.File {
	// 日志文件放在程序所在目录下的 logs 文件夹
	exePath, err := os.Executable()
	if err != nil {
		exePath = "."
	}
	logDir := filepath.Join(filepath.Dir(exePath), "logs")
	os.MkdirAll(logDir, 0755)

	logFile := filepath.Join(logDir, fmt.Sprintf("server_%s.log", time.Now().Format("2006-01-02")))
	f, err := os.OpenFile(logFile, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
	if err != nil {
		log.Printf("Failed to open log file %s: %v, logging to console only", logFile, err)
		return nil
	}

	writers := []io.Writer{f}
	if settings.LogToStdout {
		writers = append(writers, os.Stdout)
	}

	log.SetOutput(io.MultiWriter(writers...))
	log.SetFlags(log.Ldate | log.Ltime | log.Lmicroseconds)
	log.Printf("Log file: %s", logFile)
	return f
}

func parseEnvBool(name string, fallback bool) bool {
	value := strings.TrimSpace(os.Getenv(name))
	if value == "" {
		return fallback
	}

	parsed, err := strconv.ParseBool(value)
	if err != nil {
		return fallback
	}
	return parsed
}

func shouldSkipTrialValidation() bool {
	return parseEnvBool("BOERLAN_SKIP_TRIAL", false) ||
		strings.EqualFold(strings.TrimSpace(os.Getenv("APP_ENV")), "development")
}

func parseGORMLogLevel(value string, fallback logger.LogLevel) logger.LogLevel {
	switch strings.ToLower(strings.TrimSpace(value)) {
	case "silent":
		return logger.Silent
	case "error":
		return logger.Error
	case "warn", "warning":
		return logger.Warn
	case "info":
		return logger.Info
	default:
		return fallback
	}
}

func resolveLogSettings() LogSettings {
	quietMode := parseEnvBool("QUIET_MODE", true)

	settings := LogSettings{
		LogToStdout:         !quietMode,
		EnableHTTPAccessLog: !quietMode,
		GORMLogLevel:        logger.Info,
	}
	if quietMode {
		settings.GORMLogLevel = logger.Error
	}

	settings.LogToStdout = parseEnvBool("LOG_TO_STDOUT", settings.LogToStdout)
	settings.EnableHTTPAccessLog = parseEnvBool("HTTP_ACCESS_LOG", settings.EnableHTTPAccessLog)

	if level := strings.TrimSpace(os.Getenv("GORM_LOG_LEVEL")); level != "" {
		settings.GORMLogLevel = parseGORMLogLevel(level, settings.GORMLogLevel)
	}

	return settings
}

func newGORMLogger(level logger.LogLevel) logger.Interface {
	return logger.New(
		log.New(log.Writer(), "", log.LstdFlags),
		logger.Config{
			SlowThreshold:             time.Second,
			LogLevel:                  level,
			IgnoreRecordNotFoundError: true,
			Colorful:                  false,
		},
	)
}

func main() {
	runtimeLogSettings = resolveLogSettings()
	logFile := setupLogging(runtimeLogSettings)
	if logFile != nil {
		defer logFile.Close()
	}

	if shouldSkipTrialValidation() {
		log.Printf("Trial validation skipped in development mode")
	} else {
		trialStatus, err := trial.Ensure()
		if err != nil {
			log.Fatalf("Trial validation failed: %s", trialStatus.Message)
		}
		trial.StartExpiryWatcher(trialStatus)
		log.Printf("Trial valid until %s", trialStatus.ExpiresAt.Format(time.RFC3339))
	}

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

	r := gin.New()
	if runtimeLogSettings.EnableHTTPAccessLog {
		r.Use(gin.LoggerWithWriter(log.Writer()))
	}
	r.Use(gin.RecoveryWithWriter(log.Writer()))

	// CORS middleware
	r.Use(corsMiddleware())

	// Start TCP server for device communication
	tcpServer := service.NewTCPServer(db)
	tcpServer.Start()
	defer tcpServer.Stop()

	patternTransfer := service.NewPatternTransferService(db, tcpServer.ConnectionManager())

	// Setup routes
	api.SetupRouter(r, db, config.JWT.Secret, config.JWT.Expire, config.Server.Port, patternTransfer)

	// Start background workers
	downloadWorker := service.NewDownloadTaskWorker(db, patternTransfer)
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

	// 开发调试时允许环境变量覆盖配置文件中的数据库路径。
	if dbPath := strings.TrimSpace(os.Getenv("DB_PATH")); dbPath != "" {
		config.Database.Path = dbPath
	} else if dataDir := strings.TrimSpace(os.Getenv("DATA_DIR")); dataDir != "" {
		config.Database.Path = filepath.Join(dataDir, "boer-lan.db")
	} else if config.Database.Path == "" {
		config.Database.Path = "./data/boer-lan.db"
	}

	switch config.Database.Type {
	case "sqlite":
		// 确保数据目录存在
		dbDir := config.Database.Path[:len(config.Database.Path)-len("/boer-lan.db")]
		if err := os.MkdirAll(dbDir, 0755); err != nil {
			log.Fatalf("Failed to create data directory: %v", err)
		}

		db, err = gorm.Open(sqlite.Open(config.Database.Path), &gorm.Config{
			Logger: newGORMLogger(runtimeLogSettings.GORMLogLevel),
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
			Logger: newGORMLogger(runtimeLogSettings.GORMLogLevel),
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
	backfillEmployeeGroupIDs(db)

	log.Println("Database connected and migrated successfully")
}

func backfillEmployeeGroupIDs(db *gorm.DB) {
	var employees []model.Employee
	if err := db.Select("id").Where("group_id IS NULL").Find(&employees).Error; err != nil {
		log.Printf("Skip employee group backfill: %v", err)
		return
	}
	if len(employees) == 0 {
		return
	}

	backfilled := 0
	ambiguous := 0
	for _, employee := range employees {
		groupSet := make(map[uint]struct{})

		var deviceGroups []uint
		if err := db.Table("employee_devices ed").
			Select("DISTINCT d.group_id").
			Joins("JOIN devices d ON d.id = ed.device_id").
			Where("ed.employee_id = ?", employee.ID).
			Where("d.group_id IS NOT NULL").
			Pluck("d.group_id", &deviceGroups).Error; err == nil {
			for _, id := range deviceGroups {
				if id > 0 {
					groupSet[id] = struct{}{}
				}
			}
		}

		var productionGroups []uint
		if err := db.Table("production_records pr").
			Select("DISTINCT d.group_id").
			Joins("JOIN devices d ON d.id = pr.device_id").
			Where("pr.employee_id = ?", employee.ID).
			Where("d.group_id IS NOT NULL").
			Pluck("d.group_id", &productionGroups).Error; err == nil {
			for _, id := range productionGroups {
				if id > 0 {
					groupSet[id] = struct{}{}
				}
			}
		}

		var salaryGroups []uint
		if err := db.Table("salary_records sr").
			Select("DISTINCT d.group_id").
			Joins("JOIN devices d ON d.id = sr.device_id").
			Where("sr.employee_id = ?", employee.ID).
			Where("d.group_id IS NOT NULL").
			Pluck("d.group_id", &salaryGroups).Error; err == nil {
			for _, id := range salaryGroups {
				if id > 0 {
					groupSet[id] = struct{}{}
				}
			}
		}

		if len(groupSet) == 1 {
			for groupID := range groupSet {
				if err := db.Model(&model.Employee{}).
					Where("id = ?", employee.ID).
					Update("group_id", groupID).Error; err == nil {
					backfilled++
				}
			}
			continue
		}

		if len(groupSet) > 1 {
			ambiguous++
		}
	}

	if backfilled > 0 || ambiguous > 0 {
		log.Printf("Employee group backfill finished: backfilled=%d ambiguous=%d", backfilled, ambiguous)
	}
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

	var firstGroup model.Group
	if err := db.Order("id ASC").First(&firstGroup).Error; err != nil {
		firstGroup = model.Group{}
	}

	ensureDefaultAdminUser(db, &firstGroup)
	ensureDefaultOperatorUser(db, &firstGroup)
}

func ensureDefaultAdminUser(db *gorm.DB, firstGroup *model.Group) {
	var count int64
	db.Model(&model.User{}).Where("username = ?", "admin").Count(&count)
	if count > 0 {
		return
	}

	hashedPassword, _ := utils.HashPassword("admin123")

	adminPermissions := `{"home":true,"dashboard":true,"deviceManagement":true,"fileManagement":true,"statistics":true,"employeeManagement":true,"remoteMonitoring":true}`
	var adminRole model.Role
	if err := db.Where("name = ?", "admin").First(&adminRole).Error; err == nil {
		adminPermissions = adminRole.Permissions
	}

	adminUser := model.User{
		Username:    "admin",
		Password:    hashedPassword,
		Nickname:    "管理员",
		Role:        "admin",
		Permissions: adminPermissions,
	}
	if firstGroup != nil && firstGroup.ID > 0 {
		adminUser.GroupID = &firstGroup.ID
	}

	db.Create(&adminUser)
	log.Println("Default admin user created (admin/admin123)")
}

func ensureDefaultOperatorUser(db *gorm.DB, firstGroup *model.Group) {
	var count int64
	db.Model(&model.Operator{}).Where("username = ?", "operator").Count(&count)
	if count > 0 {
		return
	}

	hashedPassword, _ := utils.HashPassword("123")

	operator := model.Operator{
		Username: "operator",
		Password: hashedPassword,
		Nickname: "操作员",
	}
	if firstGroup != nil && firstGroup.ID > 0 {
		operator.GroupID = &firstGroup.ID
	}

	db.Create(&operator)
	log.Println("Default operator created (operator/123)")
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
