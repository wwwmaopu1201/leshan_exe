package service

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"net"
	"strings"
	"sync"
	"time"
	"unicode"
	"unicode/utf8"

	"boer-lan-server/internal/model"

	"golang.org/x/text/encoding/simplifiedchinese"
	"gorm.io/gorm"
)

// DeviceConnection 管理单个设备的TCP连接
type DeviceConnection struct {
	conn           net.Conn
	db             *gorm.DB
	deviceCode     string
	deviceID       uint
	deviceFlag     string
	deviceModel    uint32
	deviceName     string
	lastHeartbeat  time.Time
	connMgr        *ConnectionManager
	warnedNoPacket bool
	stateMu        sync.RWMutex
	registered     bool
	registerAt     time.Time
	loopStopCh     chan struct{}
	patternMu      sync.Mutex
	patternPacketC chan *Packet
}

// NewDeviceConnection 创建新的设备连接处理器
func NewDeviceConnection(conn net.Conn, db *gorm.DB, connMgr *ConnectionManager) *DeviceConnection {
	return &DeviceConnection{
		conn:          conn,
		db:            db,
		lastHeartbeat: time.Now(),
		connMgr:       connMgr,
	}
}

// Handle 主处理循环
func (dc *DeviceConnection) Handle() {
	defer dc.cleanup()

	remoteAddr := dc.conn.RemoteAddr().String()
	emitTCPLog(dc.db, "info", true, "[TCP] New connection from %s", remoteAddr)
	dc.startHandshakeLoop()

	parser := NewFrameParser()
	readBuffer := make([]byte, 4096)

	for {
		_ = dc.conn.SetReadDeadline(time.Now().Add(15 * time.Second))
		n, err := dc.conn.Read(readBuffer)
		if err != nil {
			var netErr net.Error
			if errors.As(err, &netErr) && netErr.Timeout() {
				if !dc.warnedNoPacket {
					pendingLen := parser.PendingLen()
					if pendingLen == 0 {
						emitTCPLog(dc.db, "warn", true, "[TCP] Connection from %s established, but no protocol packet received within 15s", remoteAddr)
					} else {
						snapshot := parser.PendingSnapshot(96)
						emitTCPLog(dc.db, "warn", true,
							"[TCP] Connection from %s received %d raw bytes within 15s, but no complete protocol packet was parsed: %s",
							remoteAddr,
							pendingLen,
							packetDataPreview(snapshot, 96),
						)
					}
					dc.warnedNoPacket = true
				}
				continue
			}
			if !isConnClosed(err) {
				snapshot := parser.PendingSnapshot(96)
				if len(snapshot) > 0 {
					emitTCPLog(dc.db, "error", true,
						"[TCP] Parse error from %s (device=%s): %v raw=%s",
						remoteAddr,
						dc.deviceCode,
						err,
						packetDataPreview(snapshot, 96),
					)
				} else {
					emitTCPLog(dc.db, "error", true, "[TCP] Parse error from %s (device=%s): %v", remoteAddr, dc.deviceCode, err)
				}
			}
			return
		}
		if n <= 0 {
			continue
		}

		packets := parser.Push(readBuffer[:n])
		for _, pkt := range packets {
			if pkt == nil {
				continue
			}
			if pkt.ParseError != "" {
				emitTCPLog(dc.db, "warn", true,
					"[TCP] Invalid packet from %s (device=%s): %s raw=%s",
					remoteAddr,
					dc.deviceCode,
					pkt.ParseError,
					packetDataPreview(pkt.Raw, 96),
				)
				continue
			}

			dc.warnedNoPacket = false
			dc.lastHeartbeat = time.Now()

			emitTCPLog(
				dc.db,
				"",
				false,
				"[TCP] Packet from %s: addr1=%d addr2=%d type=0x%04X no=0x%04X frame=%d/%d len=%d data=%s",
				remoteAddr,
				pkt.Addr1,
				pkt.Addr2,
				pkt.ParamType,
				pkt.ParamNo,
				int(pkt.FrameNo),
				int(pkt.TotalFrames),
				len(pkt.Data),
				packetDataPreview(pkt.Data, 24),
			)
			if pkt.Recovered != "" {
				emitTCPLog(dc.db, "warn", true, "[TCP] Packet recovery applied: device=%s remote=%s detail=%s", dc.deviceCode, remoteAddr, pkt.Recovered)
			}
			dc.dispatch(pkt)
		}
	}
}

// dispatch 根据ParamType和ParamNo分发处理
func (dc *DeviceConnection) dispatch(pkt *Packet) {
	if dc.routePatternPacket(pkt) {
		return
	}

	switch {
	case pkt.ParamType == PTRegister && pkt.ParamNo == PNRegister:
		dc.handleRegister(pkt)
	case pkt.ParamType == PTDeviceInfo && pkt.ParamNo == PNDeviceInfo:
		dc.handleDeviceInfo(pkt)
	case pkt.ParamType == PTMainboardSN && pkt.ParamNo == PNMainboardSN:
		dc.handleMainboardSN(pkt)
	case pkt.ParamType == PTTimeSync && pkt.ParamNo == PNTimeSync:
		dc.handleTimeSync(pkt)
	case pkt.ParamType == PTHeartbeat && pkt.ParamNo == PNHeartbeat:
		dc.handleHeartbeat(pkt)
	case pkt.ParamType == PTSewing && pkt.ParamNo == PNSewing:
		dc.handleSewing(pkt)
	case pkt.ParamType == PTSewingRange && pkt.ParamNo == PNSewingRange:
		dc.handleSewingRange(pkt)
	case pkt.ParamType == PTPatternInfo && pkt.ParamNo == PNPatternInfo:
		dc.handlePatternInfo(pkt)
	case pkt.ParamType == PTProductionCount && pkt.ParamNo == PNProductionCount:
		dc.handleProductionCount(pkt)
	case pkt.ParamType == PTBottomThreadCount && pkt.ParamNo == PNBottomThreadCount:
		dc.handleBottomThreadCount(pkt)
	case pkt.ParamType == PTCurrentSpeed && pkt.ParamNo == PNCurrentSpeed:
		dc.handleCurrentSpeed(pkt)
	case pkt.ParamType == PTMaxSpeed && pkt.ParamNo == PNMaxSpeed:
		dc.handleMaxSpeed(pkt)
	case pkt.ParamType == PTNeedleCount && pkt.ParamNo == PNNeedleCount:
		dc.handleNeedleCount(pkt)
	case pkt.ParamType == PTRealtimeStatus && pkt.ParamNo == PNRealtimeStatus:
		dc.handleRealtimeStatus(pkt)
	case pkt.ParamType == PTThreadTrim && pkt.ParamNo == PNThreadTrim:
		dc.handleThreadTrim(pkt)
	case pkt.ParamType == PTReadHighPoint && pkt.ParamNo == PNReadHighPoint:
		dc.handleHighPoint(pkt)
	case pkt.ParamType == PTReadLowPoint && pkt.ParamNo == PNReadLowPoint:
		dc.handleLowPoint(pkt)
	case pkt.ParamType == PTSetHighPoint && pkt.ParamNo == PNSetHighPoint:
		dc.handleSetHighPoint(pkt)
	case pkt.ParamType == PTSetLowPoint && pkt.ParamNo == PNSetLowPoint:
		dc.handleSetLowPoint(pkt)
	case pkt.ParamType == PTSetSpeed && pkt.ParamNo == PNSetSpeed:
		dc.handleSetSpeed(pkt)
	case pkt.ParamType == PTAlarm && pkt.ParamNo == PNAlarm:
		dc.handleAlarm(pkt)
	case pkt.ParamType == PTIdleAlarm && pkt.ParamNo == PNIdleAlarm:
		dc.handleIdleAlarm(pkt)
	case pkt.ParamType == PTOilPrompt && pkt.ParamNo == PNOilPrompt:
		dc.handleOilPrompt(pkt)
	case pkt.ParamType == PTProduction && pkt.ParamNo == PNProduction:
		dc.handleProduction(pkt)
	case pkt.ParamType == PTProduction && pkt.ParamNo == PNProductionOld:
		dc.handleProductionOld(pkt)
	default:
		emitTCPLog(dc.db, "warn", true, "[TCP] Unknown command: ParamType=0x%04X ParamNo=0x%04X (device=%s addr1=%d addr2=%d)",
			pkt.ParamType, pkt.ParamNo, dc.deviceCode, pkt.Addr1, pkt.Addr2)
	}
}

func (dc *DeviceConnection) beginPatternSession() (chan *Packet, func(), error) {
	dc.patternMu.Lock()
	defer dc.patternMu.Unlock()

	if dc.patternPacketC != nil {
		return nil, nil, fmt.Errorf("device %s pattern transfer busy", dc.deviceCode)
	}

	ch := make(chan *Packet, 64)
	dc.patternPacketC = ch

	cleanup := func() {
		dc.patternMu.Lock()
		defer dc.patternMu.Unlock()
		if dc.patternPacketC == ch {
			dc.patternPacketC = nil
		}
	}

	return ch, cleanup, nil
}

func (dc *DeviceConnection) routePatternPacket(pkt *Packet) bool {
	if !isPatternCommand(pkt) {
		return false
	}

	dc.patternMu.Lock()
	ch := dc.patternPacketC
	dc.patternMu.Unlock()
	if ch == nil {
		return false
	}

	select {
	case ch <- pkt:
	default:
		emitTCPLog(dc.db, "warn", true,
			"[TCP] Pattern packet dropped due to full session buffer: device=%s type=0x%04X no=0x%04X",
			dc.deviceCode, pkt.ParamType, pkt.ParamNo)
	}
	return true
}

// handleRegister 处理注册消息：回复注册确认
func (dc *DeviceConnection) handleRegister(pkt *Packet) {
	dc.markRegistered("register")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("register")
	}
	emitTCPLog(dc.db, "info", true, "[TCP] Register request from %s addr1=%d addr2=%d len=%d data=%s",
		dc.conn.RemoteAddr(), pkt.Addr1, pkt.Addr2, len(pkt.Data), packetDataPreview(pkt.Data, 24))
	dc.send(buildProtocolReply(pkt, nil))
}

// handleDeviceInfo 处理设备信息：解析型号+编号+名称，upsert到数据库
func (dc *DeviceConnection) handleDeviceInfo(pkt *Packet) {
	dc.markRegistered("device-info")
	if len(pkt.Data) < 8 {
		emitTCPLog(dc.db, "warn", true, "[TCP] Device info data too short: %d bytes (expected at least 8)", len(pkt.Data))
		return
	}

	// 协议格式：
	// model(uint32) + deviceId(uint32) + name(text)
	modelCode := binary.BigEndian.Uint32(pkt.Data[0:4])
	deviceCodeNum := binary.BigEndian.Uint32(pkt.Data[4:8])
	deviceName := normalizeProtocolText(pkt.Data[8:])
	deviceType, modelName := mapDeviceModel(modelCode)
	code := strings.TrimSpace(dc.deviceCode)
	if code == "0" {
		code = ""
	}
	if deviceCodeNum > 0 {
		code = fmt.Sprintf("%d", deviceCodeNum)
	}
	if code == "" {
		code, _, _, _ = dc.provisionalIdentity()
	}
	if deviceName == "" {
		if strings.TrimSpace(dc.deviceName) != "" && !strings.Contains(dc.deviceName, pendingDevicePrefix) {
			deviceName = strings.TrimSpace(dc.deviceName)
		} else {
			deviceName = "设备" + code
		}
	}

	ip := extractIP(dc.conn.RemoteAddr().String())
	now := time.Now()
	mainboardSN := strings.TrimSpace(dc.deviceFlag)

	emitTCPLog(dc.db, "info", true,
		"[TCP] Device info: modelCode=%d code=%s name=%s type=%s model=%s ip=%s flag=%s",
		modelCode, code, deviceName, deviceType, modelName, ip, mainboardSN)
	dc.deviceModel = modelCode
	dc.deviceName = deviceName
	dc.lastHeartbeat = now

	if err := dc.upsertDeviceRecord(code, deviceName, deviceType, modelName, "device-info"); err != nil {
		emitTCPLog(dc.db, "error", true, "[TCP] Failed to upsert device %s: %v", code, err)
		return
	}

	emitTCPLog(dc.db, "info", true,
		"[TCP] Device registered: code=%s id=%d name=%s modelCode=%d model=%s ip=%s",
		dc.deviceCode, dc.deviceID, deviceName, modelCode, modelName, ip)
}

// handleMainboardSN 处理设备标志符，兼容沿用 mainboard_sn 字段存储
func (dc *DeviceConnection) handleMainboardSN(pkt *Packet) {
	dc.markRegistered("device-flag")
	sn := normalizeProtocolText(pkt.Data)
	dc.deviceFlag = sn
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("device-flag")
		emitTCPLog(dc.db, "info", true, "[TCP] Device flag cached before device registration: flag=%s remote=%s",
			sn, dc.conn.RemoteAddr())
		return
	}
	if err := dc.db.Model(&model.Device{}).Where("id = ?", dc.deviceID).Updates(map[string]interface{}{
		"mainboard_sn":     sn,
		"identified_by":    dc.resolveIdentifiedBy(dc.deviceCode, sn),
		"last_protocol_at": time.Now(),
	}).Error; err != nil {
		emitTCPLog(dc.db, "error", true, "[TCP] Failed to update device flag: device=%s flag=%s err=%v", dc.deviceCode, sn, err)
		return
	}
	emitTCPLog(dc.db, "info", true, "[TCP] Device flag updated: device=%s flag=%s", dc.deviceCode, sn)
}

// handleTimeSync 处理时间同步。
// 如果设备上传的是合法 BCD 时间，仅记录；若为空或非法，则按测试服务端规则回 8 字节当前时间。
func (dc *DeviceConnection) handleTimeSync(pkt *Packet) {
	dc.markRegistered("time-sync")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("time-sync")
	}
	if len(pkt.Data) >= 7 {
		deviceTime, err := parseProtocolBCDDateTime(pkt.Data)
		if err == nil {
			emitTCPLog(dc.db, "info", true, "[TCP] Time sync value from device %s: %s", dc.deviceCode, deviceTime.Format("2006-01-02 15:04:05"))
			return
		}
		emitTCPLog(dc.db, "warn", true,
			"[TCP] Invalid time sync payload from %s (device=%s): %v data=%s",
			dc.conn.RemoteAddr(),
			dc.deviceCode,
			err,
			packetDataPreview(pkt.Data, 24),
		)
	}

	dc.send(buildProtocolReply(pkt, encodeProtocolBCDDateTime(time.Now())))
}

// handleHeartbeat 处理心跳：回复固定空包，更新时间
func (dc *DeviceConnection) handleHeartbeat(pkt *Packet) {
	dc.markRegistered("heartbeat")
	dc.lastHeartbeat = time.Now()

	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("heartbeat")
		emitTCPLog(dc.db, "warn", true, "[TCP] Heartbeat received before device info: remote=%s addr1=%d addr2=%d len=%d",
			dc.conn.RemoteAddr(), pkt.Addr1, pkt.Addr2, len(pkt.Data))
	}

	if dc.deviceID > 0 {
		dc.db.Model(&model.Device{}).Where("id = ?", dc.deviceID).Updates(map[string]interface{}{
			"last_online": dc.lastHeartbeat,
			"status":      gorm.Expr("CASE WHEN status = 'offline' THEN 'online' ELSE status END"),
		})
		dc.updateDeviceRuntime(map[string]interface{}{})
	}

	dc.send(buildProtocolReply(pkt, nil))
}

// handleSewing 处理开始/停止缝制
func (dc *DeviceConnection) handleSewing(pkt *Packet) {
	dc.markRegistered("sewing-status")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("sewing-status")
	}
	if dc.deviceID == 0 || len(pkt.Data) < 1 {
		return
	}

	var status string
	switch pkt.Data[0] {
	case 0x01:
		status = "working"
		emitTCPLog(dc.db, "info", true, "[TCP] Device %s started sewing", dc.deviceCode)
	case 0x00:
		status = "idle"
		emitTCPLog(dc.db, "info", true, "[TCP] Device %s stopped sewing", dc.deviceCode)
	default:
		status = "online"
	}

	dc.db.Model(&model.Device{}).Where("id = ?", dc.deviceID).Update("status", status)
	dc.updateDeviceRuntime(map[string]interface{}{"status": status})
}

func (dc *DeviceConnection) handleSewingRange(pkt *Packet) {
	dc.markRegistered("sewing-range")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("sewing-range")
	}
	if len(pkt.Data) < 8 {
		return
	}
	xRange := int32(binary.BigEndian.Uint32(pkt.Data[0:4]))
	yRange := int32(binary.BigEndian.Uint32(pkt.Data[4:8]))
	dc.updateDeviceRuntime(map[string]interface{}{
		"sewing_range_x": int(xRange),
		"sewing_range_y": int(yRange),
	})
	emitTCPLog(dc.db, "info", false, "[TCP] Device sewing range: device=%s x=%d y=%d", dc.deviceCode, xRange, yRange)
}

func (dc *DeviceConnection) handlePatternInfo(pkt *Packet) {
	dc.markRegistered("pattern-info")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("pattern-info")
	}
	if len(pkt.Data) < 2 {
		return
	}
	patternNo := binary.BigEndian.Uint16(pkt.Data[0:2])
	patternName := normalizeProtocolText(pkt.Data[2:])
	dc.updateDeviceRuntime(map[string]interface{}{
		"current_pattern_no":   uint(patternNo),
		"current_pattern_name": patternName,
	})
	emitTCPLog(dc.db, "info", false, "[TCP] Device pattern info: device=%s patternNo=%d patternName=%s", dc.deviceCode, patternNo, patternName)
}

func (dc *DeviceConnection) handleProductionCount(pkt *Packet) {
	dc.markRegistered("production-count")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("production-count")
	}
	if len(pkt.Data) < 8 {
		return
	}
	totalCount := binary.BigEndian.Uint32(pkt.Data[0:4])
	currentCount := binary.BigEndian.Uint32(pkt.Data[4:8])
	dc.updateDeviceRuntime(map[string]interface{}{
		"production_total":   uint(totalCount),
		"production_current": uint(currentCount),
	})
	emitTCPLog(dc.db, "info", false, "[TCP] Device production count: device=%s total=%d current=%d", dc.deviceCode, totalCount, currentCount)
}

func (dc *DeviceConnection) handleBottomThreadCount(pkt *Packet) {
	dc.markRegistered("bottom-thread-count")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("bottom-thread-count")
	}
	if len(pkt.Data) < 8 {
		return
	}
	totalLength := binary.BigEndian.Uint32(pkt.Data[0:4])
	remainLength := binary.BigEndian.Uint32(pkt.Data[4:8])
	dc.updateDeviceRuntime(map[string]interface{}{
		"bottom_thread_total":  uint(totalLength),
		"bottom_thread_remain": uint(remainLength),
	})
	emitTCPLog(dc.db, "info", false, "[TCP] Device bottom thread count: device=%s total=%d remain=%d", dc.deviceCode, totalLength, remainLength)
}

func (dc *DeviceConnection) handleCurrentSpeed(pkt *Packet) {
	dc.markRegistered("current-speed")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("current-speed")
	}
	if len(pkt.Data) < 2 {
		return
	}
	currentSpeed := binary.BigEndian.Uint16(pkt.Data[0:2])
	dc.updateDeviceRuntime(map[string]interface{}{"current_speed": uint(currentSpeed)})
	emitTCPLog(dc.db, "info", false, "[TCP] Device current speed: device=%s speed=%d", dc.deviceCode, currentSpeed)
}

func (dc *DeviceConnection) handleMaxSpeed(pkt *Packet) {
	dc.markRegistered("max-speed")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("max-speed")
	}
	if len(pkt.Data) < 2 {
		return
	}
	maxSpeed := binary.BigEndian.Uint16(pkt.Data[0:2])
	dc.updateDeviceRuntime(map[string]interface{}{"max_speed_value": uint(maxSpeed)})
	emitTCPLog(dc.db, "info", false, "[TCP] Device max speed: device=%s speed=%d", dc.deviceCode, maxSpeed)
}

func (dc *DeviceConnection) handleNeedleCount(pkt *Packet) {
	dc.markRegistered("needle-count")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("needle-count")
	}
	if len(pkt.Data) < 4 {
		return
	}
	needleCount := binary.BigEndian.Uint32(pkt.Data[0:4])
	dc.updateDeviceRuntime(map[string]interface{}{"needle_count_value": uint(needleCount)})
	emitTCPLog(dc.db, "info", false, "[TCP] Device needle count: device=%s count=%d", dc.deviceCode, needleCount)
}

func (dc *DeviceConnection) handleRealtimeStatus(pkt *Packet) {
	dc.markRegistered("realtime-status")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("realtime-status")
	}
	if len(pkt.Data) < 3 {
		return
	}
	status := pkt.Data[0]
	patternNo := binary.BigEndian.Uint16(pkt.Data[1:3])
	patternName := normalizeProtocolText(pkt.Data[3:])
	dc.updateDeviceRuntime(map[string]interface{}{
		"current_pattern_no":   uint(patternNo),
		"current_pattern_name": patternName,
	})
	emitTCPLog(dc.db, "info", false,
		"[TCP] Device realtime status: device=%s status=%d patternNo=%d patternName=%s",
		dc.deviceCode,
		status,
		patternNo,
		patternName,
	)
}

func (dc *DeviceConnection) handleThreadTrim(pkt *Packet) {
	dc.markRegistered("thread-trim")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("thread-trim")
	}
	emitTCPLog(dc.db, "info", false, "[TCP] Device thread trim complete: device=%s", dc.deviceCode)
}

func (dc *DeviceConnection) handleHighPoint(pkt *Packet) {
	dc.markRegistered("high-point")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("high-point")
	}
	if len(pkt.Data) < 2 {
		return
	}
	value := binary.BigEndian.Uint16(pkt.Data[0:2])
	dc.updateDeviceRuntime(map[string]interface{}{"high_point_value": uint(value)})
	emitTCPLog(dc.db, "info", false, "[TCP] Device high point: device=%s value=%d", dc.deviceCode, value)
}

func (dc *DeviceConnection) handleLowPoint(pkt *Packet) {
	dc.markRegistered("low-point")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("low-point")
	}
	if len(pkt.Data) < 2 {
		return
	}
	value := binary.BigEndian.Uint16(pkt.Data[0:2])
	dc.updateDeviceRuntime(map[string]interface{}{"low_point_value": uint(value)})
	emitTCPLog(dc.db, "info", false, "[TCP] Device low point: device=%s value=%d", dc.deviceCode, value)
}

func (dc *DeviceConnection) handleSetHighPoint(pkt *Packet) {
	dc.markRegistered("set-high-point")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("set-high-point")
	}
	if len(pkt.Data) >= 2 {
		value := binary.BigEndian.Uint16(pkt.Data[0:2])
		dc.updateDeviceRuntime(map[string]interface{}{"high_point_value": uint(value)})
		emitTCPLog(dc.db, "info", false, "[TCP] Device set high point request: device=%s value=%d", dc.deviceCode, value)
	}
	dc.send(buildProtocolReply(pkt, []byte{0x00}))
}

func (dc *DeviceConnection) handleSetLowPoint(pkt *Packet) {
	dc.markRegistered("set-low-point")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("set-low-point")
	}
	if len(pkt.Data) >= 2 {
		value := binary.BigEndian.Uint16(pkt.Data[0:2])
		dc.updateDeviceRuntime(map[string]interface{}{"low_point_value": uint(value)})
		emitTCPLog(dc.db, "info", false, "[TCP] Device set low point request: device=%s value=%d", dc.deviceCode, value)
	}
	dc.send(buildProtocolReply(pkt, []byte{0x00}))
}

func (dc *DeviceConnection) handleSetSpeed(pkt *Packet) {
	dc.markRegistered("set-speed")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("set-speed")
	}
	if len(pkt.Data) >= 2 {
		value := binary.BigEndian.Uint16(pkt.Data[0:2])
		dc.updateDeviceRuntime(map[string]interface{}{"current_speed": uint(value)})
		emitTCPLog(dc.db, "info", false, "[TCP] Device set speed request: device=%s value=%d", dc.deviceCode, value)
	}
	dc.send(buildProtocolReply(pkt, []byte{0x00}))
}

// handleAlarm 处理报警消息
func (dc *DeviceConnection) handleAlarm(pkt *Packet) {
	dc.markRegistered("alarm")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("alarm")
	}
	if dc.deviceID == 0 || len(pkt.Data) < 2 {
		return
	}

	alarmCode := binary.BigEndian.Uint16(pkt.Data[0:2])

	if alarmCode != 0 {
		// 报警触发
		dc.db.Model(&model.Device{}).Where("id = ?", dc.deviceID).Update("status", "alarm")
		dc.updateDeviceRuntime(map[string]interface{}{
			"status":     "alarm",
			"alarm_code": fmt.Sprintf("%d", alarmCode),
		})
		dc.db.Create(&model.AlarmRecord{
			DeviceID:  dc.deviceID,
			AlarmCode: fmt.Sprintf("%d", alarmCode),
			AlarmType: classifyAlarm(alarmCode),
			Status:    "pending",
			StartTime: time.Now(),
		})
		emitTCPLog(dc.db, "warn", true, "[TCP] Device %s alarm: code=%d", dc.deviceCode, alarmCode)
	} else {
		// 报警解除
		dc.db.Model(&model.Device{}).Where("id = ?", dc.deviceID).Update("status", "online")
		dc.updateDeviceRuntime(map[string]interface{}{
			"status":     "online",
			"alarm_code": "",
		})
		// 关闭未解决的报警
		now := time.Now()
		dc.db.Model(&model.AlarmRecord{}).
			Where("device_id = ? AND status = ?", dc.deviceID, "pending").
			Updates(map[string]interface{}{
				"status":   "resolved",
				"end_time": now,
			})
		emitTCPLog(dc.db, "info", true, "[TCP] Device %s alarm cleared", dc.deviceCode)
	}
}

func (dc *DeviceConnection) handleIdleAlarm(pkt *Packet) {
	dc.markRegistered("idle-alarm")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("idle-alarm")
	}
	if len(pkt.Data) < 3 {
		return
	}
	minutes := binary.BigEndian.Uint16(pkt.Data[0:2])
	status := pkt.Data[2]
	emitTCPLog(dc.db, "info", false, "[TCP] Device idle alarm: device=%s minutes=%d status=%d", dc.deviceCode, minutes, status)
}

func (dc *DeviceConnection) handleOilPrompt(pkt *Packet) {
	dc.markRegistered("oil-prompt")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("oil-prompt")
	}
	if len(pkt.Data) < 1 {
		return
	}
	emitTCPLog(dc.db, "info", false, "[TCP] Device oil prompt: device=%s prompt=%d", dc.deviceCode, pkt.Data[0])
}

// handleProduction 处理生产数据
func (dc *DeviceConnection) handleProduction(pkt *Packet) {
	dc.markRegistered("production-new")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("production-new")
	}
	production, err := parseProductionDataNewPayload(pkt.Data)
	if err != nil {
		emitTCPLog(dc.db, "warn", true, "[TCP] Production data parse failed: %v data=%s", err, packetDataPreview(pkt.Data, 32))
		return
	}

	emitTCPLog(dc.db, "info", true,
		"[TCP] Production data: device=%s payloadDeviceId=%d patternId=%d patternName=%s start=%s startNeedle=%d end=%s endNeedle=%d userId=%s stopReason=%d",
		dc.deviceCode,
		production.DeviceCode,
		production.PatternID,
		production.PatternName,
		production.StartTime.Format("2006-01-02 15:04:05"),
		production.StartNeedle,
		production.EndTime.Format("2006-01-02 15:04:05"),
		production.EndNeedle,
		production.UserID,
		production.StopReason,
	)

	dc.send(buildProtocolReply(pkt, []byte{0x00}))
	dc.updateDeviceRuntime(map[string]interface{}{
		"current_pattern_no":   uint(production.PatternID),
		"current_pattern_name": production.PatternName,
	})

	if dc.deviceID == 0 {
		emitTCPLog(dc.db, "warn", true, "[TCP] Production data received before device registration: payloadDeviceId=%d remote=%s",
			production.DeviceCode, dc.conn.RemoteAddr())
		return
	}

	recordTime := production.EndTime
	if recordTime.IsZero() {
		recordTime = time.Now()
	}
	pieces := 1
	stitches := int64(0)
	if production.EndNeedle >= production.StartNeedle {
		stitches = int64(production.EndNeedle - production.StartNeedle)
	}
	runningHours := 0.0
	if !production.StartTime.IsZero() && !production.EndTime.IsZero() && production.EndTime.After(production.StartTime) {
		runningHours = production.EndTime.Sub(production.StartTime).Hours()
	}

	record := model.ProductionRecord{
		DeviceID:     dc.deviceID,
		Pieces:       pieces,
		Stitches:     stitches,
		ThreadLength: 0,
		RunningTime:  runningHours,
		IdleTime:     0,
		RecordDate:   recordTime,
	}

	if err := dc.db.Create(&record).Error; err != nil {
		emitTCPLog(dc.db, "error", true, "[TCP] Failed to save production record for device %s: %v", dc.deviceCode, err)
	} else {
		emitTCPLog(dc.db, "info", false, "[TCP] Production record saved: device=%s pieces=%d stitches=%d runningHours=%.3f", dc.deviceCode, pieces, stitches, runningHours)
	}
}

func (dc *DeviceConnection) handleProductionOld(pkt *Packet) {
	dc.markRegistered("production-old")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("production-old")
	}
	production, err := parseProductionDataOldPayload(pkt.Data)
	if err != nil {
		emitTCPLog(dc.db, "warn", true, "[TCP] Production old data parse failed: %v data=%s", err, packetDataPreview(pkt.Data, 32))
		return
	}

	emitTCPLog(dc.db, "info", true,
		"[TCP] Production old data: device=%s payloadDeviceId=%d patternId=%d patternName=%s start=%s startNeedle=%d end=%s endNeedle=%d stopReason=%d",
		dc.deviceCode,
		production.DeviceCode,
		production.PatternID,
		production.PatternName,
		production.StartTime.Format("2006-01-02 15:04:05"),
		production.StartNeedle,
		production.EndTime.Format("2006-01-02 15:04:05"),
		production.EndNeedle,
		production.StopReason,
	)

	dc.send(buildProtocolReply(pkt, []byte{0x00}))
	dc.updateDeviceRuntime(map[string]interface{}{
		"current_pattern_no":   uint(production.PatternID),
		"current_pattern_name": production.PatternName,
	})
}

// send 发送数据包
func (dc *DeviceConnection) send(pkt *Packet) {
	_ = dc.writePacket(pkt)
}

func (dc *DeviceConnection) writePacket(pkt *Packet) error {
	data := BuildPacket(pkt)
	if _, err := dc.conn.Write(data); err != nil {
		emitTCPLog(dc.db, "error", true, "[TCP] Send error to device %s: %v", dc.deviceCode, err)
		return err
	}
	return nil
}

// cleanup 连接关闭时清理
func (dc *DeviceConnection) cleanup() {
	dc.stopHandshakeLoop()
	dc.conn.Close()
	if dc.deviceCode != "" {
		if dc.connMgr != nil && !dc.connMgr.Unregister(dc.deviceCode, dc) {
			return
		}
		// 标记设备离线
		dc.db.Model(&model.Device{}).Where("id = ? AND status != ?", dc.deviceID, "offline").
			Update("status", "offline")
		emitTCPLog(dc.db, "info", true, "[TCP] Device disconnected: %s", dc.deviceCode)
	}
}

// 辅助函数

func packetDataPreview(data []byte, limit int) string {
	if len(data) == 0 {
		return "-"
	}
	if limit <= 0 || len(data) <= limit {
		return fmt.Sprintf("% X", data)
	}
	return fmt.Sprintf("% X...", data[:limit])
}

func buildProtocolReply(pkt *Packet, data []byte) *Packet {
	replyData := append([]byte(nil), data...)
	return &Packet{
		ParamType:   pkt.ParamType,
		ParamNo:     pkt.ParamNo,
		TotalFrames: 1,
		FrameNo:     1,
		Data:        replyData,
	}
}

func trimNullBytes(data []byte) string {
	for i, b := range data {
		if b == 0 {
			return string(data[:i])
		}
	}
	return string(data)
}

func normalizeProtocolText(data []byte) string {
	raw := bytes.TrimSpace(data)
	if len(raw) == 0 {
		return ""
	}

	if isLikelyUTF16LEText(raw) {
		if decoded := decodeUTF16LECString(raw); decoded != "" {
			return decoded
		}
	}

	raw = bytes.TrimRight(raw, "\x00")
	if len(raw) == 0 {
		return ""
	}

	if utf8.Valid(raw) {
		text := strings.TrimSpace(string(raw))
		if looksReasonableProtocolText(text) {
			return text
		}
	}

	if decoded := decodeGB18030Text(raw); decoded != "" {
		return decoded
	}

	fallback := strings.TrimSpace(trimNullBytes(raw))
	if looksReasonableProtocolText(fallback) {
		return fallback
	}
	return ""
}

func isLikelyUTF16LEText(data []byte) bool {
	if len(data) < 2 {
		return false
	}

	validUnits := 0
	invalidUnits := 0
	evenLength := len(data) - (len(data) % 2)
	for offset := 0; offset+1 < evenLength; offset += 2 {
		word := binary.LittleEndian.Uint16(data[offset : offset+2])
		if word == 0x0000 || word == 0xFDFD || word == 0xFFFF {
			break
		}

		isPrintableASCII := word >= 0x0020 && word <= 0x007E
		isCJK := (word >= 0x3400 && word <= 0x4DBF) ||
			(word >= 0x4E00 && word <= 0x9FFF) ||
			(word >= 0xF900 && word <= 0xFAFF)
		isPunctuation := (word >= 0x3000 && word <= 0x303F) ||
			(word >= 0xFF00 && word <= 0xFFEF)

		if isPrintableASCII || isCJK || isPunctuation {
			validUnits++
			continue
		}

		invalidUnits++
	}

	return validUnits > 0 && invalidUnits == 0
}

func decodeGB18030Text(data []byte) string {
	decoded, err := simplifiedchinese.GB18030.NewDecoder().Bytes(data)
	if err != nil {
		return ""
	}

	text := strings.TrimSpace(strings.TrimRight(string(decoded), "\x00"))
	if !looksReasonableProtocolText(text) {
		return ""
	}
	return text
}

func looksReasonableProtocolText(value string) bool {
	trimmed := strings.TrimSpace(strings.TrimRight(value, "\x00"))
	if trimmed == "" {
		return false
	}

	validCount := 0
	for _, r := range trimmed {
		switch {
		case r == utf8.RuneError:
			return false
		case unicode.IsControl(r) && !unicode.IsSpace(r):
			return false
		default:
			validCount++
		}
	}

	return validCount > 0
}

func extractIP(addr string) string {
	host, _, err := net.SplitHostPort(addr)
	if err != nil {
		return addr
	}
	return host
}

func toBCD(v byte) byte {
	return ((v / 10) << 4) | (v % 10)
}

func fromBCD(v byte) (byte, error) {
	hi := (v >> 4) & 0x0F
	lo := v & 0x0F
	if hi > 9 || lo > 9 {
		return 0, fmt.Errorf("invalid BCD byte 0x%02X", v)
	}
	return hi*10 + lo, nil
}

func encodeProtocolBCDDateTime(ts time.Time) []byte {
	weekday := int(ts.Weekday())
	if weekday == 0 {
		weekday = 7
	}
	return []byte{
		toBCD(byte(ts.Year() % 100)),
		toBCD(byte(ts.Month())),
		toBCD(byte(ts.Day())),
		toBCD(byte(weekday)),
		toBCD(byte(ts.Hour())),
		toBCD(byte(ts.Minute())),
		toBCD(byte(ts.Second())),
		0x00,
	}
}

func parseProtocolBCDDateTime(data []byte) (time.Time, error) {
	if len(data) < 7 {
		return time.Time{}, fmt.Errorf("payload too short: %d", len(data))
	}

	year, err := fromBCD(data[0])
	if err != nil {
		return time.Time{}, err
	}
	month, err := fromBCD(data[1])
	if err != nil {
		return time.Time{}, err
	}
	day, err := fromBCD(data[2])
	if err != nil {
		return time.Time{}, err
	}
	week, err := fromBCD(data[3])
	if err != nil {
		return time.Time{}, err
	}
	hour, err := fromBCD(data[4])
	if err != nil {
		return time.Time{}, err
	}
	minute, err := fromBCD(data[5])
	if err != nil {
		return time.Time{}, err
	}
	second, err := fromBCD(data[6])
	if err != nil {
		return time.Time{}, err
	}

	if month < 1 || month > 12 {
		return time.Time{}, fmt.Errorf("invalid month %d", month)
	}
	if day < 1 || day > 31 {
		return time.Time{}, fmt.Errorf("invalid day %d", day)
	}
	if week < 1 || week > 7 {
		return time.Time{}, fmt.Errorf("invalid weekday %d", week)
	}
	if hour > 23 || minute > 59 || second > 59 {
		return time.Time{}, fmt.Errorf("invalid time %02d:%02d:%02d", hour, minute, second)
	}

	return time.Date(2000+int(year), time.Month(month), int(day), int(hour), int(minute), int(second), 0, time.Local), nil
}

func mapDeviceModel(code uint32) (deviceType, modelName string) {
	if code == 0 {
		return "模板机", "未知型号"
	}
	return "模板机", fmt.Sprintf("%d", code)
}

type productionDataNew struct {
	DeviceCode  uint32
	PatternID   uint16
	PatternName string
	StartTime   time.Time
	StartNeedle uint32
	EndTime     time.Time
	EndNeedle   uint32
	UserID      string
	StopReason  uint16
}

type productionDataOld struct {
	DeviceCode  uint32
	PatternID   uint16
	PatternName string
	StartTime   time.Time
	StartNeedle uint32
	EndTime     time.Time
	EndNeedle   uint32
	StopReason  uint16
}

func parseProductionDataNewPayload(data []byte) (*productionDataNew, error) {
	if len(data) < 82 {
		return nil, fmt.Errorf("payload too short: %d", len(data))
	}

	startTime, err := parseProtocolBCDDateTime(data[50:57])
	if err != nil {
		return nil, fmt.Errorf("invalid start time: %w", err)
	}
	endTime, err := parseProtocolBCDDateTime(data[61:68])
	if err != nil {
		return nil, fmt.Errorf("invalid end time: %w", err)
	}

	return &productionDataNew{
		DeviceCode:  binary.BigEndian.Uint32(data[0:4]),
		PatternID:   binary.BigEndian.Uint16(data[4:6]),
		PatternName: normalizeProtocolText(data[6:50]),
		StartTime:   startTime,
		StartNeedle: binary.BigEndian.Uint32(data[57:61]),
		EndTime:     endTime,
		EndNeedle:   binary.BigEndian.Uint32(data[68:72]),
		UserID:      normalizeProtocolText(data[72:80]),
		StopReason:  binary.BigEndian.Uint16(data[80:82]),
	}, nil
}

func parseProductionDataOldPayload(data []byte) (*productionDataOld, error) {
	const tailBytes = 7 + 4 + 7 + 4 + 2
	if len(data) < 6+tailBytes {
		return nil, fmt.Errorf("payload too short: %d", len(data))
	}

	patternNameEnd := len(data) - tailBytes
	startTime, err := parseProtocolBCDDateTime(data[patternNameEnd : patternNameEnd+7])
	if err != nil {
		return nil, fmt.Errorf("invalid start time: %w", err)
	}
	endTime, err := parseProtocolBCDDateTime(data[patternNameEnd+11 : patternNameEnd+18])
	if err != nil {
		return nil, fmt.Errorf("invalid end time: %w", err)
	}

	return &productionDataOld{
		DeviceCode:  binary.BigEndian.Uint32(data[0:4]),
		PatternID:   binary.BigEndian.Uint16(data[4:6]),
		PatternName: normalizeProtocolText(data[6:patternNameEnd]),
		StartTime:   startTime,
		StartNeedle: binary.BigEndian.Uint32(data[patternNameEnd+7 : patternNameEnd+11]),
		EndTime:     endTime,
		EndNeedle:   binary.BigEndian.Uint32(data[patternNameEnd+18 : patternNameEnd+22]),
		StopReason:  binary.BigEndian.Uint16(data[patternNameEnd+22 : patternNameEnd+24]),
	}, nil
}

func classifyAlarm(code uint16) string {
	switch {
	case code >= 1 && code <= 100:
		return "断线"
	case code >= 101 && code <= 200:
		return "张力"
	case code >= 201 && code <= 300:
		return "电机"
	default:
		return "传感器"
	}
}

func isConnClosed(err error) bool {
	if err == io.EOF {
		return true
	}
	s := err.Error()
	return strings.Contains(s, "use of closed network connection") ||
		strings.Contains(s, "connection reset by peer") ||
		strings.Contains(s, "broken pipe")
}
