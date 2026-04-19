package service

import (
	"testing"
	"time"
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
