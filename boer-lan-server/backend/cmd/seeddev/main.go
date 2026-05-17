package main

import (
	"encoding/json"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"boer-lan-server/internal/model"
	"boer-lan-server/pkg/utils"

	"gorm.io/driver/sqlite"
	"gorm.io/gorm"
	"gorm.io/gorm/logger"
)

const (
	seedGroupPrefix   = "演示数据·"
	seedRolePrefix    = "seed_"
	seedUserPrefix    = "seed"
	seedOperatorPrefx = "seedop"
	seedEmployeePrefx = "SEED"
	seedDevicePrefix  = "TD-"
	seedFilePrefix    = "seed_"
	seedLogSource     = "seed-dev"
)

type seedContext struct {
	db        *gorm.DB
	now       time.Time
	groups    map[string]model.Group
	roles     map[string]model.Role
	users     map[string]model.User
	operators map[string]model.Operator
	employees map[string]model.Employee
	devices   map[string]model.Device
	patterns  map[string]model.Pattern
	files     map[string]model.DevicePatternFile
}

func main() {
	dbPath := resolveDBPath()
	if err := os.MkdirAll(filepath.Dir(dbPath), 0755); err != nil {
		log.Fatalf("failed to create db dir: %v", err)
	}

	db, err := gorm.Open(sqlite.Open(dbPath), &gorm.Config{
		Logger: logger.Default.LogMode(logger.Silent),
	})
	if err != nil {
		log.Fatalf("failed to open sqlite db: %v", err)
	}

	if err := autoMigrate(db); err != nil {
		log.Fatalf("failed to migrate db: %v", err)
	}

	ctx := &seedContext{
		db:        db,
		now:       time.Now().In(time.Local),
		groups:    make(map[string]model.Group),
		roles:     make(map[string]model.Role),
		users:     make(map[string]model.User),
		operators: make(map[string]model.Operator),
		employees: make(map[string]model.Employee),
		devices:   make(map[string]model.Device),
		patterns:  make(map[string]model.Pattern),
		files:     make(map[string]model.DevicePatternFile),
	}

	if err := db.Transaction(func(tx *gorm.DB) error {
		ctx.db = tx
		if err := cleanupSeedData(tx); err != nil {
			return err
		}
		if err := seedGroups(ctx); err != nil {
			return err
		}
		if err := seedRoles(ctx); err != nil {
			return err
		}
		if err := seedUsers(ctx); err != nil {
			return err
		}
		if err := seedOperators(ctx); err != nil {
			return err
		}
		if err := seedEmployees(ctx); err != nil {
			return err
		}
		if err := seedDeviceTypes(ctx); err != nil {
			return err
		}
		if err := seedDevices(ctx); err != nil {
			return err
		}
		if err := seedEmployeeBindings(ctx); err != nil {
			return err
		}
		if err := seedPatterns(ctx); err != nil {
			return err
		}
		if err := seedDevicePatternFiles(ctx); err != nil {
			return err
		}
		if err := seedDownloadTasks(ctx); err != nil {
			return err
		}
		if err := seedUploadTasks(ctx); err != nil {
			return err
		}
		if err := seedProductionRecords(ctx); err != nil {
			return err
		}
		if err := seedAlarmRecords(ctx); err != nil {
			return err
		}
		if err := seedSalaryRecords(ctx); err != nil {
			return err
		}
		if err := seedLoginLogs(ctx); err != nil {
			return err
		}
		if err := seedServerConfigs(ctx); err != nil {
			return err
		}
		if err := seedDebugLogs(ctx); err != nil {
			return err
		}
		return nil
	}); err != nil {
		log.Fatalf("failed to seed db: %v", err)
	}

	fmt.Printf("Seeded development data into %s\n", dbPath)
	fmt.Println("Seed users:")
	fmt.Println("  seedadmin / Seed123456   (演示管理员，查看全部功能和全部分组)")
	fmt.Println("  seedfull  / Seed123456   (全权限非管理员，分组受限于演示一厂缝制A组)")
	fmt.Println("  seedfile  / Seed123456   (文件与下发相关功能)")
	fmt.Println("  seedstat  / Seed123456   (统计相关功能)")
	fmt.Println("  seedemp   / Seed123456   (员工管理相关功能)")
	fmt.Println("  seedoff   / Seed123456   (禁用账号，用于状态测试)")
}

func resolveDBPath() string {
	if raw := strings.TrimSpace(os.Getenv("DB_PATH")); raw != "" {
		return raw
	}
	if raw := strings.TrimSpace(os.Getenv("DATA_DIR")); raw != "" {
		return filepath.Join(raw, "boer-lan.db")
	}

	candidates := []string{
		filepath.Join("..", ".dev-data", "boer-lan.db"),
		filepath.Join("data", "boer-lan.db"),
	}
	for _, candidate := range candidates {
		if _, err := os.Stat(filepath.Dir(candidate)); err == nil {
			return candidate
		}
	}
	return filepath.Join("data", "boer-lan.db")
}

func autoMigrate(db *gorm.DB) error {
	return db.AutoMigrate(
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
	)
}

func cleanupSeedData(db *gorm.DB) error {
	userIDs := queryIDs(db, &model.User{}, "username LIKE ?", seedUserPrefix+"%")
	roleIDs := queryIDs(db, &model.Role{}, "name LIKE ?", seedRolePrefix+"%")
	groupIDs := queryIDs(db, &model.Group{}, "name LIKE ?", seedGroupPrefix+"%")
	operatorIDs := queryIDs(db, &model.Operator{}, "username LIKE ?", seedOperatorPrefx+"%")
	employeeIDs := queryIDs(db, &model.Employee{}, "code LIKE ?", seedEmployeePrefx+"%")
	deviceIDs := queryIDs(db, &model.Device{}, "code LIKE ?", seedDevicePrefix+"%")
	patternIDs := queryIDs(db, &model.Pattern{}, "file_name LIKE ?", seedFilePrefix+"%")
	deviceFileIDs := queryIDs(db, &model.DevicePatternFile{}, "file_name LIKE ?", seedFilePrefix+"%")

	if len(userIDs) > 0 {
		if err := db.Where("user_id IN ?", userIDs).Delete(&model.LoginLog{}).Error; err != nil {
			return err
		}
	}
	if len(deviceIDs) > 0 {
		if err := db.Where("device_id IN ?", deviceIDs).Delete(&model.ProductionRecord{}).Error; err != nil {
			return err
		}
		if err := db.Where("device_id IN ?", deviceIDs).Delete(&model.AlarmRecord{}).Error; err != nil {
			return err
		}
		if err := db.Where("device_id IN ?", deviceIDs).Delete(&model.SalaryRecord{}).Error; err != nil {
			return err
		}
	}
	if len(employeeIDs) > 0 {
		if err := db.Where("employee_id IN ?", employeeIDs).Delete(&model.EmployeeDevice{}).Error; err != nil {
			return err
		}
		if err := db.Where("employee_id IN ?", employeeIDs).Delete(&model.SalaryRecord{}).Error; err != nil {
			return err
		}
	}
	if len(patternIDs) > 0 || len(deviceIDs) > 0 {
		query := db.Model(&model.DownloadTask{})
		if len(patternIDs) > 0 && len(deviceIDs) > 0 {
			query = query.Where("pattern_id IN ? OR device_id IN ?", patternIDs, deviceIDs)
		} else if len(patternIDs) > 0 {
			query = query.Where("pattern_id IN ?", patternIDs)
		} else {
			query = query.Where("device_id IN ?", deviceIDs)
		}
		if err := query.Delete(&model.DownloadTask{}).Error; err != nil {
			return err
		}
	}
	if len(deviceFileIDs) > 0 || len(deviceIDs) > 0 || len(patternIDs) > 0 {
		query := db.Model(&model.UploadTask{})
		switch {
		case len(deviceFileIDs) > 0 && len(deviceIDs) > 0 && len(patternIDs) > 0:
			query = query.Where("device_file_id IN ? OR device_id IN ? OR pattern_id IN ?", deviceFileIDs, deviceIDs, patternIDs)
		case len(deviceFileIDs) > 0 && len(deviceIDs) > 0:
			query = query.Where("device_file_id IN ? OR device_id IN ?", deviceFileIDs, deviceIDs)
		case len(deviceFileIDs) > 0 && len(patternIDs) > 0:
			query = query.Where("device_file_id IN ? OR pattern_id IN ?", deviceFileIDs, patternIDs)
		case len(deviceIDs) > 0 && len(patternIDs) > 0:
			query = query.Where("device_id IN ? OR pattern_id IN ?", deviceIDs, patternIDs)
		case len(deviceFileIDs) > 0:
			query = query.Where("device_file_id IN ?", deviceFileIDs)
		case len(deviceIDs) > 0:
			query = query.Where("device_id IN ?", deviceIDs)
		default:
			query = query.Where("pattern_id IN ?", patternIDs)
		}
		if err := query.Delete(&model.UploadTask{}).Error; err != nil {
			return err
		}
	}
	if len(deviceFileIDs) > 0 {
		if err := db.Unscoped().Where("id IN ?", deviceFileIDs).Delete(&model.DevicePatternFile{}).Error; err != nil {
			return err
		}
	}
	if len(patternIDs) > 0 {
		if err := db.Unscoped().Where("id IN ?", patternIDs).Delete(&model.Pattern{}).Error; err != nil {
			return err
		}
	}
	if len(deviceIDs) > 0 {
		if err := db.Unscoped().Where("id IN ?", deviceIDs).Delete(&model.Device{}).Error; err != nil {
			return err
		}
	}
	if len(employeeIDs) > 0 {
		if err := db.Unscoped().Where("id IN ?", employeeIDs).Delete(&model.Employee{}).Error; err != nil {
			return err
		}
	}
	if len(operatorIDs) > 0 {
		if err := db.Unscoped().Where("id IN ?", operatorIDs).Delete(&model.Operator{}).Error; err != nil {
			return err
		}
	}
	if len(userIDs) > 0 {
		if err := db.Unscoped().Where("id IN ?", userIDs).Delete(&model.User{}).Error; err != nil {
			return err
		}
	}
	if len(roleIDs) > 0 {
		if err := db.Unscoped().Where("id IN ?", roleIDs).Delete(&model.Role{}).Error; err != nil {
			return err
		}
	}
	if len(groupIDs) > 0 {
		sort.Slice(groupIDs, func(i, j int) bool { return groupIDs[i] > groupIDs[j] })
		if err := db.Unscoped().Where("id IN ?", groupIDs).Delete(&model.Group{}).Error; err != nil {
			return err
		}
	}
	if err := db.Where("source = ?", seedLogSource).Delete(&model.DebugLog{}).Error; err != nil {
		return err
	}
	return nil
}

func queryIDs(db *gorm.DB, modelValue interface{}, query string, args ...interface{}) []uint {
	var ids []uint
	_ = db.Model(modelValue).Where(query, args...).Pluck("id", &ids).Error
	return ids
}

func seedGroups(ctx *seedContext) error {
	headquarter, err := createGroup(ctx, seedGroupPrefix+"总部", nil, 1, ctx.now.AddDate(0, 0, -20))
	if err != nil {
		return err
	}
	ctx.groups["hq"] = headquarter

	factory1, err := createGroup(ctx, seedGroupPrefix+"一厂", &headquarter.ID, 1, ctx.now.AddDate(0, 0, -19))
	if err != nil {
		return err
	}
	factory2, err := createGroup(ctx, seedGroupPrefix+"二厂", &headquarter.ID, 2, ctx.now.AddDate(0, 0, -19))
	if err != nil {
		return err
	}
	sampleCenter, err := createGroup(ctx, seedGroupPrefix+"样板中心", &headquarter.ID, 3, ctx.now.AddDate(0, 0, -19))
	if err != nil {
		return err
	}
	ctx.groups["factory1"] = factory1
	ctx.groups["factory2"] = factory2
	ctx.groups["sample"] = sampleCenter

	for _, item := range []struct {
		key    string
		name   string
		parent *uint
		sort   int
	}{
		{"f1a", seedGroupPrefix + "一厂·缝制A组", &factory1.ID, 1},
		{"f1b", seedGroupPrefix + "一厂·缝制B组", &factory1.ID, 2},
		{"f1q", seedGroupPrefix + "一厂·质检组", &factory1.ID, 3},
		{"f2a", seedGroupPrefix + "二厂·飞织组", &factory2.ID, 1},
		{"f2b", seedGroupPrefix + "二厂·后整组", &factory2.ID, 2},
		{"f2q", seedGroupPrefix + "二厂·质检组", &factory2.ID, 3},
	} {
		group, err := createGroup(ctx, item.name, item.parent, item.sort, ctx.now.AddDate(0, 0, -18))
		if err != nil {
			return err
		}
		ctx.groups[item.key] = group
	}
	return nil
}

func seedRoles(ctx *seedContext) error {
	roleDefs := []struct {
		key         string
		name        string
		remark      string
		permissions map[string]bool
	}{
		{
			key:    "full",
			name:   seedRolePrefix + "full",
			remark: "演示全权限角色",
			permissions: map[string]bool{
				"home": true, "dashboard": true, "deviceManagement": true, "deviceInfo": true, "remoteMonitoring": true,
				"fileManagement": true, "patternFiles": true, "devicePatternFiles": true, "downloadLog": true,
				"statistics": true, "salaryStatistics": true, "statusStatistics": true, "employeeManagement": true,
			},
		},
		{
			key:    "file",
			name:   seedRolePrefix + "file",
			remark: "演示文件与下发角色",
			permissions: map[string]bool{
				"home": true, "dashboard": true, "fileManagement": true, "patternFiles": true,
				"devicePatternFiles": true, "downloadLog": true, "deviceManagement": true, "deviceInfo": true,
			},
		},
		{
			key:    "stats",
			name:   seedRolePrefix + "stats",
			remark: "演示统计角色",
			permissions: map[string]bool{
				"home": true, "dashboard": true, "statistics": true, "salaryStatistics": true, "statusStatistics": true,
			},
		},
		{
			key:    "employee",
			name:   seedRolePrefix + "employee",
			remark: "演示员工管理角色",
			permissions: map[string]bool{
				"home": true, "dashboard": true, "employeeManagement": true, "statistics": true, "salaryStatistics": true,
			},
		},
	}

	for index, def := range roleDefs {
		role := model.Role{
			Model:           gorm.Model{CreatedAt: ctx.now.AddDate(0, 0, -16+index), UpdatedAt: ctx.now.AddDate(0, 0, -16+index)},
			Name:            def.name,
			Remark:          def.remark,
			Permissions:     mustJSON(def.permissions),
			ParentChildLink: true,
		}
		if err := ctx.db.Create(&role).Error; err != nil {
			return err
		}
		ctx.roles[def.key] = role
	}
	return nil
}

func seedUsers(ctx *seedContext) error {
	userDefs := []struct {
		key         string
		username    string
		nickname    string
		role        string
		groupID     *uint
		groupIDs    []uint
		disabled    bool
		permissions string
		email       string
		phone       string
	}{
		{
			key:         "admin",
			username:    "seedadmin",
			nickname:    "演示管理员",
			role:        "admin",
			permissions: mustJSON(ctx.roles["full"].Permissions),
			email:       "seedadmin@example.com",
			phone:       "13800000091",
		},
		{
			key:         "full",
			username:    "seedfull",
			nickname:    "全功能组长",
			role:        ctx.roles["full"].Name,
			groupID:     uintPtr(ctx.groups["f1a"].ID),
			groupIDs:    []uint{ctx.groups["f1a"].ID},
			permissions: ctx.roles["full"].Permissions,
			email:       "seedfull@example.com",
			phone:       "13800000092",
		},
		{
			key:         "file",
			username:    "seedfile",
			nickname:    "文件调度员",
			role:        ctx.roles["file"].Name,
			groupID:     uintPtr(ctx.groups["sample"].ID),
			groupIDs:    []uint{ctx.groups["sample"].ID},
			permissions: ctx.roles["file"].Permissions,
			email:       "seedfile@example.com",
			phone:       "13800000093",
		},
		{
			key:         "stats",
			username:    "seedstat",
			nickname:    "统计分析员",
			role:        ctx.roles["stats"].Name,
			groupID:     uintPtr(ctx.groups["f2a"].ID),
			groupIDs:    []uint{ctx.groups["f2a"].ID},
			permissions: ctx.roles["stats"].Permissions,
			email:       "seedstat@example.com",
			phone:       "13800000094",
		},
		{
			key:         "employee",
			username:    "seedemp",
			nickname:    "员工主管",
			role:        ctx.roles["employee"].Name,
			groupID:     uintPtr(ctx.groups["f1b"].ID),
			groupIDs:    []uint{ctx.groups["f1b"].ID},
			permissions: ctx.roles["employee"].Permissions,
			email:       "seedemp@example.com",
			phone:       "13800000095",
		},
		{
			key:         "disabled",
			username:    "seedoff",
			nickname:    "已禁用演示账号",
			role:        ctx.roles["full"].Name,
			groupID:     uintPtr(ctx.groups["f2q"].ID),
			groupIDs:    []uint{ctx.groups["f2q"].ID},
			disabled:    true,
			permissions: ctx.roles["full"].Permissions,
			email:       "seedoff@example.com",
			phone:       "13800000096",
		},
	}

	password, err := utils.HashPassword("Seed123456")
	if err != nil {
		return err
	}

	for index, def := range userDefs {
		createdAt := ctx.now.AddDate(0, 0, -14+index)
		user := model.User{
			Model:       gorm.Model{CreatedAt: createdAt, UpdatedAt: createdAt},
			Username:    def.username,
			Password:    password,
			Nickname:    def.nickname,
			Email:       def.email,
			Phone:       def.phone,
			Role:        def.role,
			Disabled:    def.disabled,
			Permissions: def.permissions,
			GroupID:     def.groupID,
			GroupIDs:    encodeGroupIDs(def.groupIDs),
		}
		if err := ctx.db.Create(&user).Error; err != nil {
			return err
		}
		ctx.users[def.key] = user
	}
	return nil
}

func seedOperators(ctx *seedContext) error {
	password, err := utils.HashPassword("Seed123456")
	if err != nil {
		return err
	}

	operatorDefs := []struct {
		key      string
		username string
		nickname string
		groupID  uint
	}{
		{"op1", seedOperatorPrefx + "01", "演示操作员一", ctx.groups["f1a"].ID},
		{"op2", seedOperatorPrefx + "02", "演示操作员二", ctx.groups["f1b"].ID},
		{"op3", seedOperatorPrefx + "03", "演示操作员三", ctx.groups["sample"].ID},
	}

	for index, def := range operatorDefs {
		operator := model.Operator{
			Model:    gorm.Model{CreatedAt: ctx.now.AddDate(0, 0, -12+index), UpdatedAt: ctx.now.AddDate(0, 0, -12+index)},
			Username: def.username,
			Password: password,
			Nickname: def.nickname,
			GroupID:  uintPtr(def.groupID),
		}
		if err := ctx.db.Create(&operator).Error; err != nil {
			return err
		}
		ctx.operators[def.key] = operator
	}
	return nil
}

func seedEmployees(ctx *seedContext) error {
	employeeDefs := []struct {
		code       string
		name       string
		department string
		position   string
		phone      string
		remark     string
	}{
		{"SEED001", "张晨", "缝制A组", "组长", "13900000001", "负责演示一厂A组"},
		{"SEED002", "李倩", "缝制A组", "车工", "13900000002", "效率稳定"},
		{"SEED003", "王敏", "缝制B组", "车工", "13900000003", "适合工资统计"},
		{"SEED004", "赵雪", "缝制B组", "车工", "13900000004", "高件数员工"},
		{"SEED005", "陈阳", "质检组", "质检", "13900000005", "报警分析用"},
		{"SEED006", "周琳", "飞织组", "车工", "13900000006", "设备联动测试"},
		{"SEED007", "孙浩", "后整组", "车工", "13900000007", "下载日志测试"},
		{"SEED008", "徐婷", "样板中心", "打样师", "13900000008", "文件管理测试"},
		{"SEED009", "高峰", "样板中心", "工艺员", "13900000009", "设备文件回传测试"},
		{"SEED010", "蒋楠", "质检组", "巡检", "13900000010", "报警明细测试"},
	}

	for index, def := range employeeDefs {
		employee := model.Employee{
			Model:      gorm.Model{CreatedAt: ctx.now.AddDate(0, 0, -10+index), UpdatedAt: ctx.now.AddDate(0, 0, -10+index)},
			Code:       def.code,
			Name:       def.name,
			Department: def.department,
			Position:   def.position,
			Phone:      def.phone,
			Remark:     def.remark,
		}
		if err := ctx.db.Create(&employee).Error; err != nil {
			return err
		}
		ctx.employees[def.code] = employee
	}
	return nil
}

func seedDeviceTypes(ctx *seedContext) error {
	return nil
}

func seedDevices(ctx *seedContext) error {
	deviceDefs := []struct {
		code         string
		name         string
		initialName  string
		deviceType   string
		model        string
		employeeCode string
		mainboardSN  string
		ip           string
		status       string
		groupKey     string
		remark       string
		sortOrder    int
		lastOnline   time.Time
	}{
		{"TD-A01", "平绣A-01", "A01", "绣花机", "K8", "SEED001", "MB-A01-2026", "192.168.10.21", "working", "f1a", "主力生产机台", 1, ctx.now.Add(-15 * time.Minute)},
		{"TD-A02", "平绣A-02", "A02", "绣花机", "K8", "SEED002", "MB-A02-2026", "192.168.10.22", "online", "f1a", "待机中", 2, ctx.now.Add(-30 * time.Minute)},
		{"TD-A03", "平绣A-03", "A03", "绣花机", "K9", "SEED002", "MB-A03-2026", "192.168.10.23", "idle", "f1a", "可用于远程监控", 3, ctx.now.Add(-50 * time.Minute)},
		{"TD-B01", "平绣B-01", "B01", "绣花机", "K10", "SEED003", "MB-B01-2026", "192.168.10.31", "working", "f1b", "B组高产机", 1, ctx.now.Add(-20 * time.Minute)},
		{"TD-B02", "平绣B-02", "B02", "绣花机", "K10", "SEED004", "MB-B02-2026", "192.168.10.32", "alarm", "f1b", "用于报警记录测试", 2, ctx.now.Add(-5 * time.Minute)},
		{"TD-Q01", "质检Q-01", "Q01", "缝纫机", "K18", "SEED005", "MB-Q01-2026", "192.168.10.41", "offline", "f1q", "离线设备", 1, ctx.now.Add(-6 * time.Hour)},
		{"TD-F01", "飞织F-01", "F01", "绣花机", "K8", "SEED006", "MB-F01-2026", "192.168.20.21", "working", "f2a", "二厂飞织主力机", 1, ctx.now.Add(-18 * time.Minute)},
		{"TD-F02", "飞织F-02", "F02", "绣花机", "K9", "SEED006", "MB-F02-2026", "192.168.20.22", "online", "f2a", "二厂演示机", 2, ctx.now.Add(-40 * time.Minute)},
		{"TD-H01", "后整H-01", "H01", "缝纫机", "K18", "SEED007", "MB-H01-2026", "192.168.20.31", "idle", "f2b", "后整设备", 1, ctx.now.Add(-35 * time.Minute)},
		{"TD-H02", "后整H-02", "H02", "缝纫机", "K18", "SEED007", "MB-H02-2026", "192.168.20.32", "offline", "f2b", "长期离线", 2, ctx.now.Add(-24 * time.Hour)},
		{"TD-S01", "打样S-01", "S01", "绣花机", "K6", "SEED008", "MB-S01-2026", "192.168.30.11", "working", "sample", "样板中心设备", 1, ctx.now.Add(-10 * time.Minute)},
		{"TD-U01", "未分组演示机", "U01", "缝纫机", "K18", "", "MB-U01-2026", "192.168.99.11", "online", "", "用于未分组设备测试", 1, ctx.now.Add(-70 * time.Minute)},
	}

	for index, def := range deviceDefs {
		var employeeName string
		if def.employeeCode != "" {
			employeeName = ctx.employees[def.employeeCode].Name
		}
		var groupID *uint
		if def.groupKey != "" {
			groupID = uintPtr(ctx.groups[def.groupKey].ID)
		}
		device := model.Device{
			Model:        gorm.Model{CreatedAt: ctx.now.AddDate(0, 0, -9+index), UpdatedAt: ctx.now.AddDate(0, 0, -1)},
			Code:         def.code,
			Name:         def.name,
			InitialName:  def.initialName,
			Type:         def.deviceType,
			ModelName:    def.model,
			EmployeeCode: def.employeeCode,
			EmployeeName: employeeName,
			MainboardSN:  def.mainboardSN,
			Remark:       def.remark,
			IP:           def.ip,
			Status:       def.status,
			GroupID:      groupID,
			SortOrder:    def.sortOrder,
			LastOnline:   def.lastOnline,
		}
		if err := ctx.db.Create(&device).Error; err != nil {
			return err
		}
		ctx.devices[def.code] = device
	}
	return nil
}

func seedEmployeeBindings(ctx *seedContext) error {
	bindings := []struct {
		employee string
		device   string
	}{
		{"SEED001", "TD-A01"},
		{"SEED002", "TD-A02"},
		{"SEED002", "TD-A03"},
		{"SEED003", "TD-B01"},
		{"SEED004", "TD-B02"},
		{"SEED006", "TD-F01"},
		{"SEED007", "TD-H01"},
		{"SEED008", "TD-S01"},
	}

	for _, binding := range bindings {
		record := model.EmployeeDevice{
			EmployeeID: ctx.employees[binding.employee].ID,
			DeviceID:   ctx.devices[binding.device].ID,
		}
		if err := ctx.db.Create(&record).Error; err != nil {
			return err
		}
	}
	return nil
}

func seedPatterns(ctx *seedContext) error {
	patternDefs := []struct {
		key         string
		name        string
		patternType string
		fileName    string
		fileSize    int64
		stitches    int
		colors      int
		width       float64
		height      float64
		unitPrice   float64
		orderNo     string
		uploadedBy  string
		createdAt   time.Time
	}{
		{"p01", "JK01+前片+M", "夹克", "seed_jk01_front_m.dst", 182400, 12480, 6, 240, 320, 3.280, "ORD-260401-001", "admin", ctx.now.AddDate(0, 0, -12)},
		{"p02", "JK01+后片+M", "夹克", "seed_jk01_back_m.dst", 176220, 11860, 5, 238, 318, 3.150, "ORD-260401-001", "admin", ctx.now.AddDate(0, 0, -12)},
		{"p03", "JK02+袖口+L", "卫衣", "seed_jk02_cuff_l.dst", 128640, 8450, 4, 160, 110, 1.280, "ORD-260401-002", "admin", ctx.now.AddDate(0, 0, -11)},
		{"p04", "PT01+裤片+XL", "裤装", "seed_pt01_leg_xl.dst", 205312, 15240, 7, 280, 360, 4.560, "ORD-260401-003", "admin", ctx.now.AddDate(0, 0, -10)},
		{"p05", "TS01+胸标+S", "T恤", "seed_ts01_logo_s.dst", 86420, 6320, 3, 120, 90, 0.860, "ORD-260401-004", "file", ctx.now.AddDate(0, 0, -9)},
		{"p06", "TS01+袖章+S", "T恤", "seed_ts01_arm_s.dst", 79220, 5210, 3, 98, 76, 0.720, "ORD-260401-004", "file", ctx.now.AddDate(0, 0, -8)},
		{"p07", "YD01+帽片+M", "运动服", "seed_yd01_hat_m.dst", 112450, 7320, 4, 150, 120, 1.650, "ORD-260401-005", "file", ctx.now.AddDate(0, 0, -7)},
		{"p08", "TZ01+前片+110", "童装", "seed_tz01_front_110.dst", 94410, 6880, 4, 118, 160, 0.980, "ORD-260401-006", "admin", ctx.now.AddDate(0, 0, -6)},
		{"p09", "TZ01+后片+110", "童装", "seed_tz01_back_110.dst", 93220, 6740, 4, 118, 160, 0.950, "ORD-260401-006", "admin", ctx.now.AddDate(0, 0, -6)},
		{"p10", "SP01+样板+均码", "样板", "seed_sp01_sample_free.dst", 156880, 10820, 5, 220, 280, 2.380, "ORD-260401-007", "file", ctx.now.AddDate(0, 0, -5)},
	}

	for _, def := range patternDefs {
		pattern := model.Pattern{
			Model:       gorm.Model{CreatedAt: def.createdAt, UpdatedAt: def.createdAt},
			Name:        def.name,
			PatternType: def.patternType,
			FileName:    def.fileName,
			FilePath:    filepath.Join("seed-share", "patterns", def.fileName),
			FileSize:    def.fileSize,
			Stitches:    def.stitches,
			Colors:      def.colors,
			Width:       def.width,
			Height:      def.height,
			UnitPrice:   def.unitPrice,
			OrderNo:     def.orderNo,
			UploadedBy:  ctx.users[def.uploadedBy].ID,
		}
		if err := ctx.db.Create(&pattern).Error; err != nil {
			return err
		}
		ctx.patterns[def.key] = pattern
	}
	return nil
}

func seedDevicePatternFiles(ctx *seedContext) error {
	fileDefs := []struct {
		key         string
		deviceCode  string
		patternNo   uint
		fileName    string
		patternType string
		fileSize    int64
		stitches    int
		unitPrice   float64
		orderNo     string
		createdAt   time.Time
	}{
		{"f001", "TD-A01", 1, "seed_dev_td-a01_front.dst", "夹克", 181220, 12380, 3.280, "ORD-260401-001", ctx.now.AddDate(0, 0, -4)},
		{"f002", "TD-A01", 2, "seed_dev_td-a01_back.dst", "夹克", 176320, 11820, 3.150, "ORD-260401-001", ctx.now.AddDate(0, 0, -4)},
		{"f003", "TD-A02", 3, "seed_dev_td-a02_logo.dst", "T恤", 86420, 6320, 0.860, "ORD-260401-004", ctx.now.AddDate(0, 0, -3)},
		{"f004", "TD-B01", 4, "seed_dev_td-b01_leg.dst", "裤装", 205312, 15240, 4.560, "ORD-260401-003", ctx.now.AddDate(0, 0, -3)},
		{"f005", "TD-B02", 5, "seed_dev_td-b02_hat.dst", "运动服", 112450, 7320, 1.650, "ORD-260401-005", ctx.now.AddDate(0, 0, -2)},
		{"f006", "TD-F01", 6, "seed_dev_td-f01_child-front.dst", "童装", 94410, 6880, 0.980, "ORD-260401-006", ctx.now.AddDate(0, 0, -2)},
		{"f007", "TD-F02", 7, "seed_dev_td-f02_child-back.dst", "童装", 93220, 6740, 0.950, "ORD-260401-006", ctx.now.AddDate(0, 0, -1)},
		{"f008", "TD-S01", 8, "seed_dev_td-s01_sample.dst", "样板", 156880, 10820, 2.380, "ORD-260401-007", ctx.now.AddDate(0, 0, -1)},
	}

	for _, def := range fileDefs {
		file := model.DevicePatternFile{
			Model:       gorm.Model{CreatedAt: def.createdAt, UpdatedAt: def.createdAt},
			DeviceID:    ctx.devices[def.deviceCode].ID,
			PatternNo:   def.patternNo,
			FileName:    def.fileName,
			PatternType: def.patternType,
			FileSize:    def.fileSize,
			Stitches:    def.stitches,
			UnitPrice:   def.unitPrice,
			OrderNo:     def.orderNo,
			FilePath:    filepath.Join("seed-share", "device-files", def.fileName),
		}
		if err := ctx.db.Create(&file).Error; err != nil {
			return err
		}
		ctx.files[def.key] = file
	}
	return nil
}

func seedDownloadTasks(ctx *seedContext) error {
	taskDefs := []struct {
		patternKey  string
		deviceCode  string
		status      string
		progress    int
		message     string
		operatorKey string
		createdAt   time.Time
	}{
		{"p01", "TD-A01", "waiting", 0, "等待下发", "op1", ctx.now.Add(-3 * time.Hour)},
		{"p02", "TD-A02", "downloading", 45, "正在推送到设备缓存", "op1", ctx.now.Add(-150 * time.Minute)},
		{"p03", "TD-B01", "paused", 62, "任务已暂停，等待恢复", "op2", ctx.now.Add(-140 * time.Minute)},
		{"p04", "TD-B02", "failed", 35, "设备报警导致下发失败", "op2", ctx.now.Add(-130 * time.Minute)},
		{"p05", "TD-F01", "completed", 100, "下发完成", "op3", ctx.now.Add(-120 * time.Minute)},
		{"p06", "TD-F02", "completed", 100, "下发完成", "op3", ctx.now.Add(-96 * time.Minute)},
		{"p07", "TD-S01", "completed", 100, "样板文件下发完成", "op3", ctx.now.Add(-72 * time.Minute)},
		{"p08", "TD-A03", "failed", 18, "网络中断，设备未响应", "op1", ctx.now.AddDate(0, 0, -1)},
		{"p09", "TD-H01", "completed", 100, "下发完成", "op2", ctx.now.AddDate(0, 0, -2)},
		{"p10", "TD-U01", "completed", 100, "下发完成", "op1", ctx.now.AddDate(0, 0, -3)},
	}

	for _, def := range taskDefs {
		task := model.DownloadTask{
			Model:      gorm.Model{CreatedAt: def.createdAt, UpdatedAt: def.createdAt.Add(15 * time.Minute)},
			PatternID:  ctx.patterns[def.patternKey].ID,
			DeviceID:   ctx.devices[def.deviceCode].ID,
			Status:     def.status,
			Progress:   def.progress,
			Message:    def.message,
			OperatorID: ctx.operators[def.operatorKey].ID,
		}
		if err := ctx.db.Create(&task).Error; err != nil {
			return err
		}
	}
	return nil
}

func seedUploadTasks(ctx *seedContext) error {
	taskDefs := []struct {
		fileKey     string
		patternKey  string
		deviceCode  string
		status      string
		progress    int
		message     string
		operatorKey string
		createdAt   time.Time
	}{
		{"f001", "p01", "TD-A01", "waiting", 0, "等待回传", "op1", ctx.now.Add(-4 * time.Hour)},
		{"f002", "p02", "TD-A01", "uploading", 55, "正在上传到服务器", "op1", ctx.now.Add(-3 * time.Hour)},
		{"f003", "p05", "TD-A02", "paused", 42, "任务已暂停", "op1", ctx.now.Add(-160 * time.Minute)},
		{"f004", "p04", "TD-B01", "completed", 100, "文件已回传并生成服务器花型", "op2", ctx.now.Add(-130 * time.Minute)},
		{"f005", "p07", "TD-B02", "failed", 68, "设备连接中断，回传失败", "op2", ctx.now.AddDate(0, 0, -1)},
		{"f006", "p08", "TD-F01", "completed", 100, "文件已回传", "op3", ctx.now.AddDate(0, 0, -1)},
		{"f007", "p09", "TD-F02", "completed", 100, "文件已回传", "op3", ctx.now.AddDate(0, 0, -2)},
		{"f008", "p10", "TD-S01", "failed", 25, "服务器校验失败", "op3", ctx.now.AddDate(0, 0, -2)},
	}

	for _, def := range taskDefs {
		patternID := ctx.patterns[def.patternKey].ID
		task := model.UploadTask{
			Model:        gorm.Model{CreatedAt: def.createdAt, UpdatedAt: def.createdAt.Add(10 * time.Minute)},
			DeviceFileID: ctx.files[def.fileKey].ID,
			PatternID:    &patternID,
			DeviceID:     ctx.devices[def.deviceCode].ID,
			Status:       def.status,
			Progress:     def.progress,
			Message:      def.message,
			OperatorID:   ctx.operators[def.operatorKey].ID,
		}
		if err := ctx.db.Create(&task).Error; err != nil {
			return err
		}
	}
	return nil
}

func seedProductionRecords(ctx *seedContext) error {
	deviceOrder := []string{"TD-A01", "TD-A02", "TD-A03", "TD-B01", "TD-B02", "TD-F01", "TD-F02", "TD-H01", "TD-S01"}
	patternOrder := []string{"p01", "p02", "p03", "p04", "p05", "p06", "p07", "p08", "p09", "p10"}
	hourSchedule := []struct {
		hour       int
		onlineSize int
	}{
		{0, 0}, {2, 0}, {4, 0}, {6, 0}, {8, 4}, {10, 6}, {12, 7}, {14, 7}, {16, 6}, {18, 5}, {20, 3}, {22, 0},
	}

	for dayOffset := 13; dayOffset >= 0; dayOffset-- {
		day := ctx.now.AddDate(0, 0, -dayOffset)
		dayBase := time.Date(day.Year(), day.Month(), day.Day(), 0, 0, 0, 0, day.Location())

		if dayOffset == 0 {
			for slotIndex, slot := range hourSchedule {
				for deviceIndex := 0; deviceIndex < slot.onlineSize; deviceIndex++ {
					deviceCode := deviceOrder[deviceIndex]
					device := ctx.devices[deviceCode]
					employee := ctx.employees[device.EmployeeCode]
					pattern := ctx.patterns[patternOrder[(slotIndex+deviceIndex)%len(patternOrder)]]
					pieces := 18 + deviceIndex*3 + slotIndex
					runningTime := 0.45 + float64((deviceIndex+slotIndex)%4)*0.22
					idleTime := 0.08 + float64((slotIndex+deviceIndex)%3)*0.05
					recordTime := dayBase.Add(time.Duration(slot.hour)*time.Hour + time.Duration(deviceIndex*6)*time.Minute)

					record := model.ProductionRecord{
						Model:        gorm.Model{CreatedAt: recordTime.Add(25 * time.Minute), UpdatedAt: recordTime.Add(25 * time.Minute)},
						DeviceID:     device.ID,
						EmployeeID:   employee.ID,
						PatternID:    pattern.ID,
						Pieces:       pieces,
						Stitches:     int64(pieces * (pattern.Stitches / 3)),
						ThreadLength: roundFloat(0.9*float64(pieces)+float64(deviceIndex), 2),
						RunningTime:  roundFloat(runningTime, 2),
						IdleTime:     roundFloat(idleTime, 2),
						SourceKey:    fmt.Sprintf("seed-prod-%s-%s-%02d-%02d", deviceCode, pattern.FileName, slot.hour, deviceIndex),
						RecordDate:   recordTime,
					}
					if err := ctx.db.Create(&record).Error; err != nil {
						return err
					}
				}
			}
			continue
		}

		for deviceIndex, deviceCode := range deviceOrder {
			device := ctx.devices[deviceCode]
			employee := ctx.employees[device.EmployeeCode]
			pattern := ctx.patterns[patternOrder[(dayOffset+deviceIndex)%len(patternOrder)]]
			recordTime := dayBase.Add(9*time.Hour + time.Duration(deviceIndex%5)*70*time.Minute)
			pieces := 70 + ((13 - dayOffset) * 6) + deviceIndex*4
			runningTime := 3.2 + float64((deviceIndex+dayOffset)%4)*0.55
			idleTime := 0.6 + float64((deviceIndex+dayOffset)%3)*0.18

			record := model.ProductionRecord{
				Model:        gorm.Model{CreatedAt: recordTime.Add(40 * time.Minute), UpdatedAt: recordTime.Add(40 * time.Minute)},
				DeviceID:     device.ID,
				EmployeeID:   employee.ID,
				PatternID:    pattern.ID,
				Pieces:       pieces,
				Stitches:     int64(pieces * (pattern.Stitches / 2)),
				ThreadLength: roundFloat(1.25*float64(pieces)+float64(deviceIndex)*2.6, 2),
				RunningTime:  roundFloat(runningTime, 2),
				IdleTime:     roundFloat(idleTime, 2),
				SourceKey:    fmt.Sprintf("seed-prod-%s-%s-%02d-%02d", deviceCode, pattern.FileName, dayOffset, deviceIndex),
				RecordDate:   recordTime,
			}
			if err := ctx.db.Create(&record).Error; err != nil {
				return err
			}
		}
	}
	return nil
}

func seedAlarmRecords(ctx *seedContext) error {
	alarmDefs := []struct {
		deviceCode   string
		alarmType    string
		alarmCode    string
		description  string
		duration     int
		status       string
		startOffsetH int
		endMinutes   int
	}{
		{"TD-B02", "张力异常", "ALM-T01", "上线张力偏高，需要复位", 420, "resolved", 6, 7},
		{"TD-B02", "断线报警", "ALM-T02", "检测到上线路断开", 360, "resolved", 18, 6},
		{"TD-Q01", "通讯离线", "ALM-N01", "设备超过 4 小时未上报心跳", 14400, "pending", 30, 0},
		{"TD-F01", "电机过载", "ALM-M01", "主电机电流偏高", 280, "resolved", 40, 5},
		{"TD-F02", "传感器异常", "ALM-S01", "针位传感器反馈超时", 510, "resolved", 52, 8},
		{"TD-A03", "断线报警", "ALM-T03", "回底线异常", 330, "resolved", 60, 5},
		{"TD-S01", "样板校验", "ALM-P01", "样板文件尺寸超限", 190, "resolved", 72, 4},
		{"TD-A01", "速度异常", "ALM-V01", "主轴速度波动超阈值", 240, "resolved", 84, 4},
		{"TD-H02", "通讯离线", "ALM-N02", "二厂后整设备掉线", 8600, "pending", 96, 0},
		{"TD-F01", "断线报警", "ALM-T04", "底线耗尽", 300, "resolved", 108, 6},
	}

	for _, def := range alarmDefs {
		startTime := ctx.now.Add(-time.Duration(def.startOffsetH) * time.Hour)
		var endTime *time.Time
		if def.endMinutes > 0 {
			t := startTime.Add(time.Duration(def.endMinutes) * time.Minute)
			endTime = &t
		}
		record := model.AlarmRecord{
			Model:       gorm.Model{CreatedAt: startTime, UpdatedAt: startTime},
			DeviceID:    ctx.devices[def.deviceCode].ID,
			AlarmType:   def.alarmType,
			AlarmCode:   def.alarmCode,
			Description: def.description,
			Duration:    def.duration,
			Status:      def.status,
			StartTime:   startTime,
			EndTime:     endTime,
		}
		if err := ctx.db.Create(&record).Error; err != nil {
			return err
		}
	}
	return nil
}

func seedSalaryRecords(ctx *seedContext) error {
	records := []struct {
		employeeCode string
		deviceCode   string
		dayOffset    int
		pieces       int
		unitPrice    float64
		bonus        float64
	}{
		{"SEED001", "TD-A01", 0, 132, 3.280, 12},
		{"SEED002", "TD-A02", 0, 118, 3.150, 8},
		{"SEED003", "TD-B01", 0, 146, 4.560, 18},
		{"SEED004", "TD-B02", 1, 128, 4.560, 6},
		{"SEED006", "TD-F01", 1, 136, 1.650, 10},
		{"SEED007", "TD-H01", 1, 92, 0.980, 0},
		{"SEED008", "TD-S01", 2, 74, 2.380, 20},
		{"SEED001", "TD-A01", 2, 126, 3.280, 10},
		{"SEED002", "TD-A03", 3, 111, 3.150, 4},
		{"SEED003", "TD-B01", 3, 154, 4.560, 22},
		{"SEED004", "TD-B02", 4, 121, 4.560, 0},
		{"SEED006", "TD-F02", 4, 108, 1.650, 5},
		{"SEED007", "TD-H01", 5, 88, 0.980, 0},
		{"SEED008", "TD-S01", 5, 69, 2.380, 12},
		{"SEED001", "TD-A01", 6, 140, 3.280, 15},
		{"SEED002", "TD-A02", 6, 116, 3.150, 0},
		{"SEED003", "TD-B01", 7, 149, 4.560, 18},
		{"SEED006", "TD-F01", 7, 133, 1.650, 9},
		{"SEED008", "TD-S01", 8, 82, 2.380, 10},
		{"SEED004", "TD-B02", 8, 127, 4.560, 6},
	}

	for _, def := range records {
		recordDate := time.Date(ctx.now.Year(), ctx.now.Month(), ctx.now.Day()-def.dayOffset, 18, 0, 0, 0, ctx.now.Location())
		salary := roundFloat(float64(def.pieces)*def.unitPrice, 2)
		totalAmount := roundFloat(salary+def.bonus, 2)
		record := model.SalaryRecord{
			Model:       gorm.Model{CreatedAt: recordDate, UpdatedAt: recordDate},
			EmployeeID:  ctx.employees[def.employeeCode].ID,
			DeviceID:    ctx.devices[def.deviceCode].ID,
			Pieces:      def.pieces,
			UnitPrice:   def.unitPrice,
			Salary:      salary,
			Bonus:       def.bonus,
			TotalAmount: totalAmount,
			RecordDate:  recordDate,
		}
		if err := ctx.db.Create(&record).Error; err != nil {
			return err
		}
	}
	return nil
}

func seedLoginLogs(ctx *seedContext) error {
	type loginDef struct {
		userKey string
		ip      string
		device  string
		status  string
		offsetH int
	}

	defs := []loginDef{
		{"admin", "192.168.10.10", "macOS Safari", "成功", 4},
		{"admin", "192.168.10.10", "Tauri Desktop", "成功", 28},
		{"full", "192.168.10.21", "Windows Client", "成功", 8},
		{"file", "192.168.30.15", "Windows Client", "成功", 14},
		{"stats", "192.168.20.52", "Windows Client", "成功", 18},
		{"employee", "192.168.10.41", "Windows Client", "成功", 32},
	}
	for _, def := range defs {
		logItem := model.LoginLog{
			UserID:    ctx.users[def.userKey].ID,
			IP:        def.ip,
			Device:    def.device,
			Status:    def.status,
			LoginTime: ctx.now.Add(-time.Duration(def.offsetH) * time.Hour),
		}
		if err := ctx.db.Create(&logItem).Error; err != nil {
			return err
		}
	}
	return nil
}

func seedServerConfigs(ctx *seedContext) error {
	configs := []model.ServerConfig{
		{
			Key:   "shared_folder",
			Value: filepath.Join(filepath.Dir(resolveDBPath()), "seed-share"),
			Desc:  "演示共享目录",
		},
		{
			Key:   "debug_output_enabled",
			Value: "true",
			Desc:  "演示调试输出开关",
		},
		{
			Key: "external_db_config",
			Value: mustJSON(map[string]interface{}{
				"dbType":              "mysql",
				"host":                "192.168.10.200",
				"port":                3306,
				"username":            "demo_sync",
				"password":            "demo_sync_pwd",
				"database":            "boer_external_demo",
				"charset":             "utf8mb4",
				"syncIntervalMinutes": 30,
				"enabled":             false,
				"updatedAt":           ctx.now.Add(-2 * time.Hour).Unix(),
			}),
			Desc: "演示外部数据库配置",
		},
		{
			Key:   "external_db_last_sync_at",
			Value: fmt.Sprintf("%d", ctx.now.Add(-45*time.Minute).Unix()),
			Desc:  "演示最近同步时间",
		},
	}

	for _, item := range configs {
		var existing model.ServerConfig
		err := ctx.db.Where("key = ?", item.Key).First(&existing).Error
		if err == nil {
			if err := ctx.db.Model(&existing).Updates(map[string]interface{}{
				"value":      item.Value,
				"desc":       item.Desc,
				"updated_at": ctx.now,
			}).Error; err != nil {
				return err
			}
			continue
		}
		item.Model = gorm.Model{CreatedAt: ctx.now.Add(-2 * time.Hour), UpdatedAt: ctx.now}
		if err := ctx.db.Create(&item).Error; err != nil {
			return err
		}
	}
	return nil
}

func seedDebugLogs(ctx *seedContext) error {
	logDefs := []struct {
		level   string
		message string
		offsetM int
	}{
		{"info", "演示环境初始化完成，设备树已加载", 5},
		{"info", "外部数据库同步配置已读取，当前为禁用状态", 12},
		{"warn", "设备 TD-B02 报警状态未清除，等待人工确认", 18},
		{"info", "花型文件 seed_jk01_front_m.dst 已进入下发队列", 26},
		{"error", "设备 TD-Q01 心跳超时，已标记离线", 40},
		{"info", "上传任务 f004 已完成并写入服务器花型库", 55},
		{"warn", "上传任务 f008 因服务器校验失败而终止", 70},
		{"info", "工资统计缓存已刷新，共 20 条演示记录", 88},
	}

	for _, def := range logDefs {
		recordTime := ctx.now.Add(-time.Duration(def.offsetM) * time.Minute)
		item := model.DebugLog{
			ID:        0,
			Level:     def.level,
			Message:   def.message,
			Source:    seedLogSource,
			CreatedAt: recordTime,
		}
		if err := ctx.db.Create(&item).Error; err != nil {
			return err
		}
	}
	return nil
}

func createGroup(ctx *seedContext, name string, parentID *uint, sortOrder int, createdAt time.Time) (model.Group, error) {
	group := model.Group{
		Model:     gorm.Model{CreatedAt: createdAt, UpdatedAt: createdAt},
		Name:      name,
		ParentID:  parentID,
		SortOrder: sortOrder,
	}
	return group, ctx.db.Create(&group).Error
}

func mustJSON(value interface{}) string {
	switch v := value.(type) {
	case string:
		return v
	default:
		bytes, err := json.Marshal(v)
		if err != nil {
			panic(err)
		}
		return string(bytes)
	}
}

func encodeGroupIDs(ids []uint) string {
	if len(ids) == 0 {
		return ""
	}
	normalized := append([]uint{}, ids...)
	sort.Slice(normalized, func(i, j int) bool { return normalized[i] < normalized[j] })
	bytes, _ := json.Marshal(normalized)
	return string(bytes)
}

func uintPtr(value uint) *uint {
	if value == 0 {
		return nil
	}
	return &value
}

func roundFloat(value float64, precision int) float64 {
	pow := 1.0
	for i := 0; i < precision; i++ {
		pow *= 10
	}
	if value >= 0 {
		return float64(int(value*pow+0.5)) / pow
	}
	return float64(int(value*pow-0.5)) / pow
}
