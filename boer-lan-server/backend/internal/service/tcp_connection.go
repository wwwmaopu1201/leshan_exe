package service

import (
	"bytes"
	"context"
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

	"boer-lan-server/internal/alarmcatalog"
	"boer-lan-server/internal/model"

	"golang.org/x/text/encoding/simplifiedchinese"
	"gorm.io/gorm"
)

const (
	patternSessionAcquireTimeout = 30 * time.Second
	patternSessionMaxDuration    = 30 * time.Minute
)

var ErrPatternTransferBusy = errors.New("device pattern transfer busy")

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
	offlineProbeAt time.Time
	loopStopCh     chan struct{}
	patternMu      sync.Mutex
	patternPacketC chan *Packet
	patternDoneC   chan struct{}
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
			dc.clearOfflineProbe()

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
	case pkt.ParamType == PTWorkUser && pkt.ParamNo == PNUpdateCurrentUserID:
		dc.handleUpdateCurrentUserResult(pkt)
	case pkt.ParamType == PTWorkUser && pkt.ParamNo == PNWorkStart:
		dc.handleWorkStart(pkt)
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
	ctx, cancel := context.WithTimeout(context.Background(), patternSessionAcquireTimeout)
	defer cancel()
	return dc.beginPatternSessionContext(ctx, true)
}

func (dc *DeviceConnection) tryBeginPatternSession() (chan *Packet, func(), error) {
	return dc.beginPatternSessionContext(context.Background(), false)
}

func (dc *DeviceConnection) beginPatternSessionContext(ctx context.Context, wait bool) (chan *Packet, func(), error) {
	for {
		dc.patternMu.Lock()
		if dc.patternPacketC == nil {
			ch := make(chan *Packet, 64)
			doneCh := make(chan struct{})
			dc.patternPacketC = ch
			dc.patternDoneC = doneCh

			var once sync.Once
			var timer *time.Timer
			cleanup := func() {
				once.Do(func() {
					if timer != nil {
						timer.Stop()
					}
					dc.patternMu.Lock()
					defer dc.patternMu.Unlock()
					if dc.patternPacketC == ch {
						dc.patternPacketC = nil
						dc.patternDoneC = nil
						close(doneCh)
					}
				})
			}
			timer = time.AfterFunc(patternSessionMaxDuration, func() {
				emitTCPLog(dc.db, "warn", true, "[TCP] Pattern session force released after timeout: device=%s", dc.deviceCode)
				cleanup()
			})
			dc.patternMu.Unlock()
			return ch, cleanup, nil
		}

		doneCh := dc.patternDoneC
		dc.patternMu.Unlock()
		if !wait {
			return nil, nil, fmt.Errorf("%w: device %s", ErrPatternTransferBusy, dc.deviceCode)
		}

		select {
		case <-ctx.Done():
			return nil, nil, fmt.Errorf("device %s pattern transfer wait timeout: %w", dc.deviceCode, ErrPatternTransferBusy)
		case <-doneCh:
		}
	}
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
	if sn == "" {
		return
	}
	dc.deviceFlag = sn
	if dc.deviceID == 0 {
		if err := dc.upsertDeviceRecord(sn, "", "", "", "device-flag"); err != nil {
			emitTCPLog(dc.db, "error", true, "[TCP] Failed to bind device by mainboard flag=%s err=%v", sn, err)
			return
		}
	} else {
		if err := dc.rebindDeviceByMainboardSN(sn); err != nil {
			emitTCPLog(dc.db, "error", true, "[TCP] Failed to rebind device by mainboard flag=%s err=%v", sn, err)
			return
		}
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

func (dc *DeviceConnection) handleUpdateCurrentUserResult(pkt *Packet) {
	dc.markRegistered("current-user-update")
	result, ok := parseCurrentUserResult(pkt.Data)
	if !ok {
		emitTCPLog(dc.db, "warn", true, "[TCP] Current user update response missing result: device=%s data=%s",
			dc.deviceCode,
			packetDataPreview(pkt.Data, 32),
		)
		return
	}
	if result == 0 {
		emitTCPLog(dc.db, "info", true, "[TCP] Current user update accepted: device=%s", dc.deviceCode)
		return
	}
	emitTCPLog(dc.db, "warn", true, "[TCP] Current user update rejected: device=%s result=%d", dc.deviceCode, result)
}

func (dc *DeviceConnection) handleWorkStart(pkt *Packet) {
	dc.markRegistered("work-start")
	if dc.deviceID == 0 {
		dc.ensurePlaceholderDevice("work-start")
	}

	employeeCode := parseWorkStartUserID(pkt.Data)
	if employeeCode == "" {
		emitTCPLog(dc.db, "warn", true, "[TCP] Work start rejected: empty employee code device=%s data=%s",
			dc.deviceCode,
			packetDataPreview(pkt.Data, 32),
		)
		dc.sendWorkStartAck(pkt, "", "", 1)
		return
	}

	employee, err := findEmployeeByCode(dc.db, employeeCode)
	switch {
	case err == nil:
		if dc.deviceID > 0 {
			dc.updateDeviceRuntime(map[string]interface{}{
				"employee_code": employee.Code,
				"employee_name": employee.Name,
			})
		}
		emitTCPLog(dc.db, "info", true, "[TCP] Work start accepted: device=%s employeeCode=%s employeeName=%s",
			dc.deviceCode,
			employee.Code,
			employee.Name,
		)
		dc.sendWorkStartAck(pkt, employee.Code, employee.Name, 0)
	case errors.Is(err, gorm.ErrRecordNotFound):
		emitTCPLog(dc.db, "warn", true, "[TCP] Work start rejected: employee code not found device=%s employeeCode=%s",
			dc.deviceCode,
			employeeCode,
		)
		dc.sendWorkStartAck(pkt, employeeCode, "", 1)
	default:
		emitTCPLog(dc.db, "error", true, "[TCP] Work start employee lookup failed: device=%s employeeCode=%s err=%v",
			dc.deviceCode,
			employeeCode,
			err,
		)
		dc.sendWorkStartAck(pkt, employeeCode, "", 1)
	}
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
		alarm := alarmcatalog.Describe(alarmCode)
		// 报警触发
		dc.db.Model(&model.Device{}).Where("id = ?", dc.deviceID).Update("status", "alarm")
		dc.updateDeviceRuntime(map[string]interface{}{
			"status":     "alarm",
			"alarm_code": alarm.Code,
		})
		dc.db.Create(&model.AlarmRecord{
			DeviceID:    dc.deviceID,
			AlarmCode:   alarm.Code,
			AlarmType:   alarm.Display(),
			Description: alarm.Description,
			Status:      "pending",
			StartTime:   time.Now(),
		})
		emitTCPLog(dc.db, "warn", true, "[TCP] Device %s alarm: code=%d display=%s", dc.deviceCode, alarmCode, alarm.Display())
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
		if err := dc.ensureProductionDeviceBinding(uint(production.DeviceCode)); err != nil {
			emitTCPLog(dc.db, "warn", true, "[TCP] Production data received before device registration and binding failed: payloadDeviceId=%d remote=%s err=%v",
				production.DeviceCode, dc.conn.RemoteAddr(), err)
			return
		}
	}

	record, created, err := dc.syncProductionSnapshot(productionSnapshot{
		PatternNo:      uint(production.PatternID),
		PatternName:    production.PatternName,
		ProtocolUserID: production.UserID,
		StartTime:      production.StartTime,
		EndTime:        production.EndTime,
		StartNeedle:    production.StartNeedle,
		EndNeedle:      production.EndNeedle,
		StopReason:     production.StopReason,
	})
	if err != nil {
		emitTCPLog(dc.db, "error", true, "[TCP] Failed to sync production record for device %s: %v", dc.deviceCode, err)
		return
	}
	if record == nil {
		return
	}

	if created {
		emitTCPLog(dc.db, "info", false,
			"[TCP] Production record saved: device=%s productionId=%d employeeId=%d patternId=%d pieces=%d unitPrice=%.3f",
			dc.deviceCode,
			record.ID,
			record.EmployeeID,
			record.PatternID,
			record.Pieces,
			record.UnitPrice,
		)
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

	if dc.deviceID == 0 {
		if err := dc.ensureProductionDeviceBinding(uint(production.DeviceCode)); err != nil {
			emitTCPLog(dc.db, "warn", true, "[TCP] Production old data received before device registration and binding failed: payloadDeviceId=%d remote=%s err=%v",
				production.DeviceCode, dc.conn.RemoteAddr(), err)
			return
		}
	}

	record, created, err := dc.syncProductionSnapshot(productionSnapshot{
		PatternNo:   uint(production.PatternID),
		PatternName: production.PatternName,
		StartTime:   production.StartTime,
		EndTime:     production.EndTime,
		StartNeedle: production.StartNeedle,
		EndNeedle:   production.EndNeedle,
		StopReason:  production.StopReason,
	})
	if err != nil {
		emitTCPLog(dc.db, "error", true, "[TCP] Failed to sync old production record for device %s: %v", dc.deviceCode, err)
		return
	}
	if created && record != nil {
		emitTCPLog(dc.db, "info", false,
			"[TCP] Production old record saved: device=%s productionId=%d employeeId=%d patternId=%d pieces=%d unitPrice=%.3f",
			dc.deviceCode,
			record.ID,
			record.EmployeeID,
			record.PatternID,
			record.Pieces,
			record.UnitPrice,
		)
	}
}

// send 发送数据包
func (dc *DeviceConnection) send(pkt *Packet) {
	_ = dc.writePacket(pkt)
}

func (dc *DeviceConnection) writePacket(pkt *Packet) error {
	data := BuildPacket(pkt)
	return dc.writeRawPacket(data)
}

func (dc *DeviceConnection) writeRawPacket(data []byte) error {
	if _, err := dc.conn.Write(data); err != nil {
		emitTCPLog(dc.db, "error", true, "[TCP] Send error to device %s: %v", dc.deviceCode, err)
		return err
	}
	return nil
}

func (dc *DeviceConnection) SendCurrentUser(employeeCode, employeeName string) error {
	return dc.writePacket(buildUpdateCurrentUserIDCommand(employeeCode, employeeName))
}

func (dc *DeviceConnection) sendWorkStartAck(request *Packet, employeeCode, employeeName string, result byte) {
	reply := buildWorkStartAckReply(request, employeeCode, employeeName, result)
	raw := BuildPacket(reply)
	emitTCPLog(dc.db, "info", true,
		"[TCP] Work start ack sent: device=%s requestNo=0x%04X type=0x%04X no=0x%04X employeeCode=%s employeeName=%s result=%d dataLen=%d data=%s rawLen=%d raw=%s",
		dc.deviceCode,
		request.ParamNo,
		reply.ParamType,
		reply.ParamNo,
		employeeCode,
		employeeName,
		result,
		len(reply.Data),
		packetDataPreview(reply.Data, 32),
		len(raw),
		packetDataPreview(raw, 128),
	)
	_ = dc.writeRawPacket(raw)
	if result == 0 {
		dc.sendWorkStartCurrentUserUpdate(employeeCode, employeeName)
	}
}

func (dc *DeviceConnection) sendCurrentUserUpdate(employeeCode, employeeName string) {
	pkt := buildUpdateCurrentUserIDCommand(employeeCode, employeeName)
	emitTCPLog(dc.db, "info", true,
		"[TCP] Current user update sent: device=%s type=0x%04X no=0x%04X employeeCode=%s employeeName=%s len=%d data=%s",
		dc.deviceCode,
		pkt.ParamType,
		pkt.ParamNo,
		employeeCode,
		employeeName,
		len(pkt.Data),
		packetDataPreview(pkt.Data, 48),
	)
	dc.send(pkt)
}

func (dc *DeviceConnection) sendWorkStartCurrentUserUpdate(employeeCode, employeeName string) {
	pkt := buildWorkStartCurrentUserCommand(employeeCode, employeeName)
	raw := BuildPacket(pkt)
	emitTCPLog(dc.db, "info", true,
		"[TCP] Work start current user update sent: device=%s type=0x%04X no=0x%04X employeeCode=%s employeeName=%s len=%d data=%s rawLen=%d raw=%s",
		dc.deviceCode,
		pkt.ParamType,
		pkt.ParamNo,
		employeeCode,
		employeeName,
		len(pkt.Data),
		packetDataPreview(pkt.Data, 32),
		len(raw),
		packetDataPreview(raw, 128),
	)
	_ = dc.writeRawPacket(raw)
}

func (dc *DeviceConnection) clearOfflineProbe() {
	dc.stateMu.Lock()
	dc.offlineProbeAt = time.Time{}
	dc.stateMu.Unlock()
}

func (dc *DeviceConnection) shouldCloseAfterIdleProbe(now time.Time) bool {
	dc.stateMu.Lock()
	defer dc.stateMu.Unlock()
	if dc.offlineProbeAt.IsZero() {
		dc.offlineProbeAt = now
		return false
	}
	return now.Sub(dc.offlineProbeAt) >= OfflineProbeGrace
}

func (dc *DeviceConnection) sendIdleProbe() error {
	return dc.writePacket(buildProtocolCommand(PTRegister, PNRegister, nil))
}

// cleanup 连接关闭时清理
func (dc *DeviceConnection) cleanup() {
	dc.stopHandshakeLoop()
	dc.conn.Close()
	if dc.deviceCode != "" {
		if dc.connMgr != nil && !dc.connMgr.Unregister(dc.deviceCode, dc) {
			return
		}
		dc.scheduleOfflineTransition()
	}
}

func (dc *DeviceConnection) scheduleOfflineTransition() {
	deviceCode := strings.TrimSpace(dc.deviceCode)
	deviceFlag := strings.TrimSpace(dc.deviceFlag)
	deviceID := dc.deviceID
	if deviceCode == "" && deviceFlag == "" && deviceID == 0 {
		return
	}

	time.AfterFunc(ReconnectGracePeriod, func() {
		if dc.connMgr != nil && dc.connMgr.HasBoundDevice(deviceID, deviceCode, deviceFlag, dc) {
			emitTCPLog(dc.db, "info", false, "[TCP] Device reconnect handoff detected, skip offline transition: device=%s flag=%s id=%d", deviceCode, deviceFlag, deviceID)
			return
		}

		if deviceID > 0 {
			offlineAt := time.Now()
			dc.db.Model(&model.Device{}).Where("id = ? AND status != ?", deviceID, "offline").
				Update("status", "offline")
			closeDeviceRuntimeSessions(dc.db, deviceID, offlineAt, "disconnect")
		}
		emitTCPLog(dc.db, "info", true, "[TCP] Device disconnected: %s", deviceCode)
	})
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
	raw := data
	if len(bytes.Trim(raw, "\x00 \t\r\n")) == 0 {
		return ""
	}

	if isLikelyUTF16LEText(raw) {
		if decoded := decodeUTF16LECString(raw); decoded != "" {
			return decoded
		}
	}

	raw = bytes.TrimSpace(bytes.TrimRight(raw, "\x00"))
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

func normalizeCompatibleProtocolText(data []byte) string {
	raw := bytes.TrimSpace(data)
	if len(raw) == 0 {
		return ""
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

	meaningfulEnd := len(data)
	if meaningfulEnd%2 != 0 {
		meaningfulEnd--
	}
	for offset := 0; offset+1 < meaningfulEnd; offset += 2 {
		word := binary.LittleEndian.Uint16(data[offset : offset+2])
		if word == 0x0000 || word == 0xFDFD || word == 0xFFFF {
			meaningfulEnd = offset
			break
		}
	}
	if meaningfulEnd <= 0 {
		return false
	}
	if firstNull := bytes.IndexByte(data[:meaningfulEnd], 0x00); firstNull > 1 && isPlainASCIIBytes(data[:firstNull]) {
		return false
	}
	if isPlainASCIIBytes(data[:meaningfulEnd]) {
		return false
	}

	validUnits := 0
	invalidUnits := 0
	for offset := 0; offset+1 < meaningfulEnd; offset += 2 {
		word := binary.LittleEndian.Uint16(data[offset : offset+2])
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

func isPlainASCIIBytes(data []byte) bool {
	if len(data) == 0 {
		return false
	}
	for _, b := range data {
		if b < 0x20 || b > 0x7E {
			return false
		}
	}
	return true
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
	// 部分下位机这里会填 0，业务上并不依赖星期字段，放宽校验避免整条生产数据丢弃。
	if week > 7 {
		return time.Time{}, fmt.Errorf("invalid weekday %d", week)
	}
	if hour > 23 || minute > 59 || second > 59 {
		return time.Time{}, fmt.Errorf("invalid time %02d:%02d:%02d", hour, minute, second)
	}

	return time.Date(2000+int(year), time.Month(month), int(day), int(hour), int(minute), int(second), 0, time.Local), nil
}

func mapDeviceModel(code uint32) (deviceType, modelName string) {
	if code == 0 {
		return model.DefaultDeviceType, "未知型号"
	}
	return model.DefaultDeviceType, fmt.Sprintf("%d", code)
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
		UserID:      parseProductionUserID(data),
		StopReason:  parseProductionStopReason(data),
	}, nil
}

func parseProductionUserID(data []byte) string {
	userBeforeStop := normalizeProtocolText(data[72:80])
	userAfterStop := normalizeProtocolText(data[74:82])
	if shouldUseStopBeforeUserLayout(userBeforeStop, binary.BigEndian.Uint16(data[80:82]), userAfterStop, binary.BigEndian.Uint16(data[72:74])) {
		return userAfterStop
	}
	return userBeforeStop
}

func parseProductionStopReason(data []byte) uint16 {
	userBeforeStop := normalizeProtocolText(data[72:80])
	stopAfterUser := binary.BigEndian.Uint16(data[80:82])
	userAfterStop := normalizeProtocolText(data[74:82])
	stopBeforeUser := binary.BigEndian.Uint16(data[72:74])
	if shouldUseStopBeforeUserLayout(userBeforeStop, stopAfterUser, userAfterStop, stopBeforeUser) {
		return stopBeforeUser
	}
	return stopAfterUser
}

func shouldUseStopBeforeUserLayout(userBeforeStop string, stopAfterUser uint16, userAfterStop string, stopBeforeUser uint16) bool {
	if userAfterStop == "" {
		return false
	}
	if userBeforeStop == "" {
		return true
	}

	// 协议文档存在两处尾部字段顺序描述。部分设备实际为“停止原因 + 用户ID”；
	// 此时按旧顺序读取时，停止原因会变成用户ID末尾两个 ASCII 字符。
	return stopAfterUser > 1000 && stopBeforeUser <= 255
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

func isConnClosed(err error) bool {
	if err == io.EOF {
		return true
	}
	s := err.Error()
	return strings.Contains(s, "use of closed network connection") ||
		strings.Contains(s, "connection reset by peer") ||
		strings.Contains(s, "broken pipe")
}
