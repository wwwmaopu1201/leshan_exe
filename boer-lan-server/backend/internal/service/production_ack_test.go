package service

import (
	"errors"
	"net"
	"testing"

	"boer-lan-server/internal/model"
	"gorm.io/gorm"
)

type productionAckConn struct {
	net.Conn
	write func([]byte)
}

func (c productionAckConn) Write(p []byte) (int, error) { c.write(p); return len(p), nil }

func TestProductionAckRequiresCommittedRecord(t *testing.T) {
	for _, old := range []bool{false, true} {
		name := "modern"
		if old {
			name = "old"
		}
		t.Run(name, func(t *testing.T) {
			db := openTCPConnectionTestDB(t, "ack_"+name)
			if err := db.AutoMigrate(&model.Pattern{}, &model.DevicePatternFile{}, &model.ProductionRecord{}, &model.SalaryRecord{}); err != nil {
				t.Fatal(err)
			}
			device := model.Device{Code: name, Name: name}
			if err := db.Create(&device).Error; err != nil {
				t.Fatal(err)
			}
			acks := 0
			failures := 0
			dc := &DeviceConnection{db: db, deviceID: device.ID, deviceCode: name, registered: true}
			dc.conn = productionAckConn{write: func(p []byte) {
				if len(p) > 23 && p[23] == 1 {
					failures++
					return
				}
				var count int64
				if err := db.Model(&model.ProductionRecord{}).Count(&count).Error; err != nil || count != 1 {
					t.Fatalf("ACK before committed record: count=%d err=%v", count, err)
				}
				acks++
			}}
			payload := buildProductionDataNewTestPayload(true)
			number := uint16(PNProduction)
			if old {
				payload = append(append([]byte{}, payload[:72]...), payload[80:82]...)
				number = PNProductionOld
			}
			packet := &Packet{ParamType: PTProduction, ParamNo: number, Data: payload}
			handle := func() {
				if old {
					dc.handleProductionOld(packet)
				} else {
					dc.handleProduction(packet)
				}
			}
			// Persistent failure must never acknowledge data that was rolled back.
			if err := db.Exec("CREATE TRIGGER reject_production BEFORE INSERT ON production_records BEGIN SELECT RAISE(ABORT,'disk write rejected'); END").Error; err != nil {
				t.Fatal(err)
			}
			handle()
			if acks != 0 || failures != 1 {
				t.Fatal("failed insert acknowledged")
			}
			db.Exec("DROP TRIGGER reject_production")
			attempts := 0
			db.Callback().Create().Before("gorm:create").Register("test:lock", func(tx *gorm.DB) {
				if tx.Statement.Table == "production_records" {
					attempts++
					if attempts <= 2 {
						tx.AddError(errors.New("database is locked"))
					}
				}
			})
			handle()
			if acks != 1 || attempts != 3 {
				t.Fatalf("lock recovery: acks=%d attempts=%d", acks, attempts)
			}
			handle()
			if acks != 2 {
				t.Fatal("duplicate must be acknowledged")
			}
			var count int64
			db.Model(&model.ProductionRecord{}).Count(&count)
			if count != 1 {
				t.Fatalf("duplicate records: %d", count)
			}
		})
	}
}
