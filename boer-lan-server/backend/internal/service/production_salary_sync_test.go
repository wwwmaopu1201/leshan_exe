package service

import (
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
