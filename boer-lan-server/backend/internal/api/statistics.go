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

func (h *StatisticsHandler) isSQLite() bool {
	return strings.EqualFold(h.db.Dialector.Name(), "sqlite")
}

func (h *StatisticsHandler) localDateExpr(column string) string {
	if h.isSQLite() {
		return fmt.Sprintf("substr(CAST(%s AS TEXT), 1, 10)", column)
	}
	return fmt.Sprintf("DATE(%s)", column)
}

func formatLocalStatsDate(value time.Time) string {
	if value.IsZero() {
		return ""
	}
	return value.In(time.Local).Format("2006-01-02")
}

func (h *StatisticsHandler) deviceNameExpr(deviceIDExpr, alias string) string {
	expr := fmt.Sprintf("COALESCE(d.name, CONCAT('设备-', %s))", deviceIDExpr)
	if h.isSQLite() {
		expr = fmt.Sprintf("COALESCE(d.name, '设备-' || %s)", deviceIDExpr)
	}
	if alias != "" {
		expr = fmt.Sprintf("%s as %s", expr, alias)
	}
	return expr
}

func (h *StatisticsHandler) alarmInfoAggregateExpr(alias string) string {
	baseExpr := "COALESCE(NULLIF(ar.alarm_type, ''), NULLIF(ar.description, ''), '报警')"
	expr := fmt.Sprintf("COALESCE(GROUP_CONCAT(DISTINCT %s ORDER BY ar.start_time SEPARATOR '、'), '-')", baseExpr)
	if h.isSQLite() {
		expr = fmt.Sprintf("COALESCE(REPLACE(GROUP_CONCAT(DISTINCT %s), ',', '、'), '-')", baseExpr)
	}
	if alias != "" {
		expr = fmt.Sprintf("%s as %s", expr, alias)
	}
	return expr
}

func (h *StatisticsHandler) productionDurationHoursExpr(recordAlias, alias string) string {
	qualified := func(field string) string {
		if strings.TrimSpace(recordAlias) == "" {
			return field
		}
		return recordAlias + "." + field
	}

	startField := qualified("start_time")
	endField := qualified("end_time")
	runningField := qualified("running_time")

	expr := fmt.Sprintf(
		"CASE WHEN %s IS NOT NULL AND %s IS NOT NULL AND julianday(%s) > julianday(%s) THEN (julianday(%s) - julianday(%s)) * 24.0 ELSE COALESCE(%s, 0) END",
		startField,
		endField,
		endField,
		startField,
		endField,
		startField,
		runningField,
	)
	if !h.isSQLite() {
		expr = fmt.Sprintf(
			"CASE WHEN %s IS NOT NULL AND %s IS NOT NULL AND %s > %s THEN TIMESTAMPDIFF(SECOND, %s, %s) / 3600.0 ELSE COALESCE(%s, 0) END",
			startField,
			endField,
			endField,
			startField,
			startField,
			endField,
			runningField,
		)
	}

	if alias != "" {
		expr = fmt.Sprintf("%s as %s", expr, alias)
	}
	return expr
}

func (h *StatisticsHandler) productionRuntimeHoursExpr(recordAlias, alias string) string {
	qualified := func(field string) string {
		if strings.TrimSpace(recordAlias) == "" {
			return field
		}
		return recordAlias + "." + field
	}

	startField := qualified("start_time")
	endField := qualified("end_time")
	runningField := qualified("running_time")
	idleField := qualified("idle_time")

	expr := fmt.Sprintf(
		"CASE WHEN %s IS NOT NULL AND %s IS NOT NULL AND julianday(%s) > julianday(%s) THEN (julianday(%s) - julianday(%s)) * 24.0 ELSE COALESCE(%s, 0) + COALESCE(%s, 0) END",
		startField,
		endField,
		endField,
		startField,
		endField,
		startField,
		runningField,
		idleField,
	)
	if !h.isSQLite() {
		expr = fmt.Sprintf(
			"CASE WHEN %s IS NOT NULL AND %s IS NOT NULL AND %s > %s THEN TIMESTAMPDIFF(SECOND, %s, %s) / 3600.0 ELSE COALESCE(%s, 0) + COALESCE(%s, 0) END",
			startField,
			endField,
			endField,
			startField,
			startField,
			endField,
			runningField,
			idleField,
		)
	}

	if alias != "" {
		expr = fmt.Sprintf("%s as %s", expr, alias)
	}
	return expr
}

func calculateUtilizationRate(processingHours, runtimeHours float64) float64 {
	if processingHours <= 0 || runtimeHours <= 0 {
		return 0
	}
	rate := (processingHours / runtimeHours) * 100
	if rate < 0 {
		return 0
	}
	if rate > 100 {
		return 100
	}
	return rate
}

func resolveProductionDurationHours(startTime, endTime *time.Time, fallback float64) float64 {
	if startTime != nil && endTime != nil && !startTime.IsZero() && !endTime.IsZero() && endTime.After(*startTime) {
		return endTime.Sub(*startTime).Hours()
	}
	if fallback < 0 {
		return 0
	}
	return fallback
}

func (h *StatisticsHandler) GetHomeStats(c *gin.Context) {
	_, scopedDeviceIDs := h.scopeDeviceFilter(c, "", nil)

	// 设备状态统计
	var totalDevices, onlineDevices, workingDevices, offlineDevices, alarmDevices int64
	deviceBaseQuery := h.db.Model(&model.Device{})
	if len(scopedDeviceIDs) > 0 {
		deviceBaseQuery = deviceBaseQuery.Where("id IN ?", scopedDeviceIDs)
	}
	deviceBaseQuery.Session(&gorm.Session{}).Count(&totalDevices)
	deviceBaseQuery.Session(&gorm.Session{}).Where("status IN ?", []string{"online", "working", "idle"}).Count(&onlineDevices)
	deviceBaseQuery.Session(&gorm.Session{}).Where("status = ?", "working").Count(&workingDevices)
	deviceBaseQuery.Session(&gorm.Session{}).Where("status = ?", "offline").Count(&offlineDevices)
	deviceBaseQuery.Session(&gorm.Session{}).Where("status = ?", "alarm").Count(&alarmDevices)

	// 近7日设备使用效率
	weeklyEfficiency := h.getWeeklyEfficiency(scopedDeviceIDs)

	// 花型使用占比
	patternUsage := h.getPatternUsage(scopedDeviceIDs)

	// 设备机型占比
	modelRatio := h.getModelRatio(scopedDeviceIDs)

	// 前三设备生产量
	topProduction := h.getTopProduction(scopedDeviceIDs)

	// 近7日设备使用率明细
	deviceUsage := h.getDeviceUsage(scopedDeviceIDs)

	// 24小时设备运行状态 + 近7日产量
	runningStatusByHour := h.getRunningStatusByHour(scopedDeviceIDs)
	productionByDay := h.getProductionByDay(scopedDeviceIDs)

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
			"deviceUsage":         deviceUsage,
			"runningStatusByHour": runningStatusByHour,
			"productionByDay":     productionByDay,
			// 兼容旧前端字段名
			"productionByHour": productionByDay,
		},
		"message": "success",
	})
}

func (h *StatisticsHandler) getWeeklyEfficiency(deviceIDs []uint) []gin.H {
	result := make([]gin.H, 0)
	weekdays := []string{"周一", "周二", "周三", "周四", "周五", "周六", "周日"}
	now := time.Now()
	var totalDevices int64
	totalDeviceQuery := h.db.Model(&model.Device{})
	if len(deviceIDs) > 0 {
		totalDeviceQuery = totalDeviceQuery.Where("id IN ?", deviceIDs)
	}
	totalDeviceQuery.Count(&totalDevices)

	for i := 6; i >= 0; i-- {
		date := now.AddDate(0, 0, -i)
		weekday := int(date.Weekday())
		if weekday == 0 {
			weekday = 7
		}

		var rows []struct {
			DeviceID       uint
			ProcessingTime float64
			RunningTime    float64
		}
		dateExpr := h.localDateExpr("record_date")
		runtimeExpr := h.productionRuntimeHoursExpr("", "")
		dailyQuery := h.db.Model(&model.ProductionRecord{}).
			Select(fmt.Sprintf("device_id, COALESCE(SUM(running_time), 0) as processing_time, COALESCE(SUM(%s), 0) as running_time", runtimeExpr)).
			Where(fmt.Sprintf("%s = ?", dateExpr), formatLocalStatsDate(date))
		if len(deviceIDs) > 0 {
			dailyQuery = dailyQuery.Where("device_id IN ?", deviceIDs)
		}
		dailyQuery.Group("device_id").Scan(&rows)

		efficiency := 0.0
		validDeviceCount := 0
		for _, row := range rows {
			if row.RunningTime <= 0 {
				continue
			}
			efficiency += calculateUtilizationRate(row.ProcessingTime, row.RunningTime)
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

func (h *StatisticsHandler) getPatternUsage(deviceIDs []uint) []gin.H {
	var results []struct {
		PatternID uint
		Name      string
		Count     int
	}

	patternNameExpr := "COALESCE(NULLIF(TRIM(pr.pattern_name), ''), NULLIF(TRIM(p.name), ''), '未知花型')"
	query := h.db.Table("production_records pr").
		Select(patternNameExpr + " as name, pr.pattern_id, COUNT(*) as count").
		Joins("LEFT JOIN patterns p ON pr.pattern_id = p.id").
		Where("pr.pattern_id IS NOT NULL")
	if len(deviceIDs) > 0 {
		query = query.Where("pr.device_id IN ?", deviceIDs)
	}
	query.
		Group("pr.pattern_id, " + patternNameExpr).
		Order("count DESC, name ASC").
		Limit(10).
		Scan(&results)

	patternUsage := make([]gin.H, 0, len(results))
	for _, r := range results {
		name := strings.TrimSpace(r.Name)
		if name == "" {
			name = "未知花型"
		}
		patternUsage = append(patternUsage, gin.H{
			"name":  name,
			"value": r.Count,
		})
	}

	return patternUsage
}

func (h *StatisticsHandler) getModelRatio(deviceIDs []uint) []gin.H {
	var results []struct {
		Model string
		Count int
	}

	modelExpr := "COALESCE(NULLIF(model_name, ''), '未设置型号')"
	query := h.db.Model(&model.Device{})
	if len(deviceIDs) > 0 {
		query = query.Where("id IN ?", deviceIDs)
	}
	query.
		Select(modelExpr + " as model, COUNT(*) as count").
		Group(modelExpr).
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

func (h *StatisticsHandler) getTopProduction(deviceIDs []uint) []gin.H {
	var results []struct {
		DeviceID uint
		Name     string
		Total    int
	}

	dateExpr := h.localDateExpr("pr.record_date")
	query := h.db.Table("production_records pr").
		Select("pr.device_id, d.name, SUM(pr.pieces) as total").
		Joins("LEFT JOIN devices d ON pr.device_id = d.id").
		Where(fmt.Sprintf("%s >= ?", dateExpr), formatLocalStatsDate(time.Now().AddDate(0, 0, -6)))
	if len(deviceIDs) > 0 {
		query = query.Where("pr.device_id IN ?", deviceIDs)
	}
	query.Group("pr.device_id, d.name").Order("total DESC").Limit(3).Scan(&results)

	topProduction := make([]gin.H, 0)
	for _, r := range results {
		topProduction = append(topProduction, gin.H{
			"name":  r.Name,
			"value": r.Total,
		})
	}

	return topProduction
}

func (h *StatisticsHandler) getDeviceUsage(deviceIDs []uint) []gin.H {
	startDate := time.Now().AddDate(0, 0, -6).Format("2006-01-02")

	var rows []struct {
		DeviceID       uint
		Name           string
		GroupID        *uint
		GroupName      string
		ProcessingTime float64
		RunningTime    float64
		IdleTime       float64
	}

	dateExpr := h.localDateExpr("pr.record_date")
	runtimeExpr := h.productionRuntimeHoursExpr("pr", "")
	query := h.db.Table("devices d").
		Select(fmt.Sprintf("d.id as device_id, %s, d.group_id, g.name as group_name, COALESCE(SUM(pr.running_time), 0) as processing_time, COALESCE(SUM(%s), 0) as running_time, COALESCE(SUM(pr.idle_time), 0) as idle_time", h.deviceNameExpr("d.id", "name"), runtimeExpr)).
		Joins(fmt.Sprintf("LEFT JOIN production_records pr ON pr.device_id = d.id AND %s >= ?", dateExpr), startDate).
		Joins("LEFT JOIN groups g ON d.group_id = g.id").
		Where("d.deleted_at IS NULL")
	if len(deviceIDs) > 0 {
		query = query.Where("d.id IN ?", deviceIDs)
	}
	query.
		Group("d.id, d.name, d.group_id, g.name").
		Order("running_time DESC, idle_time ASC").
		Scan(&rows)

	lineByGroupID := h.buildLineMetaByGroupID()
	result := make([]gin.H, 0, len(rows))
	for _, row := range rows {
		efficiency := calculateUtilizationRate(row.ProcessingTime, row.RunningTime)
		lineMeta := groupLineMeta{}
		if row.GroupID != nil {
			lineMeta = lineByGroupID[*row.GroupID]
		}
		result = append(result, gin.H{
			"deviceId":       row.DeviceID,
			"name":           row.Name,
			"groupId":        row.GroupID,
			"groupName":      row.GroupName,
			"lineId":         lineMeta.ID,
			"lineName":       lineMeta.Name,
			"processingTime": roundFloat(row.ProcessingTime, 2),
			"runningTime":    roundFloat(row.RunningTime, 2),
			"idleTime":       roundFloat(row.IdleTime, 2),
			"efficiency":     roundFloat(efficiency, 2),
		})
	}

	return result
}

type groupLineMeta struct {
	ID   uint
	Name string
}

func (h *StatisticsHandler) buildLineMetaByGroupID() map[uint]groupLineMeta {
	var groups []model.Group
	if err := h.db.Select("id", "name", "parent_id").Find(&groups).Error; err != nil {
		return map[uint]groupLineMeta{}
	}

	byID := make(map[uint]model.Group, len(groups))
	for _, group := range groups {
		byID[group.ID] = group
	}

	result := make(map[uint]groupLineMeta, len(groups))
	for _, group := range groups {
		chain := make([]model.Group, 0, 4)
		current := group
		visited := map[uint]struct{}{}
		for {
			if _, ok := visited[current.ID]; ok {
				break
			}
			visited[current.ID] = struct{}{}
			chain = append(chain, current)
			if current.ParentID == nil {
				break
			}
			parent, ok := byID[*current.ParentID]
			if !ok {
				break
			}
			current = parent
		}

		for i, j := 0, len(chain)-1; i < j; i, j = i+1, j-1 {
			chain[i], chain[j] = chain[j], chain[i]
		}
		if len(chain) > 1 && chain[0].ParentID == nil {
			chain = chain[1:]
		}
		if len(chain) == 0 {
			continue
		}
		result[group.ID] = groupLineMeta{ID: chain[0].ID, Name: chain[0].Name}
	}

	return result
}

func (h *StatisticsHandler) getRunningStatusByHour(deviceIDs []uint) []gin.H {
	var totalDevices int64
	totalDeviceQuery := h.db.Model(&model.Device{})
	if len(deviceIDs) > 0 {
		totalDeviceQuery = totalDeviceQuery.Where("id IN ?", deviceIDs)
	}
	totalDeviceQuery.Count(&totalDevices)
	total := int(totalDevices)
	if total < 0 {
		total = 0
	}

	var currentOnlineDevices int64
	onlineQuery := h.db.Model(&model.Device{}).Where("status IN ?", []string{"online", "working", "idle"})
	if len(deviceIDs) > 0 {
		onlineQuery = onlineQuery.Where("id IN ?", deviceIDs)
	}
	onlineQuery.Count(&currentOnlineDevices)

	now := time.Now()

	var records []struct {
		DeviceID   uint
		RecordDate time.Time
	}
	dateExpr := h.localDateExpr("record_date")
	hourlyQuery := h.db.Model(&model.ProductionRecord{}).
		Select("device_id, record_date").
		Where(fmt.Sprintf("%s = ?", dateExpr), formatLocalStatsDate(now))
	if len(deviceIDs) > 0 {
		hourlyQuery = hourlyQuery.Where("device_id IN ?", deviceIDs)
	}
	hourlyQuery.Scan(&records)

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

func (h *StatisticsHandler) getProductionByDay(deviceIDs []uint) []gin.H {
	startDate := time.Now().AddDate(0, 0, -6).Format("2006-01-02")
	var rows []struct {
		Date  string
		Value int
	}

	dateExpr := h.localDateExpr("record_date")
	query := h.db.Model(&model.ProductionRecord{}).
		Select(fmt.Sprintf("%s as date, COALESCE(SUM(pieces), 0) as value", dateExpr)).
		Where(fmt.Sprintf("%s >= ?", dateExpr), startDate)
	if len(deviceIDs) > 0 {
		query = query.Where("device_id IN ?", deviceIDs)
	}
	query.Group(dateExpr).Order("date ASC").Scan(&rows)

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
	deviceId, deviceIDs = h.scopeDeviceFilter(c, deviceId, deviceIDs)

	var totalPieces int
	var todayPieces int
	var threadLength float64
	var avgUsedThreadLength float64

	var todayProcessingTime, todayRuntimeTime float64
	runtimeExpr := h.productionRuntimeHoursExpr("", "")
	summaryQuery := applyDashboardDeviceFilter(h.db.Model(&model.ProductionRecord{}), deviceId, deviceIDs)
	summaryQuery.Select("COALESCE(SUM(pieces), 0)").Scan(&totalPieces)
	summaryQuery.Select("COALESCE(SUM(thread_length), 0)").Scan(&threadLength)

	recordDateExpr := h.localDateExpr("record_date")
	todayQuery := applyDashboardDeviceFilter(
		h.db.Model(&model.ProductionRecord{}).Where(fmt.Sprintf("%s = ?", recordDateExpr), formatLocalStatsDate(time.Now())),
		deviceId,
		deviceIDs,
	)
	todayQuery.Select("COALESCE(SUM(pieces), 0)").Scan(&todayPieces)
	todayQuery.Select("COALESCE(SUM(running_time), 0)").Scan(&todayProcessingTime)
	todayQuery.Select(fmt.Sprintf("COALESCE(SUM(%s), 0)", runtimeExpr)).Scan(&todayRuntimeTime)

	runtimeTime := todayRuntimeTime
	processingTime := todayProcessingTime
	usedThreadLength := threadLength
	deviceCount := h.countDashboardScopeDevices(deviceId, deviceIDs)
	onlineDeviceCount := h.countDashboardOnlineDevices(deviceId, deviceIDs)
	todayAlarmCount, totalAlarmCount := h.countDashboardAlarms(deviceId, deviceIDs)
	isAggregateScope := strings.TrimSpace(deviceId) == "" && deviceCount > 0
	if isAggregateScope {
		runtimeTime = todayRuntimeTime / float64(deviceCount)
		processingTime = todayProcessingTime / float64(deviceCount)
	}
	utilizationRate := h.getTodayUtilizationRate(deviceId, deviceIDs, deviceCount)
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
			"runningTime":            roundFloat(runtimeTime, 2),
			"processingTime":         roundFloat(processingTime, 2),
			"utilizationRate":        roundFloat(utilizationRate, 2),
			"todayAlarmCount":        todayAlarmCount,
			"totalAlarmCount":        totalAlarmCount,
			"onlineDeviceCount":      onlineDeviceCount,
			"scopeDeviceCount":       deviceCount,
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

func applyDashboardAlarmFilter(query *gorm.DB, deviceId string, deviceIDs []uint) *gorm.DB {
	if strings.TrimSpace(deviceId) != "" {
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

func (h *StatisticsHandler) countDashboardOnlineDevices(deviceId string, deviceIDs []uint) int64 {
	query := h.db.Model(&model.Device{}).
		Where("status IN ?", []string{"online", "working", "idle"})

	if strings.TrimSpace(deviceId) != "" {
		var count int64
		query.Where("id = ?", deviceId).Count(&count)
		return count
	}
	if len(deviceIDs) > 0 {
		var count int64
		query.Where("id IN ?", deviceIDs).Count(&count)
		return count
	}

	var count int64
	query.Count(&count)
	return count
}

func (h *StatisticsHandler) countDashboardAlarms(deviceId string, deviceIDs []uint) (int64, int64) {
	baseQuery := applyDashboardAlarmFilter(h.db.Model(&model.AlarmRecord{}), deviceId, deviceIDs)

	var totalCount int64
	baseQuery.Session(&gorm.Session{}).Count(&totalCount)

	var todayCount int64
	startDateExpr := h.localDateExpr("start_time")
	baseQuery.Session(&gorm.Session{}).
		Where(fmt.Sprintf("%s = ?", startDateExpr), formatLocalStatsDate(time.Now())).
		Count(&todayCount)

	return todayCount, totalCount
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

	durationExpr := h.productionDurationHoursExpr("", "")
	applyDashboardDeviceFilter(h.db.Model(&model.ProductionRecord{}), deviceId, deviceIDs).
		Select(fmt.Sprintf("device_id, stitches, %s as running_time", durationExpr)).
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
	recordDateExpr := h.localDateExpr("record_date")
	query := applyDashboardDeviceFilter(h.db.Model(&model.ProductionRecord{}), deviceId, deviceIDs).
		Select(fmt.Sprintf("%s as date, COALESCE(SUM(pieces), 0) as value", recordDateExpr)).
		Where(fmt.Sprintf("%s >= ?", recordDateExpr), startDate)

	var rows []struct {
		Date  string
		Value int
	}
	query.Group(recordDateExpr).
		Order("date ASC").
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
	runtimeExpr := h.productionRuntimeHoursExpr("", "")
	if strings.TrimSpace(deviceId) != "" {
		var totals struct {
			Processing float64
			Runtime    float64
		}
		applyDashboardDeviceFilter(
			h.db.Model(&model.ProductionRecord{}).Where(fmt.Sprintf("%s = ?", h.localDateExpr("record_date")), formatLocalStatsDate(time.Now())),
			deviceId,
			deviceIDs,
		).
			Select(fmt.Sprintf("COALESCE(SUM(running_time), 0) as processing, COALESCE(SUM(%s), 0) as runtime", runtimeExpr)).
			Scan(&totals)
		return roundFloat(calculateUtilizationRate(totals.Processing, totals.Runtime), 2)
	}

	var rows []struct {
		Processing float64
		Runtime    float64
	}
	applyDashboardDeviceFilter(
		h.db.Model(&model.ProductionRecord{}).Where(fmt.Sprintf("%s = ?", h.localDateExpr("record_date")), formatLocalStatsDate(time.Now())),
		deviceId,
		deviceIDs,
	).
		Select(fmt.Sprintf("COALESCE(SUM(running_time), 0) as processing, COALESCE(SUM(%s), 0) as runtime", runtimeExpr)).
		Group("device_id").
		Scan(&rows)

	totalUtilization := 0.0
	for _, row := range rows {
		if row.Runtime <= 0 {
			continue
		}
		totalUtilization += calculateUtilizationRate(row.Processing, row.Runtime)
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
	recordDateExpr := h.localDateExpr("record_date")
	runtimeExpr := h.productionRuntimeHoursExpr("", "")
	query := applyDashboardDeviceFilter(h.db.Model(&model.ProductionRecord{}), deviceId, deviceIDs).
		Select(fmt.Sprintf("%s as date, device_id, COALESCE(SUM(running_time), 0) as processing_time, COALESCE(SUM(%s), 0) as running_time", recordDateExpr, runtimeExpr)).
		Where(fmt.Sprintf("%s >= ?", recordDateExpr), startDate)

	var rows []struct {
		Date           string
		DeviceID       uint
		ProcessingTime float64
		RunningTime    float64
	}
	query.Group(recordDateExpr + ", device_id").
		Order("date ASC").
		Scan(&rows)

	type dayAgg struct {
		RuntimeTotal    float64
		ProcessingTotal float64
		UtilSum         float64
		UtilCount       int64
	}
	rowMap := make(map[string]dayAgg, len(rows))
	for _, row := range rows {
		agg := rowMap[row.Date]
		agg.RuntimeTotal += row.RunningTime
		agg.ProcessingTotal += row.ProcessingTime
		if row.RunningTime > 0 {
			agg.UtilSum += calculateUtilizationRate(row.ProcessingTime, row.RunningTime)
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

		runningBase := item.RuntimeTotal
		processingBase := item.ProcessingTotal
		if avgDeviceCount > 0 {
			runningBase = runningBase / float64(avgDeviceCount)
			processingBase = processingBase / float64(avgDeviceCount)
		}

		running := roundFloat(runningBase, 2)
		processing := roundFloat(processingBase, 2)
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
	deviceId, deviceIDs = h.scopeDeviceFilter(c, deviceId, deviceIDs)
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("pageSize", "10"))
	page, pageSize = normalizePagination(page, pageSize)

	baseQuery := h.buildSalaryStatsBaseQuery(startDate, endDate, employeeId, employeeKeyword, deviceId, deviceIDs)

	var total int64
	baseQuery.Session(&gorm.Session{}).Count(&total)
	fallbackRange := gin.H{}
	if total == 0 && c.Query("fallbackLatest") == "1" {
		latestQuery := h.buildSalaryStatsBaseQuery("1970-01-01", "2999-12-31", employeeId, employeeKeyword, deviceId, deviceIDs)
		var latestRow struct {
			LatestDate *time.Time
		}
		latestQuery.Session(&gorm.Session{}).
			Select("MAX(sr.record_date) as latest_date").
			Scan(&latestRow)
		if latestRow.LatestDate != nil {
			latestEndDate := formatLocalStatsDate(*latestRow.LatestDate)
			latestStartDate := formatLocalStatsDate(latestRow.LatestDate.AddDate(0, 0, -29))
			baseQuery = h.buildSalaryStatsBaseQuery(latestStartDate, latestEndDate, employeeId, employeeKeyword, deviceId, deviceIDs)
			baseQuery.Session(&gorm.Session{}).Count(&total)
			if total > 0 {
				startDate = latestStartDate
				endDate = latestEndDate
				fallbackRange = gin.H{
					"startDate": startDate,
					"endDate":   endDate,
				}
			}
		}
	}

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
		Select("sr.*, COALESCE(NULLIF(sr.pattern_name, ''), p.name, '') as pattern_name, COALESCE(NULLIF(sr.order_no, ''), p.order_no, '') as order_no, e.name as employee_name, e.code as employee_code, d.name as device_name").
		Joins("LEFT JOIN patterns p ON sr.pattern_id = p.id").
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
			"workDays":     1,
			"patternName":  strings.TrimSpace(r.PatternName),
			"orderNo":      strings.TrimSpace(r.OrderNo),
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
	salaryDateExpr := h.localDateExpr("sr.record_date")
	baseQuery.Session(&gorm.Session{}).
		Select(fmt.Sprintf("%s as date, COALESCE(SUM(sr.total_amount), 0) as total_amount", salaryDateExpr)).
		Group(salaryDateExpr).
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
			"fallbackRange": fallbackRange,
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
	deviceId, deviceIDs = h.scopeDeviceFilter(c, deviceId, deviceIDs)

	query := h.buildProductionStatsBaseQuery(startDate, endDate, deviceId, deviceIDs)
	if employeeId != "" {
		query = query.Where("pr.employee_id = ?", employeeId)
	}
	if date != "" {
		query = query.Where(fmt.Sprintf("%s = ?", h.localDateExpr("pr.record_date")), date)
	}

	var results []struct {
		model.ProductionRecord
		DeviceName      string
		PatternName     string
		PatternStitches int64
		UnitPrice       float64
		OrderNo         string
		DurationHours   float64
	}
	query.Session(&gorm.Session{}).
		Select(fmt.Sprintf("pr.*, %s, COALESCE(NULLIF(pr.pattern_name, ''), p.name, '未命名花型') as pattern_name, COALESCE(NULLIF(p.stitches, 0), pr.stitches, 0) as pattern_stitches, COALESCE(NULLIF(pr.unit_price, 0), p.unit_price, 0) as unit_price, COALESCE(NULLIF(pr.order_no, ''), p.order_no, '') as order_no, %s", h.deviceNameExpr("pr.device_id", "device_name"), h.productionDurationHoursExpr("pr", "duration_hours"))).
		Joins("LEFT JOIN devices d ON pr.device_id = d.id").
		Joins("LEFT JOIN patterns p ON pr.pattern_id = p.id").
		Order("pr.record_date DESC, pr.created_at DESC, pr.id DESC").
		Scan(&results)

	list := make([]gin.H, 0, len(results))
	for _, r := range results {
		runningHours := resolveProductionDurationHours(r.StartTime, r.EndTime, r.DurationHours)
		startTime, endTime := deriveProductionTimeRange(r.RecordDate, r.CreatedAt, runningHours)
		if r.StartTime != nil && !r.StartTime.IsZero() {
			startTime = *r.StartTime
		}
		if r.EndTime != nil && !r.EndTime.IsZero() {
			endTime = *r.EndTime
		}
		totalAmount := roundFloat(float64(r.Pieces)*r.UnitPrice, 2)
		avgSewDuration := 0.0
		if r.Pieces > 0 {
			avgSewDuration = (runningHours * 60) / float64(r.Pieces)
		}
		list = append(list, gin.H{
			"id":              r.ID,
			"deviceName":      r.DeviceName,
			"patternName":     r.PatternName,
			"patternStitches": r.PatternStitches,
			"startTime":       startTime.Format("2006-01-02 15:04:05"),
			"endTime":         endTime.Format("2006-01-02 15:04:05"),
			"sewCount":        r.Pieces,
			"sewDuration":     roundFloat(runningHours, 2),
			"avgSewDuration":  roundFloat(avgSewDuration, 2),
			"unitPrice":       roundFloat(r.UnitPrice, 3),
			"orderNo":         strings.TrimSpace(r.OrderNo),
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
	deviceId, deviceIDs = h.scopeDeviceFilter(c, deviceId, deviceIDs)
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("pageSize", "10"))
	page, pageSize = normalizePagination(page, pageSize)

	baseQuery := h.buildProductionStatsBaseQuery(startDate, endDate, deviceId, deviceIDs)
	runtimeExpr := h.productionRuntimeHoursExpr("pr", "")

	var summaryRow struct {
		TotalPieces     int64
		TotalThread     float64
		TotalProcessing float64
		TotalRuntime    float64
	}
	baseQuery.Session(&gorm.Session{}).
		Select(fmt.Sprintf("COALESCE(SUM(pr.pieces), 0) as total_pieces, COALESCE(SUM(pr.thread_length), 0) as total_thread, COALESCE(SUM(pr.running_time), 0) as total_processing, COALESCE(SUM(%s), 0) as total_runtime", runtimeExpr)).
		Scan(&summaryRow)

	totalHours := summaryRow.TotalRuntime
	avgEfficiency := roundFloat(calculateUtilizationRate(summaryRow.TotalProcessing, summaryRow.TotalRuntime), 2)

	var total int64
	baseQuery.Session(&gorm.Session{}).Count(&total)

	var listRows []struct {
		model.ProductionRecord
		DeviceName      string
		EmployeeCode    string
		EmployeeName    string
		PatternName     string
		PatternStitches int64
		DurationHours   float64
	}
	offset := (page - 1) * pageSize
	baseQuery.Session(&gorm.Session{}).
		Select(fmt.Sprintf("pr.*, %s, COALESCE(e.code, '-') as employee_code, COALESCE(e.name, '-') as employee_name, COALESCE(NULLIF(pr.pattern_name, ''), p.name, '未命名花型') as pattern_name, COALESCE(NULLIF(p.stitches, 0), pr.stitches, 0) as pattern_stitches, %s", h.deviceNameExpr("pr.device_id", "device_name"), h.productionDurationHoursExpr("pr", "duration_hours"))).
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
		runningHours := resolveProductionDurationHours(row.StartTime, row.EndTime, row.DurationHours)
		efficiency := calculateUtilizationRate(row.RunningTime, runningHours)
		startTime, _ := deriveProductionTimeRange(row.RecordDate, row.CreatedAt, runningHours)
		sewSpeed := 0.0
		if runningHours > 0 {
			sewSpeed = float64(row.Stitches) / (runningHours * 60)
		}
		avgProcessDuration := 0.0
		if row.Pieces > 0 {
			avgProcessDuration = (runningHours * 60) / float64(row.Pieces)
		}
		dateKey := row.RecordDate.Format("2006-01-02")
		patternCountKey := makeDevicePatternDateKey(row.DeviceID, row.PatternID, row.PatternName, dateKey)
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
			"cumulativeUpTime":   roundFloat(runningHours, 2),
			"totalPieces":        row.Pieces,
			"totalStitches":      row.Stitches,
			"threadLength":       roundFloat(row.ThreadLength, 2),
			"runningTime":        roundFloat(row.RunningTime, 2),
			"efficiency":         roundFloat(efficiency, 2),
		})
	}

	var trendRows []struct {
		Date           string
		Pieces         int64
		ProcessingTime float64
		RunningTime    float64
	}
	productionDateExpr := h.localDateExpr("pr.record_date")
	baseQuery.Session(&gorm.Session{}).
		Select(fmt.Sprintf("%s as date, COALESCE(SUM(pr.pieces), 0) as pieces, COALESCE(SUM(pr.running_time), 0) as processing_time, COALESCE(SUM(%s), 0) as running_time", productionDateExpr, runtimeExpr)).
		Group(productionDateExpr).
		Order("date").
		Scan(&trendRows)

	productionTrend := make([]gin.H, 0, len(trendRows))
	for _, row := range trendRows {
		efficiency := calculateUtilizationRate(row.ProcessingTime, row.RunningTime)
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
		Select(fmt.Sprintf("%s, COALESCE(SUM(pr.pieces), 0) as value", h.deviceNameExpr("pr.device_id", "name"))).
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

func (h *StatisticsHandler) GetDevicePatternStats(c *gin.Context) {
	startDate := c.Query("startDate")
	endDate := c.Query("endDate")
	deviceId := c.Query("deviceId")
	deviceIDs := parseDeviceIDs(c.Query("deviceIds"))
	deviceId, deviceIDs = h.scopeDeviceFilter(c, deviceId, deviceIDs)

	query := h.buildProductionStatsBaseQuery(startDate, endDate, deviceId, deviceIDs)

	var rows []struct {
		model.ProductionRecord
		DeviceName    string
		PatternName   string
		DurationHours float64
	}
	query.Session(&gorm.Session{}).
		Select(fmt.Sprintf("pr.*, %s, COALESCE(NULLIF(pr.pattern_name, ''), p.name, '未命名花型') as pattern_name, %s", h.deviceNameExpr("pr.device_id", "device_name"), h.productionDurationHoursExpr("pr", "duration_hours"))).
		Joins("LEFT JOIN devices d ON pr.device_id = d.id").
		Joins("LEFT JOIN patterns p ON pr.pattern_id = p.id").
		Order("COALESCE(pr.end_time, pr.start_time, pr.created_at) ASC, pr.id ASC").
		Scan(&rows)

	type aggregateRow struct {
		Key           string
		PatternNo     uint
		PatternName   string
		TotalPieces   int
		TotalDuration float64
		LastCompleted time.Time
		RecentInfo    string
	}

	aggregates := make(map[string]*aggregateRow)
	pieceCounters := make(map[string]int)
	aggregateList := make([]*aggregateRow, 0)
	detailList := make([]gin.H, 0, len(rows))

	for _, row := range rows {
		patternNo := row.PatternNo
		patternName := strings.TrimSpace(row.PatternName)
		if patternName == "" {
			patternName = "未命名花型"
		}

		key := fmt.Sprintf("id:%d", row.PatternID)
		if row.PatternID == 0 {
			key = "name:" + strings.ToLower(patternName)
		}

		item, exists := aggregates[key]
		if !exists {
			item = &aggregateRow{
				Key:         key,
				PatternNo:   patternNo,
				PatternName: patternName,
			}
			aggregates[key] = item
			aggregateList = append(aggregateList, item)
		}

		runningHours := resolveProductionDurationHours(row.StartTime, row.EndTime, row.DurationHours)
		item.TotalPieces += row.Pieces
		item.TotalDuration += runningHours

		completedAt := row.CreatedAt
		if row.EndTime != nil && !row.EndTime.IsZero() {
			completedAt = *row.EndTime
		} else if row.StartTime != nil && !row.StartTime.IsZero() {
			completedAt = *row.StartTime
		}
		if completedAt.After(item.LastCompleted) {
			item.LastCompleted = completedAt
			item.RecentInfo = fmt.Sprintf("设备:%s / 工号:%s / 停止原因:%d", strings.TrimSpace(row.DeviceName), strings.TrimSpace(row.ProtocolUserID), row.StopReason)
		}

		startTime, endTime := deriveProductionTimeRange(row.RecordDate, row.CreatedAt, runningHours)
		if row.StartTime != nil && !row.StartTime.IsZero() {
			startTime = *row.StartTime
		}
		if row.EndTime != nil && !row.EndTime.IsZero() {
			endTime = *row.EndTime
		}

		pieceCounters[key] += 1
		detailList = append(detailList, gin.H{
			"id":            row.ID,
			"patternNo":     patternNo,
			"patternName":   patternName,
			"pieceIndex":    pieceCounters[key],
			"startTime":     startTime.Format("2006-01-02 15:04:05"),
			"endTime":       endTime.Format("2006-01-02 15:04:05"),
			"durationHours": roundFloat(runningHours, 3),
			"durationText":  formatStatsDurationHours(runningHours),
		})
	}

	sort.Slice(aggregateList, func(i, j int) bool {
		if aggregateList[i].LastCompleted.Equal(aggregateList[j].LastCompleted) {
			return aggregateList[i].PatternName < aggregateList[j].PatternName
		}
		return aggregateList[i].LastCompleted.After(aggregateList[j].LastCompleted)
	})

	aggregateTable := make([]gin.H, 0, len(aggregateList))
	for _, row := range aggregateList {
		lastCompleted := "-"
		if !row.LastCompleted.IsZero() {
			lastCompleted = row.LastCompleted.Format("2006-01-02 15:04:05")
		}
		aggregateTable = append(aggregateTable, gin.H{
			"patternNo":         row.PatternNo,
			"patternName":       row.PatternName,
			"totalPieces":       row.TotalPieces,
			"totalDuration":     roundFloat(row.TotalDuration, 3),
			"totalDurationText": formatStatsDurationHours(row.TotalDuration),
			"lastCompleted":     lastCompleted,
			"recentInfo":        row.RecentInfo,
		})
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"patternTable": aggregateTable,
			"detailTable":  detailList,
		},
		"message": "success",
	})
}

func (h *StatisticsHandler) GetDurationStats(c *gin.Context) {
	startDate := c.Query("startDate")
	endDate := c.Query("endDate")
	deviceId := c.Query("deviceId")
	deviceIDs := parseDeviceIDs(c.Query("deviceIds"))
	deviceId, deviceIDs = h.scopeDeviceFilter(c, deviceId, deviceIDs)
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("pageSize", "10"))
	page, pageSize = normalizePagination(page, pageSize)

	baseProdQuery := h.buildProductionStatsBaseQuery(startDate, endDate, deviceId, deviceIDs)
	baseAlarmQuery := h.buildAlarmStatsBaseQuery(startDate, endDate, deviceId, deviceIDs, "")
	durationExpr := h.productionDurationHoursExpr("pr", "")

	var prodSummary struct {
		RunningTime float64
		IdleTime    float64
	}
	baseProdQuery.Session(&gorm.Session{}).
		Select(fmt.Sprintf("COALESCE(SUM(%s), 0) as running_time, COALESCE(SUM(pr.idle_time), 0) as idle_time", durationExpr)).
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
		DeviceName    string
		EmployeeCode  string
		EmployeeName  string
		PatternName   string
		DurationHours float64
	}
	offset := (page - 1) * pageSize
	baseProdQuery.Session(&gorm.Session{}).
		Select(fmt.Sprintf("pr.*, %s, COALESCE(e.code, '-') as employee_code, COALESCE(e.name, '-') as employee_name, COALESCE(NULLIF(pr.pattern_name, ''), p.name, '未命名花型') as pattern_name, %s", h.deviceNameExpr("pr.device_id", "device_name"), h.productionDurationHoursExpr("pr", "duration_hours"))).
		Joins("LEFT JOIN devices d ON pr.device_id = d.id").
		Joins("LEFT JOIN employees e ON pr.employee_id = e.id").
		Joins("LEFT JOIN patterns p ON pr.pattern_id = p.id").
		Order("pr.record_date DESC, pr.created_at DESC, pr.id DESC").
		Offset(offset).
		Limit(pageSize).
		Scan(&listRows)

	list := make([]gin.H, 0, len(listRows))
	for _, row := range listRows {
		runningHours := resolveProductionDurationHours(row.StartTime, row.EndTime, row.DurationHours)
		startTime, endTime := deriveProductionTimeRange(row.RecordDate, row.CreatedAt, runningHours)
		if row.StartTime != nil && !row.StartTime.IsZero() {
			startTime = *row.StartTime
		}
		if row.EndTime != nil && !row.EndTime.IsZero() {
			endTime = *row.EndTime
		}
		avgSewDuration := 0.0
		if row.Pieces > 0 {
			avgSewDuration = (runningHours * 60) / float64(row.Pieces)
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
			"sewDuration":    roundFloat(runningHours, 2),
			"avgSewDuration": roundFloat(avgSewDuration, 2),
			"totalTime":      roundFloat(runningHours+row.IdleTime, 2),
			"runningTime":    roundFloat(runningHours, 2),
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
	productionDateExpr := h.localDateExpr("pr.record_date")
	baseProdQuery.Session(&gorm.Session{}).
		Select(fmt.Sprintf("%s as date, COALESCE(SUM(%s), 0) as running_time, COALESCE(SUM(pr.idle_time), 0) as idle_time", productionDateExpr, durationExpr)).
		Group(productionDateExpr).
		Order("date").
		Scan(&prodTrendRows)

	var alarmTrendRows []struct {
		Date      string
		AlarmTime float64
	}
	alarmDateExpr := h.localDateExpr("ar.start_time")
	baseAlarmQuery.Session(&gorm.Session{}).
		Select(fmt.Sprintf("%s as date, COALESCE(SUM(ar.duration), 0) / 3600 as alarm_time", alarmDateExpr)).
		Group(alarmDateExpr).
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
		Select(fmt.Sprintf("%s, COALESCE(SUM(%s), 0) as running_time, COALESCE(SUM(pr.idle_time), 0) as idle_time", h.deviceNameExpr("pr.device_id", "name"), durationExpr)).
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
	deviceId, deviceIDs = h.scopeDeviceFilter(c, deviceId, deviceIDs)
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
	alarmDateExpr := h.localDateExpr("ar.start_time")
	baseQuery.Session(&gorm.Session{}).
		Select(fmt.Sprintf("%s as date, COUNT(*) as count, COALESCE(AVG(ar.duration), 0) / 60 as avg_duration", alarmDateExpr)).
		Group(alarmDateExpr).
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
	deviceId, deviceIDs = h.scopeDeviceFilter(c, deviceId, deviceIDs)
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
		Select("sr.*, COALESCE(NULLIF(sr.pattern_name, ''), p.name, '') as pattern_name, COALESCE(NULLIF(sr.order_no, ''), p.order_no, '') as order_no, e.name as employee_name, e.code as employee_code, d.name as device_name").
		Joins("LEFT JOIN patterns p ON sr.pattern_id = p.id").
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
			row.PatternName,
			row.OrderNo,
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
		[]string{"员工工号", "员工姓名", "设备名称", "花型名称", "订单号", "加工件数", "单价(元)", "工资(元)", "奖金(元)", "合计(元)", "日期"},
		rows,
	)
}

func (h *StatisticsHandler) exportProcessCSV(c *gin.Context) {
	startDate := c.Query("startDate")
	endDate := c.Query("endDate")
	deviceId := c.Query("deviceId")
	deviceIDs := parseDeviceIDs(c.Query("deviceIds"))
	deviceId, deviceIDs = h.scopeDeviceFilter(c, deviceId, deviceIDs)

	baseQuery := h.buildProductionStatsBaseQuery(startDate, endDate, deviceId, deviceIDs)

	var rowsData []struct {
		model.ProductionRecord
		DeviceName      string
		EmployeeCode    string
		EmployeeName    string
		PatternName     string
		PatternStitches int64
		DurationHours   float64
	}
	baseQuery.Session(&gorm.Session{}).
		Select(fmt.Sprintf("pr.*, %s, COALESCE(e.code, '-') as employee_code, COALESCE(e.name, '-') as employee_name, COALESCE(NULLIF(pr.pattern_name, ''), p.name, '未命名花型') as pattern_name, COALESCE(NULLIF(p.stitches, 0), pr.stitches, 0) as pattern_stitches, %s", h.deviceNameExpr("pr.device_id", "device_name"), h.productionDurationHoursExpr("pr", "duration_hours"))).
		Joins("LEFT JOIN devices d ON pr.device_id = d.id").
		Joins("LEFT JOIN employees e ON pr.employee_id = e.id").
		Joins("LEFT JOIN patterns p ON pr.pattern_id = p.id").
		Order("pr.record_date DESC, pr.created_at DESC, pr.id DESC").
		Scan(&rowsData)

	patternSewCountMap := h.loadPatternSewCountByDevicePatternDate(startDate, endDate, deviceId, deviceIDs)
	alarmInfoMap := h.loadAlarmInfoByDeviceDate(startDate, endDate, deviceId, deviceIDs)

	rows := make([][]string, 0, len(rowsData))
	for _, row := range rowsData {
		runningHours := resolveProductionDurationHours(row.StartTime, row.EndTime, row.DurationHours)
		startTime, _ := deriveProductionTimeRange(row.RecordDate, row.CreatedAt, runningHours)
		if row.StartTime != nil && !row.StartTime.IsZero() {
			startTime = *row.StartTime
		}
		sewSpeed := 0.0
		if runningHours > 0 {
			sewSpeed = float64(row.Stitches) / (runningHours * 60)
		}
		avgProcessDuration := 0.0
		if row.Pieces > 0 {
			avgProcessDuration = (runningHours * 60) / float64(row.Pieces)
		}
		dateKey := row.RecordDate.Format("2006-01-02")
		patternSewCount := patternSewCountMap[makeDevicePatternDateKey(row.DeviceID, row.PatternID, row.PatternName, dateKey)]
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
			csvFloat(runningHours+row.IdleTime, 2),
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
	deviceId, deviceIDs = h.scopeDeviceFilter(c, deviceId, deviceIDs)

	baseProdQuery := h.buildProductionStatsBaseQuery(startDate, endDate, deviceId, deviceIDs)

	var rowsData []struct {
		model.ProductionRecord
		DeviceName    string
		EmployeeCode  string
		EmployeeName  string
		PatternName   string
		DurationHours float64
	}
	baseProdQuery.Session(&gorm.Session{}).
		Select(fmt.Sprintf("pr.*, %s, COALESCE(e.code, '-') as employee_code, COALESCE(e.name, '-') as employee_name, COALESCE(NULLIF(pr.pattern_name, ''), p.name, '未命名花型') as pattern_name, %s", h.deviceNameExpr("pr.device_id", "device_name"), h.productionDurationHoursExpr("pr", "duration_hours"))).
		Joins("LEFT JOIN devices d ON pr.device_id = d.id").
		Joins("LEFT JOIN employees e ON pr.employee_id = e.id").
		Joins("LEFT JOIN patterns p ON pr.pattern_id = p.id").
		Order("pr.record_date DESC, pr.created_at DESC, pr.id DESC").
		Scan(&rowsData)

	rows := make([][]string, 0, len(rowsData))
	for _, row := range rowsData {
		runningHours := resolveProductionDurationHours(row.StartTime, row.EndTime, row.DurationHours)
		startTime, endTime := deriveProductionTimeRange(row.RecordDate, row.CreatedAt, runningHours)
		if row.StartTime != nil && !row.StartTime.IsZero() {
			startTime = *row.StartTime
		}
		if row.EndTime != nil && !row.EndTime.IsZero() {
			endTime = *row.EndTime
		}
		avgSewDuration := 0.0
		if row.Pieces > 0 {
			avgSewDuration = (runningHours * 60) / float64(row.Pieces)
		}
		rows = append(rows, []string{
			row.DeviceName,
			row.EmployeeCode,
			row.EmployeeName,
			row.RecordDate.Format("2006-01-02"),
			row.PatternName,
			startTime.Format("2006-01-02 15:04:05"),
			endTime.Format("2006-01-02 15:04:05"),
			csvFloat(runningHours, 2),
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
	deviceId, deviceIDs = h.scopeDeviceFilter(c, deviceId, deviceIDs)

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

func makeDevicePatternDateKey(deviceID, patternID uint, patternName, date string) string {
	if patternID > 0 {
		return fmt.Sprintf("%d_id_%d_%s", deviceID, patternID, date)
	}
	return fmt.Sprintf("%d_name_%s_%s", deviceID, strings.ToLower(strings.TrimSpace(patternName)), date)
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
	patternNameExpr := "COALESCE(NULLIF(TRIM(pr.pattern_name), ''), NULLIF(TRIM(p.name), ''), '未命名花型')"

	var rows []struct {
		DeviceID        uint
		PatternID       uint
		PatternName     string
		RecordDate      string
		PatternSewCount int64
	}
	recordDateExpr := h.localDateExpr("pr.record_date")
	baseQuery.Session(&gorm.Session{}).
		Select("pr.device_id, pr.pattern_id, " + patternNameExpr + " as pattern_name, " + recordDateExpr + " as record_date, COALESCE(SUM(pr.pieces), 0) as pattern_sew_count").
		Joins("LEFT JOIN patterns p ON pr.pattern_id = p.id").
		Group("pr.device_id, pr.pattern_id, " + patternNameExpr + ", " + recordDateExpr).
		Scan(&rows)

	result := make(map[string]int64, len(rows))
	for _, row := range rows {
		result[makeDevicePatternDateKey(row.DeviceID, row.PatternID, row.PatternName, row.RecordDate)] = row.PatternSewCount
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
	recordDateExpr := h.localDateExpr("ar.start_time")
	baseQuery.Session(&gorm.Session{}).
		Select(fmt.Sprintf("ar.device_id, %s as record_date, %s, MIN(ar.start_time) as alarm_time", recordDateExpr, h.alarmInfoAggregateExpr("alarm_info"))).
		Group("ar.device_id, " + recordDateExpr).
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
	recordDateExpr := h.localDateExpr("pr.record_date")
	baseQuery.Session(&gorm.Session{}).
		Select("pr.device_id, " + recordDateExpr + " as record_date, COALESCE(MAX(e.code), '-') as employee_code, COALESCE(MAX(e.name), '-') as employee_name").
		Joins("LEFT JOIN employees e ON pr.employee_id = e.id").
		Group("pr.device_id, " + recordDateExpr).
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
	query = applyDateRangeFilter(query, h.localDateExpr("sr.record_date"), startDate, endDate)
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
	query = applyDateRangeFilter(query, h.localDateExpr("pr.record_date"), startDate, endDate)
	if len(deviceIDs) > 0 {
		query = query.Where("pr.device_id IN ?", deviceIDs)
	} else if deviceId != "" {
		query = query.Where("pr.device_id = ?", deviceId)
	}
	return query
}

func (h *StatisticsHandler) buildAlarmStatsBaseQuery(startDate, endDate, deviceId string, deviceIDs []uint, alarmType string) *gorm.DB {
	query := h.db.Table("alarm_records ar")
	query = applyDateRangeFilter(query, h.localDateExpr("ar.start_time"), startDate, endDate)
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

func (h *StatisticsHandler) scopeDeviceFilter(c *gin.Context, deviceId string, deviceIDs []uint) (string, []uint) {
	scope, err := loadUserGroupScope(h.db, c.GetUint("userId"), c.GetString("role"))
	if err != nil {
		return "", []uint{0}
	}

	normalizedDeviceID := strings.TrimSpace(deviceId)
	normalizedDeviceIDs := normalizeGroupIDs(deviceIDs)
	if scope.All {
		return normalizedDeviceID, normalizedDeviceIDs
	}
	if len(scope.GroupIDs) == 0 {
		return "", []uint{0}
	}

	var allowedDeviceIDs []uint
	if err := h.db.Model(&model.Device{}).
		Where("group_id IN ?", scope.GroupIDs).
		Pluck("id", &allowedDeviceIDs).Error; err != nil {
		return "", []uint{0}
	}
	allowedDeviceIDs = normalizeGroupIDs(allowedDeviceIDs)
	if len(allowedDeviceIDs) == 0 {
		return "", []uint{0}
	}
	allowedSet := make(map[uint]struct{}, len(allowedDeviceIDs))
	for _, id := range allowedDeviceIDs {
		allowedSet[id] = struct{}{}
	}

	if normalizedDeviceID != "" {
		parsed, err := strconv.ParseUint(normalizedDeviceID, 10, 64)
		if err != nil {
			return "", []uint{0}
		}
		if _, ok := allowedSet[uint(parsed)]; ok {
			return normalizedDeviceID, nil
		}
		return "", []uint{0}
	}

	if len(normalizedDeviceIDs) > 0 {
		filtered := make([]uint, 0, len(normalizedDeviceIDs))
		for _, id := range normalizedDeviceIDs {
			if _, ok := allowedSet[id]; ok {
				filtered = append(filtered, id)
			}
		}
		if len(filtered) == 0 {
			return "", []uint{0}
		}
		return "", filtered
	}

	return "", allowedDeviceIDs
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

func applyDateRangeFilter(query *gorm.DB, dateExpr, startDate, endDate string) *gorm.DB {
	if strings.TrimSpace(startDate) == "" && strings.TrimSpace(endDate) == "" {
		today := formatLocalStatsDate(time.Now())
		return query.Where(fmt.Sprintf("%s = ?", dateExpr), today)
	}
	if startDate != "" {
		query = query.Where(fmt.Sprintf("%s >= ?", dateExpr), startDate)
	}
	if endDate != "" {
		query = query.Where(fmt.Sprintf("%s <= ?", dateExpr), endDate)
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

func formatStatsDurationHours(hours float64) string {
	totalSeconds := int(math.Round(hours * 3600))
	if totalSeconds < 0 {
		totalSeconds = 0
	}
	h := totalSeconds / 3600
	m := (totalSeconds % 3600) / 60
	s := totalSeconds % 60
	if h > 0 {
		return fmt.Sprintf("%d小时%d分%d秒", h, m, s)
	}
	if m > 0 {
		return fmt.Sprintf("%d分%d秒", m, s)
	}
	return fmt.Sprintf("%d秒", s)
}
