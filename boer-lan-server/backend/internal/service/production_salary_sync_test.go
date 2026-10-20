package service

import (
	"path/filepath"
	"testing"
	"time"

	"boer-lan-server/internal/model"

	"gorm.io/driver/sqlite"
	"gorm.io/gorm"
)

func TestComputeProductionSourceKeyIgnoresProtocolUserID(t *testing.T) {
	start := time.Date(2026, 4, 19, 10, 0, 0, 0, time.UTC)
	end := start.Add(95 * time.Second)

	base := productionSnapshot{
		PatternNo:   12,
		PatternName: "JK01+前片+M",
		StartTime:   start,
		EndTime:     end,
		StartNeedle: 1200,
		EndNeedle:   8450,
		StopReason:  3,
	}

	keyOld := computeProductionSourceKey(9, base)
	keyNew := computeProductionSourceKey(9, productionSnapshot{
		PatternNo:      base.PatternNo,
		PatternName:    base.PatternName,
		ProtocolUserID: "SEW001",
		StartTime:      base.StartTime,
		EndTime:        base.EndTime,
		StartNeedle:    base.StartNeedle,
		EndNeedle:      base.EndNeedle,
		StopReason:     base.StopReason,
	})

	if keyOld == "" || keyNew == "" {
		t.Fatalf("expected non-empty source keys, got old=%q new=%q", keyOld, keyNew)
	}
	if keyOld != keyNew {
		t.Fatalf("expected same source key for old/new production payload, got old=%q new=%q", keyOld, keyNew)
	}
}

func TestBackfillPreservesHistoryForArchivedAndMissingDevices(t *testing.T) {
	db, err := gorm.Open(sqlite.Open(filepath.Join(t.TempDir(), "history.db")), &gorm.Config{})
	if err != nil {
		t.Fatal(err)
	}
	sqlDB, err := db.DB()
	if err != nil {
		t.Fatal(err)
	}
	defer sqlDB.Close()
	if err := db.AutoMigrate(&model.Device{}, &model.Employee{}, &model.Pattern{}, &model.DevicePatternFile{}, &model.ProductionRecord{}, &model.SalaryRecord{}); err != nil {
		t.Fatal(err)
	}
	employee := model.Employee{Code: "E1", Name: "历史员工"}
	if err := db.Create(&employee).Error; err != nil {
		t.Fatal(err)
	}
	device := model.Device{Code: "D1", Name: "历史设备", EmployeeCode: employee.Code}
	if err := db.Create(&device).Error; err != nil {
		t.Fatal(err)
	}
	if err := db.Delete(&device).Error; err != nil {
		t.Fatal(err)
	}
	records := []model.ProductionRecord{
		{DeviceID: device.ID + 100, Pieces: 1, SourceKey: "missing-device"},
		{DeviceID: device.ID, Pieces: 2, UnitPrice: 3, RecordDate: time.Now(), SourceKey: "archived-device"},
	}
	if err := db.Create(&records).Error; err != nil {
		t.Fatal(err)
	}
	// 缺失设备的记录不能阻止后续补全，重复运行不能重复生成工资。
	BackfillProductionDerivedData(db)
	BackfillProductionDerivedData(db)
	var salary model.SalaryRecord
	if err := db.Where("source_production_id = ?", records[1].ID).First(&salary).Error; err != nil {
		t.Fatal(err)
	}
	if salary.EmployeeID != employee.ID || salary.TotalAmount != 6 {
		t.Fatalf("incorrect historical salary: %+v", salary)
	}
	var productionCount, salaryCount int64
	if err := db.Model(&model.ProductionRecord{}).Count(&productionCount).Error; err != nil {
		t.Fatal(err)
	}
	if err := db.Model(&model.SalaryRecord{}).Count(&salaryCount).Error; err != nil {
		t.Fatal(err)
	}
	if productionCount != 2 || salaryCount != 1 {
		t.Fatalf("history must be preserved without duplicates: production=%d salary=%d", productionCount, salaryCount)
	}
}

func TestComputeProductionSourceKeyChangesOnBusinessFields(t *testing.T) {
	start := time.Date(2026, 4, 19, 10, 0, 0, 0, time.UTC)
	end := start.Add(95 * time.Second)

	base := productionSnapshot{
		PatternNo:   12,
		PatternName: "JK01+前片+M",
		StartTime:   start,
		EndTime:     end,
		StartNeedle: 1200,
		EndNeedle:   8450,
		StopReason:  3,
	}

	keyA := computeProductionSourceKey(9, base)
	keyB := computeProductionSourceKey(9, productionSnapshot{
		PatternNo:   base.PatternNo,
		PatternName: base.PatternName,
		StartTime:   base.StartTime,
		EndTime:     base.EndTime,
		StartNeedle: base.StartNeedle,
		EndNeedle:   base.EndNeedle + 1,
		StopReason:  base.StopReason,
	})

	if keyA == keyB {
		t.Fatalf("expected different source keys when business fields differ, got %q", keyA)
	}
}

func TestResolveProductionEmployeeFallsBackToDeviceCurrentEmployeeWhenProtocolUserIDEmpty(t *testing.T) {
	db, err := gorm.Open(sqlite.Open("file:production_employee_fallback?mode=memory&cache=shared"), &gorm.Config{})
	if err != nil {
		t.Fatalf("open sqlite: %v", err)
	}
	if err := db.AutoMigrate(&model.Employee{}); err != nil {
		t.Fatalf("migrate sqlite: %v", err)
	}

	employee := model.Employee{Code: "SR000001", Name: "张三", Phone: "13800000000"}
	if err := db.Create(&employee).Error; err != nil {
		t.Fatalf("create employee: %v", err)
	}

	resolved, ok, code, name, err := resolveProductionEmployee(db, model.Device{
		EmployeeCode: employee.Code,
		EmployeeName: employee.Name,
	}, "")
	if err != nil {
		t.Fatalf("resolve production employee: %v", err)
	}
	if !ok || resolved.ID != employee.ID || code != employee.Code || name != employee.Name {
		t.Fatalf("expected fallback employee %s/%s, got ok=%v id=%d code=%s name=%s",
			employee.Code,
			employee.Name,
			ok,
			resolved.ID,
			code,
			name,
		)
	}
}
