package api

import (
	"encoding/csv"
	"fmt"
	"math"
	"net/http"
	"sort"
	"strconv"
	"strings"
	"time"

	"boer-lan-server/internal/model"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

type StatisticsHandler struct {
	db *gorm.DB
}

func NewStatisticsHandler(db *gorm.DB) *StatisticsHandler {
	return &StatisticsHandler{db: db}
}

func (h *StatisticsHandler) GetHomeStats(c *gin.Context) {
	// 设备状态统计
	var totalDevices, onlineDevices, workingDevices, offlineDevices, alarmDevices int64
	h.db.Model(&model.Device{}).Count(&totalDevices)
	h.db.Model(&model.Device{}).Where("status IN ?", []string{"online", "working", "idle"}).Count(&onlineDevices)
	h.db.Model(&model.Device{}).Where("status = ?", "working").Count(&workingDevices)
	h.db.Model(&model.Device{}).Where("status = ?", "offline").Count(&offlineDevices)
	h.db.Model(&model.Device{}).Where("status = ?", "alarm").Count(&alarmDevices)

	// 近7日设备使用效率
	weeklyEfficiency := h.getWeeklyEfficiency()

	// 花型使用占比
	patternUsage := h.getPatternUsage()

	// 设备机型占比
	modelRatio := h.getModelRatio()

	// 前三设备生产量
	topProduction := h.getTopProduction()

	// 24小时设备运行状态 + 近7日产量
	runningStatusByHour := h.getRunningStatusByHour()
	productionByDay := h.getProductionByDay()

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"totalDevices":        totalDevices,
			"onlineDevices":       onlineDevices,
			"workingDevices":      workingDevices,
			"offlineDevices":      offlineDevices,
			"alarmDevices":        alarmDevices,
			"weeklyEfficiency":    weeklyEfficiency,
			"patternUsage":        patternUsage,
			"modelRatio":          modelRatio,
			"topProduction":       topProduction,
			"runningStatusByHour": runningStatusByHour,
			"productionByDay":     productionByDay,
			// 兼容旧前端字段名
			"productionByHour": productionByDay,
		},
		"message": "success",
	})
}

func (h *StatisticsHandler) getWeeklyEfficiency() []gin.H {
	result := make([]gin.H, 0)
	weekdays := []string{"周一", "周二", "周三", "周四", "周五", "周六", "周日"}
	now := time.Now()
	var totalDevices int64
	h.db.Model(&model.Device{}).Count(&totalDevices)

	for i := 6; i >= 0; i-- {
		date := now.AddDate(0, 0, -i)
		weekday := int(date.Weekday())
		if weekday == 0 {
			weekday = 7
		}

		var rows []struct {
			DeviceID    uint
			RunningTime float64
			IdleTime    float64
		}
		h.db.Model(&model.ProductionRecord{}).
			Select("device_id, COALESCE(SUM(running_time), 0) as running_time, COALESCE(SUM(idle_time), 0) as idle_time").
			Where("DATE(record_date) = DATE(?)", date).
			Group("device_id").
			Scan(&rows)

		efficiency := 0.0
		validDeviceCount := 0
		for _, row := range rows {
			totalTime := row.RunningTime + row.IdleTime
			if totalTime <= 0 {
				continue
			}
			efficiency += (row.RunningTime / totalTime) * 100
			validDeviceCount++
		}
		if totalDevices > 0 {
			efficiency = efficiency / float64(totalDevices)
		} else if validDeviceCount > 0 {
			efficiency = efficiency / float64(validDeviceCount)
		}

		result = append(result, gin.H{
			"date":  weekdays[weekday-1],
			"value": int(efficiency),
		})
	}
	return result
}

func (h *StatisticsHandler) getPatternUsage() []gin.H {
	var results []struct {
		PatternID uint
		Name      string
		Count     int
	}

	h.db.Table("production_records pr").
		Select("pr.pattern_id, p.name, COUNT(*) as count").
		Joins("LEFT JOIN patterns p ON pr.pattern_id = p.id").
		Where("pr.pattern_id IS NOT NULL").
		Group("pr.pattern_id, p.name").
		Order("count DESC").
		Limit(10).
		Scan(&results)

	patternUsage := make([]gin.H, 0)
	for _, r := range results {
		name := r.Name
		if name == "" {
			name = "未知花型"
		}
		patternUsage = append(patternUsage, gin.H{
			"name":  name,
			"value": r.Count,
		})
	}

	if len(patternUsage) == 0 {
		return []gin.H{}
	}

	return patternUsage
}

func (h *StatisticsHandler) getModelRatio() []gin.H {
	var results []struct {
		Model string
		Count int
	}

	h.db.Model(&model.Device{}).
		Select("model, COUNT(*) as count").
		Group("model").
		Order("count DESC").
		Scan(&results)

	modelRatio := make([]gin.H, 0)
	for _, r := range results {
		modelRatio = append(modelRatio, gin.H{
			"name":  r.Model,
			"value": r.Count,
		})
	}

	return modelRatio
}

func (h *StatisticsHandler) getTopProduction() []gin.H {
	var results []struct {
		DeviceID uint
		Name     string
		Total    int
	}

	h.db.Table("production_records pr").
		Select("pr.device_id, d.name, SUM(pr.pieces) as total").
		Joins("LEFT JOIN devices d ON pr.device_id = d.id").
		Where("pr.record_date >= ?", time.Now().AddDate(0, 0, -7)).
		Group("pr.device_id, d.name").
		Order("total DESC").
		Limit(3).
		Scan(&results)

	topProduction := make([]gin.H, 0)
	for _, r := range results {
		topProduction = append(topProduction, gin.H{
			"name":  r.Name,
			"value": r.Total,
		})
	}

	return topProduction
}

func (h *StatisticsHandler) getRunningStatusByHour() []gin.H {
	var totalDevices int64
	h.db.Model(&model.Device{}).Count(&totalDevices)
	total := int(totalDevices)
	if total < 0 {
		total = 0
	}

	var currentOnlineDevices int64
	h.db.Model(&model.Device{}).Where("status IN ?", []string{"online", "working", "idle"}).Count(&currentOnlineDevices)

	now := time.Now()
	dayStart := time.Date(now.Year(), now.Month(), now.Day(), 0, 0, 0, 0, now.Location())
	dayEnd := dayStart.Add(24 * time.Hour)

	var records []struct {
		DeviceID   uint
		RecordDate time.Time
	}
	h.db.Model(&model.ProductionRecord{}).
		Select("device_id, record_date").
		Where("record_date >= ? AND record_date < ?", dayStart, dayEnd).
		Scan(&records)

	hourDeviceMap := make(map[int]map[uint]struct{}, 24)
	for _, record := range records {
		hour := record.RecordDate.Hour()
		if _, exists := hourDeviceMap[hour]; !exists {
			hourDeviceMap[hour] = make(map[uint]struct{})
		}
		hourDeviceMap[hour][record.DeviceID] = struct{}{}
	}

	result := make([]gin.H, 0, 24)
	currentHour := now.Hour()
	for hour := 0; hour < 24; hour++ {
		online := len(hourDeviceMap[hour])
		if hour == currentHour && online == 0 {
			online = int(currentOnlineDevices)
		}
		if online < 0 {
			online = 0
		}
		if total > 0 && online > total {
			online = total
		}

		offline := total - online
		if offline < 0 {
			offline = 0
		}

		result = append(result, gin.H{
			"hour":    fmt.Sprintf("%02d:00", hour),
			"online":  online,
			"offline": offline,
		})
	}
	return result
}

func (h *StatisticsHandler) getProductionByDay() []gin.H {
	startDate := time.Now().AddDate(0, 0, -6).Format("2006-01-02")
	var rows []struct {
		Date  string
		Value int
	}

	h.db.Model(&model.ProductionRecord{}).
		Select("DATE(record_date) as date, COALESCE(SUM(pieces), 0) as value").
		Where("DATE(record_date) >= ?", startDate).
		Group("DATE(record_date)").
		Order("DATE(record_date) ASC").
		Scan(&rows)

	valueMap := make(map[string]int, len(rows))
	for _, row := range rows {
		valueMap[row.Date] = row.Value
	}

	result := make([]gin.H, 0, 7)
	now := time.Now()
	for i := 6; i >= 0; i-- {
		day := now.AddDate(0, 0, -i)
		key := day.Format("2006-01-02")
		value := valueMap[key]
		result = append(result, gin.H{
			"date":  day.Format("01-02"),
			"value": value,
		})
	}
	return result
}

func (h *StatisticsHandler) GetDashboardData(c *gin.Context) {
	deviceId := strings.TrimSpace(c.Query("deviceId"))
	deviceIDs := parseDeviceIDs(c.Query("deviceIds"))

	var totalPieces int
	var todayPieces int
	var threadLength float64
	var avgUsedThreadLength float64

	var todayRunningTime, todayIdleTime float64
	summaryQuery := applyDashboardDeviceFilter(h.db.Model(&model.ProductionRecord{}), deviceId, deviceIDs)
	summaryQuery.Select("COALESCE(SUM(pieces), 0)").Scan(&totalPieces)
	summaryQuery.Select("COALESCE(SUM(thread_length), 0)").Scan(&threadLength)

	todayQuery := applyDashboardDeviceFilter(
		h.db.Model(&model.ProductionRecord{}).Where("DATE(record_date) = DATE(?)", time.Now()),
		deviceId,
		deviceIDs,
	)
	todayQuery.Select("COALESCE(SUM(pieces), 0)").Scan(&todayPieces)
	todayQuery.Select("COALESCE(SUM(running_time), 0)").Scan(&todayRunningTime)
	todayQuery.Select("COALESCE(SUM(idle_time), 0)").Scan(&todayIdleTime)

	processingTime := todayRunningTime * 0.8
	usedThreadLength := threadLength
	deviceCount := h.countDashboardScopeDevices(deviceId, deviceIDs)
	isAggregateScope := strings.TrimSpace(deviceId) == "" && deviceCount > 0
	if isAggregateScope {
		todayRunningTime = todayRunningTime / float64(deviceCount)
		todayIdleTime = todayIdleTime / float64(deviceCount)
	}
	utilizationRate := h.getTodayUtilizationRate(deviceId, deviceIDs, deviceCount)
	if !isAggregateScope && todayRunningTime+todayIdleTime > 0 {
		utilizationRate = (todayRunningTime / (todayRunningTime + todayIdleTime)) * 100
	}
	processingTime = todayRunningTime * 0.8
	if deviceCount > 0 {
		avgUsedThreadLength = usedThreadLength / float64(deviceCount)
	}
	totalThreadLength := usedThreadLength * 1.25
	if totalThreadLength < usedThreadLength {
		totalThreadLength = usedThreadLength
	}

	spindleSpeed := h.getDashboardSpindleSpeed(deviceId, deviceIDs)

	// 近10天产量趋势
	hourlyProduction := h.getHourlyProduction(deviceId, deviceIDs)
	// 近7天运行/加工时长趋势 + 近7天使用率趋势
	trendAvgDeviceCount := int64(0)
	if isAggregateScope {
		trendAvgDeviceCount = deviceCount
	}
	runningProcessingTrend, utilizationTrend := h.getRuntimeAndUtilizationTrends(deviceId, deviceIDs, trendAvgDeviceCount)

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"totalPieces":            totalPieces,
			"todayPieces":            todayPieces,
			"threadLength":           roundFloat(usedThreadLength, 2), // 兼容旧字段，表示已用底线
			"totalThreadLength":      roundFloat(totalThreadLength, 2),
			"usedThreadLength":       roundFloat(usedThreadLength, 2),
			"avgUsedThreadLength":    roundFloat(avgUsedThreadLength, 2),
			"spindleSpeed":           spindleSpeed,
			"runningTime":            roundFloat(todayRunningTime, 2),
			"processingTime":         roundFloat(processingTime, 2),
			"utilizationRate":        roundFloat(utilizationRate, 2),
			"hourlyProduction":       hourlyProduction,
			"runningProcessingTrend": runningProcessingTrend,
			"utilizationTrend":       utilizationTrend,
		},
		"message": "success",
	})
}

func applyDashboardDeviceFilter(query *gorm.DB, deviceId string, deviceIDs []uint) *gorm.DB {
	if deviceId != "" {
		return query.Where("device_id = ?", deviceId)
	}
	if len(deviceIDs) > 0 {
		return query.Where("device_id IN ?", deviceIDs)
	}
	return query
}

func (h *StatisticsHandler) countDashboardScopeDevices(deviceId string, deviceIDs []uint) int64 {
	if strings.TrimSpace(deviceId) != "" {
		return 1
	}
	if len(deviceIDs) > 0 {
		unique := make(map[uint]struct{}, len(deviceIDs))
		for _, id := range deviceIDs {
			if id == 0 {
				continue
			}
			unique[id] = struct{}{}
		}
		return int64(len(unique))
	}

	var count int64
	h.db.Model(&model.Device{}).Count(&count)
	return count
}

func calculateSpindleSpeed(stitches int64, runningHours float64) float64 {
	if stitches <= 0 || runningHours <= 0 {
		return 0
	}

	// 采用“针数/分钟”近似主轴转速（RPM），并限制在合理量程。
	rpm := float64(stitches) / (runningHours * 60)
	if rpm <= 0 {
		return 0
	}
	if rpm > 5000 {
		rpm = 5000
	}
	return rpm
}

func (h *StatisticsHandler) getDashboardSpindleSpeed(deviceId string, deviceIDs []uint) int {
	var rows []struct {
		DeviceID    uint
		Stitches    int64
		RunningTime float64
	}

	applyDashboardDeviceFilter(h.db.Model(&model.ProductionRecord{}), deviceId, deviceIDs).
		Select("device_id, stitches, running_time").
		Order("record_date DESC, created_at DESC, id DESC").
		Limit(500).
		Scan(&rows)

	if len(rows) == 0 {
		return 0
	}

	deviceSpeeds := make(map[uint]float64)
	for _, row := range rows {
		if row.DeviceID == 0 {
			continue
		}
		if _, exists := deviceSpeeds[row.DeviceID]; exists {
			continue
		}
		speed := calculateSpindleSpeed(row.Stitches, row.RunningTime)
		if speed <= 0 {
			continue
		}
		deviceSpeeds[row.DeviceID] = speed
	}

	if len(deviceSpeeds) == 0 {
		return 0
	}

	total := 0.0
	for _, speed := range deviceSpeeds {
		total += speed
	}
	return int(math.Round(total / float64(len(deviceSpeeds))))
}

func (h *StatisticsHandler) getHourlyProduction(deviceId string, deviceIDs []uint) []gin.H {
	startDate := time.Now().AddDate(0, 0, -9).Format("2006-01-02")
	query := applyDashboardDeviceFilter(h.db.Model(&model.ProductionRecord{}), deviceId, deviceIDs).
		Select("DATE(record_date) as date, COALESCE(SUM(pieces), 0) as value").
		Where("DATE(record_date) >= ?", startDate)

	var rows []struct {
		Date  string
		Value int
	}
	query.Group("DATE(record_date)").
		Order("DATE(record_date) ASC").
		Scan(&rows)

	valueMap := make(map[string]int, len(rows))
	for _, row := range rows {
		valueMap[row.Date] = row.Value
	}

	result := make([]gin.H, 0, 10)
	now := time.Now()
	for i := 9; i >= 0; i-- {
		day := now.AddDate(0, 0, -i)
		key := day.Format("2006-01-02")
		value := valueMap[key]
		result = append(result, gin.H{
			"date":  day.Format("01-02"),
			"value": value,
		})
	}
	return result
}

func (h *StatisticsHandler) getTodayUtilizationRate(deviceId string, deviceIDs []uint, deviceCount int64) float64 {
	if strings.TrimSpace(deviceId) != "" {
		var totals struct {
			Running float64
			Idle    float64
		}
		applyDashboardDeviceFilter(
			h.db.Model(&model.ProductionRecord{}).Where("DATE(record_date) = DATE(?)", time.Now()),
			deviceId,
			deviceIDs,
		).
			Select("COALESCE(SUM(running_time), 0) as running, COALESCE(SUM(idle_time), 0) as idle").
			Scan(&totals)
		if totals.Running+totals.Idle <= 0 {
			return 0
		}
		return roundFloat((totals.Running/(totals.Running+totals.Idle))*100, 2)
	}

	var rows []struct {
		Running float64
		Idle    float64
	}
	applyDashboardDeviceFilter(
		h.db.Model(&model.ProductionRecord{}).Where("DATE(record_date) = DATE(?)", time.Now()),
		deviceId,
		deviceIDs,
	).
		Select("COALESCE(SUM(running_time), 0) as running, COALESCE(SUM(idle_time), 0) as idle").
		Group("device_id").
		Scan(&rows)

	totalUtilization := 0.0
	for _, row := range rows {
		if row.Running+row.Idle <= 0 {
			continue
		}
		totalUtilization += (row.Running / (row.Running + row.Idle)) * 100
	}

	if deviceCount > 0 {
		return roundFloat(totalUtilization/float64(deviceCount), 2)
	}
	if len(rows) > 0 {
		return roundFloat(totalUtilization/float64(len(rows)), 2)
	}
	return 0
}

func (h *StatisticsHandler) getRuntimeAndUtilizationTrends(deviceId string, deviceIDs []uint, avgDeviceCount int64) ([]gin.H, []gin.H) {
	startDate := time.Now().AddDate(0, 0, -6).Format("2006-01-02")
	query := applyDashboardDeviceFilter(h.db.Model(&model.ProductionRecord{}), deviceId, deviceIDs).
		Select("DATE(record_date) as date, device_id, COALESCE(SUM(running_time), 0) as running_time, COALESCE(SUM(idle_time), 0) as idle_time").
		Where("DATE(record_date) >= ?", startDate)

	var rows []struct {
		Date        string
		DeviceID    uint
		RunningTime float64
		IdleTime    float64
	}
	query.Group("DATE(record_date)").
		Order("DATE(record_date) ASC").
		Scan(&rows)

	type dayAgg struct {
		RunningTotal float64
		IdleTotal    float64
		UtilSum      float64
		UtilCount    int64
	}
	rowMap := make(map[string]dayAgg, len(rows))
	for _, row := range rows {
		agg := rowMap[row.Date]
		agg.RunningTotal += row.RunningTime
		agg.IdleTotal += row.IdleTime
		if row.RunningTime+row.IdleTime > 0 {
			agg.UtilSum += (row.RunningTime / (row.RunningTime + row.IdleTime)) * 100
			agg.UtilCount++
		}
		rowMap[row.Date] = agg
	}

	runningProcessingTrend := make([]gin.H, 0, 7)
	utilizationTrend := make([]gin.H, 0, 7)
	now := time.Now()
	for i := 6; i >= 0; i-- {
		day := now.AddDate(0, 0, -i)
		key := day.Format("2006-01-02")
		item := rowMap[key]

		runningBase := item.RunningTotal
		idleBase := item.IdleTotal
		if avgDeviceCount > 0 {
			runningBase = runningBase / float64(avgDeviceCount)
			idleBase = idleBase / float64(avgDeviceCount)
		}

		running := roundFloat(runningBase, 2)
		processing := roundFloat(runningBase*0.8, 2)
		utilization := 0.0
		if avgDeviceCount > 0 {
			utilization = roundFloat(item.UtilSum/float64(avgDeviceCount), 2)
		} else if item.UtilCount > 0 {
			utilization = roundFloat(item.UtilSum/float64(item.UtilCount), 2)
		}

		runningProcessingTrend = append(runningProcessingTrend, gin.H{
			"date":           day.Format("01-02"),
			"runningTime":    running,
			"processingTime": processing,
		})
		utilizationTrend = append(utilizationTrend, gin.H{
			"date":  day.Format("01-02"),
			"value": utilization,
		})
	}
	return runningProcessingTrend, utilizationTrend
}

func (h *StatisticsHandler) GetSalaryStats(c *gin.Context) {
	startDate := c.Query("startDate")
	endDate := c.Query("endDate")
	employeeId := c.Query("employeeId")
	employeeKeyword := strings.TrimSpace(c.Query("employeeKeyword"))
	deviceId := c.Query("deviceId")
	deviceIDs := parseDeviceIDs(c.Query("deviceIds"))
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("pageSize", "10"))
	page, pageSize = normalizePagination(page, pageSize)

	baseQuery := h.buildSalaryStatsBaseQuery(startDate, endDate, employeeId, employeeKeyword, deviceId, deviceIDs)

	var total int64
	baseQuery.Session(&gorm.Session{}).Count(&total)

	var summaryRow struct {
		TotalSalary   float64
		TotalPieces   int64
		EmployeeCount int64
	}
	baseQuery.Session(&gorm.Session{}).
		Select("COALESCE(SUM(sr.total_amount), 0) as total_salary, COALESCE(SUM(sr.pieces), 0) as total_pieces, COUNT(DISTINCT sr.employee_id) as employee_count").
		Scan(&summaryRow)

	averageSalary := 0.0
	if summaryRow.EmployeeCount > 0 {
		averageSalary = summaryRow.TotalSalary / float64(summaryRow.EmployeeCount)
	}
	totalSalary := roundFloat(summaryRow.TotalSalary, 2)
	averageSalary = roundFloat(averageSalary, 2)

	var results []struct {
		model.SalaryRecord
		EmployeeName string
		EmployeeCode string
		DeviceName   string
	}
	offset := (page - 1) * pageSize
	baseQuery.Session(&gorm.Session{}).
		Select("sr.*, e.name as employee_name, e.code as employee_code, d.name as device_name").
		Order("sr.record_date DESC").
		Offset(offset).
		Limit(pageSize).
		Scan(&results)

	list := make([]gin.H, 0, len(results))
	for _, r := range results {
		list = append(list, gin.H{
			"id":           r.ID,
			"employeeId":   r.EmployeeID,
			"employeeName": r.EmployeeName,
			"employeeCode": r.EmployeeCode,
			"deviceId":     r.DeviceID,
			"deviceName":   r.DeviceName,
			"totalPieces":  r.Pieces,
			"unitPrice":    roundFloat(r.UnitPrice, 3),
			"salary":       roundFloat(r.Salary, 2),
			"bonus":        roundFloat(r.Bonus, 2),
			"totalAmount":  roundFloat(r.TotalAmount, 2),
			"date":         r.RecordDate.Format("2006-01-02"),
		})
	}

	var rankRows []struct {
		EmployeeName string
		TotalAmount  float64
	}
	baseQuery.Session(&gorm.Session{}).
		Select("COALESCE(e.name, '未知员工') as employee_name, COALESCE(SUM(sr.total_amount), 0) as total_amount").
		Group("sr.employee_id, e.name").
		Order("total_amount DESC").
		Limit(10).
		Scan(&rankRows)

	salaryRank := make([]gin.H, 0, len(rankRows))
	for _, row := range rankRows {
		salaryRank = append(salaryRank, gin.H{
			"name":  row.EmployeeName,
			"value": roundFloat(row.TotalAmount, 2),
		})
	}

	var trendRows []struct {
		Date        string
		TotalAmount float64
	}
	baseQuery.Session(&gorm.Session{}).
		Select("DATE(sr.record_date) as date, COALESCE(SUM(sr.total_amount), 0) as total_amount").
		Group("DATE(sr.record_date)").
		Order("date").
		Scan(&trendRows)

	salaryTrend := make([]gin.H, 0, len(trendRows))
	for _, row := range trendRows {
		salaryTrend = append(salaryTrend, gin.H{
			"date":  row.Date,
			"value": roundFloat(row.TotalAmount, 2),
		})
	}

	summary := gin.H{
		"totalSalary":   totalSalary,
		"totalPieces":   summaryRow.TotalPieces,
		"averageSalary": averageSalary,
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"summary":       summary,
			"list":          list,
			"total":         total,
			"salaryRank":    salaryRank,
			"salaryTrend":   salaryTrend,
			"totalSalary":   totalSalary,
			"totalPieces":   summaryRow.TotalPieces,
			"averageSalary": averageSalary,
		},
		"message": "success",
	})
}

func (h *StatisticsHandler) GetSalaryDetail(c *gin.Context) {
	startDate := c.Query("startDate")
	endDate := c.Query("endDate")
	date := c.Query("date")
	employeeId := c.Query("employeeId")
	deviceId := c.Query("deviceId")
	deviceIDs := parseDeviceIDs(c.Query("deviceIds"))

	query := h.buildProductionStatsBaseQuery(startDate, endDate, deviceId, deviceIDs)
	if employeeId != "" {
		query = query.Where("pr.employee_id = ?", employeeId)
	}
	if date != "" {
		query = query.Where("DATE(pr.record_date) = ?", date)
	}

	var results []struct {
		model.ProductionRecord
		DeviceName      string
		PatternName     string
		PatternStitches int64
		UnitPrice       float64
	}
	query.Session(&gorm.Session{}).
		Select("pr.*, COALESCE(d.name, CONCAT('设备-', pr.device_id)) as device_name, COALESCE(p.name, '未命名花型') as pattern_name, COALESCE(NULLIF(p.stitches, 0), pr.stitches, 0) as pattern_stitches, COALESCE(sr.unit_price, 0) as unit_price").
		Joins("LEFT JOIN devices d ON pr.device_id = d.id").
		Joins("LEFT JOIN patterns p ON pr.pattern_id = p.id").
		Joins("LEFT JOIN (SELECT employee_id, device_id, DATE(record_date) as record_date, MAX(unit_price) as unit_price FROM salary_records GROUP BY employee_id, device_id, DATE(record_date)) sr ON sr.employee_id = pr.employee_id AND sr.device_id = pr.device_id AND sr.record_date = DATE(pr.record_date)").
		Order("pr.record_date DESC, pr.created_at DESC, pr.id DESC").
		Scan(&results)

	list := make([]gin.H, 0, len(results))
	for _, r := range results {
		startTime, endTime := deriveProductionTimeRange(r.RecordDate, r.CreatedAt, r.RunningTime)
		totalAmount := roundFloat(float64(r.Pieces)*r.UnitPrice, 2)
		avgSewDuration := 0.0
		if r.Pieces > 0 {
			avgSewDuration = (r.RunningTime * 60) / float64(r.Pieces)
		}
		list = append(list, gin.H{
			"id":              r.ID,
			"deviceName":      r.DeviceName,
			"patternName":     r.PatternName,
			"patternStitches": r.PatternStitches,
			"startTime":       startTime.Format("2006-01-02 15:04:05"),
			"endTime":         endTime.Format("2006-01-02 15:04:05"),
			"sewCount":        r.Pieces,
			"sewDuration":     roundFloat(r.RunningTime, 2),
			"avgSewDuration":  roundFloat(avgSewDuration, 2),
			"unitPrice":       roundFloat(r.UnitPrice, 3),
			"totalAmount":     totalAmount,
			"date":            r.RecordDate.Format("2006-01-02"),
		})
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"data":    list,
		"message": "success",
	})
}

func (h *StatisticsHandler) GetProcessOverview(c *gin.Context) {
	startDate := c.Query("startDate")
	endDate := c.Query("endDate")
	deviceId := c.Query("deviceId")
	deviceIDs := parseDeviceIDs(c.Query("deviceIds"))
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("pageSize", "10"))
	page, pageSize = normalizePagination(page, pageSize)

	baseQuery := h.buildProductionStatsBaseQuery(startDate, endDate, deviceId, deviceIDs)

	var summaryRow struct {
		TotalPieces  int64
		TotalThread  float64
		TotalRunning float64
		TotalIdle    float64
	}
	baseQuery.Session(&gorm.Session{}).
		Select("COALESCE(SUM(pr.pieces), 0) as total_pieces, COALESCE(SUM(pr.thread_length), 0) as total_thread, COALESCE(SUM(pr.running_time), 0) as total_running, COALESCE(SUM(pr.idle_time), 0) as total_idle").
		Scan(&summaryRow)

	totalHours := summaryRow.TotalRunning + summaryRow.TotalIdle
	avgEfficiency := 0.0
	if totalHours > 0 {
		avgEfficiency = (summaryRow.TotalRunning / totalHours) * 100
	}
	avgEfficiency = roundFloat(avgEfficiency, 2)

	var total int64
	baseQuery.Session(&gorm.Session{}).Count(&total)

	var listRows []struct {
		model.ProductionRecord
		DeviceName      string
		EmployeeCode    string
		EmployeeName    string
		PatternName     string
		PatternStitches int64
	}
	offset := (page - 1) * pageSize
	baseQuery.Session(&gorm.Session{}).
		Select("pr.*, COALESCE(d.name, CONCAT('设备-', pr.device_id)) as device_name, COALESCE(e.code, '-') as employee_code, COALESCE(e.name, '-') as employee_name, COALESCE(p.name, '未命名花型') as pattern_name, COALESCE(NULLIF(p.stitches, 0), pr.stitches, 0) as pattern_stitches").
		Joins("LEFT JOIN devices d ON pr.device_id = d.id").
		Joins("LEFT JOIN employees e ON pr.employee_id = e.id").
		Joins("LEFT JOIN patterns p ON pr.pattern_id = p.id").
		Order("pr.record_date DESC, pr.created_at DESC, pr.id DESC").
		Offset(offset).
		Limit(pageSize).
		Scan(&listRows)

	patternSewCountMap := h.loadPatternSewCountByDevicePatternDate(startDate, endDate, deviceId, deviceIDs)
	alarmInfoMap := h.loadAlarmInfoByDeviceDate(startDate, endDate, deviceId, deviceIDs)

	list := make([]gin.H, 0, len(listRows))
	for _, row := range listRows {
		efficiency := 0.0
		if row.RunningTime+row.IdleTime > 0 {
			efficiency = row.RunningTime / (row.RunningTime + row.IdleTime) * 100
		}
		startTime, _ := deriveProductionTimeRange(row.RecordDate, row.CreatedAt, row.RunningTime)
		sewSpeed := 0.0
		if row.RunningTime > 0 {
			sewSpeed = float64(row.Stitches) / (row.RunningTime * 60)
		}
		avgProcessDuration := 0.0
		if row.Pieces > 0 {
			avgProcessDuration = (row.RunningTime * 60) / float64(row.Pieces)
		}
		dateKey := row.RecordDate.Format("2006-01-02")
		patternCountKey := makeDevicePatternDateKey(row.DeviceID, row.PatternID, dateKey)
		patternSewCount := patternSewCountMap[patternCountKey]
		alarmInfo := alarmInfoMap[makeDeviceDateKey(row.DeviceID, dateKey)]
		alarmText := "-"
		alarmTime := "-"
		if alarmInfo.AlarmInfo != "" {
			alarmText = alarmInfo.AlarmInfo
		}
		if alarmInfo.AlarmTime != "" {
			alarmTime = alarmInfo.AlarmTime
		}
		list = append(list, gin.H{
			"id":                 row.ID,
			"deviceName":         row.DeviceName,
			"employeeCode":       row.EmployeeCode,
			"employeeName":       row.EmployeeName,
			"date":               dateKey,
			"patternName":        row.PatternName,
			"patternStitches":    row.PatternStitches,
			"sewSpeed":           roundFloat(sewSpeed, 2),
			"startTime":          startTime.Format("2006-01-02 15:04:05"),
			"processCount":       row.Pieces,
			"avgProcessDuration": roundFloat(avgProcessDuration, 2),
			"patternSewCount":    patternSewCount,
			"alarmInfo":          alarmText,
			"alarmTime":          alarmTime,
			"cumulativeUpTime":   roundFloat(row.RunningTime+row.IdleTime, 2),
			"totalPieces":        row.Pieces,
			"totalStitches":      row.Stitches,
			"threadLength":       roundFloat(row.ThreadLength, 2),
			"runningTime":        roundFloat(row.RunningTime, 2),
			"efficiency":         roundFloat(efficiency, 2),
		})
	}

	var trendRows []struct {
		Date        string
		Pieces      int64
		RunningTime float64
		IdleTime    float64
	}
	baseQuery.Session(&gorm.Session{}).
		Select("DATE(pr.record_date) as date, COALESCE(SUM(pr.pieces), 0) as pieces, COALESCE(SUM(pr.running_time), 0) as running_time, COALESCE(SUM(pr.idle_time), 0) as idle_time").
		Group("DATE(pr.record_date)").
		Order("date").
		Scan(&trendRows)

	productionTrend := make([]gin.H, 0, len(trendRows))
	for _, row := range trendRows {
		efficiency := 0.0
		if row.RunningTime+row.IdleTime > 0 {
			efficiency = row.RunningTime / (row.RunningTime + row.IdleTime) * 100
		}
		productionTrend = append(productionTrend, gin.H{
			"date":       row.Date,
			"pieces":     row.Pieces,
			"value":      row.Pieces,
			"efficiency": roundFloat(efficiency, 2),
		})
	}

	var distributionRows []struct {
		Name  string
		Value int64
	}
	baseQuery.Session(&gorm.Session{}).
		Select("COALESCE(d.name, CONCAT('设备-', pr.device_id)) as name, COALESCE(SUM(pr.pieces), 0) as value").
		Joins("LEFT JOIN devices d ON pr.device_id = d.id").
		Group("pr.device_id, d.name").
		Order("value DESC").
		Limit(10).
		Scan(&distributionRows)

	deviceDistribution := make([]gin.H, 0, len(distributionRows))
	for _, row := range distributionRows {
		deviceDistribution = append(deviceDistribution, gin.H{
			"name":  row.Name,
			"value": row.Value,
		})
	}

	overview := gin.H{
		"totalPieces":   summaryRow.TotalPieces,
		"totalThread":   roundFloat(summaryRow.TotalThread, 2),
		"totalHours":    roundFloat(totalHours, 2),
		"avgEfficiency": avgEfficiency,
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"overview":           overview,
			"list":               list,
			"total":              total,
			"productionTrend":    productionTrend,
			"deviceDistribution": deviceDistribution,
			"totalPieces":        summaryRow.TotalPieces,
			"totalThread":        roundFloat(summaryRow.TotalThread, 2),
			"totalHours":         roundFloat(totalHours, 2),
			"avgEfficiency":      avgEfficiency,
		},
		"message": "success",
	})
}

func (h *StatisticsHandler) GetDurationStats(c *gin.Context) {
	startDate := c.Query("startDate")
	endDate := c.Query("endDate")
	deviceId := c.Query("deviceId")
	deviceIDs := parseDeviceIDs(c.Query("deviceIds"))
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("pageSize", "10"))
	page, pageSize = normalizePagination(page, pageSize)

	baseProdQuery := h.buildProductionStatsBaseQuery(startDate, endDate, deviceId, deviceIDs)
	baseAlarmQuery := h.buildAlarmStatsBaseQuery(startDate, endDate, deviceId, deviceIDs, "")

	var prodSummary struct {
		RunningTime float64
		IdleTime    float64
	}
	baseProdQuery.Session(&gorm.Session{}).
		Select("COALESCE(SUM(pr.running_time), 0) as running_time, COALESCE(SUM(pr.idle_time), 0) as idle_time").
		Scan(&prodSummary)

	var alarmSummary struct {
		DurationSeconds int64
	}
	baseAlarmQuery.Session(&gorm.Session{}).
		Select("COALESCE(SUM(ar.duration), 0) as duration_seconds").
		Scan(&alarmSummary)

	alarmHours := float64(alarmSummary.DurationSeconds) / 3600.0
	totalTime := prodSummary.RunningTime + prodSummary.IdleTime + alarmHours

	summary := gin.H{
		"totalTime":   roundFloat(totalTime, 2),
		"runningTime": roundFloat(prodSummary.RunningTime, 2),
		"idleTime":    roundFloat(prodSummary.IdleTime, 2),
		"alarmTime":   roundFloat(alarmHours, 2),
	}

	var total int64
	baseProdQuery.Session(&gorm.Session{}).Count(&total)

	var listRows []struct {
		model.ProductionRecord
		DeviceName   string
		EmployeeCode string
		EmployeeName string
		PatternName  string
	}
	offset := (page - 1) * pageSize
	baseProdQuery.Session(&gorm.Session{}).
		Select("pr.*, COALESCE(d.name, CONCAT('设备-', pr.device_id)) as device_name, COALESCE(e.code, '-') as employee_code, COALESCE(e.name, '-') as employee_name, COALESCE(p.name, '未命名花型') as pattern_name").
		Joins("LEFT JOIN devices d ON pr.device_id = d.id").
		Joins("LEFT JOIN employees e ON pr.employee_id = e.id").
		Joins("LEFT JOIN patterns p ON pr.pattern_id = p.id").
		Order("pr.record_date DESC, pr.created_at DESC, pr.id DESC").
		Offset(offset).
		Limit(pageSize).
		Scan(&listRows)

	list := make([]gin.H, 0, len(listRows))
	for _, row := range listRows {
		startTime, endTime := deriveProductionTimeRange(row.RecordDate, row.CreatedAt, row.RunningTime)
		avgSewDuration := 0.0
		if row.Pieces > 0 {
			avgSewDuration = (row.RunningTime * 60) / float64(row.Pieces)
		}
		list = append(list, gin.H{
			"id":             row.ID,
			"deviceName":     row.DeviceName,
			"employeeCode":   row.EmployeeCode,
			"employeeName":   row.EmployeeName,
			"date":           row.RecordDate.Format("2006-01-02"),
			"patternName":    row.PatternName,
			"startTime":      startTime.Format("2006-01-02 15:04:05"),
			"endTime":        endTime.Format("2006-01-02 15:04:05"),
			"sewDuration":    roundFloat(row.RunningTime, 2),
			"avgSewDuration": roundFloat(avgSewDuration, 2),
			"totalTime":      roundFloat(row.RunningTime+row.IdleTime, 2),
			"runningTime":    roundFloat(row.RunningTime, 2),
			"idleTime":       roundFloat(row.IdleTime, 2),
		})
	}

	durationPie := []gin.H{
		{"name": "运行时长", "value": roundFloat(prodSummary.RunningTime, 2)},
		{"name": "空闲时长", "value": roundFloat(prodSummary.IdleTime, 2)},
		{"name": "报警时长", "value": roundFloat(alarmHours, 2)},
	}

	var prodTrendRows []struct {
		Date        string
		RunningTime float64
		IdleTime    float64
	}
	baseProdQuery.Session(&gorm.Session{}).
		Select("DATE(pr.record_date) as date, COALESCE(SUM(pr.running_time), 0) as running_time, COALESCE(SUM(pr.idle_time), 0) as idle_time").
		Group("DATE(pr.record_date)").
		Order("date").
		Scan(&prodTrendRows)

	var alarmTrendRows []struct {
		Date      string
		AlarmTime float64
	}
	baseAlarmQuery.Session(&gorm.Session{}).
		Select("DATE(ar.start_time) as date, COALESCE(SUM(ar.duration), 0) / 3600 as alarm_time").
		Group("DATE(ar.start_time)").
		Order("date").
		Scan(&alarmTrendRows)

	type trendPoint struct {
		Date        string
		RunningTime float64
		IdleTime    float64
		AlarmTime   float64
	}
	trendMap := make(map[string]*trendPoint)
	for _, row := range prodTrendRows {
		trendMap[row.Date] = &trendPoint{
			Date:        row.Date,
			RunningTime: row.RunningTime,
			IdleTime:    row.IdleTime,
		}
	}
	for _, row := range alarmTrendRows {
		item, ok := trendMap[row.Date]
		if !ok {
			item = &trendPoint{Date: row.Date}
			trendMap[row.Date] = item
		}
		item.AlarmTime = row.AlarmTime
	}

	dates := make([]string, 0, len(trendMap))
	for date := range trendMap {
		dates = append(dates, date)
	}
	sort.Strings(dates)

	durationTrend := make([]gin.H, 0, len(dates))
	for _, date := range dates {
		row := trendMap[date]
		durationTrend = append(durationTrend, gin.H{
			"date":        row.Date,
			"runningTime": roundFloat(row.RunningTime, 2),
			"idleTime":    roundFloat(row.IdleTime, 2),
			"alarmTime":   roundFloat(row.AlarmTime, 2),
		})
	}

	var deviceSummaryRows []struct {
		Name        string
		RunningTime float64
		IdleTime    float64
	}
	baseProdQuery.Session(&gorm.Session{}).
		Select("COALESCE(d.name, CONCAT('设备-', pr.device_id)) as name, COALESCE(SUM(pr.running_time), 0) as running_time, COALESCE(SUM(pr.idle_time), 0) as idle_time").
		Joins("LEFT JOIN devices d ON pr.device_id = d.id").
		Group("pr.device_id, d.name").
		Order("running_time DESC").
		Limit(10).
		Scan(&deviceSummaryRows)

	deviceStats := make([]gin.H, 0, len(deviceSummaryRows))
	for _, row := range deviceSummaryRows {
		deviceStats = append(deviceStats, gin.H{
			"name":        row.Name,
			"runningTime": roundFloat(row.RunningTime, 2),
			"idleTime":    roundFloat(row.IdleTime, 2),
		})
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"summary":       summary,
			"list":          list,
			"total":         total,
			"durationPie":   durationPie,
			"durationTrend": durationTrend,
			"totalTime":     summary["totalTime"],
			"runningTime":   summary["runningTime"],
			"idleTime":      summary["idleTime"],
			"alarmTime":     summary["alarmTime"],
			"deviceStats":   deviceStats,
		},
		"message": "success",
	})
}

func (h *StatisticsHandler) GetAlarmStats(c *gin.Context) {
	startDate := c.Query("startDate")
	endDate := c.Query("endDate")
	deviceId := c.Query("deviceId")
	deviceIDs := parseDeviceIDs(c.Query("deviceIds"))
	alarmType := c.Query("alarmType")
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("pageSize", "10"))
	page, pageSize = normalizePagination(page, pageSize)

	baseQuery := h.buildAlarmStatsBaseQuery(startDate, endDate, deviceId, deviceIDs, alarmType)

	var total int64
	baseQuery.Session(&gorm.Session{}).Count(&total)

	var totalDuration int64
	baseQuery.Session(&gorm.Session{}).
		Select("COALESCE(SUM(ar.duration), 0)").
		Scan(&totalDuration)

	var affectedDevices int64
	baseQuery.Session(&gorm.Session{}).
		Distinct("ar.device_id").
		Count(&affectedDevices)

	var resolvedCount int64
	baseQuery.Session(&gorm.Session{}).
		Where("ar.status = ?", "resolved").
		Count(&resolvedCount)

	resolvedRate := 0.0
	if total > 0 {
		resolvedRate = float64(resolvedCount) / float64(total) * 100
	}
	resolvedRate = roundFloat(resolvedRate, 2)

	var typeRows []struct {
		AlarmType string
		Count     int64
	}
	baseQuery.Session(&gorm.Session{}).
		Select("ar.alarm_type as alarm_type, COUNT(*) as count").
		Group("ar.alarm_type").
		Order("count DESC").
		Scan(&typeRows)

	alarmTypePie := make([]gin.H, 0, len(typeRows))
	for _, row := range typeRows {
		name := row.AlarmType
		if name == "" {
			name = "未分类"
		}
		alarmTypePie = append(alarmTypePie, gin.H{
			"name":  name,
			"value": row.Count,
		})
	}

	var trendRows []struct {
		Date        string
		Count       int64
		AvgDuration float64
	}
	baseQuery.Session(&gorm.Session{}).
		Select("DATE(ar.start_time) as date, COUNT(*) as count, COALESCE(AVG(ar.duration), 0) / 60 as avg_duration").
		Group("DATE(ar.start_time)").
		Order("date").
		Scan(&trendRows)

	alarmTrend := make([]gin.H, 0, len(trendRows))
	for _, row := range trendRows {
		avgDuration := roundFloat(row.AvgDuration, 2)
		alarmTrend = append(alarmTrend, gin.H{
			"date":        row.Date,
			"count":       row.Count,
			"avgDuration": avgDuration,
			"avgTime":     avgDuration,
		})
	}

	var records []struct {
		model.AlarmRecord
		DeviceName string
	}
	offset := (page - 1) * pageSize
	baseQuery.Session(&gorm.Session{}).
		Select("ar.*, d.name as device_name").
		Joins("LEFT JOIN devices d ON ar.device_id = d.id").
		Order("ar.start_time DESC").
		Offset(offset).
		Limit(pageSize).
		Scan(&records)

	employeeByDeviceDate := h.loadEmployeeInfoByDeviceDate(startDate, endDate, deviceId, deviceIDs)

	list := make([]gin.H, 0, len(records))
	for _, row := range records {
		endTime := "-"
		if row.EndTime != nil {
			endTime = row.EndTime.Format("2006-01-02 15:04:05")
		}
		dateKey := row.StartTime.Format("2006-01-02")
		employeeInfo := employeeByDeviceDate[makeDeviceDateKey(row.DeviceID, dateKey)]
		if employeeInfo.EmployeeCode == "" {
			employeeInfo.EmployeeCode = "-"
		}
		if employeeInfo.EmployeeName == "" {
			employeeInfo.EmployeeName = "-"
		}
		alarmInfo := strings.TrimSpace(row.AlarmType)
		if alarmInfo == "" {
			alarmInfo = strings.TrimSpace(row.Description)
		}
		if alarmInfo == "" {
			alarmInfo = "报警"
		}
		list = append(list, gin.H{
			"id":           row.ID,
			"deviceName":   row.DeviceName,
			"employeeCode": employeeInfo.EmployeeCode,
			"employeeName": employeeInfo.EmployeeName,
			"alarmTime":    row.StartTime.Format("2006-01-02 15:04:05"),
			"alarmInfo":    alarmInfo,
			"alarmType":    row.AlarmType,
			"alarmCode":    row.AlarmCode,
			"description":  row.Description,
			"duration":     formatDuration(row.Duration),
			"status":       formatAlarmStatus(row.Status),
			"startTime":    row.StartTime.Format("2006-01-02 15:04:05"),
			"endTime":      endTime,
		})
	}

	summary := gin.H{
		"totalAlarms":     total,
		"totalDuration":   roundFloat(float64(totalDuration)/60.0, 2),
		"affectedDevices": affectedDevices,
		"resolvedRate":    resolvedRate,
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"summary":         summary,
			"list":            list,
			"total":           total,
			"alarmTypePie":    alarmTypePie,
			"alarmTrend":      alarmTrend,
			"totalAlarms":     total,
			"totalDuration":   summary["totalDuration"],
			"affectedDevices": affectedDevices,
			"resolvedRate":    resolvedRate,
		},
		"message": "success",
	})
}

func (h *StatisticsHandler) ExportStatistics(c *gin.Context) {
	exportType := c.Param("type")
	switch exportType {
	case "salary":
		h.exportSalaryCSV(c)
	case "process":
		h.exportProcessCSV(c)
	case "duration":
		h.exportDurationCSV(c)
	case "alarm":
		h.exportAlarmCSV(c)
	default:
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "不支持的导出类型",
		})
	}
}

func (h *StatisticsHandler) exportSalaryCSV(c *gin.Context) {
	startDate := c.Query("startDate")
	endDate := c.Query("endDate")
	employeeId := c.Query("employeeId")
	employeeKeyword := strings.TrimSpace(c.Query("employeeKeyword"))
	deviceId := c.Query("deviceId")
	deviceIDs := parseDeviceIDs(c.Query("deviceIds"))
	mode := c.DefaultQuery("mode", "all")

	baseQuery := h.buildSalaryStatsBaseQuery(startDate, endDate, employeeId, employeeKeyword, deviceId, deviceIDs)

	fileNamePrefix := "salary_stats"
	switch mode {
	case "merged":
		var rowsData []struct {
			EmployeeName string
			EmployeeCode string
			DeviceName   string
			UnitPrice    float64
			TotalPieces  int64
			Salary       float64
			Bonus        float64
			TotalAmount  float64
		}
		baseQuery.Session(&gorm.Session{}).
			Select("e.name as employee_name, e.code as employee_code, d.name as device_name, COALESCE(sr.unit_price, 0) as unit_price, COALESCE(SUM(sr.pieces), 0) as total_pieces, COALESCE(SUM(sr.salary), 0) as salary, COALESCE(SUM(sr.bonus), 0) as bonus, COALESCE(SUM(sr.total_amount), 0) as total_amount").
			Group("sr.employee_id, e.name, e.code, sr.device_id, d.name, sr.unit_price").
			Order("total_amount DESC").
			Scan(&rowsData)

		rows := make([][]string, 0, len(rowsData))
		for _, row := range rowsData {
			rows = append(rows, []string{
				row.EmployeeCode,
				row.EmployeeName,
				row.DeviceName,
				strconv.FormatInt(row.TotalPieces, 10),
				csvFloat(row.UnitPrice, 3),
				csvFloat(row.Salary, 2),
				csvFloat(row.Bonus, 2),
				csvFloat(row.TotalAmount, 2),
			})
		}

		writeCSVResponse(c,
			fileNamePrefix+"_merged_"+time.Now().Format("20060102_150405")+".csv",
			[]string{"员工工号", "员工姓名", "设备名称", "加工件数", "单价(元)", "工资(元)", "奖金(元)", "合计(元)"},
			rows,
		)
		return
	case "current":
		fileNamePrefix = "salary_stats_current"
	default:
		fileNamePrefix = "salary_stats_all"
	}

	query := baseQuery.Session(&gorm.Session{}).
		Select("sr.*, e.name as employee_name, e.code as employee_code, d.name as device_name").
		Order("sr.record_date DESC")

	if mode == "current" {
		page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
		pageSize, _ := strconv.Atoi(c.DefaultQuery("pageSize", "10"))
		page, pageSize = normalizePagination(page, pageSize)
		offset := (page - 1) * pageSize
		query = query.Offset(offset).Limit(pageSize)
	}

	var rowsData []struct {
		model.SalaryRecord
		EmployeeName string
		EmployeeCode string
		DeviceName   string
	}
	query.Scan(&rowsData)

	rows := make([][]string, 0, len(rowsData))
	for _, row := range rowsData {
		rows = append(rows, []string{
			row.EmployeeCode,
			row.EmployeeName,
			row.DeviceName,
			strconv.Itoa(row.Pieces),
			csvFloat(row.UnitPrice, 3),
			csvFloat(row.Salary, 2),
			csvFloat(row.Bonus, 2),
			csvFloat(row.TotalAmount, 2),
			row.RecordDate.Format("2006-01-02"),
		})
	}

	writeCSVResponse(c,
		fileNamePrefix+"_"+time.Now().Format("20060102_150405")+".csv",
		[]string{"员工工号", "员工姓名", "设备名称", "加工件数", "单价(元)", "工资(元)", "奖金(元)", "合计(元)", "日期"},
		rows,
	)
}

func (h *StatisticsHandler) exportProcessCSV(c *gin.Context) {
	startDate := c.Query("startDate")
	endDate := c.Query("endDate")
	deviceId := c.Query("deviceId")
	deviceIDs := parseDeviceIDs(c.Query("deviceIds"))

	baseQuery := h.buildProductionStatsBaseQuery(startDate, endDate, deviceId, deviceIDs)

	var rowsData []struct {
		model.ProductionRecord
		DeviceName      string
		EmployeeCode    string
		EmployeeName    string
		PatternName     string
		PatternStitches int64
	}
	baseQuery.Session(&gorm.Session{}).
		Select("pr.*, COALESCE(d.name, CONCAT('设备-', pr.device_id)) as device_name, COALESCE(e.code, '-') as employee_code, COALESCE(e.name, '-') as employee_name, COALESCE(p.name, '未命名花型') as pattern_name, COALESCE(NULLIF(p.stitches, 0), pr.stitches, 0) as pattern_stitches").
		Joins("LEFT JOIN devices d ON pr.device_id = d.id").
		Joins("LEFT JOIN employees e ON pr.employee_id = e.id").
		Joins("LEFT JOIN patterns p ON pr.pattern_id = p.id").
		Order("pr.record_date DESC, pr.created_at DESC, pr.id DESC").
		Scan(&rowsData)

	patternSewCountMap := h.loadPatternSewCountByDevicePatternDate(startDate, endDate, deviceId, deviceIDs)
	alarmInfoMap := h.loadAlarmInfoByDeviceDate(startDate, endDate, deviceId, deviceIDs)

	rows := make([][]string, 0, len(rowsData))
	for _, row := range rowsData {
		startTime, _ := deriveProductionTimeRange(row.RecordDate, row.CreatedAt, row.RunningTime)
		sewSpeed := 0.0
		if row.RunningTime > 0 {
			sewSpeed = float64(row.Stitches) / (row.RunningTime * 60)
		}
		avgProcessDuration := 0.0
		if row.Pieces > 0 {
			avgProcessDuration = (row.RunningTime * 60) / float64(row.Pieces)
		}
		dateKey := row.RecordDate.Format("2006-01-02")
		patternSewCount := patternSewCountMap[makeDevicePatternDateKey(row.DeviceID, row.PatternID, dateKey)]
		alarmInfo := alarmInfoMap[makeDeviceDateKey(row.DeviceID, dateKey)]
		alarmText := "-"
		alarmTime := "-"
		if alarmInfo.AlarmInfo != "" {
			alarmText = alarmInfo.AlarmInfo
		}
		if alarmInfo.AlarmTime != "" {
			alarmTime = alarmInfo.AlarmTime
		}
		rows = append(rows, []string{
			row.DeviceName,
			row.EmployeeCode,
			row.EmployeeName,
			dateKey,
			row.PatternName,
			strconv.FormatInt(row.PatternStitches, 10),
			csvFloat(sewSpeed, 2),
			startTime.Format("2006-01-02 15:04:05"),
			strconv.Itoa(row.Pieces),
			csvFloat(avgProcessDuration, 2),
			strconv.FormatInt(patternSewCount, 10),
			alarmText,
			alarmTime,
			csvFloat(row.RunningTime+row.IdleTime, 2),
		})
	}

	writeCSVResponse(c,
		"process_overview_"+time.Now().Format("20060102_150405")+".csv",
		[]string{"设备名称", "员工工号", "员工姓名", "日期", "花型名称", "花型针数", "缝纫速度(针/分钟)", "开始时间", "加工次数", "平均加工时长(min/次)", "花型缝纫次数", "报警信息", "报警时间", "累计开机时长(h)"},
		rows,
	)
}

func (h *StatisticsHandler) exportDurationCSV(c *gin.Context) {
	startDate := c.Query("startDate")
	endDate := c.Query("endDate")
	deviceId := c.Query("deviceId")
	deviceIDs := parseDeviceIDs(c.Query("deviceIds"))

	baseProdQuery := h.buildProductionStatsBaseQuery(startDate, endDate, deviceId, deviceIDs)

	var rowsData []struct {
		model.ProductionRecord
		DeviceName   string
		EmployeeCode string
		EmployeeName string
		PatternName  string
	}
	baseProdQuery.Session(&gorm.Session{}).
		Select("pr.*, COALESCE(d.name, CONCAT('设备-', pr.device_id)) as device_name, COALESCE(e.code, '-') as employee_code, COALESCE(e.name, '-') as employee_name, COALESCE(p.name, '未命名花型') as pattern_name").
		Joins("LEFT JOIN devices d ON pr.device_id = d.id").
		Joins("LEFT JOIN employees e ON pr.employee_id = e.id").
		Joins("LEFT JOIN patterns p ON pr.pattern_id = p.id").
		Order("pr.record_date DESC, pr.created_at DESC, pr.id DESC").
		Scan(&rowsData)

	rows := make([][]string, 0, len(rowsData))
	for _, row := range rowsData {
		startTime, endTime := deriveProductionTimeRange(row.RecordDate, row.CreatedAt, row.RunningTime)
		avgSewDuration := 0.0
		if row.Pieces > 0 {
			avgSewDuration = (row.RunningTime * 60) / float64(row.Pieces)
		}
		rows = append(rows, []string{
			row.DeviceName,
			row.EmployeeCode,
			row.EmployeeName,
			row.RecordDate.Format("2006-01-02"),
			row.PatternName,
			startTime.Format("2006-01-02 15:04:05"),
			endTime.Format("2006-01-02 15:04:05"),
			csvFloat(row.RunningTime, 2),
			csvFloat(avgSewDuration, 2),
		})
	}

	writeCSVResponse(c,
		"duration_stats_"+time.Now().Format("20060102_150405")+".csv",
		[]string{"设备名称", "员工工号", "员工姓名", "日期", "花型名称", "开始时间", "结束时间", "缝纫时长(h)", "平均缝纫时长(min/次)"},
		rows,
	)
}

func (h *StatisticsHandler) exportAlarmCSV(c *gin.Context) {
	startDate := c.Query("startDate")
	endDate := c.Query("endDate")
	deviceId := c.Query("deviceId")
	deviceIDs := parseDeviceIDs(c.Query("deviceIds"))
	alarmType := c.Query("alarmType")

	baseQuery := h.buildAlarmStatsBaseQuery(startDate, endDate, deviceId, deviceIDs, alarmType)

	var rowsData []struct {
		model.AlarmRecord
		DeviceName string
	}
	baseQuery.Session(&gorm.Session{}).
		Select("ar.*, d.name as device_name").
		Joins("LEFT JOIN devices d ON ar.device_id = d.id").
		Order("ar.start_time DESC").
		Scan(&rowsData)

	employeeByDeviceDate := h.loadEmployeeInfoByDeviceDate(startDate, endDate, deviceId, deviceIDs)

	rows := make([][]string, 0, len(rowsData))
	for _, row := range rowsData {
		dateKey := row.StartTime.Format("2006-01-02")
		employeeInfo := employeeByDeviceDate[makeDeviceDateKey(row.DeviceID, dateKey)]
		if employeeInfo.EmployeeCode == "" {
			employeeInfo.EmployeeCode = "-"
		}
		if employeeInfo.EmployeeName == "" {
			employeeInfo.EmployeeName = "-"
		}
		alarmInfo := strings.TrimSpace(row.AlarmType)
		if alarmInfo == "" {
			alarmInfo = strings.TrimSpace(row.Description)
		}
		if alarmInfo == "" {
			alarmInfo = "报警"
		}
		rows = append(rows, []string{
			row.DeviceName,
			employeeInfo.EmployeeCode,
			employeeInfo.EmployeeName,
			row.StartTime.Format("2006-01-02 15:04:05"),
			alarmInfo,
			row.AlarmCode,
			formatDuration(row.Duration),
			formatAlarmStatus(row.Status),
		})
	}

	writeCSVResponse(c,
		"alarm_stats_"+time.Now().Format("20060102_150405")+".csv",
		[]string{"设备名称", "员工工号", "员工姓名", "报警时间", "报警信息", "报警代码", "持续时长", "处理状态"},
		rows,
	)
}

func writeCSVResponse(c *gin.Context, fileName string, headers []string, rows [][]string) {
	c.Header("Content-Type", "text/csv; charset=utf-8")
	c.Header("Content-Disposition", fmt.Sprintf("attachment; filename=\"%s\"", fileName))
	c.Header("Access-Control-Expose-Headers", "Content-Disposition")
	c.Status(http.StatusOK)
	_, _ = c.Writer.Write([]byte("\xEF\xBB\xBF"))

	writer := csv.NewWriter(c.Writer)
	_ = writer.Write(headers)
	_ = writer.WriteAll(rows)
	writer.Flush()
}

type alarmDailyInfo struct {
	AlarmInfo string
	AlarmTime string
}

type employeeDailyInfo struct {
	EmployeeCode string
	EmployeeName string
}

func makeDeviceDateKey(deviceID uint, date string) string {
	return fmt.Sprintf("%d_%s", deviceID, date)
}

func makeDevicePatternDateKey(deviceID, patternID uint, date string) string {
	return fmt.Sprintf("%d_%d_%s", deviceID, patternID, date)
}

func deriveProductionTimeRange(recordDate, createdAt time.Time, runningHours float64) (time.Time, time.Time) {
	startTime := createdAt
	if startTime.IsZero() {
		base := recordDate
		if base.IsZero() {
			base = time.Now()
		}
		startTime = time.Date(base.Year(), base.Month(), base.Day(), 8, 0, 0, 0, base.Location())
	}
	duration := time.Duration(runningHours * float64(time.Hour))
	if duration < 0 {
		duration = 0
	}
	return startTime, startTime.Add(duration)
}

func (h *StatisticsHandler) loadPatternSewCountByDevicePatternDate(startDate, endDate, deviceId string, deviceIDs []uint) map[string]int64 {
	baseQuery := h.buildProductionStatsBaseQuery(startDate, endDate, deviceId, deviceIDs)

	var rows []struct {
		DeviceID        uint
		PatternID       uint
		RecordDate      string
		PatternSewCount int64
	}
	baseQuery.Session(&gorm.Session{}).
		Select("pr.device_id, pr.pattern_id, DATE(pr.record_date) as record_date, COALESCE(SUM(pr.pieces), 0) as pattern_sew_count").
		Group("pr.device_id, pr.pattern_id, DATE(pr.record_date)").
		Scan(&rows)

	result := make(map[string]int64, len(rows))
	for _, row := range rows {
		result[makeDevicePatternDateKey(row.DeviceID, row.PatternID, row.RecordDate)] = row.PatternSewCount
	}
	return result
}

func (h *StatisticsHandler) loadAlarmInfoByDeviceDate(startDate, endDate, deviceId string, deviceIDs []uint) map[string]alarmDailyInfo {
	baseQuery := h.buildAlarmStatsBaseQuery(startDate, endDate, deviceId, deviceIDs, "")

	var rows []struct {
		DeviceID   uint
		RecordDate string
		AlarmInfo  string
		AlarmTime  time.Time
	}
	baseQuery.Session(&gorm.Session{}).
		Select("ar.device_id, DATE(ar.start_time) as record_date, COALESCE(GROUP_CONCAT(DISTINCT COALESCE(NULLIF(ar.alarm_type, ''), NULLIF(ar.description, ''), '报警') ORDER BY ar.start_time SEPARATOR '、'), '-') as alarm_info, MIN(ar.start_time) as alarm_time").
		Group("ar.device_id, DATE(ar.start_time)").
		Scan(&rows)

	result := make(map[string]alarmDailyInfo, len(rows))
	for _, row := range rows {
		alarmTime := "-"
		if !row.AlarmTime.IsZero() {
			alarmTime = row.AlarmTime.Format("2006-01-02 15:04:05")
		}
		alarmInfo := row.AlarmInfo
		if alarmInfo == "" {
			alarmInfo = "-"
		}
		result[makeDeviceDateKey(row.DeviceID, row.RecordDate)] = alarmDailyInfo{
			AlarmInfo: alarmInfo,
			AlarmTime: alarmTime,
		}
	}
	return result
}

func (h *StatisticsHandler) loadEmployeeInfoByDeviceDate(startDate, endDate, deviceId string, deviceIDs []uint) map[string]employeeDailyInfo {
	baseQuery := h.buildProductionStatsBaseQuery(startDate, endDate, deviceId, deviceIDs)

	var rows []struct {
		DeviceID     uint
		RecordDate   string
		EmployeeCode string
		EmployeeName string
	}
	baseQuery.Session(&gorm.Session{}).
		Select("pr.device_id, DATE(pr.record_date) as record_date, COALESCE(MAX(e.code), '-') as employee_code, COALESCE(MAX(e.name), '-') as employee_name").
		Joins("LEFT JOIN employees e ON pr.employee_id = e.id").
		Group("pr.device_id, DATE(pr.record_date)").
		Scan(&rows)

	result := make(map[string]employeeDailyInfo, len(rows))
	for _, row := range rows {
		employeeCode := row.EmployeeCode
		if employeeCode == "" {
			employeeCode = "-"
		}
		employeeName := row.EmployeeName
		if employeeName == "" {
			employeeName = "-"
		}
		result[makeDeviceDateKey(row.DeviceID, row.RecordDate)] = employeeDailyInfo{
			EmployeeCode: employeeCode,
			EmployeeName: employeeName,
		}
	}
	return result
}

func (h *StatisticsHandler) buildSalaryStatsBaseQuery(startDate, endDate, employeeId, employeeKeyword, deviceId string, deviceIDs []uint) *gorm.DB {
	query := h.db.Table("salary_records sr").
		Joins("LEFT JOIN employees e ON sr.employee_id = e.id").
		Joins("LEFT JOIN devices d ON sr.device_id = d.id")
	query = applyDateRangeFilter(query, "sr.record_date", startDate, endDate)
	if employeeId != "" {
		query = query.Where("sr.employee_id = ?", employeeId)
	}
	if employeeKeyword != "" {
		like := "%" + employeeKeyword + "%"
		query = query.Where("(e.name LIKE ? OR e.code LIKE ?)", like, like)
	}
	if len(deviceIDs) > 0 {
		query = query.Where("sr.device_id IN ?", deviceIDs)
	} else if deviceId != "" {
		query = query.Where("sr.device_id = ?", deviceId)
	}
	return query
}

func (h *StatisticsHandler) buildProductionStatsBaseQuery(startDate, endDate, deviceId string, deviceIDs []uint) *gorm.DB {
	query := h.db.Table("production_records pr")
	query = applyDateRangeFilter(query, "pr.record_date", startDate, endDate)
	if len(deviceIDs) > 0 {
		query = query.Where("pr.device_id IN ?", deviceIDs)
	} else if deviceId != "" {
		query = query.Where("pr.device_id = ?", deviceId)
	}
	return query
}

func (h *StatisticsHandler) buildAlarmStatsBaseQuery(startDate, endDate, deviceId string, deviceIDs []uint, alarmType string) *gorm.DB {
	query := h.db.Table("alarm_records ar")
	query = applyDateRangeFilter(query, "ar.start_time", startDate, endDate)
	if len(deviceIDs) > 0 {
		query = query.Where("ar.device_id IN ?", deviceIDs)
	} else if deviceId != "" {
		query = query.Where("ar.device_id = ?", deviceId)
	}
	if alarmType != "" {
		query = query.Where("ar.alarm_type = ?", alarmType)
	}
	return query
}

func parseDeviceIDs(raw string) []uint {
	if raw == "" {
		return nil
	}
	parts := strings.Split(raw, ",")
	ids := make([]uint, 0, len(parts))
	for _, part := range parts {
		part = strings.TrimSpace(part)
		if part == "" {
			continue
		}
		parsed, err := strconv.ParseUint(part, 10, 64)
		if err != nil || parsed == 0 {
			continue
		}
		ids = append(ids, uint(parsed))
	}
	if len(ids) == 0 {
		return nil
	}
	return ids
}

func applyDateRangeFilter(query *gorm.DB, column, startDate, endDate string) *gorm.DB {
	if startDate != "" {
		query = query.Where(fmt.Sprintf("DATE(%s) >= ?", column), startDate)
	}
	if endDate != "" {
		query = query.Where(fmt.Sprintf("DATE(%s) <= ?", column), endDate)
	}
	return query
}

func normalizePagination(page, pageSize int) (int, int) {
	if page < 1 {
		page = 1
	}
	if pageSize < 1 {
		pageSize = 10
	}
	if pageSize > 200 {
		pageSize = 200
	}
	return page, pageSize
}

func roundFloat(value float64, precision int) float64 {
	if precision < 0 {
		return value
	}
	factor := math.Pow10(precision)
	return math.Round(value*factor) / factor
}

func csvFloat(value float64, precision int) string {
	return strconv.FormatFloat(roundFloat(value, precision), 'f', precision, 64)
}

func formatDuration(seconds int) string {
	if seconds < 60 {
		return strconv.Itoa(seconds) + "s"
	}
	return strconv.Itoa(seconds/60) + "min"
}

func formatAlarmStatus(status string) string {
	if status == "resolved" {
		return "已处理"
	}
	return "处理中"
}
