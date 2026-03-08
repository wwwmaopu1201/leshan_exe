package service

import (
	"encoding/json"
	"fmt"
	"log"
	"net/url"
	"strings"
	"sync"
	"time"

	"boer-lan-server/internal/model"

	"gorm.io/driver/mysql"
	"gorm.io/driver/sqlserver"
	"gorm.io/gorm"
	"gorm.io/gorm/clause"
)

const (
	externalDBConfigKey = "external_db_config"
	lastSyncAtKey       = "external_db_last_sync_at"
)

type externalDBConfig struct {
	DBType              string `json:"dbType"`
	Host                string `json:"host"`
	Port                int    `json:"port"`
	Username            string `json:"username"`
	Password            string `json:"password"`
	Database            string `json:"database"`
	Charset             string `json:"charset"`
	SyncIntervalMinutes int    `json:"syncIntervalMinutes"`
	Enabled             bool   `json:"enabled"`
}

// ExternalDBSyncService 周期性同步本地数据到外部数据库
// 当前版本支持 MySQL；MSSQL 会记录提示并跳过。
type ExternalDBSyncService struct {
	db     *gorm.DB
	stopCh chan struct{}
	once   sync.Once
	mu     sync.Mutex
}

func NewExternalDBSyncService(db *gorm.DB) *ExternalDBSyncService {
	return &ExternalDBSyncService{
		db:     db,
		stopCh: make(chan struct{}),
	}
}

// RunExternalDBSyncOnce 提供给API层的手动触发入口
func RunExternalDBSyncOnce(db *gorm.DB) error {
	service := NewExternalDBSyncService(db)
	return service.syncOnce(true)
}

func (s *ExternalDBSyncService) Start() {
	go s.loop()
}

func (s *ExternalDBSyncService) Stop() {
	s.once.Do(func() {
		close(s.stopCh)
	})
}

func (s *ExternalDBSyncService) loop() {
	ticker := time.NewTicker(1 * time.Minute)
	defer ticker.Stop()

	_ = s.syncOnce(false)

	for {
		select {
		case <-s.stopCh:
			return
		case <-ticker.C:
			if err := s.syncOnce(false); err != nil {
				log.Printf("external db sync error: %v", err)
			}
		}
	}
}

func (s *ExternalDBSyncService) syncOnce(force bool) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	cfg, err := s.loadExternalDBConfig()
	if err != nil {
		return err
	}
	if !cfg.Enabled {
		return nil
	}

	interval := cfg.SyncIntervalMinutes
	if interval <= 0 {
		interval = 30
	}

	lastSyncAt, err := s.loadLastSyncAt()
	if err != nil {
		return err
	}
	if !force && time.Since(lastSyncAt) < time.Duration(interval)*time.Minute {
		return nil
	}

	switch cfg.DBType {
	case "mysql":
		if err := s.syncToMySQL(cfg, lastSyncAt); err != nil {
			return err
		}
	case "mssql":
		if err := s.syncToMSSQL(cfg, lastSyncAt); err != nil {
			return err
		}
	default:
		return fmt.Errorf("unsupported external db type: %s", cfg.DBType)
	}

	return nil
}

func (s *ExternalDBSyncService) loadExternalDBConfig() (externalDBConfig, error) {
	cfg := externalDBConfig{
		DBType:              "mysql",
		Host:                "127.0.0.1",
		Port:                3306,
		Charset:             "utf8mb4",
		SyncIntervalMinutes: 30,
	}

	var record model.ServerConfig
	err := s.db.Where("key = ?", externalDBConfigKey).First(&record).Error
	if err != nil {
		if err == gorm.ErrRecordNotFound {
			return cfg, nil
		}
		return cfg, err
	}

	if err := json.Unmarshal([]byte(record.Value), &cfg); err != nil {
		return cfg, err
	}
	cfg.DBType = strings.ToLower(strings.TrimSpace(cfg.DBType))
	cfg.Host = strings.TrimSpace(cfg.Host)
	cfg.Username = strings.TrimSpace(cfg.Username)
	cfg.Database = strings.TrimSpace(cfg.Database)
	cfg.Charset = strings.TrimSpace(cfg.Charset)
	if cfg.SyncIntervalMinutes <= 0 {
		cfg.SyncIntervalMinutes = 30
	}
	if cfg.Charset == "" {
		cfg.Charset = "utf8mb4"
	}
	if cfg.Port <= 0 {
		if cfg.DBType == "mssql" {
			cfg.Port = 1433
		} else {
			cfg.Port = 3306
		}
	}
	return cfg, nil
}

func (s *ExternalDBSyncService) loadLastSyncAt() (time.Time, error) {
	var record model.ServerConfig
	err := s.db.Where("key = ?", lastSyncAtKey).First(&record).Error
	if err != nil {
		if err == gorm.ErrRecordNotFound {
			return time.Time{}, nil
		}
		return time.Time{}, err
	}

	if strings.TrimSpace(record.Value) == "" {
		return time.Time{}, nil
	}

	v, parseErr := time.Parse(time.RFC3339, record.Value)
	if parseErr == nil {
		return v, nil
	}

	// 兼容历史 unix 秒格式
	var unixTs int64
	if _, err := fmt.Sscanf(record.Value, "%d", &unixTs); err == nil && unixTs > 0 {
		return time.Unix(unixTs, 0), nil
	}
	return time.Time{}, nil
}

func (s *ExternalDBSyncService) saveLastSyncAt(t time.Time) error {
	value := t.Format(time.RFC3339)
	record := model.ServerConfig{
		Key:   lastSyncAtKey,
		Value: value,
		Desc:  "外部数据库最近同步时间",
	}

	var existing model.ServerConfig
	err := s.db.Where("key = ?", lastSyncAtKey).First(&existing).Error
	if err == gorm.ErrRecordNotFound {
		return s.db.Create(&record).Error
	}
	if err != nil {
		return err
	}
	return s.db.Model(&existing).Updates(record).Error
}

func (s *ExternalDBSyncService) syncToMySQL(cfg externalDBConfig, lastSyncAt time.Time) error {
	if cfg.Host == "" || cfg.Username == "" || cfg.Database == "" {
		return fmt.Errorf("external mysql config incomplete")
	}

	dsn := fmt.Sprintf("%s:%s@tcp(%s:%d)/%s?charset=%s&parseTime=True&loc=Local",
		cfg.Username,
		cfg.Password,
		cfg.Host,
		cfg.Port,
		cfg.Database,
		cfg.Charset,
	)

	externalDB, err := gorm.Open(mysql.Open(dsn), &gorm.Config{})
	if err != nil {
		return err
	}

	if err := externalDB.AutoMigrate(
		&model.Device{},
		&model.Employee{},
		&model.Pattern{},
		&model.ProductionRecord{},
		&model.AlarmRecord{},
		&model.SalaryRecord{},
	); err != nil {
		return err
	}

	if err := s.syncDevices(externalDB, lastSyncAt); err != nil {
		return err
	}
	if err := s.syncEmployees(externalDB, lastSyncAt); err != nil {
		return err
	}
	if err := s.syncPatterns(externalDB, lastSyncAt); err != nil {
		return err
	}
	if err := s.syncProductionRecords(externalDB, lastSyncAt); err != nil {
		return err
	}
	if err := s.syncAlarmRecords(externalDB, lastSyncAt); err != nil {
		return err
	}
	if err := s.syncSalaryRecords(externalDB, lastSyncAt); err != nil {
		return err
	}

	if err := s.saveLastSyncAt(time.Now()); err != nil {
		return err
	}

	log.Printf("external db sync completed: mysql %s:%d/%s", cfg.Host, cfg.Port, cfg.Database)
	return nil
}

func buildMSSQLDSN(cfg externalDBConfig) string {
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

func (s *ExternalDBSyncService) syncToMSSQL(cfg externalDBConfig, lastSyncAt time.Time) error {
	if cfg.Host == "" || cfg.Username == "" || cfg.Database == "" {
		return fmt.Errorf("external mssql config incomplete")
	}

	dsn := buildMSSQLDSN(cfg)
	externalDB, err := gorm.Open(sqlserver.Open(dsn), &gorm.Config{})
	if err != nil {
		return err
	}

	if err := externalDB.AutoMigrate(
		&model.Device{},
		&model.Employee{},
		&model.Pattern{},
		&model.ProductionRecord{},
		&model.AlarmRecord{},
		&model.SalaryRecord{},
	); err != nil {
		return err
	}

	if err := s.syncDevices(externalDB, lastSyncAt); err != nil {
		return err
	}
	if err := s.syncEmployees(externalDB, lastSyncAt); err != nil {
		return err
	}
	if err := s.syncPatterns(externalDB, lastSyncAt); err != nil {
		return err
	}
	if err := s.syncProductionRecords(externalDB, lastSyncAt); err != nil {
		return err
	}
	if err := s.syncAlarmRecords(externalDB, lastSyncAt); err != nil {
		return err
	}
	if err := s.syncSalaryRecords(externalDB, lastSyncAt); err != nil {
		return err
	}

	if err := s.saveLastSyncAt(time.Now()); err != nil {
		return err
	}

	log.Printf("external db sync completed: mssql %s:%d/%s", cfg.Host, cfg.Port, cfg.Database)
	return nil
}

func applyIncrementalFilter(query *gorm.DB, lastSyncAt time.Time) *gorm.DB {
	if !lastSyncAt.IsZero() {
		return query.Where("updated_at >= ?", lastSyncAt)
	}
	return query
}

func upsertBatch[T any](tx *gorm.DB, rows []T) error {
	if len(rows) == 0 {
		return nil
	}
	return tx.Clauses(clause.OnConflict{
		Columns:   []clause.Column{{Name: "id"}},
		UpdateAll: true,
	}).Create(&rows).Error
}

func syncModelInBatches[T any](src *gorm.DB, dst *gorm.DB, batchSize int, lastSyncAt time.Time) error {
	offset := 0
	for {
		var rows []T
		query := applyIncrementalFilter(src.Model(new(T)), lastSyncAt).
			Order("id ASC").
			Offset(offset).
			Limit(batchSize)
		if err := query.Find(&rows).Error; err != nil {
			return err
		}
		if len(rows) == 0 {
			return nil
		}
		if err := upsertBatch(dst, rows); err != nil {
			return err
		}
		offset += len(rows)
	}
}

func (s *ExternalDBSyncService) syncDevices(dst *gorm.DB, lastSyncAt time.Time) error {
	return syncModelInBatches[model.Device](s.db, dst, 300, lastSyncAt)
}

func (s *ExternalDBSyncService) syncEmployees(dst *gorm.DB, lastSyncAt time.Time) error {
	return syncModelInBatches[model.Employee](s.db, dst, 500, lastSyncAt)
}

func (s *ExternalDBSyncService) syncPatterns(dst *gorm.DB, lastSyncAt time.Time) error {
	return syncModelInBatches[model.Pattern](s.db, dst, 300, lastSyncAt)
}

func (s *ExternalDBSyncService) syncProductionRecords(dst *gorm.DB, lastSyncAt time.Time) error {
	return syncModelInBatches[model.ProductionRecord](s.db, dst, 1000, lastSyncAt)
}

func (s *ExternalDBSyncService) syncAlarmRecords(dst *gorm.DB, lastSyncAt time.Time) error {
	return syncModelInBatches[model.AlarmRecord](s.db, dst, 1000, lastSyncAt)
}

func (s *ExternalDBSyncService) syncSalaryRecords(dst *gorm.DB, lastSyncAt time.Time) error {
	return syncModelInBatches[model.SalaryRecord](s.db, dst, 1000, lastSyncAt)
}
