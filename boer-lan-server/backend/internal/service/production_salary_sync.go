package service

import (
	"crypto/sha1"
	"encoding/hex"
	"errors"
	"fmt"
	"log"
	"math"
	"path/filepath"
	"strings"
	"time"

	"boer-lan-server/internal/model"

	"gorm.io/gorm"
)

type productionSnapshot struct {
	PatternNo      uint
	PatternName    string
	ProtocolUserID string
	StartTime      time.Time
	EndTime        time.Time
	StartNeedle    uint32
	EndNeedle      uint32
	StopReason     uint16
}

type productionResolution struct {
	Employee     model.Employee
	HasEmployee  bool
	PatternID    uint
	PatternName  string
	UnitPrice    float64
	OrderNo      string
	EmployeeCode string
	EmployeeName string
}

func (dc *DeviceConnection) syncProductionSnapshot(snapshot productionSnapshot) (*model.ProductionRecord, bool, error) {
	if dc.deviceID == 0 {
		return nil, false, fmt.Errorf("device session is not bound")
	}

	var record model.ProductionRecord
	created := false
	err := dc.db.Transaction(func(tx *gorm.DB) error {
		device, err := loadProductionDevice(tx, dc.deviceID)
		if err != nil {
			return err
		}

		resolution, err := resolveProductionResolution(tx, device, snapshot)
		if err != nil {
			return err
		}

		recordTime := snapshot.EndTime
		if recordTime.IsZero() {
			recordTime = snapshot.StartTime
		}
		if recordTime.IsZero() {
			recordTime = time.Now()
		}

		stitches := int64(0)
		if snapshot.EndNeedle >= snapshot.StartNeedle {
			stitches = int64(snapshot.EndNeedle - snapshot.StartNeedle)
		}
		runningHours := 0.0
		if !snapshot.StartTime.IsZero() && !snapshot.EndTime.IsZero() && snapshot.EndTime.After(snapshot.StartTime) {
			runningHours = snapshot.EndTime.Sub(snapshot.StartTime).Hours()
		}

		sourceKey := computeProductionSourceKey(dc.deviceID, snapshot)
		if sourceKey != "" {
			err := tx.Where("source_key = ?", sourceKey).First(&record).Error
			switch {
			case err == nil:
				updates := make(map[string]interface{})
				if record.EmployeeID == 0 && resolution.HasEmployee {
					updates["employee_id"] = resolution.Employee.ID
				}
				if record.PatternID == 0 && resolution.PatternID > 0 {
					updates["pattern_id"] = resolution.PatternID
				}
				if record.PatternName == "" && resolution.PatternName != "" {
					updates["pattern_name"] = resolution.PatternName
				}
				if record.ProtocolUserID == "" && snapshot.ProtocolUserID != "" {
					updates["protocol_user_id"] = snapshot.ProtocolUserID
				}
				if record.UnitPrice == 0 && resolution.UnitPrice > 0 {
					updates["unit_price"] = roundDecimal(resolution.UnitPrice, 3)
				}
				if strings.TrimSpace(record.OrderNo) == "" && resolution.OrderNo != "" {
					updates["order_no"] = resolution.OrderNo
				}
				if len(updates) > 0 {
					if err := tx.Model(&record).Updates(updates).Error; err != nil {
						return err
					}
					if err := tx.First(&record, record.ID).Error; err != nil {
						return err
					}
				}
				_, err = upsertSalaryRecordFromProduction(tx, &record, false)
				return err
			case !errors.Is(err, gorm.ErrRecordNotFound):
				return err
			}
		}

		record = model.ProductionRecord{
			DeviceID:       dc.deviceID,
			EmployeeID:     0,
			PatternID:      resolution.PatternID,
			PatternNo:      snapshot.PatternNo,
			PatternName:    resolution.PatternName,
			ProtocolUserID: snapshot.ProtocolUserID,
			Pieces:         1,
			Stitches:       stitches,
			ThreadLength:   0,
			RunningTime:    runningHours,
			IdleTime:       0,
			StartTime:      nullableTime(snapshot.StartTime),
			EndTime:        nullableTime(snapshot.EndTime),
			StartNeedle:    uint(snapshot.StartNeedle),
			EndNeedle:      uint(snapshot.EndNeedle),
			StopReason:     uint(snapshot.StopReason),
			UnitPrice:      roundDecimal(resolution.UnitPrice, 3),
			OrderNo:        strings.TrimSpace(resolution.OrderNo),
			SourceKey:      sourceKey,
			RecordDate:     recordTime,
		}
		if resolution.HasEmployee {
			record.EmployeeID = resolution.Employee.ID
		}

		if err := tx.Create(&record).Error; err != nil {
			return err
		}
		created = true

		if resolution.HasEmployee {
			updates := map[string]interface{}{
				"employee_code": resolution.EmployeeCode,
				"employee_name": resolution.EmployeeName,
			}
			if err := tx.Model(&model.Device{}).Where("id = ?", dc.deviceID).Updates(updates).Error; err != nil {
				return err
			}
		}

		_, err = upsertSalaryRecordFromProduction(tx, &record, false)
		return err
	})
	if err != nil {
		return nil, false, err
	}
	return &record, created, nil
}

func BackfillProductionDerivedData(db *gorm.DB) {
	const batchSize = 200
	var lastID uint
	updatedCount := 0
	salaryBackfilled := 0

	for {
		var records []model.ProductionRecord
		if err := db.
			Order("id ASC").
			Where("id > ?", lastID).
			Limit(batchSize).
			Find(&records).Error; err != nil {
			log.Printf("Skip production backfill: %v", err)
			return
		}
		if len(records) == 0 {
			break
		}

		for i := range records {
			record := records[i]
			changed, salaryCreated, err := backfillSingleProductionRecord(db, &record)
			if err != nil {
				log.Printf("Skip production record backfill id=%d: %v", record.ID, err)
				continue
			}
			if changed {
				updatedCount++
			}
			if salaryCreated {
				salaryBackfilled++
			}
		}

		lastID = records[len(records)-1].ID
	}

	if updatedCount > 0 || salaryBackfilled > 0 {
		log.Printf("Production derived data backfilled: updated=%d salary_created=%d", updatedCount, salaryBackfilled)
	}
}

func backfillSingleProductionRecord(db *gorm.DB, record *model.ProductionRecord) (bool, bool, error) {
	if record == nil || record.ID == 0 || record.DeviceID == 0 {
		return false, false, nil
	}

	changed := false
	salaryCreated := false
	err := db.Transaction(func(tx *gorm.DB) error {
		device, err := loadProductionDevice(tx, record.DeviceID)
		if err != nil {
			return err
		}

		updates := make(map[string]interface{})

		if record.EmployeeID == 0 {
			employee, hasEmployee, _, _, err := resolveProductionEmployee(tx, device, strings.TrimSpace(record.ProtocolUserID))
			if err != nil {
				return err
			}
			if hasEmployee {
				record.EmployeeID = employee.ID
				updates["employee_id"] = employee.ID
				updates["protocol_user_id"] = firstNonEmpty(strings.TrimSpace(record.ProtocolUserID), employee.Code)
			}
		}

		patternID, patternName, unitPrice, orderNo, err := resolveExistingProductionPattern(tx, device, record)
		if err != nil {
			return err
		}
		if record.PatternID == 0 && patternID > 0 {
			record.PatternID = patternID
			updates["pattern_id"] = patternID
		}
		if strings.TrimSpace(record.PatternName) == "" && patternName != "" {
			record.PatternName = patternName
			updates["pattern_name"] = patternName
		}
		if record.UnitPrice == 0 && unitPrice > 0 {
			record.UnitPrice = roundDecimal(unitPrice, 3)
			updates["unit_price"] = record.UnitPrice
		}
		if strings.TrimSpace(record.OrderNo) == "" && orderNo != "" {
			record.OrderNo = orderNo
			updates["order_no"] = orderNo
		}

		if len(updates) > 0 {
			if err := tx.Model(record).Updates(updates).Error; err != nil {
				return err
			}
			changed = true
		}

		created, err := upsertSalaryRecordFromProduction(tx, record, true)
		if err != nil {
			return err
		}
		if created {
			salaryCreated = true
		}

		return nil
	})
	return changed, salaryCreated, err
}

func loadProductionDevice(tx *gorm.DB, deviceID uint) (model.Device, error) {
	var device model.Device
	if err := tx.First(&device, deviceID).Error; err != nil {
		return model.Device{}, err
	}
	return device, nil
}

func resolveProductionResolution(tx *gorm.DB, device model.Device, snapshot productionSnapshot) (productionResolution, error) {
	resolution := productionResolution{
		PatternName: strings.TrimSpace(snapshot.PatternName),
	}

	employee, hasEmployee, employeeCode, employeeName, err := resolveProductionEmployee(tx, device, strings.TrimSpace(snapshot.ProtocolUserID))
	if err != nil {
		return resolution, err
	}
	if hasEmployee {
		resolution.Employee = employee
		resolution.HasEmployee = true
		resolution.EmployeeCode = employeeCode
		resolution.EmployeeName = employeeName
	}

	patternID, patternName, unitPrice, orderNo, err := resolvePatternSnapshot(tx, device.ID, snapshot.PatternNo, strings.TrimSpace(snapshot.PatternName))
	if err != nil {
		return resolution, err
	}
	resolution.PatternID = patternID
	if patternName != "" {
		resolution.PatternName = patternName
	}
	resolution.UnitPrice = roundDecimal(unitPrice, 3)
	resolution.OrderNo = strings.TrimSpace(orderNo)

	return resolution, nil
}

func resolveProductionEmployee(tx *gorm.DB, device model.Device, protocolUserID string) (model.Employee, bool, string, string, error) {
	code := strings.TrimSpace(protocolUserID)
	if code == "" {
		code = strings.TrimSpace(device.EmployeeCode)
	}
	name := strings.TrimSpace(device.EmployeeName)

	if code != "" {
		employee, err := findEmployeeByCode(tx, code)
		switch {
		case err == nil:
			return employee, true, employee.Code, employee.Name, nil
		case !errors.Is(err, gorm.ErrRecordNotFound):
			return model.Employee{}, false, "", "", err
		}

		placeholderName := code
		if strings.EqualFold(strings.TrimSpace(device.EmployeeCode), code) && name != "" {
			placeholderName = name
		}
		employee = model.Employee{
			Code:    code,
			Name:    placeholderName,
			GroupID: device.GroupID,
		}
		if err := tx.Create(&employee).Error; err != nil {
			return model.Employee{}, false, "", "", err
		}
		return employee, true, employee.Code, employee.Name, nil
	}

	if name != "" {
		var employee model.Employee
		err := tx.Where("name = ?", name).First(&employee).Error
		switch {
		case err == nil:
			return employee, true, employee.Code, employee.Name, nil
		case errors.Is(err, gorm.ErrRecordNotFound):
			return model.Employee{}, false, "", "", nil
		default:
			return model.Employee{}, false, "", "", err
		}
	}

	return model.Employee{}, false, "", "", nil
}

func findEmployeeByCode(tx *gorm.DB, code string) (model.Employee, error) {
	var employee model.Employee
	if err := tx.Where("LOWER(code) = ?", strings.ToLower(strings.TrimSpace(code))).First(&employee).Error; err != nil {
		return model.Employee{}, err
	}
	return employee, nil
}

func resolveExistingProductionPattern(tx *gorm.DB, device model.Device, record *model.ProductionRecord) (uint, string, float64, string, error) {
	if record == nil {
		return 0, "", 0, "", nil
	}

	if record.PatternID > 0 {
		var pattern model.Pattern
		if err := tx.First(&pattern, record.PatternID).Error; err == nil {
			name := strings.TrimSpace(record.PatternName)
			if name == "" {
				name = normalizePatternDisplayName(pattern.Name, pattern.FileName)
			}
			return pattern.ID, name, pattern.UnitPrice, strings.TrimSpace(pattern.OrderNo), nil
		}
	}

	return resolvePatternSnapshot(tx, device.ID, record.PatternNo, strings.TrimSpace(record.PatternName))
}

func resolvePatternSnapshot(tx *gorm.DB, deviceID, patternNo uint, rawPatternName string) (uint, string, float64, string, error) {
	candidates := buildPatternLookupCandidates(rawPatternName)

	pattern, found, err := findPatternByCandidates(tx, candidates)
	if err != nil {
		return 0, "", 0, "", err
	}
	if found {
		name := normalizePatternDisplayName(pattern.Name, pattern.FileName)
		if name == "" {
			name = firstNonEmpty(strings.TrimSpace(rawPatternName), fmt.Sprintf("花型#%d", patternNo))
		}
		return pattern.ID, name, pattern.UnitPrice, strings.TrimSpace(pattern.OrderNo), nil
	}

	deviceFile, fileFound, err := findDevicePatternFile(tx, deviceID, patternNo, candidates)
	if err != nil {
		return 0, "", 0, "", err
	}
	if fileFound {
		devicePatternName := normalizePatternDisplayName("", deviceFile.FileName)
		if devicePatternName == "" {
			devicePatternName = firstNonEmpty(strings.TrimSpace(rawPatternName), fmt.Sprintf("花型#%d", patternNo))
		}

		if followPattern, followFound, err := findPatternByCandidates(tx, buildPatternLookupCandidates(deviceFile.FileName)); err != nil {
			return 0, "", 0, "", err
		} else if followFound {
			name := normalizePatternDisplayName(followPattern.Name, followPattern.FileName)
			if name == "" {
				name = devicePatternName
			}
			unitPrice := followPattern.UnitPrice
			if unitPrice == 0 && deviceFile.UnitPrice > 0 {
				unitPrice = deviceFile.UnitPrice
			}
			orderNo := strings.TrimSpace(followPattern.OrderNo)
			if orderNo == "" {
				orderNo = strings.TrimSpace(deviceFile.OrderNo)
			}
			return followPattern.ID, name, unitPrice, orderNo, nil
		}

		return 0, devicePatternName, deviceFile.UnitPrice, strings.TrimSpace(deviceFile.OrderNo), nil
	}

	fallbackName := strings.TrimSpace(rawPatternName)
	if fallbackName == "" && patternNo > 0 {
		fallbackName = fmt.Sprintf("花型#%d", patternNo)
	}
	return 0, fallbackName, 0, "", nil
}

func findPatternByCandidates(tx *gorm.DB, candidates []string) (model.Pattern, bool, error) {
	if len(candidates) == 0 {
		return model.Pattern{}, false, nil
	}

	var pattern model.Pattern
	lowerCandidates := lowercaseValues(candidates)
	err := tx.
		Where("LOWER(name) IN ? OR LOWER(file_name) IN ?", lowerCandidates, lowerCandidates).
		Order("id DESC").
		First(&pattern).Error
	switch {
	case err == nil:
		return pattern, true, nil
	case errors.Is(err, gorm.ErrRecordNotFound):
		return model.Pattern{}, false, nil
	default:
		return model.Pattern{}, false, err
	}
}

func findDevicePatternFile(tx *gorm.DB, deviceID, patternNo uint, candidates []string) (model.DevicePatternFile, bool, error) {
	if deviceID == 0 {
		return model.DevicePatternFile{}, false, nil
	}

	if patternNo > 0 {
		var file model.DevicePatternFile
		err := tx.
			Where("device_id = ? AND pattern_no = ?", deviceID, patternNo).
			Order("id DESC").
			First(&file).Error
		switch {
		case err == nil:
			return file, true, nil
		case !errors.Is(err, gorm.ErrRecordNotFound):
			return model.DevicePatternFile{}, false, err
		}
	}

	if len(candidates) == 0 {
		return model.DevicePatternFile{}, false, nil
	}

	var file model.DevicePatternFile
	err := tx.
		Where("device_id = ? AND LOWER(file_name) IN ?", deviceID, lowercaseValues(candidates)).
		Order("id DESC").
		First(&file).Error
	switch {
	case err == nil:
		return file, true, nil
	case errors.Is(err, gorm.ErrRecordNotFound):
		return model.DevicePatternFile{}, false, nil
	default:
		return model.DevicePatternFile{}, false, err
	}
}

func upsertSalaryRecordFromProduction(tx *gorm.DB, record *model.ProductionRecord, skipLegacyAggregate bool) (bool, error) {
	if record == nil || record.ID == 0 || record.EmployeeID == 0 || record.Pieces <= 0 {
		return false, nil
	}

	var existing model.SalaryRecord
	err := tx.Where("source_production_id = ?", record.ID).First(&existing).Error
	switch {
	case err == nil:
		updates := buildSalaryRecordFromProduction(record)
		return false, tx.Model(&existing).Updates(updates).Error
	case !errors.Is(err, gorm.ErrRecordNotFound):
		return false, err
	}

	if skipLegacyAggregate {
		var legacyCount int64
		if err := tx.Model(&model.SalaryRecord{}).
			Where("source_production_id IS NULL").
			Where("employee_id = ? AND device_id = ?", record.EmployeeID, record.DeviceID).
			Where("DATE(record_date) = DATE(?)", record.RecordDate).
			Count(&legacyCount).Error; err != nil {
			return false, err
		}
		if legacyCount > 0 {
			return false, nil
		}
	}

	sourceProductionID := record.ID
	salaryRecord := model.SalaryRecord{
		EmployeeID:         record.EmployeeID,
		DeviceID:           record.DeviceID,
		PatternID:          record.PatternID,
		PatternName:        strings.TrimSpace(record.PatternName),
		OrderNo:            strings.TrimSpace(record.OrderNo),
		SourceProductionID: &sourceProductionID,
		Pieces:             record.Pieces,
		UnitPrice:          roundDecimal(record.UnitPrice, 3),
		Salary:             roundDecimal(float64(record.Pieces)*record.UnitPrice, 2),
		Bonus:              0,
		TotalAmount:        roundDecimal(float64(record.Pieces)*record.UnitPrice, 2),
		RecordDate:         record.RecordDate,
	}
	if err := tx.Create(&salaryRecord).Error; err != nil {
		return false, err
	}
	return true, nil
}

func buildSalaryRecordFromProduction(record *model.ProductionRecord) map[string]interface{} {
	salary := roundDecimal(float64(record.Pieces)*record.UnitPrice, 2)
	return map[string]interface{}{
		"employee_id":  record.EmployeeID,
		"device_id":    record.DeviceID,
		"pattern_id":   record.PatternID,
		"pattern_name": strings.TrimSpace(record.PatternName),
		"order_no":     strings.TrimSpace(record.OrderNo),
		"pieces":       record.Pieces,
		"unit_price":   roundDecimal(record.UnitPrice, 3),
		"salary":       salary,
		"bonus":        0,
		"total_amount": salary,
		"record_date":  record.RecordDate,
	}
}

func computeProductionSourceKey(deviceID uint, snapshot productionSnapshot) string {
	if deviceID == 0 {
		return ""
	}

	parts := []string{
		fmt.Sprintf("%d", deviceID),
		fmt.Sprintf("%d", snapshot.PatternNo),
		strings.TrimSpace(snapshot.PatternName),
		strings.TrimSpace(snapshot.ProtocolUserID),
		fmt.Sprintf("%d", snapshot.StartNeedle),
		fmt.Sprintf("%d", snapshot.EndNeedle),
		fmt.Sprintf("%d", snapshot.StopReason),
	}
	if !snapshot.StartTime.IsZero() {
		parts = append(parts, snapshot.StartTime.UTC().Format(time.RFC3339Nano))
	}
	if !snapshot.EndTime.IsZero() {
		parts = append(parts, snapshot.EndTime.UTC().Format(time.RFC3339Nano))
	}

	sum := sha1.Sum([]byte(strings.Join(parts, "|")))
	return hex.EncodeToString(sum[:])
}

func buildPatternLookupCandidates(raw string) []string {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return nil
	}

	seen := map[string]struct{}{}
	candidates := make([]string, 0, 4)
	appendValue := func(value string) {
		value = strings.TrimSpace(value)
		if value == "" {
			return
		}
		if _, exists := seen[value]; exists {
			return
		}
		seen[value] = struct{}{}
		candidates = append(candidates, value)
	}

	appendValue(raw)
	base := strings.TrimSuffix(raw, filepath.Ext(raw))
	appendValue(base)
	if filepath.Ext(raw) == "" {
		appendValue(raw + ".dst")
	}
	return candidates
}

func normalizePatternDisplayName(name, fileName string) string {
	name = strings.TrimSpace(name)
	if name != "" {
		return name
	}
	fileName = strings.TrimSpace(fileName)
	if fileName == "" {
		return ""
	}
	return strings.TrimSpace(strings.TrimSuffix(fileName, filepath.Ext(fileName)))
}

func lowercaseValues(values []string) []string {
	result := make([]string, 0, len(values))
	seen := make(map[string]struct{}, len(values))
	for _, value := range values {
		value = strings.ToLower(strings.TrimSpace(value))
		if value == "" {
			continue
		}
		if _, exists := seen[value]; exists {
			continue
		}
		seen[value] = struct{}{}
		result = append(result, value)
	}
	return result
}

func nullableTime(t time.Time) *time.Time {
	if t.IsZero() {
		return nil
	}
	copyValue := t
	return &copyValue
}

func firstNonEmpty(values ...string) string {
	for _, value := range values {
		value = strings.TrimSpace(value)
		if value != "" {
			return value
		}
	}
	return ""
}

func roundDecimal(value float64, precision int) float64 {
	pow := math.Pow(10, float64(precision))
	return math.Round(value*pow) / pow
}
