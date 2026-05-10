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
	if code == "" || strings.HasPrefix(code, pendingDevicePrefix) {
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

	deviceType = model.DefaultDeviceType
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
			Type:         fallbackString(deviceType, model.DefaultDeviceType),
			ModelName:    fallbackString(modelName, "待识别"),
			IdentifiedBy: dc.resolveIdentifiedBy(code, mainboardSN),
			MainboardSN:  mainboardSN,
			IP:           ip,
			Status:       "idle",
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
			"status":        "idle",
			"last_online":   now,
			"identified_by": dc.resolveIdentifiedBy(code, mainboardSN),
		}
		if strings.TrimSpace(name) != "" {
			normalizedName := strings.TrimSpace(name)
			if shouldRefreshProtocolDeviceName(device, normalizedName) {
				updates["name"] = normalizedName
			}
			if shouldRefreshProtocolInitialName(device, normalizedName) {
				updates["initial_name"] = normalizedName
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
	if mainboardSN != "" {
		if err := dc.db.Where("mainboard_sn = ?", mainboardSN).First(device).Error; err == nil {
			return nil
		}
	}

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

	ensureDeviceRuntimeSession(dc.db, dc.deviceID, time.Now())
	emitTCPLog(dc.db, "info", true, "[TCP] Device session bound: code=%s id=%d reason=%s", dc.deviceCode, dc.deviceID, reason)
}

func (dc *DeviceConnection) rebindDeviceByMainboardSN(mainboardSN string) error {
	mainboardSN = strings.TrimSpace(mainboardSN)
	if mainboardSN == "" {
		return nil
	}

	var current model.Device
	if dc.deviceID > 0 {
		if err := dc.db.First(&current, dc.deviceID).Error; err != nil && !errors.Is(err, gorm.ErrRecordNotFound) {
			return err
		}
	}

	var existing model.Device
	err := dc.db.Where("mainboard_sn = ?", mainboardSN).Order("updated_at DESC, id DESC").First(&existing).Error
	switch {
	case err == nil && existing.ID > 0:
		if current.ID == 0 || existing.ID == current.ID {
			dc.bindDeviceRecord(existing, "device-flag")
			return nil
		}
		return dc.mergeDeviceRecord(current, existing, "device-flag")
	case err != nil && !errors.Is(err, gorm.ErrRecordNotFound):
		return err
	case current.ID > 0:
		return nil
	default:
		return gorm.ErrRecordNotFound
	}
}

func (dc *DeviceConnection) mergeDeviceRecord(source, target model.Device, reason string) error {
	if source.ID == 0 || target.ID == 0 || source.ID == target.ID {
		dc.bindDeviceRecord(target, reason)
		return nil
	}

	return dc.db.Transaction(func(tx *gorm.DB) error {
		updates := make(map[string]interface{})
		if shouldReplaceDeviceCode(target, source) {
			updates["code"] = strings.TrimSpace(source.Code)
		}
		if shouldRefreshProtocolDeviceName(target, source.Name) {
			updates["name"] = strings.TrimSpace(source.Name)
		}
		if shouldRefreshProtocolInitialName(target, source.InitialName) {
			updates["initial_name"] = strings.TrimSpace(source.InitialName)
		}
		if strings.TrimSpace(target.Type) == "" || target.Type == "模板机" {
			if strings.TrimSpace(source.Type) != "" && source.Type != "模板机" {
				updates["type"] = strings.TrimSpace(source.Type)
			}
		}
		if strings.TrimSpace(target.ModelName) == "" || target.ModelName == "待识别" || target.ModelName == "未知型号" {
			if strings.TrimSpace(source.ModelName) != "" && source.ModelName != "待识别" && source.ModelName != "未知型号" {
				updates["model_name"] = strings.TrimSpace(source.ModelName)
			}
		}
		if strings.TrimSpace(target.MainboardSN) == "" && strings.TrimSpace(source.MainboardSN) != "" {
			updates["mainboard_sn"] = strings.TrimSpace(source.MainboardSN)
		}
		if strings.TrimSpace(target.IP) == "" && strings.TrimSpace(source.IP) != "" {
			updates["ip"] = strings.TrimSpace(source.IP)
		}
		if target.GroupID == nil && source.GroupID != nil {
			updates["group_id"] = *source.GroupID
		}
		if strings.TrimSpace(target.IdentifiedBy) != "mainboard" {
			updates["identified_by"] = "mainboard"
		}
		if len(updates) > 0 {
			if err := tx.Model(&model.Device{}).Where("id = ?", target.ID).Updates(updates).Error; err != nil {
				return err
			}
		}
		if err := reassignDeviceReferences(tx, source.ID, target.ID); err != nil {
			return err
		}
		if err := tx.Delete(&model.Device{}, source.ID).Error; err != nil {
			return err
		}
		if err := tx.First(&target, target.ID).Error; err != nil {
			return err
		}
		dc.bindDeviceRecord(target, reason)
		return nil
	})
}

func reassignDeviceReferences(tx *gorm.DB, sourceID, targetID uint) error {
	if sourceID == 0 || targetID == 0 || sourceID == targetID {
		return nil
	}

	modelUpdates := []struct {
		model interface{}
		field string
	}{
		{&model.EmployeeDevice{}, "device_id"},
		{&model.DevicePatternFile{}, "device_id"},
		{&model.UploadTask{}, "device_id"},
		{&model.DownloadTask{}, "device_id"},
		{&model.ProductionRecord{}, "device_id"},
		{&model.AlarmRecord{}, "device_id"},
		{&model.SalaryRecord{}, "device_id"},
	}
	for _, item := range modelUpdates {
		if err := tx.Model(item.model).Where(item.field+" = ?", sourceID).Update(item.field, targetID).Error; err != nil {
			return err
		}
	}
	return nil
}

func (dc *DeviceConnection) ensureProductionDeviceBinding(payloadDeviceCode uint) error {
	if dc.deviceID > 0 {
		return nil
	}

	code := strings.TrimSpace(dc.deviceCode)
	if code == "" || strings.HasPrefix(code, pendingDevicePrefix) {
		if payloadDeviceCode > 0 {
			code = fmt.Sprintf("%d", payloadDeviceCode)
		}
	}
	if code == "" {
		return fmt.Errorf("production payload has no usable device code")
	}

	name := strings.TrimSpace(dc.deviceName)
	if name == "" || isPlaceholderDeviceName(name, code, dc.deviceFlag) {
		name = "设备" + code
	}

	deviceType := model.DefaultDeviceType
	modelName := "待识别"
	if dc.deviceModel > 0 {
		deviceType, modelName = mapDeviceModel(dc.deviceModel)
	}

	return dc.upsertDeviceRecord(code, name, deviceType, modelName, "production-bind")
}

func shouldReplaceDeviceCode(target, source model.Device) bool {
	sourceCode := strings.TrimSpace(source.Code)
	targetCode := strings.TrimSpace(target.Code)
	targetMainboard := strings.TrimSpace(target.MainboardSN)
	if sourceCode == "" || sourceCode == targetCode {
		return false
	}
	if strings.HasPrefix(sourceCode, pendingDevicePrefix) {
		return false
	}
	if targetCode == "" || strings.HasPrefix(targetCode, pendingDevicePrefix) {
		return true
	}
	if targetMainboard != "" && targetCode == targetMainboard {
		return true
	}
	return false
}

func fallbackString(value, fallback string) string {
	if strings.TrimSpace(value) == "" {
		return fallback
	}
	return strings.TrimSpace(value)
}

func shouldRefreshProtocolDeviceName(device model.Device, protocolName string) bool {
	protocolName = strings.TrimSpace(protocolName)
	if protocolName == "" {
		return false
	}

	currentName := strings.TrimSpace(device.Name)
	if currentName == "" {
		return true
	}
	if currentName == protocolName {
		return false
	}
	if currentName == strings.TrimSpace(device.InitialName) {
		return true
	}
	return isPlaceholderDeviceName(currentName, device.Code, device.MainboardSN)
}

func shouldRefreshProtocolInitialName(device model.Device, protocolName string) bool {
	protocolName = strings.TrimSpace(protocolName)
	if protocolName == "" {
		return false
	}

	currentInitialName := strings.TrimSpace(device.InitialName)
	return currentInitialName != protocolName
}

func isPlaceholderDeviceName(value, code, mainboardSN string) bool {
	value = strings.TrimSpace(value)
	code = strings.TrimSpace(code)
	mainboardSN = strings.TrimSpace(mainboardSN)
	if value == "" {
		return true
	}
	if strings.HasPrefix(value, "待识别设备") {
		return true
	}
	if code != "" && value == "设备 "+code {
		return true
	}
	if mainboardSN != "" && value == "设备 "+mainboardSN {
		return true
	}
	return false
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
	ensureDeviceRuntimeSession(dc.db, dc.deviceID, time.Now())
}
