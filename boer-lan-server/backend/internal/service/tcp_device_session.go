package service

import (
	"errors"
	"fmt"
	"strings"
	"time"

	"boer-lan-server/internal/model"

	"gorm.io/gorm"
)

const (
	registerProbeInterval = 5 * time.Second
	pendingDevicePrefix   = "PENDING-"
)

func (dc *DeviceConnection) startHandshakeLoop() {
	dc.stateMu.Lock()
	if dc.loopStopCh != nil {
		dc.stateMu.Unlock()
		return
	}
	stopCh := make(chan struct{})
	dc.loopStopCh = stopCh
	dc.stateMu.Unlock()

	dc.sendRegisterProbe("initial")

	go func() {
		ticker := time.NewTicker(registerProbeInterval)
		defer ticker.Stop()

		for {
			select {
			case <-stopCh:
				return
			case <-ticker.C:
				if dc.isRegistered() {
					continue
				}
				dc.sendRegisterProbe("retry")
			}
		}
	}()
}

func (dc *DeviceConnection) stopHandshakeLoop() {
	dc.stateMu.Lock()
	stopCh := dc.loopStopCh
	dc.loopStopCh = nil
	dc.stateMu.Unlock()

	if stopCh != nil {
		close(stopCh)
	}
}

func (dc *DeviceConnection) isRegistered() bool {
	dc.stateMu.RLock()
	defer dc.stateMu.RUnlock()
	return dc.registered
}

func (dc *DeviceConnection) markRegistered(reason string) {
	dc.stateMu.Lock()
	alreadyRegistered := dc.registered
	if !dc.registered {
		dc.registered = true
		dc.registerAt = time.Now()
	}
	dc.stateMu.Unlock()

	if !alreadyRegistered {
		emitTCPLog(dc.db, "info", true, "[TCP] Protocol established: remote=%s device=%s reason=%s",
			dc.conn.RemoteAddr(),
			dc.deviceCode,
			reason,
		)
	}
}

func (dc *DeviceConnection) sendRegisterProbe(trigger string) {
	pkt := buildProtocolCommand(PTRegister, PNRegister, nil)
	if err := dc.writePacket(pkt); err != nil {
		return
	}
	emitTCPLog(dc.db, "info", true, "[TCP] Active register probe sent: trigger=%s remote=%s", trigger, dc.conn.RemoteAddr())
}

func (dc *DeviceConnection) ensurePlaceholderDevice(reason string) {
	code, name, deviceType, modelName := dc.provisionalIdentity()
	if code == "" {
		return
	}
	if err := dc.upsertDeviceRecord(code, name, deviceType, modelName, reason); err != nil {
		emitTCPLog(dc.db, "warn", true, "[TCP] Ensure placeholder device failed: remote=%s reason=%s err=%v", dc.conn.RemoteAddr(), reason, err)
	}
}

func (dc *DeviceConnection) provisionalIdentity() (code, name, deviceType, modelName string) {
	ip := extractIP(dc.conn.RemoteAddr().String())
	flag := strings.TrimSpace(dc.deviceFlag)

	currentCode := strings.TrimSpace(dc.deviceCode)
	if currentCode == "0" {
		currentCode = ""
	}
	if currentCode != "" && !strings.HasPrefix(currentCode, pendingDevicePrefix) {
		code = currentCode
	} else if flag != "" {
		code = flag
	} else if ip != "" {
		code = pendingDeviceCodeForIP(ip)
	}

	if strings.TrimSpace(dc.deviceName) != "" {
		name = strings.TrimSpace(dc.deviceName)
	} else if flag != "" {
		name = "设备 " + flag
	} else if ip != "" {
		name = "待识别设备 " + ip
	} else {
		name = "待识别设备"
	}

	deviceType = "模板机"
	modelName = "待识别"
	return
}

func pendingDeviceCodeForIP(ip string) string {
	ip = strings.TrimSpace(ip)
	if ip == "" {
		return ""
	}

	replacer := strings.NewReplacer(".", "-", ":", "-", "/", "-")
	return pendingDevicePrefix + replacer.Replace(ip)
}

func (dc *DeviceConnection) upsertDeviceRecord(code, name, deviceType, modelName, reason string) error {
	code = strings.TrimSpace(code)
	if code == "" {
		return fmt.Errorf("device code is empty")
	}

	ip := extractIP(dc.conn.RemoteAddr().String())
	mainboardSN := strings.TrimSpace(dc.deviceFlag)
	now := time.Now()

	var device model.Device
	err := dc.resolveExistingDevice(&device, code, mainboardSN, ip)
	switch {
	case errors.Is(err, gorm.ErrRecordNotFound):
		device = model.Device{
			Code:         code,
			Name:         fallbackString(name, "设备 "+code),
			InitialName:  fallbackString(name, "设备 "+code),
			Type:         fallbackString(deviceType, "模板机"),
			ModelName:    fallbackString(modelName, "待识别"),
			IdentifiedBy: dc.resolveIdentifiedBy(code, mainboardSN),
			MainboardSN:  mainboardSN,
			IP:           ip,
			Status:       "online",
			LastOnline:   now,
		}
		if err := dc.db.Create(&device).Error; err != nil {
			return err
		}
	case err != nil:
		return err
	default:
		updates := map[string]interface{}{
			"ip":            ip,
			"status":        "online",
			"last_online":   now,
			"identified_by": dc.resolveIdentifiedBy(code, mainboardSN),
		}
		if strings.TrimSpace(name) != "" {
			updates["name"] = strings.TrimSpace(name)
			if strings.TrimSpace(device.InitialName) == "" {
				updates["initial_name"] = strings.TrimSpace(name)
			}
		}
		if strings.TrimSpace(deviceType) != "" {
			updates["type"] = strings.TrimSpace(deviceType)
		}
		if strings.TrimSpace(modelName) != "" {
			updates["model_name"] = strings.TrimSpace(modelName)
		}
		if mainboardSN != "" {
			updates["mainboard_sn"] = mainboardSN
		}
		if code != device.Code {
			var existingByCode model.Device
			if err := dc.db.Where("code = ?", code).First(&existingByCode).Error; errors.Is(err, gorm.ErrRecordNotFound) {
				updates["code"] = code
			}
		}
		if err := dc.db.Model(&device).Updates(updates).Error; err != nil {
			return err
		}
		if err := dc.db.First(&device, device.ID).Error; err != nil {
			return err
		}
	}

	dc.bindDeviceRecord(device, reason)
	return nil
}

func (dc *DeviceConnection) resolveExistingDevice(device *model.Device, code, mainboardSN, ip string) error {
	if dc.deviceID > 0 {
		if err := dc.db.First(device, dc.deviceID).Error; err == nil {
			return nil
		}
	}

	if code != "" {
		if err := dc.db.Where("code = ?", code).First(device).Error; err == nil {
			return nil
		}
	}

	if mainboardSN != "" {
		if err := dc.db.Where("mainboard_sn = ?", mainboardSN).First(device).Error; err == nil {
			return nil
		}
	}

	if ip != "" {
		if err := dc.db.Where("ip = ? AND code LIKE ?", ip, pendingDevicePrefix+"%").First(device).Error; err == nil {
			return nil
		}
	}

	return gorm.ErrRecordNotFound
}

func (dc *DeviceConnection) bindDeviceRecord(device model.Device, reason string) {
	oldCode := strings.TrimSpace(dc.deviceCode)
	dc.deviceCode = strings.TrimSpace(device.Code)
	dc.deviceID = device.ID
	if strings.TrimSpace(device.Name) != "" {
		dc.deviceName = strings.TrimSpace(device.Name)
	}

	if oldCode != "" && oldCode != dc.deviceCode && dc.connMgr != nil {
		dc.connMgr.Unregister(oldCode, dc)
	}
	if dc.deviceCode != "" && dc.connMgr != nil {
		dc.connMgr.Register(dc.deviceCode, dc)
	}

	emitTCPLog(dc.db, "info", true, "[TCP] Device session bound: code=%s id=%d reason=%s", dc.deviceCode, dc.deviceID, reason)
}

func fallbackString(value, fallback string) string {
	if strings.TrimSpace(value) == "" {
		return fallback
	}
	return strings.TrimSpace(value)
}

func (dc *DeviceConnection) resolveIdentifiedBy(code, mainboardSN string) string {
	if strings.TrimSpace(mainboardSN) != "" {
		return "mainboard"
	}
	if strings.HasPrefix(strings.TrimSpace(code), pendingDevicePrefix) {
		return "ip-pending"
	}
	return "protocol"
}

func (dc *DeviceConnection) updateDeviceRuntime(updates map[string]interface{}) {
	if dc.deviceID == 0 {
		return
	}
	if updates == nil {
		updates = make(map[string]interface{})
	}
	updates["last_protocol_at"] = time.Now()
	if err := dc.db.Model(&model.Device{}).Where("id = ?", dc.deviceID).Updates(updates).Error; err != nil {
		emitTCPLog(dc.db, "warn", true, "[TCP] Update device runtime failed: device=%s id=%d err=%v", dc.deviceCode, dc.deviceID, err)
	}
}
