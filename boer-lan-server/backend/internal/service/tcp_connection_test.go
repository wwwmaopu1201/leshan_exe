package service

import (
	"encoding/binary"
	"testing"
	"time"

	"boer-lan-server/internal/alarmcatalog"
)

func buildProductionDataNewTestPayload(userBeforeStop bool) []byte {
	data := make([]byte, 82)
	start := time.Date(2026, 4, 26, 15, 13, 12, 0, time.Local)
	end := start.Add(10 * time.Second)

	binary.BigEndian.PutUint32(data[0:4], 147741)
	binary.BigEndian.PutUint16(data[4:6], 1)
	copy(data[6:50], []byte("QQ"))
	copy(data[50:57], encodeProtocolBCDDateTime(start)[:7])
	binary.BigEndian.PutUint32(data[57:61], 0)
	copy(data[61:68], encodeProtocolBCDDateTime(end)[:7])
	binary.BigEndian.PutUint32(data[68:72], 232)

	if userBeforeStop {
		copy(data[72:80], []byte("SR000001"))
		binary.BigEndian.PutUint16(data[80:82], 3)
		return data
	}

	binary.BigEndian.PutUint16(data[72:74], 3)
	copy(data[74:82], []byte("SR000001"))
	return data
}

func TestParseProductionDataNewPayloadUserBeforeStopReason(t *testing.T) {
	production, err := parseProductionDataNewPayload(buildProductionDataNewTestPayload(true))
	if err != nil {
		t.Fatalf("parse production payload: %v", err)
	}
	if production.UserID != "SR000001" {
		t.Fatalf("expected user id SR000001, got %q", production.UserID)
	}
	if production.StopReason != 3 {
		t.Fatalf("expected stop reason 3, got %d", production.StopReason)
	}
}

func TestParseProductionDataNewPayloadStopReasonBeforeUser(t *testing.T) {
	production, err := parseProductionDataNewPayload(buildProductionDataNewTestPayload(false))
	if err != nil {
		t.Fatalf("parse production payload: %v", err)
	}
	if production.UserID != "SR000001" {
		t.Fatalf("expected user id SR000001, got %q", production.UserID)
	}
	if production.StopReason != 3 {
		t.Fatalf("expected stop reason 3, got %d", production.StopReason)
	}
}

func TestDescribeAlarmUsesCatalogValue(t *testing.T) {
	alarm := alarmcatalog.Describe(58)
	if alarm.Code != "E.PRESS_UP" {
		t.Fatalf("expected alarm code E.PRESS_UP, got %q", alarm.Code)
	}
	if alarm.Description != "压框没压下!" {
		t.Fatalf("expected alarm description 压框没压下!, got %q", alarm.Description)
	}
	if alarm.Display() != "E.PRESS_UP - 压框没压下!" {
		t.Fatalf("expected display text, got %q", alarm.Display())
	}
}
