package api

import (
	"encoding/json"
	"math"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"boer-lan-server/internal/model"

	"github.com/gin-gonic/gin"
	"gorm.io/driver/sqlite"
	"gorm.io/gorm"
)

func TestGetDeviceUsageIncludesGroupsLinesAndIdleDevices(t *testing.T) {
	db, err := gorm.Open(sqlite.Open("file:statistics_test?mode=memory&cache=shared"), &gorm.Config{})
	if err != nil {
		t.Fatalf("open sqlite: %v", err)
	}
	if err := db.AutoMigrate(&model.Group{}, &model.Device{}, &model.DeviceRuntimeSession{}, &model.ProductionRecord{}); err != nil {
		t.Fatalf("migrate schema: %v", err)
	}

	root := model.Group{Name: "总分组"}
	if err := db.Create(&root).Error; err != nil {
		t.Fatalf("create root group: %v", err)
	}
	line := model.Group{Name: "一号生产线", ParentID: &root.ID}
	if err := db.Create(&line).Error; err != nil {
		t.Fatalf("create line group: %v", err)
	}
	group := model.Group{Name: "A组", ParentID: &line.ID}
	if err := db.Create(&group).Error; err != nil {
		t.Fatalf("create device group: %v", err)
	}

	activeDevice := model.Device{Code: "D-001", Name: "设备一", GroupID: &group.ID}
	idleDevice := model.Device{Code: "D-002", Name: "设备二", GroupID: &group.ID}
	if err := db.Create(&activeDevice).Error; err != nil {
		t.Fatalf("create active device: %v", err)
	}
	if err := db.Create(&idleDevice).Error; err != nil {
		t.Fatalf("create idle device: %v", err)
	}
	runtimeStart := time.Now().Add(-4 * time.Hour)
	runtimeEnd := time.Now()
	processStart := runtimeEnd.Add(-2 * time.Hour)
	processEnd := processStart.Add(1 * time.Hour)
	if err := db.Create(&model.ProductionRecord{
		DeviceID:    activeDevice.ID,
		RunningTime: 1,
		IdleTime:    0,
		StartTime:   &processStart,
		EndTime:     &processEnd,
		SourceKey:   "test-device-usage-001",
		RecordDate:  time.Now(),
	}).Error; err != nil {
		t.Fatalf("create production record: %v", err)
	}
	if err := db.Create(&model.DeviceRuntimeSession{
		DeviceID:        activeDevice.ID,
		StartedAt:       runtimeStart,
		LastSeenAt:      runtimeEnd,
		EndedAt:         &runtimeEnd,
		DurationSeconds: int64(runtimeEnd.Sub(runtimeStart).Seconds()),
		EndReason:       "test",
	}).Error; err != nil {
		t.Fatalf("create runtime session: %v", err)
	}

	handler := NewStatisticsHandler(db)
	rows := handler.getDeviceUsage(nil)
	if len(rows) != 2 {
		t.Fatalf("expected all devices in usage rows, got %d rows: %#v", len(rows), rows)
	}

	byName := map[string]gin.H{}
	for _, row := range rows {
		byName[row["name"].(string)] = row
	}

	active := byName["设备一"]
	if active["groupName"] != "A组" {
		t.Fatalf("expected group name A组, got %#v", active["groupName"])
	}
	if active["lineName"] != "一号生产线" {
		t.Fatalf("expected line name 一号生产线, got %#v", active["lineName"])
	}
	if active["processingTime"] != 1.0 {
		t.Fatalf("expected 1h processing time, got %#v", active["processingTime"])
	}
	if active["runningTime"] != 4.0 {
		t.Fatalf("expected 4h device running time, got %#v", active["runningTime"])
	}
	if active["efficiency"] != 25.0 {
		t.Fatalf("expected 25%% efficiency, got %#v", active["efficiency"])
	}

	idle := byName["设备二"]
	if idle["groupName"] != "A组" || idle["lineName"] != "一号生产线" {
		t.Fatalf("expected idle device group and line metadata, got %#v", idle)
	}
	if idle["efficiency"] != 0.0 {
		t.Fatalf("expected idle device zero efficiency, got %#v", idle["efficiency"])
	}
}

func TestDashboardUtilizationUsesProcessingOverRuntime(t *testing.T) {
	db, err := gorm.Open(sqlite.Open("file:dashboard_utilization_test?mode=memory&cache=shared"), &gorm.Config{})
	if err != nil {
		t.Fatalf("open sqlite: %v", err)
	}
	if err := db.AutoMigrate(&model.Device{}, &model.DeviceRuntimeSession{}, &model.ProductionRecord{}); err != nil {
		t.Fatalf("migrate schema: %v", err)
	}

	device := model.Device{Code: "D-003", Name: "设备三"}
	if err := db.Create(&device).Error; err != nil {
		t.Fatalf("create device: %v", err)
	}
	runtimeStart := time.Now().Add(-4 * time.Hour)
	runtimeEnd := time.Now()
	processStart := runtimeEnd.Add(-2 * time.Hour)
	processEnd := processStart.Add(1 * time.Hour)
	if err := db.Create(&model.ProductionRecord{
		DeviceID:    device.ID,
		RunningTime: 1,
		IdleTime:    0,
		StartTime:   &processStart,
		EndTime:     &processEnd,
		SourceKey:   "test-dashboard-utilization-001",
		RecordDate:  time.Now(),
	}).Error; err != nil {
		t.Fatalf("create production record: %v", err)
	}
	if err := db.Create(&model.DeviceRuntimeSession{
		DeviceID:        device.ID,
		StartedAt:       runtimeStart,
		LastSeenAt:      runtimeEnd,
		EndedAt:         &runtimeEnd,
		DurationSeconds: int64(runtimeEnd.Sub(runtimeStart).Seconds()),
		EndReason:       "test",
	}).Error; err != nil {
		t.Fatalf("create runtime session: %v", err)
	}

	handler := NewStatisticsHandler(db)
	got := handler.getTodayUtilizationRate("", nil, 1)
	if got != 25.0 {
		t.Fatalf("expected dashboard utilization 25%%, got %#v", got)
	}

	runningTrend, utilizationTrend := handler.getRuntimeAndUtilizationTrends("", nil, 1)
	todayRunning := runningTrend[len(runningTrend)-1]
	todayUtilization := utilizationTrend[len(utilizationTrend)-1]
	if todayRunning["runningTime"] != 4.0 {
		t.Fatalf("expected today runtime trend 4h, got %#v", todayRunning["runningTime"])
	}
	if todayRunning["processingTime"] != 1.0 {
		t.Fatalf("expected today processing trend 1h, got %#v", todayRunning["processingTime"])
	}
	if todayUtilization["value"] != 25.0 {
		t.Fatalf("expected today utilization trend 25%%, got %#v", todayUtilization["value"])
	}
}

func TestDashboardUtilizationExtendsTodayRuntimeForOnlineDevice(t *testing.T) {
	db, err := gorm.Open(sqlite.Open("file:dashboard_online_runtime_test?mode=memory&cache=shared"), &gorm.Config{})
	if err != nil {
		t.Fatalf("open sqlite: %v", err)
	}
	if err := db.AutoMigrate(&model.Device{}, &model.DeviceRuntimeSession{}, &model.ProductionRecord{}); err != nil {
		t.Fatalf("migrate schema: %v", err)
	}

	device := model.Device{Code: "D-005", Name: "在线设备", Status: "online"}
	if err := db.Create(&device).Error; err != nil {
		t.Fatalf("create device: %v", err)
	}
	startTime := time.Now().Add(-10 * time.Minute)
	endTime := time.Now().Add(-9 * time.Minute)
	if err := db.Create(&model.ProductionRecord{
		DeviceID:    device.ID,
		RunningTime: 1.0 / 60.0,
		StartTime:   &startTime,
		EndTime:     &endTime,
		SourceKey:   "test-dashboard-online-runtime-001",
		RecordDate:  time.Now(),
	}).Error; err != nil {
		t.Fatalf("create production record: %v", err)
	}
	if err := db.Create(&model.DeviceRuntimeSession{
		DeviceID:   device.ID,
		StartedAt:  startTime,
		LastSeenAt: endTime,
	}).Error; err != nil {
		t.Fatalf("create runtime session: %v", err)
	}

	handler := NewStatisticsHandler(db)
	got := handler.getTodayUtilizationRate("", nil, 1)
	if got >= 100 {
		t.Fatalf("expected online device utilization below 100%%, got %#v", got)
	}
	if got <= 0 {
		t.Fatalf("expected positive utilization, got %#v", got)
	}
}

func TestDashboardEmptyDeviceIDsScopeReturnsZeroData(t *testing.T) {
	db, err := gorm.Open(sqlite.Open("file:dashboard_empty_scope_test?mode=memory&cache=shared"), &gorm.Config{})
	if err != nil {
		t.Fatalf("open sqlite: %v", err)
	}
	if err := db.AutoMigrate(&model.Device{}, &model.DeviceRuntimeSession{}, &model.ProductionRecord{}, &model.AlarmRecord{}); err != nil {
		t.Fatalf("migrate schema: %v", err)
	}

	device := model.Device{Code: "D-004", Name: "未分组设备"}
	if err := db.Create(&device).Error; err != nil {
		t.Fatalf("create device: %v", err)
	}
	if err := db.Create(&model.ProductionRecord{
		DeviceID:   device.ID,
		Pieces:     8,
		SourceKey:  "test-dashboard-empty-scope-001",
		RecordDate: time.Now(),
	}).Error; err != nil {
		t.Fatalf("create production record: %v", err)
	}

	handler := NewStatisticsHandler(db)
	recorder := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(recorder)
	c.Request = httptest.NewRequest(http.MethodGet, "/api/statistics/dashboard?deviceIds=0", nil)
	c.Set("role", "admin")

	handler.GetDashboardData(c)

	var body struct {
		Code int `json:"code"`
		Data struct {
			TotalPieces      float64 `json:"totalPieces"`
			TodayPieces      float64 `json:"todayPieces"`
			ScopeDeviceCount float64 `json:"scopeDeviceCount"`
		} `json:"data"`
	}
	if err := json.Unmarshal(recorder.Body.Bytes(), &body); err != nil {
		t.Fatalf("decode response: %v", err)
	}
	if body.Code != 0 {
		t.Fatalf("expected success response, got code=%d body=%s", body.Code, recorder.Body.String())
	}
	if body.Data.TotalPieces != 0 || body.Data.TodayPieces != 0 || body.Data.ScopeDeviceCount != 0 {
		t.Fatalf("expected empty scope zero data, got %#v", body.Data)
	}
}

func TestGetDurationStatsSummaryUsesRuntimeSessions(t *testing.T) {
	db, err := gorm.Open(sqlite.Open("file:duration_stats_summary_test?mode=memory&cache=shared"), &gorm.Config{})
	if err != nil {
		t.Fatalf("open sqlite: %v", err)
	}
	if err := db.AutoMigrate(&model.User{}, &model.Device{}, &model.Employee{}, &model.Pattern{}, &model.DeviceRuntimeSession{}, &model.ProductionRecord{}, &model.AlarmRecord{}); err != nil {
		t.Fatalf("migrate schema: %v", err)
	}

	user := model.User{Username: "admin", Password: "x", Nickname: "admin", Role: "admin"}
	if err := db.Create(&user).Error; err != nil {
		t.Fatalf("create user: %v", err)
	}
	device := model.Device{Code: "D-006", Name: "时长设备"}
	if err := db.Create(&device).Error; err != nil {
		t.Fatalf("create device: %v", err)
	}

	now := time.Now()
	runtimeStart := now.Add(-1 * time.Hour)
	processStart := now.Add(-50 * time.Minute)
	processEnd := now.Add(-40 * time.Minute)
	if err := db.Create(&model.DeviceRuntimeSession{
		DeviceID:        device.ID,
		StartedAt:       runtimeStart,
		LastSeenAt:      now,
		EndedAt:         &now,
		DurationSeconds: int64(now.Sub(runtimeStart).Seconds()),
		EndReason:       "test",
	}).Error; err != nil {
		t.Fatalf("create runtime session: %v", err)
	}
	if err := db.Create(&model.ProductionRecord{
		DeviceID:    device.ID,
		Pieces:      1,
		RunningTime: 10.0 / 60.0,
		StartTime:   &processStart,
		EndTime:     &processEnd,
		SourceKey:   "test-duration-summary-001",
		RecordDate:  now,
	}).Error; err != nil {
		t.Fatalf("create production record: %v", err)
	}
	if err := db.Create(&model.AlarmRecord{
		DeviceID:  device.ID,
		AlarmType: "测试报警",
		Duration:  5 * 60,
		Status:    "resolved",
		StartTime: now.Add(-30 * time.Minute),
	}).Error; err != nil {
		t.Fatalf("create alarm record: %v", err)
	}

	handler := NewStatisticsHandler(db)
	recorder := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(recorder)
	today := formatLocalStatsDate(now)
	c.Request = httptest.NewRequest(http.MethodGet, "/api/statistics/duration?startDate="+today+"&endDate="+today, nil)
	c.Set("userId", user.ID)
	c.Set("role", "admin")

	handler.GetDurationStats(c)

	var body struct {
		Code int `json:"code"`
		Data struct {
			Summary struct {
				TotalTime   float64 `json:"totalTime"`
				RunningTime float64 `json:"runningTime"`
				IdleTime    float64 `json:"idleTime"`
				AlarmTime   float64 `json:"alarmTime"`
			} `json:"summary"`
		} `json:"data"`
	}
	if err := json.Unmarshal(recorder.Body.Bytes(), &body); err != nil {
		t.Fatalf("decode response: %v", err)
	}
	if body.Code != 0 {
		t.Fatalf("expected success response, got code=%d body=%s", body.Code, recorder.Body.String())
	}
	assertClose := func(name string, got, want float64) {
		t.Helper()
		if math.Abs(got-want) > 0.00001 {
			t.Fatalf("expected %s %.6f, got %.6f", name, want, got)
		}
	}
	assertClose("total time", body.Data.Summary.TotalTime, 1)
	assertClose("processing time", body.Data.Summary.RunningTime, 10.0/60.0)
	assertClose("alarm time", body.Data.Summary.AlarmTime, 5.0/60.0)
	assertClose("idle time", body.Data.Summary.IdleTime, 45.0/60.0)
}
