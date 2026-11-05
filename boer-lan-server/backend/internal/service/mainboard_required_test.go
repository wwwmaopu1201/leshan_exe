package service

import (
	"encoding/binary"
	"net"
	"testing"

	"boer-lan-server/internal/model"

	"gorm.io/gorm"
)

func deviceInfoPayload(name string) []byte {
	payload := make([]byte, 8, 8+len(name))
	binary.BigEndian.PutUint32(payload[4:8], 0)
	return append(payload, []byte(name)...)
}

func newMainboardTestConnection(t *testing.T, db *gorm.DB) *DeviceConnection {
	t.Helper()
	serverConn, clientConn := net.Pipe()
	t.Cleanup(func() {
		serverConn.Close()
		clientConn.Close()
	})
	return NewDeviceConnection(serverConn, db, nil)
}

// Customer 1104: machines behind one NAT IP shared a PENDING row, so the same
// device record flipped between 0504020, 0504024 and 0504031.
func TestDevicesWithoutMainboardAreNotStoredAndDoNotShareRecords(t *testing.T) {
	db := openTCPConnectionTestDB(t, "mainboard_required")

	noFlag := newMainboardTestConnection(t, db)
	noFlag.handleDeviceInfo(&Packet{Data: deviceInfoPayload("0504020")})
	noFlag.ensurePlaceholderDevice("heartbeat")
	if err := noFlag.ensureProductionDeviceBinding(0); err == nil {
		t.Fatalf("expected production binding to fail without mainboard flag")
	}
	var count int64
	db.Model(&model.Device{}).Count(&count)
	if count != 0 || noFlag.deviceID != 0 {
		t.Fatalf("device without mainboard flag must not be stored, count=%d id=%d", count, noFlag.deviceID)
	}

	a := newMainboardTestConnection(t, db)
	a.handleDeviceInfo(&Packet{Data: deviceInfoPayload("0504031")})
	a.handleMainboardSN(&Packet{Data: []byte("CNC14ZKBCN371700037")})

	b := newMainboardTestConnection(t, db)
	b.handleMainboardSN(&Packet{Data: []byte("BE002ZKBCN361700922")})
	b.handleDeviceInfo(&Packet{Data: deviceInfoPayload("0504024")})

	if a.deviceID == 0 || b.deviceID == 0 || a.deviceID == b.deviceID {
		t.Fatalf("expected separate records, got a=%d b=%d", a.deviceID, b.deviceID)
	}

	noFlag.handleDeviceInfo(&Packet{Data: deviceInfoPayload("0504020")})
	a.handleDeviceInfo(&Packet{Data: deviceInfoPayload("0504031")})

	var devices []model.Device
	db.Order("id").Find(&devices)
	if len(devices) != 2 {
		t.Fatalf("expected 2 devices, got %d", len(devices))
	}
	want := map[string]string{"CNC14ZKBCN371700037": "0504031", "BE002ZKBCN361700922": "0504024"}
	for _, d := range devices {
		if want[d.MainboardSN] != d.Name {
			t.Fatalf("device %d: mainboard=%q name=%q", d.ID, d.MainboardSN, d.Name)
		}
	}
}
