package api

import (
	"net/http"
	"strconv"
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
	var totalDevices, onlineDevices, offlineDevices, alarmDevices int64
	h.db.Model(&model.Device{}).Count(&totalDevices)
	h.db.Model(&model.Device{}).Where("status IN ?", []string{"online", "working", "idle"}).Count(&onlineDevices)
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

	// 今日每小时产量
	productionByHour := h.getProductionByHour()

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"totalDevices":     totalDevices,
			"onlineDevices":    onlineDevices,
			"offlineDevices":   offlineDevices,
			"alarmDevices":     alarmDevices,
			"weeklyEfficiency": weeklyEfficiency,
			"patternUsage":     patternUsage,
			"modelRatio":       modelRatio,
			"topProduction":    topProduction,
			"productionByHour": productionByHour,
		},
		"message": "success",
	})
}

func (h *StatisticsHandler) getWeeklyEfficiency() []gin.H {
	result := make([]gin.H, 0)
	weekdays := []string{"周一", "周二", "周三", "周四", "周五", "周六", "周日"}
	now := time.Now()

	for i := 6; i >= 0; i-- {
		date := now.AddDate(0, 0, -i)
		weekday := int(date.Weekday())
		if weekday == 0 {
			weekday = 7
		}

		var totalRunning, totalTime float64
		h.db.Model(&model.ProductionRecord{}).
			Where("DATE(record_date) = DATE(?)", date).
			Select("COALESCE(SUM(running_time), 0)").Scan(&totalRunning)
		h.db.Model(&model.ProductionRecord{}).
			Where("DATE(record_date) = DATE(?)", date).
			Select("COALESCE(SUM(running_time + idle_time), 0)").Scan(&totalTime)

		efficiency := 0.0
		if totalTime > 0 {
			efficiency = (totalRunning / totalTime) * 100
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
		Limit(4).
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
		patternUsage = []gin.H{
			{"name": "Pattern-001", "value": 35},
			{"name": "Pattern-002", "value": 28},
			{"name": "Pattern-003", "value": 22},
			{"name": "其他", "value": 15},
		}
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

func (h *StatisticsHandler) getProductionByHour() []gin.H {
	// 模拟每小时产量数据（实际应从设备实时数据获取）
	hours := []string{"08:00", "09:00", "10:00", "11:00", "12:00", "13:00", "14:00", "15:00", "16:00", "17:00"}
	values := []int{120, 180, 200, 190, 80, 150, 210, 195, 185, 160}

	result := make([]gin.H, 0)
	for i, hour := range hours {
		result = append(result, gin.H{
			"hour":  hour,
			"value": values[i],
		})
	}
	return result
}

func (h *StatisticsHandler) GetDashboardData(c *gin.Context) {
	deviceId := c.Query("deviceId")

	var totalPieces int
	var threadLength float64
	var runningTime, idleTime float64

	query := h.db.Model(&model.ProductionRecord{}).
		Where("record_date >= ?", time.Now().AddDate(0, 0, -7))

	if deviceId != "" {
		query = query.Where("device_id = ?", deviceId)
	}

	query.Select("COALESCE(SUM(pieces), 0)").Scan(&totalPieces)
	query.Select("COALESCE(SUM(thread_length), 0)").Scan(&threadLength)
	query.Select("COALESCE(SUM(running_time), 0)").Scan(&runningTime)
	query.Select("COALESCE(SUM(idle_time), 0)").Scan(&idleTime)

	utilizationRate := 0.0
	totalTime := runningTime + idleTime
	if totalTime > 0 {
		utilizationRate = (runningTime / totalTime) * 100
	}

	// 每小时产量
	hourlyProduction := h.getHourlyProduction(deviceId)

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"totalPieces":      totalPieces,
			"threadLength":     threadLength,
			"spindleSpeed":     3500, // 实时数据需从设备获取
			"runningTime":      runningTime,
			"processingTime":   runningTime * 0.8,
			"utilizationRate":  utilizationRate,
			"hourlyProduction": hourlyProduction,
		},
		"message": "success",
	})
}

func (h *StatisticsHandler) getHourlyProduction(deviceId string) []gin.H {
	hours := []string{"08:00", "09:00", "10:00", "11:00", "12:00", "13:00", "14:00", "15:00"}
	values := []int{150, 180, 200, 190, 100, 160, 210, 195}

	result := make([]gin.H, 0)
	for i, hour := range hours {
		result = append(result, gin.H{
			"hour":  hour,
			"value": values[i],
		})
	}
	return result
}

func (h *StatisticsHandler) GetSalaryStats(c *gin.Context) {
	startDate := c.Query("startDate")
	endDate := c.Query("endDate")
	employeeId := c.Query("employeeId")
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("pageSize", "10"))

	query := h.db.Table("salary_records sr").
		Select(`sr.*, e.name as employee_name, e.code as employee_code, d.name as device_name`).
		Joins("LEFT JOIN employees e ON sr.employee_id = e.id").
		Joins("LEFT JOIN devices d ON sr.device_id = d.id")

	if startDate != "" {
		query = query.Where("sr.record_date >= ?", startDate)
	}
	if endDate != "" {
		query = query.Where("sr.record_date <= ?", endDate)
	}
	if employeeId != "" {
		query = query.Where("sr.employee_id = ?", employeeId)
	}

	var total int64
	query.Count(&total)

	var results []struct {
		model.SalaryRecord
		EmployeeName string
		EmployeeCode string
		DeviceName   string
	}

	offset := (page - 1) * pageSize
	query.Offset(offset).Limit(pageSize).Order("sr.record_date DESC").Scan(&results)

	// 汇总统计
	var totalSalary, totalPieces float64
	h.db.Model(&model.SalaryRecord{}).Select("COALESCE(SUM(total_amount), 0)").Scan(&totalSalary)
	h.db.Model(&model.SalaryRecord{}).Select("COALESCE(SUM(pieces), 0)").Scan(&totalPieces)

	var employeeCount int64
	h.db.Model(&model.SalaryRecord{}).Distinct("employee_id").Count(&employeeCount)

	averageSalary := 0.0
	if employeeCount > 0 {
		averageSalary = totalSalary / float64(employeeCount)
	}

	list := make([]gin.H, 0)
	for _, r := range results {
		list = append(list, gin.H{
			"id":           r.ID,
			"employeeName": r.EmployeeName,
			"employeeCode": r.EmployeeCode,
			"deviceName":   r.DeviceName,
			"totalPieces":  r.Pieces,
			"unitPrice":    r.UnitPrice,
			"salary":       r.Salary,
			"bonus":        r.Bonus,
			"totalAmount":  r.TotalAmount,
			"date":         r.RecordDate.Format("2006-01-02"),
		})
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"totalSalary":   totalSalary,
			"totalPieces":   int(totalPieces),
			"averageSalary": averageSalary,
			"list":          list,
			"total":         total,
		},
		"message": "success",
	})
}

func (h *StatisticsHandler) GetSalaryDetail(c *gin.Context) {
	employeeId := c.Query("employeeId")
	startDate := c.Query("startDate")
	endDate := c.Query("endDate")

	query := h.db.Table("salary_records sr").
		Select(`sr.*, e.name as employee_name, d.name as device_name`).
		Joins("LEFT JOIN employees e ON sr.employee_id = e.id").
		Joins("LEFT JOIN devices d ON sr.device_id = d.id")

	if employeeId != "" {
		query = query.Where("sr.employee_id = ?", employeeId)
	}
	if startDate != "" {
		query = query.Where("sr.record_date >= ?", startDate)
	}
	if endDate != "" {
		query = query.Where("sr.record_date <= ?", endDate)
	}

	var results []struct {
		model.SalaryRecord
		EmployeeName string
		DeviceName   string
	}
	query.Order("sr.record_date DESC").Scan(&results)

	list := make([]gin.H, 0)
	for _, r := range results {
		list = append(list, gin.H{
			"employeeName": r.EmployeeName,
			"deviceName":   r.DeviceName,
			"pieces":       r.Pieces,
			"unitPrice":    r.UnitPrice,
			"salary":       r.Salary,
			"bonus":        r.Bonus,
			"totalAmount":  r.TotalAmount,
			"date":         r.RecordDate.Format("2006-01-02"),
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

	query := h.db.Model(&model.ProductionRecord{})

	if startDate != "" {
		query = query.Where("record_date >= ?", startDate)
	}
	if endDate != "" {
		query = query.Where("record_date <= ?", endDate)
	}
	if deviceId != "" {
		query = query.Where("device_id = ?", deviceId)
	}

	var totalPieces int
	var totalThread float64
	var totalRunning, totalIdle float64

	query.Select("COALESCE(SUM(pieces), 0)").Scan(&totalPieces)
	query.Select("COALESCE(SUM(thread_length), 0)").Scan(&totalThread)
	query.Select("COALESCE(SUM(running_time), 0)").Scan(&totalRunning)
	query.Select("COALESCE(SUM(idle_time), 0)").Scan(&totalIdle)

	avgEfficiency := 0.0
	totalTime := totalRunning + totalIdle
	if totalTime > 0 {
		avgEfficiency = (totalRunning / totalTime) * 100
	}

	// 按日期分组的产量趋势
	var dailyData []struct {
		Date   string
		Pieces int
	}
	h.db.Model(&model.ProductionRecord{}).
		Select("DATE(record_date) as date, SUM(pieces) as pieces").
		Where("record_date >= ?", time.Now().AddDate(0, 0, -7)).
		Group("DATE(record_date)").
		Order("date").
		Scan(&dailyData)

	productionTrend := make([]gin.H, 0)
	for _, d := range dailyData {
		productionTrend = append(productionTrend, gin.H{
			"date":  d.Date,
			"value": d.Pieces,
		})
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"totalPieces":     totalPieces,
			"totalThread":     totalThread,
			"totalHours":      totalRunning + totalIdle,
			"avgEfficiency":   avgEfficiency,
			"productionTrend": productionTrend,
		},
		"message": "success",
	})
}

func (h *StatisticsHandler) GetDurationStats(c *gin.Context) {
	startDate := c.Query("startDate")
	endDate := c.Query("endDate")
	deviceId := c.Query("deviceId")

	query := h.db.Model(&model.ProductionRecord{})

	if startDate != "" {
		query = query.Where("record_date >= ?", startDate)
	}
	if endDate != "" {
		query = query.Where("record_date <= ?", endDate)
	}
	if deviceId != "" {
		query = query.Where("device_id = ?", deviceId)
	}

	var totalRunning, totalIdle float64
	query.Select("COALESCE(SUM(running_time), 0)").Scan(&totalRunning)
	query.Select("COALESCE(SUM(idle_time), 0)").Scan(&totalIdle)

	// 报警时长统计
	var alarmDuration int
	alarmQuery := h.db.Model(&model.AlarmRecord{})
	if startDate != "" {
		alarmQuery = alarmQuery.Where("start_time >= ?", startDate)
	}
	if endDate != "" {
		alarmQuery = alarmQuery.Where("start_time <= ?", endDate)
	}
	if deviceId != "" {
		alarmQuery = alarmQuery.Where("device_id = ?", deviceId)
	}
	alarmQuery.Select("COALESCE(SUM(duration), 0)").Scan(&alarmDuration)

	totalTime := totalRunning + totalIdle
	alarmTimeHours := float64(alarmDuration) / 3600.0

	// 按设备分组的时长统计
	var deviceDuration []struct {
		DeviceID    uint
		DeviceName  string
		RunningTime float64
		IdleTime    float64
	}
	h.db.Table("production_records pr").
		Select("pr.device_id, d.name as device_name, SUM(pr.running_time) as running_time, SUM(pr.idle_time) as idle_time").
		Joins("LEFT JOIN devices d ON pr.device_id = d.id").
		Group("pr.device_id, d.name").
		Order("running_time DESC").
		Limit(10).
		Scan(&deviceDuration)

	deviceStats := make([]gin.H, 0)
	for _, d := range deviceDuration {
		deviceStats = append(deviceStats, gin.H{
			"name":        d.DeviceName,
			"runningTime": d.RunningTime,
			"idleTime":    d.IdleTime,
		})
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"totalTime":    totalTime,
			"runningTime":  totalRunning,
			"idleTime":     totalIdle,
			"alarmTime":    alarmTimeHours,
			"deviceStats":  deviceStats,
		},
		"message": "success",
	})
}

func (h *StatisticsHandler) GetAlarmStats(c *gin.Context) {
	startDate := c.Query("startDate")
	endDate := c.Query("endDate")
	deviceId := c.Query("deviceId")
	alarmType := c.Query("alarmType")
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("pageSize", "10"))

	query := h.db.Model(&model.AlarmRecord{})

	if startDate != "" {
		query = query.Where("start_time >= ?", startDate)
	}
	if endDate != "" {
		query = query.Where("start_time <= ?", endDate)
	}
	if deviceId != "" {
		query = query.Where("device_id = ?", deviceId)
	}
	if alarmType != "" {
		query = query.Where("alarm_type = ?", alarmType)
	}

	// 汇总统计
	var totalAlarms int64
	var totalDuration int
	query.Count(&totalAlarms)
	query.Select("COALESCE(SUM(duration), 0)").Scan(&totalDuration)

	var affectedDevices int64
	query.Distinct("device_id").Count(&affectedDevices)

	var resolvedCount int64
	query.Where("status = ?", "resolved").Count(&resolvedCount)

	resolvedRate := 0.0
	if totalAlarms > 0 {
		resolvedRate = float64(resolvedCount) / float64(totalAlarms) * 100
	}

	// 报警类型分布
	var typeDistribution []struct {
		AlarmType string
		Count     int
	}
	h.db.Model(&model.AlarmRecord{}).
		Select("alarm_type, COUNT(*) as count").
		Group("alarm_type").
		Scan(&typeDistribution)

	alarmTypePie := make([]gin.H, 0)
	for _, t := range typeDistribution {
		alarmTypePie = append(alarmTypePie, gin.H{
			"name":  t.AlarmType,
			"value": t.Count,
		})
	}

	// 日报警趋势
	var dailyAlarms []struct {
		Date    string
		Count   int
		AvgTime float64
	}
	h.db.Model(&model.AlarmRecord{}).
		Select("DATE(start_time) as date, COUNT(*) as count, AVG(duration)/60 as avg_time").
		Where("start_time >= ?", time.Now().AddDate(0, 0, -7)).
		Group("DATE(start_time)").
		Order("date").
		Scan(&dailyAlarms)

	alarmTrend := make([]gin.H, 0)
	for _, d := range dailyAlarms {
		alarmTrend = append(alarmTrend, gin.H{
			"date":    d.Date,
			"count":   d.Count,
			"avgTime": d.AvgTime,
		})
	}

	// 报警记录列表
	var records []model.AlarmRecord
	offset := (page - 1) * pageSize
	h.db.Model(&model.AlarmRecord{}).
		Offset(offset).Limit(pageSize).
		Order("start_time DESC").
		Find(&records)

	list := make([]gin.H, 0)
	for _, r := range records {
		var device model.Device
		h.db.First(&device, r.DeviceID)

		endTimeStr := "-"
		if r.EndTime != nil {
			endTimeStr = r.EndTime.Format("2006-01-02 15:04:05")
		}

		list = append(list, gin.H{
			"id":          r.ID,
			"deviceName":  device.Name,
			"alarmType":   r.AlarmType,
			"alarmCode":   r.AlarmCode,
			"description": r.Description,
			"duration":    formatDuration(r.Duration),
			"status":      formatAlarmStatus(r.Status),
			"startTime":   r.StartTime.Format("2006-01-02 15:04:05"),
			"endTime":     endTimeStr,
		})
	}

	var total int64
	h.db.Model(&model.AlarmRecord{}).Count(&total)

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"totalAlarms":     totalAlarms,
			"totalDuration":   totalDuration / 60, // 转换为分钟
			"affectedDevices": affectedDevices,
			"resolvedRate":    resolvedRate,
			"alarmTypePie":    alarmTypePie,
			"alarmTrend":      alarmTrend,
			"list":            list,
			"total":           total,
		},
		"message": "success",
	})
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
