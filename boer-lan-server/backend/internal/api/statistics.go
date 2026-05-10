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

	"boer-lan-server/internal/alarmcatalog"
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

func (h *StatisticsHandler) localDateTimeExpr(column string) string {
	if h.isSQLite() {
		return fmt.Sprintf("strftime('%%Y-%%m-%%d %%H:%%M:%%S', %s, 'localtime')", column)
	}
	return fmt.Sprintf("DATE_FORMAT(%s, '%%Y-%%m-%%d %%H:%%i:%%s')", column)
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

func normalizeAlarmDisplay(alarmCode, alarmType, description string) (string, string, string) {
	code := strings.TrimSpace(alarmCode)
	alarmType = strings.TrimSpace(alarmType)
	description = strings.TrimSpace(description)

	if descriptor, ok := alarmcatalog.LookupRaw(code); ok {
		if description != "" {
			descriptor.Description = description
		}
		return descriptor.Code, descriptor.Display(), descriptor.Description
	}
	if descriptor, ok := alarmcatalog.LookupRaw(alarmType); ok {
		if description != "" {
			descriptor.Description = description
		}
		return descriptor.Code, descriptor.Display(), descriptor.Description
	}

	info := alarmType
	if info == "" {
		info = description
	}
	if info == "" {
		info = "报警"
	}
	return code, info, description
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

func calculateDurationTotalAndIdle(processingHours, runtimeHours, alarmHours float64) (float64, float64) {
	processingHours = math.Max(processingHours, 0)
	runtimeHours = math.Max(runtimeHours, 0)
	alarmHours = math.Max(alarmHours, 0)

	totalHours := runtimeHours
	if processingHours+alarmHours > totalHours {
		totalHours = processingHours + alarmHours
	}

	idleHours := totalHours - processingHours - alarmHours
	if idleHours < 0 {
		idleHours = 0
	}
	return totalHours, idleHours
}

type deviceDailyRuntimeAgg struct {
	Date       string
	DeviceID   uint
	Processing float64
	Runtime    float64
	Idle       float64
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

func effectiveStatsDateRange(startDate, endDate string) (string, string) {
	if strings.TrimSpace(startDate) == "" && strings.TrimSpace(endDate) == "" {
		today := formatLocalStatsDate(time.Now())
		return today, today
	}
	return startDate, endDate
}

func resolveCumulativeUpTime(usage deviceDailyRuntimeAgg, processingHours, idleHours float64) float64 {
	if usage.Runtime > 0 {
		return usage.Runtime
	}
	fallback := math.Max(processingHours, 0) + math.Max(idleHours, 0)
	if fallback <= 0 && processingHours > 0 {
		return processingHours
	}
	return fallback
}

func (h *StatisticsHandler) loadDeviceDailyRuntimeAggs(startDate, endDate, deviceId string, deviceIDs []uint, extendTodayForOnline bool) []deviceDailyRuntimeAgg {
	if strings.TrimSpace(startDate) == "" && strings.TrimSpace(endDate) == "" {
		today := formatLocalStatsDate(time.Now())
		startDate = today
		endDate = today
	}

	dateExpr := h.localDateExpr("record_date")
	durationExpr := h.productionDurationHoursExpr("", "")
	query := applyDashboardDeviceFilter(h.db.Model(&model.ProductionRecord{}), deviceId, deviceIDs)
	if strings.TrimSpace(startDate) != "" {
		query = query.Where(fmt.Sprintf("%s >= ?", dateExpr), startDate)
	}
	if strings.TrimSpace(endDate) != "" {
		query = query.Where(fmt.Sprintf("%s <= ?", dateExpr), endDate)
	}

	var productionRows []struct {
		Date       string
		DeviceID   uint
		Processing float64
		IdleTime   float64
	}
	query.
		Select(fmt.Sprintf("%s as date, device_id, COALESCE(SUM(%s), 0) as processing, COALESCE(SUM(idle_time), 0) as idle_time", dateExpr, durationExpr)).
		Group(dateExpr + ", device_id").
		Scan(&productionRows)

	buckets := make(map[string]*deviceDailyRuntimeAgg)
	bucketFor := func(date string, deviceID uint) *deviceDailyRuntimeAgg {
		key := fmt.Sprintf("%s:%d", date, deviceID)
		current := buckets[key]
		if current == nil {
			current = &deviceDailyRuntimeAgg{
				Date:     date,
				DeviceID: deviceID,
			}
			buckets[key] = current
		}
		return current
	}

	for _, row := range productionRows {
		current := bucketFor(row.Date, row.DeviceID)
		current.Processing += math.Max(row.Processing, 0)
		current.Idle += math.Max(row.IdleTime, 0)
	}

	startAt, endAt := resolveStatsTimeBounds(startDate, endDate)
	sessionQuery := h.db.Model(&model.DeviceRuntimeSession{}).
		Where("started_at < ?", endAt).
		Where("ended_at IS NULL OR ended_at > ?", startAt)
	if strings.TrimSpace(deviceId) != "" {
		sessionQuery = sessionQuery.Where("device_id = ?", deviceId)
	} else if len(deviceIDs) > 0 {
		sessionQuery = sessionQuery.Where("device_id IN ?", deviceIDs)
	}

	var sessions []model.DeviceRuntimeSession
	if err := sessionQuery.Find(&sessions).Error; err == nil {
		now := time.Now()
		for _, session := range sessions {
			sessionStart := session.StartedAt.In(time.Local)
			sessionEnd := now
			if session.EndedAt != nil && !session.EndedAt.IsZero() {
				sessionEnd = session.EndedAt.In(time.Local)
			} else if !extendTodayForOnline {
				sessionEnd = session.LastSeenAt.In(time.Local)
			}
			if sessionEnd.IsZero() || !sessionEnd.After(sessionStart) {
				continue
			}

			if sessionStart.Before(startAt) {
				sessionStart = startAt
			}
			if sessionEnd.After(endAt) {
				sessionEnd = endAt
			}
			if !sessionEnd.After(sessionStart) {
				continue
			}

			for cursor := sessionStart; cursor.Before(sessionEnd); {
				nextDay := time.Date(cursor.Year(), cursor.Month(), cursor.Day()+1, 0, 0, 0, 0, time.Local)
				segmentEnd := sessionEnd
				if nextDay.Before(segmentEnd) {
					segmentEnd = nextDay
				}
				dateKey := formatLocalStatsDate(cursor)
				current := bucketFor(dateKey, session.DeviceID)
				current.Runtime += segmentEnd.Sub(cursor).Hours()
				cursor = segmentEnd
			}
		}
	}

	result := make([]deviceDailyRuntimeAgg, 0, len(buckets))
	for _, item := range buckets {
		if item.Runtime <= 0 && item.Processing > 0 {
			item.Runtime = item.Processing + item.Idle
		}
		if item.Runtime > item.Processing {
			item.Idle = item.Runtime - item.Processing
		}
		item.Runtime = math.Max(item.Runtime, 0)
		item.Idle = math.Max(item.Idle, 0)
		result = append(result, *item)
	}

	return result
}

func resolveStatsTimeBounds(startDate, endDate string) (time.Time, time.Time) {
	start := parseStatsDateOrDefault(startDate, time.Date(1970, 1, 1, 0, 0, 0, 0, time.Local))
	endBase := parseStatsDateOrDefault(endDate, time.Now())
	end := time.Date(endBase.Year(), endBase.Month(), endBase.Day()+1, 0, 0, 0, 0, time.Local)
	if !end.After(start) {
		end = start.Add(24 * time.Hour)
	}
	return start, end
}

func parseStatsDateOrDefault(raw string, fallback time.Time) time.Time {
	parsed, err := time.ParseInLocation("2006-01-02", strings.TrimSpace(raw), time.Local)
	if err != nil {
		return time.Date(fallback.Year(), fallback.Month(), fallback.Day(), 0, 0, 0, 0, time.Local)
	}
	return parsed
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
	deviceBaseQuery.Session(&gorm.Session{}).Where("status IN ?", []string{"working", "idle"}).Count(&onlineDevices)
	deviceBaseQuery.Session(&gorm.Session{}).Where("status = ?", "working").Count(&workingDevices)
	deviceBaseQuery.Session(&gorm.Session{}).Where("status = ?", "offline").Count(&offlineDevices)
	deviceBaseQuery.Session(&gorm.Session{}).Where("alarm_code <> ?", "").Count(&alarmDevices)

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

		rows := h.loadDeviceDailyRuntimeAggs(formatLocalStatsDate(date), formatLocalStatsDate(date), "", deviceIDs, true)

		efficiency := 0.0
		validDeviceCount := 0
		for _, row := range rows {
			if row.Runtime <= 0 {
				continue
			}
			efficiency += calculateUtilizationRate(row.Processing, row.Runtime)
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

	var devices []struct {
		DeviceID  uint
		Name      string
		GroupID   *uint
		GroupName string
	}

	query := h.db.Table("devices d").
		Select(fmt.Sprintf("d.id as device_id, %s, d.group_id, g.name as group_name", h.deviceNameExpr("d.id", "name"))).
		Joins("LEFT JOIN groups g ON d.group_id = g.id").
		Where("d.deleted_at IS NULL")
	if len(deviceIDs) > 0 {
		query = query.Where("d.id IN ?", deviceIDs)
	}
	query.
		Order("d.sort_order ASC, d.code ASC, d.id ASC").
		Scan(&devices)

	usageByDeviceID := make(map[uint]deviceDailyRuntimeAgg)
	for _, item := range h.loadDeviceDailyRuntimeAggs(startDate, formatLocalStatsDate(time.Now()), "", deviceIDs, true) {
		current := usageByDeviceID[item.DeviceID]
		current.DeviceID = item.DeviceID
		current.Processing += item.Processing
		current.Runtime += item.Runtime
		current.Idle += item.Idle
		usageByDeviceID[item.DeviceID] = current
	}

	lineByGroupID := h.buildLineMetaByGroupID()
	result := make([]gin.H, 0, len(devices))
	for _, row := range devices {
		usage := usageByDeviceID[row.DeviceID]
		efficiency := calculateUtilizationRate(usage.Processing, usage.Runtime)
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
			"processingTime": roundFloat(usage.Processing, 2),
			"runningTime":    roundFloat(usage.Runtime, 2),
			"idleTime":       roundFloat(usage.Idle, 2),
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
	onlineQuery := h.db.Model(&model.Device{}).Where("status IN ?", []string{"working", "idle"})
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
	deviceId, deviceIDs := h.resolveDashboardDeviceScope(c)

	var totalPieces int
	var todayPieces int
	var threadLength float64
	var avgUsedThreadLength float64

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

	var todayProcessingTime, todayRuntimeTime float64
	for _, item := range h.loadDeviceDailyRuntimeAggs(formatLocalStatsDate(time.Now()), formatLocalStatsDate(time.Now()), deviceId, deviceIDs, true) {
		todayProcessingTime += item.Processing
		todayRuntimeTime += item.Runtime
	}

	runtimeTime := todayRuntimeTime
	processingTime := todayProcessingTime
	usedThreadLength := threadLength
	deviceCount := h.countDashboardScopeDevices(deviceId, deviceIDs)
	onlineDeviceCount := h.countDashboardOnlineDevices(deviceId, deviceIDs)
	todayAlarmCount, totalAlarmCount := h.countDashboardAlarms(deviceId, deviceIDs)
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
	runningProcessingTrend, utilizationTrend := h.getRuntimeAndUtilizationTrends(deviceId, deviceIDs, 0)

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
			"runningTime":            roundFloat(runtimeTime, 6),
			"processingTime":         roundFloat(processingTime, 6),
			"utilizationRate":        roundFloat(utilizationRate, 4),
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

func (h *StatisticsHandler) resolveDashboardDeviceScope(c *gin.Context) (string, []uint) {
	deviceId := strings.TrimSpace(c.Query("deviceId"))
	rawDeviceIDs, hasDeviceIDs := c.GetQuery("deviceIds")
	deviceIDs := parseDeviceIDs(rawDeviceIDs)

	if deviceId == "" && hasDeviceIDs && len(deviceIDs) == 0 {
		return "", []uint{0}
	}

	return h.scopeDeviceFilter(c, deviceId, deviceIDs)
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
		Where("status IN ?", []string{"working", "idle"})

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
	rows := h.loadDeviceDailyRuntimeAggs(formatLocalStatsDate(time.Now()), formatLocalStatsDate(time.Now()), deviceId, deviceIDs, true)

	if strings.TrimSpace(deviceId) != "" {
		var totals deviceDailyRuntimeAgg
		for _, row := range rows {
			totals.Processing += row.Processing
			totals.Runtime += row.Runtime
		}
		return roundFloat(calculateUtilizationRate(totals.Processing, totals.Runtime), 4)
	}

	totalUtilization := 0.0
	for _, row := range rows {
		if row.Runtime <= 0 {
			continue
		}
		totalUtilization += calculateUtilizationRate(row.Processing, row.Runtime)
	}

	if deviceCount > 0 {
		if len(rows) > 0 {
			return roundFloat(totalUtilization/float64(len(rows)), 4)
		}
		return 0
	}
	if len(rows) > 0 {
		return roundFloat(totalUtilization/float64(len(rows)), 4)
	}
	return 0
}

func (h *StatisticsHandler) getRuntimeAndUtilizationTrends(deviceId string, deviceIDs []uint, avgDeviceCount int64) ([]gin.H, []gin.H) {
	startDate := time.Now().AddDate(0, 0, -6).Format("2006-01-02")
	rows := h.loadDeviceDailyRuntimeAggs(startDate, formatLocalStatsDate(time.Now()), deviceId, deviceIDs, true)

	type dayAgg struct {
		RuntimeTotal    float64
		ProcessingTotal float64
		UtilSum         float64
		UtilCount       int64
	}
	rowMap := make(map[string]dayAgg, len(rows))
	for _, row := range rows {
		agg := rowMap[row.Date]
		agg.RuntimeTotal += row.Runtime
		agg.ProcessingTotal += row.Processing
		if row.Runtime > 0 {
			agg.UtilSum += calculateUtilizationRate(row.Processing, row.Runtime)
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

		running := roundFloat(runningBase, 6)
		processing := roundFloat(processingBase, 6)
		utilization := 0.0
		if avgDeviceCount > 0 {
			utilization = roundFloat(item.UtilSum/float64(avgDeviceCount), 4)
		} else if item.UtilCount > 0 {
			utilization = roundFloat(item.UtilSum/float64(item.UtilCount), 4)
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
	salaryAmountExpr := h.salaryAmountExpr()
	baseQuery.Session(&gorm.Session{}).
		Select(fmt.Sprintf("COALESCE(SUM(%s), 0) as total_salary, COALESCE(SUM(sr.pieces), 0) as total_pieces, COUNT(DISTINCT sr.employee_id) as employee_count", salaryAmountExpr)).
		Scan(&summaryRow)

	averageSalary := 0.0
	if summaryRow.EmployeeCount > 0 {
		averageSalary = summaryRow.TotalSalary / float64(summaryRow.EmployeeCount)
	}
	totalSalary := roundFloat(summaryRow.TotalSalary, 2)
	averageSalary = roundFloat(averageSalary, 2)

	var results []struct {
		model.SalaryRecord
		EmployeeName  string
		EmployeeCode  string
		DeviceName    string
		LiveUnitPrice float64
		LiveSalary    float64
		LiveTotal     float64
	}
	offset := (page - 1) * pageSize
	baseQuery.Session(&gorm.Session{}).
		Select(fmt.Sprintf("sr.*, COALESCE(NULLIF(sr.pattern_name, ''), p.name, '') as pattern_name, COALESCE(NULLIF(p.order_no, ''), '') as order_no, e.name as employee_name, e.code as employee_code, d.name as device_name, %s as live_unit_price, %s as live_salary, %s as live_total", h.salaryUnitPriceExpr(), salaryAmountExpr, salaryAmountExpr)).
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
			"unitPrice":    roundFloat(r.LiveUnitPrice, 3),
			"salary":       roundFloat(r.LiveSalary, 2),
			"totalAmount":  roundFloat(r.LiveTotal, 2),
			"date":         r.RecordDate.Format("2006-01-02"),
		})
	}

	var rankRows []struct {
		EmployeeName string
		TotalAmount  float64
	}
	baseQuery.Session(&gorm.Session{}).
		Select(fmt.Sprintf("COALESCE(e.name, '未知员工') as employee_name, COALESCE(SUM(%s), 0) as total_amount", salaryAmountExpr)).
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
		Select(fmt.Sprintf("%s as date, COALESCE(SUM(%s), 0) as total_amount", salaryDateExpr, salaryAmountExpr)).
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
		Select(fmt.Sprintf("pr.*, %s, COALESCE(NULLIF(pr.pattern_name, ''), p.name, '未命名花型') as pattern_name, COALESCE(NULLIF(p.stitches, 0), pr.stitches, 0) as pattern_stitches, %s as unit_price, COALESCE(NULLIF(pr.order_no, ''), p.order_no, '') as order_no, %s", h.deviceNameExpr("pr.device_id", "device_name"), h.productionUnitPriceExpr(), h.productionDurationHoursExpr("pr", "duration_hours"))).
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

	var summaryRow struct {
		TotalPieces int64
		TotalThread float64
	}
	baseQuery.Session(&gorm.Session{}).
		Select("COALESCE(SUM(pr.pieces), 0) as total_pieces, COALESCE(SUM(pr.thread_length), 0) as total_thread").
		Scan(&summaryRow)

	usageStartDate, usageEndDate := effectiveStatsDateRange(startDate, endDate)

	totalProcessing := 0.0
	totalHours := 0.0
	usageByDeviceDate := make(map[string]deviceDailyRuntimeAgg)
	for _, item := range h.loadDeviceDailyRuntimeAggs(usageStartDate, usageEndDate, deviceId, deviceIDs, true) {
		totalProcessing += item.Processing
		totalHours += item.Runtime
		usageByDeviceDate[makeDeviceDateKey(item.DeviceID, item.Date)] = item
	}
	avgEfficiency := roundFloat(calculateUtilizationRate(totalProcessing, totalHours), 2)

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
		Select(fmt.Sprintf("pr.*, %s, COALESCE(NULLIF(e.code, ''), NULLIF(pr.protocol_user_id, ''), '-') as employee_code, COALESCE(NULLIF(e.name, ''), '-') as employee_name, COALESCE(NULLIF(pr.pattern_name, ''), p.name, '未命名花型') as pattern_name, COALESCE(NULLIF(p.stitches, 0), pr.stitches, 0) as pattern_stitches, %s", h.deviceNameExpr("pr.device_id", "device_name"), h.productionDurationHoursExpr("pr", "duration_hours"))).
		Joins("LEFT JOIN devices d ON pr.device_id = d.id").
		Joins("LEFT JOIN employees e ON pr.employee_id = e.id").
		Joins("LEFT JOIN patterns p ON pr.pattern_id = p.id").
		Order("pr.record_date DESC, pr.created_at DESC, pr.id DESC").
		Offset(offset).
		Limit(pageSize).
		Scan(&listRows)

	patternSewCountMap := h.loadPatternSewCountByDevicePatternDate(startDate, endDate, deviceId, deviceIDs)
	alarmWindows := h.loadAlarmWindows(startDate, endDate, deviceId, deviceIDs)

	list := make([]gin.H, 0, len(listRows))
	for _, row := range listRows {
		runningHours := resolveProductionDurationHours(row.StartTime, row.EndTime, row.DurationHours)
		startTime, endTime := resolveProductionTimeRange(row.RecordDate, row.CreatedAt, row.StartTime, row.EndTime, runningHours)
		sewSpeed := 0.0
		if runningHours > 0 {
			sewSpeed = float64(row.Stitches) / (runningHours * 60)
		}
		avgProcessDuration := 0.0
		if row.Pieces > 0 {
			avgProcessDuration = (runningHours * 60) / float64(row.Pieces)
		}
		dateKey := row.RecordDate.Format("2006-01-02")
		deviceDayUsage := usageByDeviceDate[makeDeviceDateKey(row.DeviceID, dateKey)]
		cumulativeUpTime := resolveCumulativeUpTime(deviceDayUsage, runningHours, row.IdleTime)
		efficiency := calculateUtilizationRate(deviceDayUsage.Processing, deviceDayUsage.Runtime)
		patternCountKey := makeDevicePatternDateKey(row.DeviceID, row.PatternID, row.PatternName, dateKey)
		patternSewCount := patternSewCountMap[patternCountKey]
		alarmInfo := resolveProductionAlarmInfo(alarmWindows, row.DeviceID, startTime, endTime)
		alarmText := "无"
		alarmTime := "无"
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
			"cumulativeUpTime":   roundFloat(cumulativeUpTime, 6),
			"totalPieces":        row.Pieces,
			"totalStitches":      row.Stitches,
			"threadLength":       roundFloat(row.ThreadLength, 2),
			"runningTime":        roundFloat(row.RunningTime, 2),
			"efficiency":         roundFloat(efficiency, 2),
		})
	}

	var trendRows []struct {
		Date   string
		Pieces int64
	}
	productionDateExpr := h.localDateExpr("pr.record_date")
	baseQuery.Session(&gorm.Session{}).
		Select(fmt.Sprintf("%s as date, COALESCE(SUM(pr.pieces), 0) as pieces", productionDateExpr)).
		Group(productionDateExpr).
		Order("date").
		Scan(&trendRows)

	usageByDate := make(map[string]deviceDailyRuntimeAgg)
	for _, item := range usageByDeviceDate {
		current := usageByDate[item.Date]
		current.Date = item.Date
		current.Processing += item.Processing
		current.Runtime += item.Runtime
		usageByDate[item.Date] = current
	}

	productionTrend := make([]gin.H, 0, len(trendRows))
	for _, row := range trendRows {
		usage := usageByDate[row.Date]
		efficiency := calculateUtilizationRate(usage.Processing, usage.Runtime)
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
	usageStartDate, usageEndDate := startDate, endDate
	if strings.TrimSpace(usageStartDate) == "" && strings.TrimSpace(usageEndDate) == "" {
		today := formatLocalStatsDate(time.Now())
		usageStartDate = today
		usageEndDate = today
	}

	runtimeAggs := h.loadDeviceDailyRuntimeAggs(usageStartDate, usageEndDate, deviceId, deviceIDs, true)
	totalProcessingHours := 0.0
	totalRuntimeHours := 0.0
	for _, item := range runtimeAggs {
		totalProcessingHours += item.Processing
		totalRuntimeHours += item.Runtime
	}

	var alarmSummary struct {
		DurationSeconds int64
	}
	baseAlarmQuery.Session(&gorm.Session{}).
		Select("COALESCE(SUM(ar.duration), 0) as duration_seconds").
		Scan(&alarmSummary)

	alarmHours := float64(alarmSummary.DurationSeconds) / 3600.0
	totalTime, idleHours := calculateDurationTotalAndIdle(totalProcessingHours, totalRuntimeHours, alarmHours)

	summary := gin.H{
		"totalTime":   roundFloat(totalTime, 6),
		"runningTime": roundFloat(totalProcessingHours, 6),
		"idleTime":    roundFloat(idleHours, 6),
		"alarmTime":   roundFloat(alarmHours, 6),
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
		Select(fmt.Sprintf("pr.*, %s, COALESCE(NULLIF(e.code, ''), NULLIF(pr.protocol_user_id, ''), '-') as employee_code, COALESCE(NULLIF(e.name, ''), '-') as employee_name, COALESCE(NULLIF(pr.pattern_name, ''), p.name, '未命名花型') as pattern_name, %s", h.deviceNameExpr("pr.device_id", "device_name"), h.productionDurationHoursExpr("pr", "duration_hours"))).
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
		sewDurationSeconds := int64(math.Round(runningHours * 3600))
		avgSewDurationSeconds := int64(0)
		if sewDurationSeconds < 0 {
			sewDurationSeconds = 0
		}
		if row.Pieces > 0 {
			avgSewDuration = (runningHours * 60) / float64(row.Pieces)
			avgSewDurationSeconds = int64(math.Round(runningHours * 3600 / float64(row.Pieces)))
			if avgSewDurationSeconds < 0 {
				avgSewDurationSeconds = 0
			}
		}
		list = append(list, gin.H{
			"id":                    row.ID,
			"deviceName":            row.DeviceName,
			"employeeCode":          row.EmployeeCode,
			"employeeName":          row.EmployeeName,
			"date":                  row.RecordDate.Format("2006-01-02"),
			"patternName":           row.PatternName,
			"startTime":             startTime.Format("2006-01-02 15:04:05"),
			"endTime":               endTime.Format("2006-01-02 15:04:05"),
			"sewDuration":           roundFloat(runningHours, 6),
			"sewDurationSeconds":    sewDurationSeconds,
			"avgSewDuration":        roundFloat(avgSewDuration, 4),
			"avgSewDurationSeconds": avgSewDurationSeconds,
			"totalTime":             roundFloat(runningHours+row.IdleTime, 6),
			"runningTime":           roundFloat(runningHours, 6),
			"idleTime":              roundFloat(row.IdleTime, 6),
		})
	}

	durationPie := []gin.H{
		{"name": "加工时长", "value": roundFloat(totalProcessingHours, 6)},
		{"name": "空闲时长", "value": roundFloat(idleHours, 6)},
		{"name": "报警时长", "value": roundFloat(alarmHours, 6)},
	}

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
		Date       string
		Processing float64
		Runtime    float64
		IdleTime   float64
		AlarmTime  float64
	}
	trendMap := make(map[string]*trendPoint)
	for _, row := range runtimeAggs {
		item, ok := trendMap[row.Date]
		if !ok {
			item = &trendPoint{Date: row.Date}
			trendMap[row.Date] = item
		}
		item.Processing += row.Processing
		item.Runtime += row.Runtime
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
		total, idle := calculateDurationTotalAndIdle(row.Processing, row.Runtime, row.AlarmTime)
		durationTrend = append(durationTrend, gin.H{
			"date":        row.Date,
			"totalTime":   roundFloat(total, 6),
			"runningTime": roundFloat(row.Processing, 6),
			"idleTime":    roundFloat(idle, 6),
			"alarmTime":   roundFloat(row.AlarmTime, 6),
		})
	}

	var alarmDeviceRows []struct {
		DeviceID  uint
		AlarmTime float64
	}
	baseAlarmQuery.Session(&gorm.Session{}).
		Select("ar.device_id, COALESCE(SUM(ar.duration), 0) / 3600.0 as alarm_time").
		Group("ar.device_id").
		Scan(&alarmDeviceRows)

	type deviceDurationPoint struct {
		DeviceID   uint
		Name       string
		Processing float64
		Runtime    float64
		AlarmTime  float64
		TotalTime  float64
		IdleTime   float64
	}
	deviceMap := make(map[uint]*deviceDurationPoint)
	for _, row := range runtimeAggs {
		item := deviceMap[row.DeviceID]
		if item == nil {
			item = &deviceDurationPoint{DeviceID: row.DeviceID}
			deviceMap[row.DeviceID] = item
		}
		item.Processing += row.Processing
		item.Runtime += row.Runtime
	}
	for _, row := range alarmDeviceRows {
		item := deviceMap[row.DeviceID]
		if item == nil {
			item = &deviceDurationPoint{DeviceID: row.DeviceID}
			deviceMap[row.DeviceID] = item
		}
		item.AlarmTime += row.AlarmTime
	}

	deviceIDsForNames := make([]uint, 0, len(deviceMap))
	for deviceID := range deviceMap {
		if deviceID > 0 {
			deviceIDsForNames = append(deviceIDsForNames, deviceID)
		}
	}
	var deviceNameRows []struct {
		ID   uint
		Name string
		Code string
	}
	if len(deviceIDsForNames) > 0 {
		h.db.Model(&model.Device{}).
			Select("id, name, code").
			Where("id IN ?", deviceIDsForNames).
			Find(&deviceNameRows)
	}
	for _, row := range deviceNameRows {
		if item := deviceMap[row.ID]; item != nil {
			item.Name = strings.TrimSpace(row.Name)
			if item.Name == "" {
				item.Name = strings.TrimSpace(row.Code)
			}
		}
	}

	devicePoints := make([]*deviceDurationPoint, 0, len(deviceMap))
	for _, item := range deviceMap {
		item.TotalTime, item.IdleTime = calculateDurationTotalAndIdle(item.Processing, item.Runtime, item.AlarmTime)
		if strings.TrimSpace(item.Name) == "" {
			item.Name = fmt.Sprintf("设备#%d", item.DeviceID)
		}
		devicePoints = append(devicePoints, item)
	}
	sort.Slice(devicePoints, func(i, j int) bool {
		if devicePoints[i].TotalTime == devicePoints[j].TotalTime {
			return devicePoints[i].DeviceID < devicePoints[j].DeviceID
		}
		return devicePoints[i].TotalTime > devicePoints[j].TotalTime
	})

	if len(devicePoints) > 10 {
		devicePoints = devicePoints[:10]
	}
	deviceStats := make([]gin.H, 0, len(devicePoints))
	for _, row := range devicePoints {
		deviceStats = append(deviceStats, gin.H{
			"name":        row.Name,
			"totalTime":   roundFloat(row.TotalTime, 6),
			"runningTime": roundFloat(row.Processing, 6),
			"idleTime":    roundFloat(row.IdleTime, 6),
			"alarmTime":   roundFloat(row.AlarmTime, 6),
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
		AlarmCode string
		AlarmType string
		Count     int64
	}
	baseQuery.Session(&gorm.Session{}).
		Select("ar.alarm_code as alarm_code, ar.alarm_type as alarm_type, COUNT(*) as count").
		Group("ar.alarm_code, ar.alarm_type").
		Order("count DESC").
		Scan(&typeRows)

	typeCounts := make(map[string]int64)
	for _, row := range typeRows {
		_, name, _ := normalizeAlarmDisplay(row.AlarmCode, row.AlarmType, "")
		typeCounts[name] += row.Count
	}

	typeNames := make([]string, 0, len(typeCounts))
	for name := range typeCounts {
		typeNames = append(typeNames, name)
	}
	sort.Slice(typeNames, func(i, j int) bool {
		if typeCounts[typeNames[i]] == typeCounts[typeNames[j]] {
			return typeNames[i] < typeNames[j]
		}
		return typeCounts[typeNames[i]] > typeCounts[typeNames[j]]
	})

	alarmTypePie := make([]gin.H, 0, len(typeNames))
	for _, name := range typeNames {
		alarmTypePie = append(alarmTypePie, gin.H{
			"name":  name,
			"value": typeCounts[name],
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
		alarmCode, alarmInfo, description := normalizeAlarmDisplay(row.AlarmCode, row.AlarmType, row.Description)
		list = append(list, gin.H{
			"id":           row.ID,
			"deviceName":   row.DeviceName,
			"employeeCode": employeeInfo.EmployeeCode,
			"employeeName": employeeInfo.EmployeeName,
			"alarmTime":    row.StartTime.Format("2006-01-02 15:04:05"),
			"alarmInfo":    alarmInfo,
			"alarmType":    alarmInfo,
			"alarmCode":    alarmCode,
			"description":  description,
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
	salaryUnitPriceExpr := h.salaryUnitPriceExpr()
	salaryAmountExpr := h.salaryAmountExpr()

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
			TotalAmount  float64
		}
		baseQuery.Session(&gorm.Session{}).
			Select(fmt.Sprintf("e.name as employee_name, e.code as employee_code, d.name as device_name, %s as unit_price, COALESCE(SUM(sr.pieces), 0) as total_pieces, COALESCE(SUM(%s), 0) as salary, COALESCE(SUM(%s), 0) as total_amount", salaryUnitPriceExpr, salaryAmountExpr, salaryAmountExpr)).
			Group(fmt.Sprintf("sr.employee_id, e.name, e.code, sr.device_id, d.name, %s", salaryUnitPriceExpr)).
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
				csvFloat(row.TotalAmount, 2),
			})
		}

		writeCSVResponse(c,
			fileNamePrefix+"_merged_"+time.Now().Format("20060102_150405")+".csv",
			[]string{"员工工号", "员工姓名", "设备名称", "加工件数", "单价(元)", "工资(元)", "合计(元)"},
			rows,
		)
		return
	case "current":
		fileNamePrefix = "salary_stats_current"
	default:
		fileNamePrefix = "salary_stats_all"
	}

	query := baseQuery.Session(&gorm.Session{}).
		Select(fmt.Sprintf("sr.*, COALESCE(NULLIF(sr.pattern_name, ''), p.name, '') as pattern_name, COALESCE(NULLIF(p.order_no, ''), '') as order_no, e.name as employee_name, e.code as employee_code, d.name as device_name, %s as live_unit_price, %s as live_salary, %s as live_total", salaryUnitPriceExpr, salaryAmountExpr, salaryAmountExpr)).
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
		EmployeeName  string
		EmployeeCode  string
		DeviceName    string
		LiveUnitPrice float64
		LiveSalary    float64
		LiveTotal     float64
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
			csvFloat(row.LiveUnitPrice, 3),
			csvFloat(row.LiveSalary, 2),
			csvFloat(row.LiveTotal, 2),
			row.RecordDate.Format("2006-01-02"),
		})
	}

	writeCSVResponse(c,
		fileNamePrefix+"_"+time.Now().Format("20060102_150405")+".csv",
		[]string{"员工工号", "员工姓名", "设备名称", "花型名称", "订单号", "加工件数", "单价(元)", "工资(元)", "合计(元)", "日期"},
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
		Select(fmt.Sprintf("pr.*, %s, COALESCE(NULLIF(e.code, ''), NULLIF(pr.protocol_user_id, ''), '-') as employee_code, COALESCE(NULLIF(e.name, ''), '-') as employee_name, COALESCE(NULLIF(pr.pattern_name, ''), p.name, '未命名花型') as pattern_name, COALESCE(NULLIF(p.stitches, 0), pr.stitches, 0) as pattern_stitches, %s", h.deviceNameExpr("pr.device_id", "device_name"), h.productionDurationHoursExpr("pr", "duration_hours"))).
		Joins("LEFT JOIN devices d ON pr.device_id = d.id").
		Joins("LEFT JOIN employees e ON pr.employee_id = e.id").
		Joins("LEFT JOIN patterns p ON pr.pattern_id = p.id").
		Order("pr.record_date DESC, pr.created_at DESC, pr.id DESC").
		Scan(&rowsData)

	patternSewCountMap := h.loadPatternSewCountByDevicePatternDate(startDate, endDate, deviceId, deviceIDs)
	alarmWindows := h.loadAlarmWindows(startDate, endDate, deviceId, deviceIDs)
	usageStartDate, usageEndDate := effectiveStatsDateRange(startDate, endDate)
	usageByDeviceDate := make(map[string]deviceDailyRuntimeAgg)
	for _, item := range h.loadDeviceDailyRuntimeAggs(usageStartDate, usageEndDate, deviceId, deviceIDs, true) {
		usageByDeviceDate[makeDeviceDateKey(item.DeviceID, item.Date)] = item
	}

	rows := make([][]string, 0, len(rowsData))
	for _, row := range rowsData {
		runningHours := resolveProductionDurationHours(row.StartTime, row.EndTime, row.DurationHours)
		startTime, endTime := resolveProductionTimeRange(row.RecordDate, row.CreatedAt, row.StartTime, row.EndTime, runningHours)
		sewSpeed := 0.0
		if runningHours > 0 {
			sewSpeed = float64(row.Stitches) / (runningHours * 60)
		}
		avgProcessDuration := 0.0
		if row.Pieces > 0 {
			avgProcessDuration = (runningHours * 60) / float64(row.Pieces)
		}
		dateKey := row.RecordDate.Format("2006-01-02")
		deviceDayUsage := usageByDeviceDate[makeDeviceDateKey(row.DeviceID, dateKey)]
		cumulativeUpTime := resolveCumulativeUpTime(deviceDayUsage, runningHours, row.IdleTime)
		patternSewCount := patternSewCountMap[makeDevicePatternDateKey(row.DeviceID, row.PatternID, row.PatternName, dateKey)]
		alarmInfo := resolveProductionAlarmInfo(alarmWindows, row.DeviceID, startTime, endTime)
		alarmText := "无"
		alarmTime := "无"
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
			csvFloat(cumulativeUpTime, 6),
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
		Select(fmt.Sprintf("pr.*, %s, COALESCE(NULLIF(e.code, ''), NULLIF(pr.protocol_user_id, ''), '-') as employee_code, COALESCE(NULLIF(e.name, ''), '-') as employee_name, COALESCE(NULLIF(pr.pattern_name, ''), p.name, '未命名花型') as pattern_name, %s", h.deviceNameExpr("pr.device_id", "device_name"), h.productionDurationHoursExpr("pr", "duration_hours"))).
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
		alarmCode, alarmInfo, _ := normalizeAlarmDisplay(row.AlarmCode, row.AlarmType, row.Description)
		rows = append(rows, []string{
			row.DeviceName,
			employeeInfo.EmployeeCode,
			employeeInfo.EmployeeName,
			row.StartTime.Format("2006-01-02 15:04:05"),
			alarmInfo,
			alarmCode,
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

type alarmWindowInfo struct {
	DeviceID  uint
	StartTime time.Time
	EndTime   time.Time
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

func resolveProductionTimeRange(recordDate, createdAt time.Time, startTime, endTime *time.Time, runningHours float64) (time.Time, time.Time) {
	resolvedStart, resolvedEnd := deriveProductionTimeRange(recordDate, createdAt, runningHours)
	if startTime != nil && !startTime.IsZero() {
		resolvedStart = *startTime
		resolvedEnd = resolvedStart.Add(time.Duration(runningHours * float64(time.Hour)))
	}
	if endTime != nil && !endTime.IsZero() {
		resolvedEnd = *endTime
	}
	if resolvedEnd.Before(resolvedStart) {
		resolvedEnd = resolvedStart
	}
	return resolvedStart, resolvedEnd
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
		AlarmTime  string
	}
	recordDateExpr := h.localDateExpr("ar.start_time")
	alarmStartExpr := h.localDateTimeExpr("MIN(ar.start_time)")
	baseQuery.Session(&gorm.Session{}).
		Select(fmt.Sprintf("ar.device_id, %s as record_date, %s, %s as alarm_time", recordDateExpr, h.alarmInfoAggregateExpr("alarm_info"), alarmStartExpr)).
		Group("ar.device_id, " + recordDateExpr).
		Scan(&rows)

	result := make(map[string]alarmDailyInfo, len(rows))
	for _, row := range rows {
		alarmTime := strings.TrimSpace(row.AlarmTime)
		if alarmTime == "" {
			alarmTime = "无"
		}
		alarmInfo := row.AlarmInfo
		if alarmInfo == "" {
			alarmInfo = "无"
		}
		result[makeDeviceDateKey(row.DeviceID, row.RecordDate)] = alarmDailyInfo{
			AlarmInfo: alarmInfo,
			AlarmTime: alarmTime,
		}
	}
	return result
}

func (h *StatisticsHandler) loadAlarmWindows(startDate, endDate, deviceId string, deviceIDs []uint) []alarmWindowInfo {
	baseQuery := h.buildAlarmStatsBaseQuery(startDate, endDate, deviceId, deviceIDs, "")

	var rows []struct {
		DeviceID    uint
		AlarmCode   string
		AlarmType   string
		Description string
		Duration    int
		StartTime   time.Time
		EndTime     *time.Time
	}
	baseQuery.Session(&gorm.Session{}).
		Select("ar.device_id, ar.alarm_code, ar.alarm_type, ar.description, ar.duration, ar.start_time, ar.end_time").
		Order("ar.start_time ASC, ar.id ASC").
		Scan(&rows)

	result := make([]alarmWindowInfo, 0, len(rows))
	for _, row := range rows {
		if row.StartTime.IsZero() {
			continue
		}
		_, alarmInfo, _ := normalizeAlarmDisplay(row.AlarmCode, row.AlarmType, row.Description)
		alarmInfo = strings.TrimSpace(alarmInfo)
		if alarmInfo == "" {
			alarmInfo = "报警"
		}
		alarmEnd := row.StartTime
		if row.EndTime != nil && !row.EndTime.IsZero() && row.EndTime.After(row.StartTime) {
			alarmEnd = *row.EndTime
		} else if row.Duration > 0 {
			alarmEnd = row.StartTime.Add(time.Duration(row.Duration) * time.Second)
		}
		result = append(result, alarmWindowInfo{
			DeviceID:  row.DeviceID,
			StartTime: row.StartTime,
			EndTime:   alarmEnd,
			AlarmInfo: alarmInfo,
			AlarmTime: row.StartTime.Format("2006-01-02 15:04:05"),
		})
	}
	return result
}

func resolveProductionAlarmInfo(alarms []alarmWindowInfo, deviceID uint, startTime, endTime time.Time) alarmDailyInfo {
	if endTime.Before(startTime) {
		endTime = startTime
	}
	for _, alarm := range alarms {
		if alarm.DeviceID != deviceID {
			continue
		}
		if !alarm.StartTime.After(endTime) && !alarm.EndTime.Before(startTime) {
			return alarmDailyInfo{
				AlarmInfo: alarm.AlarmInfo,
				AlarmTime: alarm.AlarmTime,
			}
		}
	}
	return alarmDailyInfo{AlarmInfo: "无", AlarmTime: "无"}
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
		Select("pr.device_id, " + recordDateExpr + " as record_date, COALESCE(MAX(NULLIF(e.code, '')), MAX(NULLIF(pr.protocol_user_id, '')), '-') as employee_code, COALESCE(MAX(NULLIF(e.name, '')), '-') as employee_name").
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
		Joins("LEFT JOIN devices d ON sr.device_id = d.id").
		Joins("LEFT JOIN patterns p ON sr.pattern_id = p.id")
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

func (h *StatisticsHandler) salaryUnitPriceExpr() string {
	return h.livePatternUnitPriceExpr("sr", "p")
}

func (h *StatisticsHandler) salaryAmountExpr() string {
	return fmt.Sprintf("(COALESCE(sr.pieces, 0) * %s)", h.salaryUnitPriceExpr())
}

func (h *StatisticsHandler) productionUnitPriceExpr() string {
	return h.livePatternUnitPriceExpr("pr", "p")
}

func (h *StatisticsHandler) livePatternUnitPriceExpr(recordAlias, joinedPatternAlias string) string {
	recordPatternName := fmt.Sprintf("TRIM(COALESCE(%s.pattern_name, ''))", recordAlias)
	joinedUnitPrice := fmt.Sprintf("%s.unit_price", joinedPatternAlias)

	matchedPatternUnitPrice := fmt.Sprintf(`(
		SELECT p_live.unit_price
		FROM patterns p_live
		WHERE p_live.deleted_at IS NULL
			AND %s <> ''
			AND TRIM(COALESCE(p_live.name, '')) = %s
		ORDER BY
			p_live.updated_at DESC,
			p_live.id DESC
		LIMIT 1
	)`, recordPatternName, recordPatternName)

	return fmt.Sprintf("COALESCE(NULLIF(%s, 0), NULLIF(%s, 0), %s.unit_price, 0)", joinedUnitPrice, matchedPatternUnitPrice, recordAlias)
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
		rawCodes := alarmcatalog.RawCodesFor(alarmType)
		if len(rawCodes) > 0 {
			query = query.Where("(ar.alarm_type = ? OR ar.description = ? OR ar.alarm_code IN ?)", alarmType, alarmType, rawCodes)
		} else {
			query = query.Where("(ar.alarm_type = ? OR ar.description = ? OR ar.alarm_code = ?)", alarmType, alarmType, alarmType)
		}
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
