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

func TestGetSalaryStatsUsesCurrentPatternUnitPrice(t *testing.T) {
	gin.SetMode(gin.TestMode)

	db, err := gorm.Open(sqlite.Open("file:salary_live_price_test?mode=memory&cache=shared"), &gorm.Config{})
	if err != nil {
		t.Fatalf("open sqlite: %v", err)
	}
	if err := db.AutoMigrate(&model.User{}, &model.Employee{}, &model.Device{}, &model.Pattern{}, &model.SalaryRecord{}); err != nil {
		t.Fatalf("migrate schema: %v", err)
	}

	user := model.User{Username: "stats-user", Password: "hashed", Role: "admin"}
	employee := model.Employee{Code: "E-001", Name: "员工一"}
	device := model.Device{Code: "D-001", Name: "设备一"}
	pattern := model.Pattern{Name: "花型一", UnitPrice: 2.5}
	if err := db.Create(&user).Error; err != nil {
		t.Fatalf("create user: %v", err)
	}
	if err := db.Create(&employee).Error; err != nil {
		t.Fatalf("create employee: %v", err)
	}
	if err := db.Create(&device).Error; err != nil {
		t.Fatalf("create device: %v", err)
	}
	if err := db.Create(&pattern).Error; err != nil {
		t.Fatalf("create pattern: %v", err)
	}
	recordDate := time.Date(2026, 5, 5, 10, 0, 0, 0, time.Local)
	if err := db.Create(&model.SalaryRecord{
		EmployeeID:  employee.ID,
		DeviceID:    device.ID,
		PatternID:   pattern.ID,
		PatternName: pattern.Name,
		Pieces:      10,
		UnitPrice:   1,
		Salary:      10,
		Bonus:       3,
		TotalAmount: 13,
		RecordDate:  recordDate,
	}).Error; err != nil {
		t.Fatalf("create salary record: %v", err)
	}

	req := httptest.NewRequest(http.MethodGet, "/api/statistics/salary?startDate=2026-05-05&endDate=2026-05-05", nil)
	w := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(w)
	c.Request = req
	c.Set("userId", user.ID)
	c.Set("role", user.Role)

	NewStatisticsHandler(db).GetSalaryStats(c)

	if w.Code != http.StatusOK {
		t.Fatalf("expected status 200, got %d, body=%s", w.Code, w.Body.String())
	}

	var body struct {
		Code int `json:"code"`
		Data struct {
			Summary struct {
				TotalSalary float64 `json:"totalSalary"`
			} `json:"summary"`
			List []struct {
				UnitPrice   float64 `json:"unitPrice"`
				Salary      float64 `json:"salary"`
				TotalAmount float64 `json:"totalAmount"`
			} `json:"list"`
		} `json:"data"`
	}
	if err := json.Unmarshal(w.Body.Bytes(), &body); err != nil {
		t.Fatalf("decode response: %v", err)
	}
	if body.Code != 0 {
		t.Fatalf("expected code 0, got %d", body.Code)
	}
	if len(body.Data.List) != 1 {
		t.Fatalf("expected one salary row, got %d", len(body.Data.List))
	}
	row := body.Data.List[0]
	if row.UnitPrice != 2.5 || row.Salary != 25 || row.TotalAmount != 25 {
		t.Fatalf("expected live price salary values, got %#v", row)
	}
	if body.Data.Summary.TotalSalary != 25 {
		t.Fatalf("expected live summary total 25, got %.2f", body.Data.Summary.TotalSalary)
	}
}

func TestGetSalaryStatsUsesCurrentPatternUnitPriceWithoutPatternID(t *testing.T) {
	gin.SetMode(gin.TestMode)

	db, err := gorm.Open(sqlite.Open("file:salary_live_price_by_name_test?mode=memory&cache=shared"), &gorm.Config{})
	if err != nil {
		t.Fatalf("open sqlite: %v", err)
	}
	if err := db.AutoMigrate(&model.User{}, &model.Employee{}, &model.Device{}, &model.Pattern{}, &model.SalaryRecord{}); err != nil {
		t.Fatalf("migrate schema: %v", err)
	}

	user := model.User{Username: "stats-user", Password: "hashed", Role: "admin"}
	employee := model.Employee{Code: "E-002", Name: "员工二"}
	device := model.Device{Code: "D-002", Name: "设备二"}
	pattern := model.Pattern{Name: "花型二", UnitPrice: 3.75, OrderNo: "ORDER-1"}
	if err := db.Create(&user).Error; err != nil {
		t.Fatalf("create user: %v", err)
	}
	if err := db.Create(&employee).Error; err != nil {
		t.Fatalf("create employee: %v", err)
	}
	if err := db.Create(&device).Error; err != nil {
		t.Fatalf("create device: %v", err)
	}
	if err := db.Create(&pattern).Error; err != nil {
		t.Fatalf("create pattern: %v", err)
	}
	recordDate := time.Date(2026, 5, 5, 10, 0, 0, 0, time.Local)
	if err := db.Create(&model.SalaryRecord{
		EmployeeID:  employee.ID,
		DeviceID:    device.ID,
		PatternName: pattern.Name,
		Pieces:      8,
		UnitPrice:   1,
		Salary:      8,
		TotalAmount: 8,
		RecordDate:  recordDate,
	}).Error; err != nil {
		t.Fatalf("create salary record: %v", err)
	}

	req := httptest.NewRequest(http.MethodGet, "/api/statistics/salary?startDate=2026-05-05&endDate=2026-05-05", nil)
	w := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(w)
	c.Request = req
	c.Set("userId", user.ID)
	c.Set("role", user.Role)

	NewStatisticsHandler(db).GetSalaryStats(c)

	if w.Code != http.StatusOK {
		t.Fatalf("expected status 200, got %d, body=%s", w.Code, w.Body.String())
	}

	var body struct {
		Code int `json:"code"`
		Data struct {
			Summary struct {
				TotalSalary float64 `json:"totalSalary"`
			} `json:"summary"`
			List []struct {
				UnitPrice   float64 `json:"unitPrice"`
				Salary      float64 `json:"salary"`
				TotalAmount float64 `json:"totalAmount"`
			} `json:"list"`
		} `json:"data"`
	}
	if err := json.Unmarshal(w.Body.Bytes(), &body); err != nil {
		t.Fatalf("decode response: %v", err)
	}
	if body.Code != 0 {
		t.Fatalf("expected code 0, got %d", body.Code)
	}
	if len(body.Data.List) != 1 {
		t.Fatalf("expected one salary row, got %d", len(body.Data.List))
	}
	row := body.Data.List[0]
	if row.UnitPrice != 3.75 || row.Salary != 30 || row.TotalAmount != 30 {
		t.Fatalf("expected live price matched by pattern name, got %#v", row)
	}
	if body.Data.Summary.TotalSalary != 30 {
		t.Fatalf("expected live summary total 30, got %.2f", body.Data.Summary.TotalSalary)
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

func TestDashboardDataShowsTotalProcessingTimeForAggregateScope(t *testing.T) {
	db, err := gorm.Open(sqlite.Open("file:dashboard_total_processing_test?mode=memory&cache=shared"), &gorm.Config{})
	if err != nil {
		t.Fatalf("open sqlite: %v", err)
	}
	if err := db.AutoMigrate(&model.User{}, &model.Device{}, &model.DeviceRuntimeSession{}, &model.ProductionRecord{}, &model.AlarmRecord{}); err != nil {
		t.Fatalf("migrate schema: %v", err)
	}

	user := model.User{Username: "dashboard-admin", Password: "x", Role: "admin"}
	if err := db.Create(&user).Error; err != nil {
		t.Fatalf("create user: %v", err)
	}
	deviceOne := model.Device{Code: "D-007", Name: "设备七"}
	deviceTwo := model.Device{Code: "D-008", Name: "设备八"}
	if err := db.Create(&deviceOne).Error; err != nil {
		t.Fatalf("create device one: %v", err)
	}
	if err := db.Create(&deviceTwo).Error; err != nil {
		t.Fatalf("create device two: %v", err)
	}

	now := time.Now()
	startOne := now.Add(-3 * time.Hour)
	endOne := startOne.Add(1 * time.Hour)
	startTwo := now.Add(-90 * time.Minute)
	endTwo := startTwo.Add(30 * time.Minute)
	if err := db.Create(&model.ProductionRecord{
		DeviceID:    deviceOne.ID,
		Pieces:      1,
		RunningTime: 1,
		StartTime:   &startOne,
		EndTime:     &endOne,
		SourceKey:   "test-dashboard-total-processing-001",
		RecordDate:  now,
	}).Error; err != nil {
		t.Fatalf("create production record one: %v", err)
	}
	if err := db.Create(&model.ProductionRecord{
		DeviceID:    deviceTwo.ID,
		Pieces:      1,
		RunningTime: 0.5,
		StartTime:   &startTwo,
		EndTime:     &endTwo,
		SourceKey:   "test-dashboard-total-processing-002",
		RecordDate:  now,
	}).Error; err != nil {
		t.Fatalf("create production record two: %v", err)
	}

	recorder := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(recorder)
	c.Request = httptest.NewRequest(http.MethodGet, "/api/statistics/dashboard", nil)
	c.Set("userId", user.ID)
	c.Set("role", user.Role)

	NewStatisticsHandler(db).GetDashboardData(c)

	var body struct {
		Code int `json:"code"`
		Data struct {
			ProcessingTime         float64 `json:"processingTime"`
			RunningProcessingTrend []struct {
				ProcessingTime float64 `json:"processingTime"`
			} `json:"runningProcessingTrend"`
		} `json:"data"`
	}
	if err := json.Unmarshal(recorder.Body.Bytes(), &body); err != nil {
		t.Fatalf("decode response: %v", err)
	}
	if body.Code != 0 {
		t.Fatalf("expected success response, got code=%d body=%s", body.Code, recorder.Body.String())
	}
	if body.Data.ProcessingTime != 1.5 {
		t.Fatalf("expected dashboard processing total 1.5h, got %#v", body.Data.ProcessingTime)
	}
	if len(body.Data.RunningProcessingTrend) == 0 {
		t.Fatalf("expected running processing trend data")
	}
	todayTrend := body.Data.RunningProcessingTrend[len(body.Data.RunningProcessingTrend)-1]
	if todayTrend.ProcessingTime != 1.5 {
		t.Fatalf("expected trend processing total 1.5h, got %#v", todayTrend.ProcessingTime)
	}
}

func TestDashboardDataPreservesSubMinuteProcessingTime(t *testing.T) {
	db, err := gorm.Open(sqlite.Open("file:dashboard_sub_minute_processing_test?mode=memory&cache=shared"), &gorm.Config{})
	if err != nil {
		t.Fatalf("open sqlite: %v", err)
	}
	if err := db.AutoMigrate(&model.User{}, &model.Device{}, &model.DeviceRuntimeSession{}, &model.ProductionRecord{}, &model.AlarmRecord{}); err != nil {
		t.Fatalf("migrate schema: %v", err)
	}

	user := model.User{Username: "dashboard-sub-minute-admin", Password: "x", Role: "admin"}
	if err := db.Create(&user).Error; err != nil {
		t.Fatalf("create user: %v", err)
	}
	device := model.Device{Code: "D-009", Name: "秒级设备"}
	if err := db.Create(&device).Error; err != nil {
		t.Fatalf("create device: %v", err)
	}

	now := time.Now()
	processStart := now.Add(-10 * time.Second)
	processEnd := now
	if err := db.Create(&model.ProductionRecord{
		DeviceID:    device.ID,
		Pieces:      1,
		RunningTime: 10.0 / 3600.0,
		StartTime:   &processStart,
		EndTime:     &processEnd,
		SourceKey:   "test-dashboard-sub-minute-processing-001",
		RecordDate:  now,
	}).Error; err != nil {
		t.Fatalf("create production record: %v", err)
	}
	if err := db.Create(&model.DeviceRuntimeSession{
		DeviceID:        device.ID,
		StartedAt:       processStart.Add(-10 * time.Second),
		LastSeenAt:      now,
		EndedAt:         &now,
		DurationSeconds: 20,
		EndReason:       "test",
	}).Error; err != nil {
		t.Fatalf("create runtime session: %v", err)
	}

	recorder := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(recorder)
	c.Request = httptest.NewRequest(http.MethodGet, "/api/statistics/dashboard", nil)
	c.Set("userId", user.ID)
	c.Set("role", user.Role)

	NewStatisticsHandler(db).GetDashboardData(c)

	var body struct {
		Code int `json:"code"`
		Data struct {
			ProcessingTime   float64 `json:"processingTime"`
			RunningTime      float64 `json:"runningTime"`
			Utilization      float64 `json:"utilizationRate"`
			UtilizationTrend []struct {
				Value float64 `json:"value"`
			} `json:"utilizationTrend"`
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
		if math.Abs(got-want) > 0.000001 {
			t.Fatalf("expected %s %.6f, got %.6f", name, want, got)
		}
	}
	assertClose("processing time", body.Data.ProcessingTime, 10.0/3600.0)
	assertClose("running time", body.Data.RunningTime, 20.0/3600.0)
	if body.Data.Utilization <= 0 {
		t.Fatalf("expected utilization above 0, got %.6f", body.Data.Utilization)
	}
	if len(body.Data.UtilizationTrend) == 0 {
		t.Fatalf("expected utilization trend data")
	}
	todayUtilization := body.Data.UtilizationTrend[len(body.Data.UtilizationTrend)-1]
	if todayUtilization.Value <= 0 {
		t.Fatalf("expected utilization trend above 0, got %.6f", todayUtilization.Value)
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

	device := model.Device{Code: "D-005", Name: "开机设备", Status: "idle"}
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

func TestGetProcessOverviewListUsesRuntimeSessionForCumulativeUpTime(t *testing.T) {
	db, err := gorm.Open(sqlite.Open("file:process_overview_uptime_test?mode=memory&cache=shared"), &gorm.Config{})
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
	device := model.Device{Code: "D-007", Name: "加工概况设备"}
	if err := db.Create(&device).Error; err != nil {
		t.Fatalf("create device: %v", err)
	}

	now := time.Now()
	runtimeStart := now.Add(-10 * time.Second)
	processStart := now.Add(-9 * time.Second)
	processEnd := now.Add(-8 * time.Second)
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
		Stitches:    120,
		RunningTime: 1.0 / 3600.0,
		StartTime:   &processStart,
		EndTime:     &processEnd,
		SourceKey:   "test-process-overview-uptime-001",
		RecordDate:  now,
	}).Error; err != nil {
		t.Fatalf("create production record: %v", err)
	}

	handler := NewStatisticsHandler(db)
	recorder := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(recorder)
	today := formatLocalStatsDate(now)
	c.Request = httptest.NewRequest(http.MethodGet, "/api/statistics/process?startDate="+today+"&endDate="+today, nil)
	c.Set("userId", user.ID)
	c.Set("role", "admin")

	handler.GetProcessOverview(c)

	var body struct {
		Code int `json:"code"`
		Data struct {
			List []struct {
				CumulativeUpTime float64 `json:"cumulativeUpTime"`
			} `json:"list"`
		} `json:"data"`
	}
	if err := json.Unmarshal(recorder.Body.Bytes(), &body); err != nil {
		t.Fatalf("decode response: %v", err)
	}
	if body.Code != 0 {
		t.Fatalf("expected success response, got code=%d body=%s", body.Code, recorder.Body.String())
	}
	if len(body.Data.List) != 1 {
		t.Fatalf("expected one process row, got %d body=%s", len(body.Data.List), recorder.Body.String())
	}
	want := 10.0 / 3600.0
	if math.Abs(body.Data.List[0].CumulativeUpTime-want) > 0.00001 {
		t.Fatalf("expected cumulative uptime %.6fh from runtime session, got %.6f", want, body.Data.List[0].CumulativeUpTime)
	}
}

func TestGetProcessOverviewListAlarmDisplay(t *testing.T) {
	db, err := gorm.Open(sqlite.Open("file:process_overview_alarm_display_test?mode=memory&cache=shared"), &gorm.Config{})
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
	device := model.Device{Code: "D-008", Name: "报警显示设备"}
	if err := db.Create(&device).Error; err != nil {
		t.Fatalf("create device: %v", err)
	}

	now := time.Now()
	recordDate := time.Date(now.Year(), now.Month(), now.Day(), 10, 0, 0, 0, now.Location())
	recordEnd := recordDate.Add(30 * time.Minute)
	sourceKey := "test-process-overview-alarm-display-001"
	if err := db.Create(&model.ProductionRecord{
		DeviceID:   device.ID,
		Pieces:     1,
		Stitches:   120,
		StartTime:  &recordDate,
		EndTime:    &recordEnd,
		SourceKey:  sourceKey,
		RecordDate: recordDate,
	}).Error; err != nil {
		t.Fatalf("create production record: %v", err)
	}

	fetch := func(t *testing.T) struct {
		AlarmInfo string `json:"alarmInfo"`
		AlarmTime string `json:"alarmTime"`
	} {
		t.Helper()
		handler := NewStatisticsHandler(db)
		recorder := httptest.NewRecorder()
		c, _ := gin.CreateTestContext(recorder)
		today := formatLocalStatsDate(recordDate)
		c.Request = httptest.NewRequest(http.MethodGet, "/api/statistics/process?startDate="+today+"&endDate="+today, nil)
		c.Set("userId", user.ID)
		c.Set("role", "admin")

		handler.GetProcessOverview(c)

		var body struct {
			Code int `json:"code"`
			Data struct {
				List []struct {
					AlarmInfo string `json:"alarmInfo"`
					AlarmTime string `json:"alarmTime"`
				} `json:"list"`
			} `json:"data"`
		}
		if err := json.Unmarshal(recorder.Body.Bytes(), &body); err != nil {
			t.Fatalf("decode response: %v", err)
		}
		if body.Code != 0 {
			t.Fatalf("expected success response, got code=%d body=%s", body.Code, recorder.Body.String())
		}
		if len(body.Data.List) != 1 {
			t.Fatalf("expected one process row, got %d body=%s", len(body.Data.List), recorder.Body.String())
		}
		return body.Data.List[0]
	}

	noAlarm := fetch(t)
	if noAlarm.AlarmInfo != "无" || noAlarm.AlarmTime != "无" {
		t.Fatalf("expected no alarm display 无/无, got %q/%q", noAlarm.AlarmInfo, noAlarm.AlarmTime)
	}

	previousAlarmStart := recordDate.Add(-30 * time.Minute)
	if err := db.Create(&model.AlarmRecord{
		DeviceID:  device.ID,
		AlarmType: "断线报警",
		Duration:  60,
		Status:    "resolved",
		StartTime: previousAlarmStart,
	}).Error; err != nil {
		t.Fatalf("create previous alarm record: %v", err)
	}

	normalSewing := fetch(t)
	if normalSewing.AlarmInfo != "无" || normalSewing.AlarmTime != "无" {
		t.Fatalf("expected previous alarm not to display during normal sewing, got %q/%q", normalSewing.AlarmInfo, normalSewing.AlarmTime)
	}

	alarmStart := recordDate.Add(15 * time.Minute)
	if err := db.Create(&model.AlarmRecord{
		DeviceID:  device.ID,
		AlarmType: "断线报警",
		Duration:  60,
		Status:    "resolved",
		StartTime: alarmStart,
	}).Error; err != nil {
		t.Fatalf("create alarm record: %v", err)
	}

	withAlarm := fetch(t)
	if withAlarm.AlarmInfo != "断线报警" {
		t.Fatalf("expected alarm info 断线报警, got %q", withAlarm.AlarmInfo)
	}
	if withAlarm.AlarmTime != alarmStart.Format("2006-01-02 15:04:05") {
		t.Fatalf("expected alarm start time %q, got %q", alarmStart.Format("2006-01-02 15:04:05"), withAlarm.AlarmTime)
	}
}
