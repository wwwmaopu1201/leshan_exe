package service

import (
	"encoding/binary"
	"errors"
	"net"
	"testing"
	"time"

	"boer-lan-server/internal/alarmcatalog"
	"boer-lan-server/internal/model"

	"gorm.io/driver/sqlite"
	"gorm.io/gorm"
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

func TestParseProductionDataNewPayloadUTF16PatternNameKeepsLastCharacter(t *testing.T) {
	payload := buildProductionDataNewTestPayload(true)
	copy(payload[6:50], make([]byte, 44))
	copy(payload[6:50], encodeUTF16LE("66678"))

	production, err := parseProductionDataNewPayload(payload)
	if err != nil {
		t.Fatalf("parse production payload: %v", err)
	}
	if production.PatternName != "66678" {
		t.Fatalf("expected pattern name 66678, got %q", production.PatternName)
	}
}

func TestNormalizeProtocolTextKeepsASCIIFixedName(t *testing.T) {
	data := make([]byte, 44)
	copy(data, []byte("66678"))

	if got := normalizeProtocolText(data); got != "66678" {
		t.Fatalf("expected ASCII pattern name 66678, got %q", got)
	}
}

func TestHandleWorkStartAcceptsEmployeeCodeAndUpdatesDevice(t *testing.T) {
	db := openTCPConnectionTestDB(t, "work_start_accept")
	employee := model.Employee{Code: "SR000001", Name: "张三", Phone: "13800000000"}
	if err := db.Create(&employee).Error; err != nil {
		t.Fatalf("create employee: %v", err)
	}
	device := model.Device{Code: "D-001", Name: "设备D-001", Type: model.DefaultDeviceType, Status: "online"}
	if err := db.Create(&device).Error; err != nil {
		t.Fatalf("create device: %v", err)
	}

	serverConn, clientConn := net.Pipe()
	defer serverConn.Close()
	defer clientConn.Close()

	dc := NewDeviceConnection(serverConn, db, nil)
	dc.deviceID = device.ID
	dc.deviceCode = device.Code

	done := make(chan struct{})
	go func() {
		dc.handleWorkStart(&Packet{
			ParamType: PTWorkUser,
			ParamNo:   PNWorkStart,
			Data:      encodeFixedString(employee.Code, workUserIDBytes),
		})
		close(done)
	}()

	_ = clientConn.SetReadDeadline(time.Now().Add(time.Second))
	reply, err := ParsePacket(clientConn)
	if err != nil {
		t.Fatalf("read work start reply: %v", err)
	}

	if reply.ParamType != PTWorkUser || reply.ParamNo != PNWorkStart {
		t.Fatalf("unexpected reply command type=0x%04X no=0x%04X", reply.ParamType, reply.ParamNo)
	}
	if result, ok := parseCurrentUserResult(reply.Data); !ok || result != 0 {
		t.Fatalf("expected success result 0, got result=%d ok=%v", result, ok)
	}
	if len(reply.Data) != workUserIDBytes+workUserNameBytes+1 {
		t.Fatalf("expected work start ack payload length %d, got %d", workUserIDBytes+workUserNameBytes+1, len(reply.Data))
	}
	if code := parseWorkStartUserID(reply.Data[:workUserIDBytes]); code != employee.Code {
		t.Fatalf("expected ack employee code %s, got %s", employee.Code, code)
	}

	<-done

	var updated model.Device
	if err := db.First(&updated, device.ID).Error; err != nil {
		t.Fatalf("load updated device: %v", err)
	}
	if updated.EmployeeCode != employee.Code || updated.EmployeeName != employee.Name {
		t.Fatalf("expected device employee %s/%s, got %s/%s",
			employee.Code,
			employee.Name,
			updated.EmployeeCode,
			updated.EmployeeName,
		)
	}
}

func TestHandleWorkStartRejectsUnknownEmployeeCode(t *testing.T) {
	db := openTCPConnectionTestDB(t, "work_start_reject")
	device := model.Device{Code: "D-001", Name: "设备D-001", Type: model.DefaultDeviceType, Status: "online"}
	if err := db.Create(&device).Error; err != nil {
		t.Fatalf("create device: %v", err)
	}

	serverConn, clientConn := net.Pipe()
	defer serverConn.Close()
	defer clientConn.Close()

	dc := NewDeviceConnection(serverConn, db, nil)
	dc.deviceID = device.ID
	dc.deviceCode = device.Code

	done := make(chan struct{})
	go func() {
		dc.handleWorkStart(&Packet{
			ParamType: PTWorkUser,
			ParamNo:   PNWorkStart,
			Data:      encodeFixedString("UNKNOWN", workUserIDBytes),
		})
		close(done)
	}()

	_ = clientConn.SetReadDeadline(time.Now().Add(time.Second))
	reply, err := ParsePacket(clientConn)
	if err != nil {
		t.Fatalf("read work start reply: %v", err)
	}
	<-done

	if result, ok := parseCurrentUserResult(reply.Data); !ok || result != 1 {
		t.Fatalf("expected failure result 1, got result=%d ok=%v", result, ok)
	}
	if len(reply.Data) != workUserIDBytes+workUserNameBytes+1 {
		t.Fatalf("expected work start ack payload length %d, got %d", workUserIDBytes+workUserNameBytes+1, len(reply.Data))
	}

	var updated model.Device
	if err := db.First(&updated, device.ID).Error; err != nil {
		t.Fatalf("load updated device: %v", err)
	}
	if updated.EmployeeCode != "" || updated.EmployeeName != "" {
		t.Fatalf("expected empty device employee, got %s/%s", updated.EmployeeCode, updated.EmployeeName)
	}
}

func TestPatternSessionTryReturnsBusyAndWaitsForCleanup(t *testing.T) {
	serverConn, clientConn := net.Pipe()
	defer serverConn.Close()
	defer clientConn.Close()

	dc := NewDeviceConnection(serverConn, nil, nil)
	dc.deviceCode = "D-001"

	_, cleanup, err := dc.beginPatternSession()
	if err != nil {
		t.Fatalf("begin first pattern session: %v", err)
	}

	if _, _, err := dc.tryBeginPatternSession(); !errors.Is(err, ErrPatternTransferBusy) {
		t.Fatalf("expected busy error, got %v", err)
	}

	acquired := make(chan error, 1)
	go func() {
		_, cleanupSecond, err := dc.beginPatternSession()
		if err == nil {
			cleanupSecond()
		}
		acquired <- err
	}()

	select {
	case err := <-acquired:
		t.Fatalf("second session acquired before cleanup: %v", err)
	case <-time.After(20 * time.Millisecond):
	}

	cleanup()

	select {
	case err := <-acquired:
		if err != nil {
			t.Fatalf("second session should acquire after cleanup: %v", err)
		}
	case <-time.After(time.Second):
		t.Fatal("second session did not acquire after cleanup")
	}
}

func openTCPConnectionTestDB(t *testing.T, name string) *gorm.DB {
	t.Helper()
	db, err := gorm.Open(sqlite.Open("file:"+name+"?mode=memory&cache=shared"), &gorm.Config{})
	if err != nil {
		t.Fatalf("open sqlite: %v", err)
	}
	if err := db.AutoMigrate(&model.Device{}, &model.Employee{}, &model.ServerConfig{}, &model.DebugLog{}, &model.DeviceRuntimeSession{}); err != nil {
		t.Fatalf("migrate sqlite: %v", err)
	}
	if err := db.Create(&model.ServerConfig{Key: debugOutputEnabledConfigKey, Value: "false"}).Error; err != nil {
		t.Fatalf("disable debug output: %v", err)
	}
	return db
}

func TestIdleProbeRequiresSecondCheckBeforeClose(t *testing.T) {
	dc := &DeviceConnection{}
	now := time.Now()

	if dc.shouldCloseAfterIdleProbe(now) {
		t.Fatal("first idle check should send probe instead of closing")
	}
	if dc.shouldCloseAfterIdleProbe(now.Add(OfflineProbeGrace - time.Millisecond)) {
		t.Fatal("should not close before probe grace expires")
	}
	if !dc.shouldCloseAfterIdleProbe(now.Add(OfflineProbeGrace)) {
		t.Fatal("should close after probe grace expires without activity")
	}

	dc.clearOfflineProbe()
	if dc.shouldCloseAfterIdleProbe(now.Add(2 * OfflineProbeGrace)) {
		t.Fatal("activity should clear pending idle probe")
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
