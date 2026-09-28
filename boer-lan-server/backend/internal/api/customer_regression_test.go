package api

import (
	"encoding/json"
	"net/http/httptest"
	"path/filepath"
	"testing"
	"time"

	"boer-lan-server/internal/model"
	"github.com/gin-gonic/gin"
	"golang.org/x/text/encoding/simplifiedchinese"
	"gorm.io/driver/sqlite"
	"gorm.io/gorm"
)

func customerAPIDB(t *testing.T) *gorm.DB {
	t.Helper()
	db, err := gorm.Open(sqlite.Open(filepath.Join(t.TempDir(), "test.db")), &gorm.Config{})
	if err != nil {
		t.Fatal(err)
	}
	if err := db.AutoMigrate(&model.DeviceTypeCatalog{}, &model.Role{}, &model.User{}, &model.LoginLog{}); err != nil {
		t.Fatal(err)
	}
	sqlDB, _ := db.DB()
	t.Cleanup(func() { sqlDB.Close() })
	return db
}

func TestDetailedPermissionsDoNotGrantSiblingModules(t *testing.T) {
	permissions := parsePermissionMap(`{"deviceManagement":true,"deviceInfo":true,"remoteMonitoring":false,"fileManagement":true,"patternFiles":true,"downloadLog":false}`)
	if hasAnyPermission(permissions, "remoteMonitoring", "deviceManagement") {
		t.Fatal("device information granted remote control")
	}
	if hasAnyPermission(permissions, "fileManagement", "downloadLog") {
		t.Fatal("pattern permission granted download logs")
	}
	if !hasAnyPermission(permissions, "deviceManagement", "deviceInfo") {
		t.Fatal("selected child denied")
	}
	if !hasAnyPermission(map[string]bool{"deviceManagement": true}, "remoteMonitoring") {
		t.Fatal("legacy module permission was lost")
	}
}

func TestLegacyDefaultPermissionsKeepDeviceInformation(t *testing.T) {
	if !hasAnyPermission(map[string]bool{"deviceManagement": true, "remoteMonitoring": true}, "deviceInfo") {
		t.Fatal("legacy default role lost device access")
	}
}

func TestEmptyRoleDoesNotFallBackToUserGrants(t *testing.T) {
	db := customerAPIDB(t)
	role := model.Role{Name: "empty", Permissions: `{}`}
	db.Create(&role)
	user := model.User{Username: "worker", Role: "empty", Permissions: `{"home":true}`}
	db.Create(&user)
	value := encodeEffectivePermissionsJSON(db, user)
	if value != "{}" {
		t.Fatalf("empty role escalated: %s", value)
	}
}

func TestCatalogRejectsDuplicatesAndRestoresDeletedValue(t *testing.T) {
	db := customerAPIDB(t)
	const table = "device_type_catalogs"
	if err := saveCatalogEntry(db, table, "type-a", 50, true); err != nil {
		t.Fatal(err)
	}
	if err := saveCatalogEntry(db, table, "type-a", 50, true); err == nil {
		t.Fatal("duplicate succeeded")
	}
	db.Where("value = ?", "type-a").Delete(&model.DeviceTypeCatalog{})
	if err := saveCatalogEntry(db, table, "type-a", 50, true); err != nil {
		t.Fatal(err)
	}
	var count int64
	db.Model(&model.DeviceTypeCatalog{}).Count(&count)
	if count != 1 {
		t.Fatalf("restored %d rows", count)
	}
}

func TestPersonalLoginLogsAreScoped(t *testing.T) {
	gin.SetMode(gin.TestMode)
	db := customerAPIDB(t)
	users := []model.User{{Username: "one"}, {Username: "two"}}
	db.Create(&users)
	for _, u := range users {
		db.Create(&model.LoginLog{UserID: u.ID, Username: u.Username, LoginTime: time.Now()})
	}
	response := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(response)
	c.Request = httptest.NewRequest("GET", "/api/auth/login-logs", nil)
	c.Set("userId", users[0].ID)
	c.Set("role", "user")
	(&AuthHandler{db: db}).GetLoginLogs(c)
	var body struct {
		Data struct {
			Total int
			List  []struct {
				UserID uint `json:"userId"`
			}
		}
	}
	if err := json.Unmarshal(response.Body.Bytes(), &body); err != nil {
		t.Fatal(err)
	}
	if body.Data.Total != 1 || len(body.Data.List) != 1 || body.Data.List[0].UserID != users[0].ID {
		t.Fatalf("unscoped logs: %s", response.Body.String())
	}
}

func TestDecodeCommandOutputSupportsUTF8AndGBK(t *testing.T) {
	expected := "请求超时，主机不可达"
	gbk, err := simplifiedchinese.GBK.NewEncoder().Bytes([]byte(expected))
	if err != nil {
		t.Fatal(err)
	}
	if decodeCommandOutput(gbk) != expected || decodeCommandOutput([]byte(expected)) != expected {
		t.Fatal("diagnostic output corrupted")
	}
}

func TestStatisticsDateRangeNormalizesAndRejectsInvalidDates(t *testing.T) {
	gin.SetMode(gin.TestMode)
	router := gin.New()
	router.Use(ValidateStatisticsDateRange())
	router.GET("/api/statistics/check", func(c *gin.Context) { c.JSON(200, gin.H{"start": c.Query("startDate"), "end": c.Query("endDate")}) })
	response := httptest.NewRecorder()
	router.ServeHTTP(response, httptest.NewRequest("GET", "/api/statistics/check?startDate=2026-06-30&endDate=2026-06-01", nil))
	if response.Code != 200 || response.Body.String() != `{"end":"2026-06-30","start":"2026-06-01"}` {
		t.Fatalf("dates not normalized: %s", response.Body.String())
	}
	for _, query := range []string{"startDate=2026-02-30", "startDate=2020-01-01&endDate=2026-01-01"} {
		response = httptest.NewRecorder()
		router.ServeHTTP(response, httptest.NewRequest("GET", "/api/statistics/check?"+query, nil))
		if response.Code != 400 {
			t.Fatalf("invalid date accepted: %s", query)
		}
	}
}
