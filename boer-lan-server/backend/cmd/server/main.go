package main

import (
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"
	"unicode"
	"unicode/utf16"

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

const (
	appIdentifier = "com.boer.lan-server"
	startupAPIURL = "http://47.92.226.92:56/api.php"
)

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
	logDir := strings.TrimSpace(os.Getenv("LOG_DIR"))
	if logDir == "" {
		// 打包版保持原逻辑：日志文件放在程序所在目录下的 logs 文件夹。
		exePath, err := os.Executable()
		if err != nil {
			exePath = "."
		}
		logDir = filepath.Join(filepath.Dir(exePath), "logs")
	}
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

func ensureStartupAPIAllowed() error {
	client := &http.Client{Timeout: 5 * time.Second}
	req, err := http.NewRequest(http.MethodGet, startupAPIURL, nil)
	if err != nil {
		return fmt.Errorf("failed to create startup api request: %w", err)
	}
	req.Header.Set("User-Agent", "BoerLAN-Backend")
	req.Header.Set("Accept", "application/json,text/plain,*/*")

	resp, err := client.Do(req)
	if err != nil {
		return fmt.Errorf("failed to request startup api: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode < http.StatusOK || resp.StatusCode >= http.StatusMultipleChoices {
		return fmt.Errorf("startup api returned status %d", resp.StatusCode)
	}

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return fmt.Errorf("failed to read startup api response: %w", err)
	}

	value := strings.TrimPrefix(strings.TrimSpace(string(body)), "\ufeff")
	var allowed bool
	if err := json.Unmarshal([]byte(value), &allowed); err == nil {
		if allowed {
			return nil
		}
		return fmt.Errorf("startup api disabled startup")
	}

	switch strings.ToLower(value) {
	case "true":
		return nil
	case "false":
		return fmt.Errorf("startup api disabled startup")
	default:
		return fmt.Errorf("startup api returned invalid value %q", value)
	}
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

	if err := ensureStartupAPIAllowed(); err != nil {
		log.Fatalf("网络错误，请检查网络连接: %v", err)
	}

	if shouldSkipTrialValidation() {
		log.Printf("Trial validation skipped in development mode")
	} else {
		trialStatus, err := trial.Ensure()
		if err != nil {
			if trialStatus != nil && trialStatus.Message != "" {
				log.Fatalf("Trial validation failed: %s", trialStatus.Message)
			}
			log.Fatalf("Trial validation failed: %v", err)
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
	r.Static("/uploads", "./uploads")

	// CORS middleware
	r.Use(corsMiddleware())

	// Start TCP server for device communication
	tcpServer := service.NewTCPServer(db)
	tcpServer.Start()
	defer tcpServer.Stop()

	patternTransfer := service.NewPatternTransferService(db, tcpServer.ConnectionManager())

	// Setup routes
	api.SetupRouter(r, db, config.JWT.Secret, config.JWT.Expire, config.Server.Port, patternTransfer, tcpServer.ConnectionManager())

	// Start background workers
	downloadWorker := service.NewDownloadTaskWorker(db, patternTransfer)
	downloadWorker.Start()
	defer downloadWorker.Stop()

	devicePatternSyncWorker := service.NewDevicePatternSyncWorker(db, patternTransfer)
	devicePatternSyncWorker.Start()
	defer devicePatternSyncWorker.Stop()

	externalDBSyncWorker := service.NewExternalDBSyncService(db)
	externalDBSyncWorker.Start()
	defer externalDBSyncWorker.Stop()

	// Start server
	addr := fmt.Sprintf(":%d", config.Server.Port)
	log.Printf("Server starting on %s", addr)
	httpServer := &http.Server{
		Addr:              addr,
		Handler:           r,
		ReadHeaderTimeout: 30 * time.Second,
		ReadTimeout:       5 * time.Minute,
		WriteTimeout:      30 * time.Minute,
		IdleTimeout:       5 * time.Minute,
	}
	if err := httpServer.ListenAndServe(); err != nil && err != http.ErrServerClosed {
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
			sharedDataDir, err := resolveSharedDataDir()
			if err != nil {
				return
			}
			dataDir = sharedDataDir
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

func resolveSharedDataDir() (string, error) {
	if raw := strings.TrimSpace(os.Getenv("BOERLAN_DATA_DIR")); raw != "" {
		return raw, nil
	}

	homeDir, err := os.UserHomeDir()
	if err != nil {
		return "", err
	}

	switch runtime.GOOS {
	case "darwin":
		return filepath.Join(homeDir, "Library", "Application Support", appIdentifier), nil
	case "windows":
		if appData := strings.TrimSpace(os.Getenv("APPDATA")); appData != "" {
			return filepath.Join(appData, appIdentifier), nil
		}
		return filepath.Join(homeDir, "AppData", "Roaming", appIdentifier), nil
	default:
		if xdgDataHome := strings.TrimSpace(os.Getenv("XDG_DATA_HOME")); xdgDataHome != "" {
			return filepath.Join(xdgDataHome, appIdentifier), nil
		}
		return filepath.Join(homeDir, ".local", "share", appIdentifier), nil
	}
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
		sharedDataDir, err := resolveSharedDataDir()
		if err != nil {
			log.Fatalf("Failed to resolve shared data dir: %v", err)
		}
		config.Database.Path = filepath.Join(sharedDataDir, "boer-lan.db")
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
		&model.DeviceTypeCatalog{},
		&model.ElectricControlTypeCatalog{},
		&model.DeviceRuntimeSession{},
		&model.Pattern{},
		&model.PatternTypeCatalog{},
		&model.OrderNoCatalog{},
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
	ensureDeviceIdentityIndexes(db)

	// Create default data if not exists
	initDefaultData(db)
	repairLegacyDeviceData(db)
	backfillEmployeeGroupIDs(db)
	service.BackfillProductionDerivedData(db)

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
	ensureDefaultRootGroup(db)
	normalizeTopLevelGroupsUnderRoot(db)
	clearLegacyDefaultDeviceType(db)

	ensureDefaultAdminUser(db)
}

func clearLegacyDefaultDeviceType(db *gorm.DB) {
	const legacyDefaultDeviceType = "电控类型"
	if err := db.Model(&model.Device{}).
		Where("type = ?", legacyDefaultDeviceType).
		Update("type", "").Error; err != nil {
		log.Printf("Failed to clear legacy default device type: %v", err)
	}
	if err := db.Where("value = ?", legacyDefaultDeviceType).
		Delete(&model.DeviceTypeCatalog{}).Error; err != nil {
		log.Printf("Failed to remove legacy default device type catalog: %v", err)
	}
}

func ensureDefaultRootGroup(db *gorm.DB) {
	var groupCount int64
	if err := db.Model(&model.Group{}).Count(&groupCount).Error; err != nil {
		log.Printf("Skip default root group ensure: %v", err)
		return
	}
	if groupCount > 0 {
		return
	}

	group := model.Group{
		Name:      "总分组",
		SortOrder: 1,
	}
	if err := db.Create(&group).Error; err != nil {
		log.Printf("Failed to create default root group: %v", err)
		return
	}
	log.Println("Default root group created (总分组)")
}

func normalizeTopLevelGroupsUnderRoot(db *gorm.DB) {
	var rootGroup model.Group
	if err := db.Where("name = ?", "总分组").Order("id ASC").First(&rootGroup).Error; err != nil {
		return
	}

	if err := db.Model(&model.Group{}).
		Where("parent_id IS NULL").
		Where("id <> ?", rootGroup.ID).
		Update("parent_id", rootGroup.ID).Error; err != nil {
		log.Printf("Skip top-level group normalization: %v", err)
	}
}

func ensureDefaultAdminUser(db *gorm.DB) {
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
	var rootGroup model.Group
	if err := db.Where("name = ?", "总分组").Order("id ASC").First(&rootGroup).Error; err == nil {
		adminUser.GroupID = &rootGroup.ID
	}

	db.Create(&adminUser)
	log.Println("Default admin user created (admin/admin123)")
}

func repairLegacyDeviceData(db *gorm.DB) {
	repairMisdecodedDeviceTextFields(db)
	archiveDuplicateDevicesByMainboard(db)
	archivePlaceholderMainboardDevices(db)
	archiveDuplicatePendingDevices(db)
}

func ensureDeviceIdentityIndexes(db *gorm.DB) {
	migrator := db.Migrator()
	if migrator == nil {
		return
	}

	if migrator.HasIndex(&model.Device{}, "idx_devices_code") {
		_ = migrator.DropIndex(&model.Device{}, "idx_devices_code")
	}
	_ = migrator.CreateIndex(&model.Device{}, "Code")
	_ = migrator.CreateIndex(&model.Device{}, "MainboardSN")
}

func repairMisdecodedDeviceTextFields(db *gorm.DB) {
	var devices []model.Device
	if err := db.Find(&devices).Error; err != nil {
		log.Printf("Skip device text repair: %v", err)
		return
	}

	repairedCount := 0
	for _, device := range devices {
		updates := map[string]interface{}{}

		if repaired := recoverMisdecodedASCIIText(device.Code); repaired != "" && repaired != device.Code {
			updates["code"] = repaired
		}
		if repaired := recoverMisdecodedASCIIText(device.Name); repaired != "" && repaired != device.Name {
			updates["name"] = repaired
		}
		if repaired := recoverMisdecodedASCIIText(device.InitialName); repaired != "" && repaired != device.InitialName {
			updates["initial_name"] = repaired
		}
		if repaired := recoverMisdecodedASCIIText(device.MainboardSN); repaired != "" && repaired != device.MainboardSN {
			updates["mainboard_sn"] = repaired
		}

		if len(updates) == 0 {
			continue
		}
		if err := db.Model(&model.Device{}).Where("id = ?", device.ID).Updates(updates).Error; err != nil {
			log.Printf("Skip device text repair id=%d: %v", device.ID, err)
			continue
		}
		repairedCount++
	}

	if repairedCount > 0 {
		log.Printf("Legacy device text repaired: %d", repairedCount)
	}
}

func archiveDuplicatePendingDevices(db *gorm.DB) {
	var devices []model.Device
	if err := db.Select("id", "code", "ip").Find(&devices).Error; err != nil {
		log.Printf("Skip pending device cleanup: %v", err)
		return
	}

	archivedCount := 0
	for _, device := range devices {
		code := strings.TrimSpace(device.Code)
		ip := strings.TrimSpace(device.IP)
		if !strings.HasPrefix(code, "PENDING-") || ip == "" {
			continue
		}

		var duplicateCount int64
		if err := db.Model(&model.Device{}).
			Where("id <> ?", device.ID).
			Where("ip = ?", ip).
			Where("code NOT LIKE ?", "PENDING-%").
			Count(&duplicateCount).Error; err != nil {
			continue
		}
		if duplicateCount == 0 {
			continue
		}

		if err := db.Delete(&model.Device{}, device.ID).Error; err != nil {
			log.Printf("Skip archiving duplicate pending device id=%d: %v", device.ID, err)
			continue
		}
		archivedCount++
	}

	if archivedCount > 0 {
		log.Printf("Duplicate pending devices archived: %d", archivedCount)
	}
}

func archiveDuplicateDevicesByMainboard(db *gorm.DB) {
	var devices []model.Device
	if err := db.Where("deleted_at IS NULL").Find(&devices).Error; err != nil {
		log.Printf("Skip mainboard device dedupe: %v", err)
		return
	}

	groups := make(map[string][]model.Device)
	for _, device := range devices {
		mainboardSN := strings.TrimSpace(device.MainboardSN)
		if mainboardSN == "" {
			continue
		}
		groups[mainboardSN] = append(groups[mainboardSN], device)
	}

	archivedCount := 0
	for _, duplicated := range groups {
		if len(duplicated) <= 1 {
			continue
		}

		keep := duplicated[0]
		for _, candidate := range duplicated[1:] {
			if scoreDeviceIdentity(candidate) > scoreDeviceIdentity(keep) {
				keep = candidate
			}
		}

		for _, candidate := range duplicated {
			if candidate.ID == keep.ID {
				continue
			}
			if err := db.Delete(&model.Device{}, candidate.ID).Error; err != nil {
				log.Printf("Skip duplicate mainboard device archive id=%d: %v", candidate.ID, err)
				continue
			}
			archivedCount++
		}
	}

	if archivedCount > 0 {
		log.Printf("Duplicate mainboard devices archived: %d", archivedCount)
	}
}

func archivePlaceholderMainboardDevices(db *gorm.DB) {
	var devices []model.Device
	if err := db.Where("deleted_at IS NULL").Find(&devices).Error; err != nil {
		log.Printf("Skip placeholder mainboard device cleanup: %v", err)
		return
	}

	archivedCount := 0
	for _, device := range devices {
		code := strings.TrimSpace(device.Code)
		mainboardSN := strings.TrimSpace(device.MainboardSN)
		name := strings.TrimSpace(device.Name)
		ip := strings.TrimSpace(device.IP)
		if code == "" || mainboardSN == "" || ip == "" {
			continue
		}
		if code != mainboardSN {
			continue
		}
		if name != "设备 "+code {
			continue
		}

		var richerCount int64
		if err := db.Model(&model.Device{}).
			Where("id <> ?", device.ID).
			Where("deleted_at IS NULL").
			Where("ip = ?", ip).
			Where("code <> ?", code).
			Where("name NOT LIKE ?", "设备 %").
			Count(&richerCount).Error; err != nil {
			continue
		}
		if richerCount == 0 {
			continue
		}

		if err := db.Delete(&model.Device{}, device.ID).Error; err != nil {
			log.Printf("Skip placeholder mainboard device archive id=%d: %v", device.ID, err)
			continue
		}
		archivedCount++
	}

	if archivedCount > 0 {
		log.Printf("Placeholder mainboard devices archived: %d", archivedCount)
	}
}

func scoreDeviceIdentity(device model.Device) int {
	score := 0
	code := strings.TrimSpace(device.Code)
	mainboardSN := strings.TrimSpace(device.MainboardSN)
	name := strings.TrimSpace(device.Name)

	if code != "" && !strings.HasPrefix(code, "PENDING-") && code != mainboardSN {
		score += 100
	}
	if strings.TrimSpace(device.IdentifiedBy) == "mainboard" {
		score += 20
	}
	if name != "" && !strings.HasPrefix(name, "待识别设备") && !strings.HasPrefix(name, "设备 ") {
		score += 10
	}
	if !device.LastOnline.IsZero() {
		score += 5
	}
	return score
}

func recoverMisdecodedASCIIText(value string) string {
	trimmed := strings.TrimSpace(value)
	if trimmed == "" || isMostlyASCIIText(trimmed) {
		return ""
	}

	raw := encodeUTF16LEString(trimmed)
	candidates := []string{
		strings.TrimSpace(string(raw)),
	}

	for _, candidate := range candidates {
		if candidate == "" || candidate == trimmed {
			continue
		}
		if isMostlyASCIIText(candidate) && isReasonableStorageText(candidate) {
			return candidate
		}
	}
	return ""
}

func encodeUTF16LEString(value string) []byte {
	encoded := utf16.Encode([]rune(value))
	buf := make([]byte, len(encoded)*2)
	for i, code := range encoded {
		buf[i*2] = byte(code)
		buf[i*2+1] = byte(code >> 8)
	}
	return buf
}

func isMostlyASCIIText(value string) bool {
	runes := []rune(strings.TrimSpace(value))
	if len(runes) == 0 {
		return false
	}

	asciiCount := 0
	for _, r := range runes {
		if r <= unicode.MaxASCII && !unicode.IsControl(r) {
			asciiCount++
		}
	}
	return asciiCount*100/len(runes) >= 80
}

func isReasonableStorageText(value string) bool {
	value = strings.TrimSpace(value)
	if value == "" {
		return false
	}
	for _, r := range value {
		if unicode.IsControl(r) && !unicode.IsSpace(r) {
			return false
		}
	}
	return true
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
