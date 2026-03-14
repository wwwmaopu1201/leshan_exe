package service

import (
	"bufio"
	"encoding/binary"
	"fmt"
	"io"
	"log"
	"net"
	"strings"
	"time"

	"boer-lan-server/internal/model"

	"gorm.io/gorm"
)

// DeviceConnection 管理单个设备的TCP连接
type DeviceConnection struct {
	conn          net.Conn
	db            *gorm.DB
	deviceCode    string
	deviceID      uint
	lastHeartbeat time.Time
	connMgr       *ConnectionManager
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
	log.Printf("[TCP] New connection from %s", remoteAddr)

	reader := bufio.NewReader(dc.conn)

	for {
		pkt, err := ParsePacket(reader)
		if err != nil {
			if !isConnClosed(err) {
				log.Printf("[TCP] Parse error from %s (device=%s): %v", remoteAddr, dc.deviceCode, err)
			}
			return
		}

		dc.dispatch(pkt)
	}
}

// dispatch 根据ParamType和ParamNo分发处理
func (dc *DeviceConnection) dispatch(pkt *Packet) {
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
	case pkt.ParamType == PTAlarm && pkt.ParamNo == PNAlarm:
		dc.handleAlarm(pkt)
	case pkt.ParamType == PTProduction && pkt.ParamNo == PNProduction:
		dc.handleProduction(pkt)
	default:
		log.Printf("[TCP] Unknown command: ParamType=0x%04X ParamNo=0x%04X (device=%s)", pkt.ParamType, pkt.ParamNo, dc.deviceCode)
	}
}

// handleRegister 处理注册消息：回复注册确认
func (dc *DeviceConnection) handleRegister(pkt *Packet) {
	log.Printf("[TCP] Register request from %s", dc.conn.RemoteAddr())
	// 回复注册：交换Addr1和Addr2，保持相同ParamType/ParamNo
	reply := &Packet{
		Addr1:       pkt.Addr2,
		Addr2:       pkt.Addr1,
		ParamType:   pkt.ParamType,
		ParamNo:     pkt.ParamNo,
		TotalFrames: 1,
		FrameNo:     0,
		Data:        pkt.Data,
	}
	dc.send(reply)
}

// handleDeviceInfo 处理设备信息：解析型号+编号+名称，upsert到数据库
func (dc *DeviceConnection) handleDeviceInfo(pkt *Packet) {
	if len(pkt.Data) < 6 {
		log.Printf("[TCP] Device info data too short: %d bytes", len(pkt.Data))
		return
	}

	// 数据格式：device_type(2B) + device_code(2B) + device_name(剩余)
	// device_type: 设备类型编号
	// device_code: 设备编号（16位整数）
	// device_name: 设备名称（字符串，可能以0结尾）
	deviceTypeCode := binary.BigEndian.Uint16(pkt.Data[0:2])
	deviceCodeNum := binary.BigEndian.Uint16(pkt.Data[2:4])

	// 设备名称从第4字节开始
	deviceName := trimNullBytes(pkt.Data[4:])

	// 根据类型编号映射设备类型和型号
	deviceType, modelName := mapDeviceType(deviceTypeCode)

	// 设备编号格式化为字符串
	code := fmt.Sprintf("%d", deviceCodeNum)

	ip := extractIP(dc.conn.RemoteAddr().String())

	log.Printf("[TCP] Device info: type=%s model=%s code=%s name=%s ip=%s",
		deviceType, modelName, code, deviceName, ip)

	// Upsert设备记录
	var device model.Device
	result := dc.db.Where(model.Device{Code: code}).Assign(model.Device{
		Name:        deviceName,
		InitialName: deviceName,
		Type:        deviceType,
		ModelName:   modelName,
		IP:          ip,
		Status:      "online",
		LastOnline:  time.Now(),
	}).FirstOrCreate(&device)

	if result.Error != nil {
		log.Printf("[TCP] Failed to upsert device %s: %v", code, result.Error)
		return
	}

	// 如果设备已存在，更新字段（FirstOrCreate在记录已存在时不执行Assign）
	if result.RowsAffected == 0 {
		dc.db.Model(&device).Updates(map[string]interface{}{
			"ip":         ip,
			"status":     "online",
			"last_online": time.Now(),
		})
	}

	dc.deviceCode = code
	dc.deviceID = device.ID
	dc.connMgr.Register(code, dc)

	log.Printf("[TCP] Device registered: code=%s id=%d", code, device.ID)
}

// handleMainboardSN 处理主板序列号
func (dc *DeviceConnection) handleMainboardSN(pkt *Packet) {
	sn := trimNullBytes(pkt.Data)
	if dc.deviceID == 0 {
		log.Printf("[TCP] MainboardSN received but device not identified yet: %s", sn)
		return
	}
	dc.db.Model(&model.Device{}).Where("id = ?", dc.deviceID).Update("mainboard_sn", sn)
	log.Printf("[TCP] MainboardSN updated: device=%s sn=%s", dc.deviceCode, sn)
}

// handleTimeSync 处理时间同步：回复BCD编码的当前时间
func (dc *DeviceConnection) handleTimeSync(pkt *Packet) {
	now := time.Now()
	// BCD编码：年(2B)+月(1B)+日(1B)+时(1B)+分(1B)+秒(1B) = 7字节
	data := make([]byte, 7)
	year := now.Year()
	data[0] = toBCD(byte(year / 100))
	data[1] = toBCD(byte(year % 100))
	data[2] = toBCD(byte(now.Month()))
	data[3] = toBCD(byte(now.Day()))
	data[4] = toBCD(byte(now.Hour()))
	data[5] = toBCD(byte(now.Minute()))
	data[6] = toBCD(byte(now.Second()))

	reply := &Packet{
		Addr1:       pkt.Addr2,
		Addr2:       pkt.Addr1,
		ParamType:   pkt.ParamType,
		ParamNo:     pkt.ParamNo,
		TotalFrames: 1,
		FrameNo:     0,
		Data:        data,
	}
	dc.send(reply)
}

// handleHeartbeat 处理心跳：原样回复，更新时间
func (dc *DeviceConnection) handleHeartbeat(pkt *Packet) {
	dc.lastHeartbeat = time.Now()

	if dc.deviceID > 0 {
		dc.db.Model(&model.Device{}).Where("id = ?", dc.deviceID).Updates(map[string]interface{}{
			"last_online": dc.lastHeartbeat,
			"status":      gorm.Expr("CASE WHEN status = 'offline' THEN 'online' ELSE status END"),
		})
	}

	// 原样回复心跳
	reply := &Packet{
		Addr1:       pkt.Addr2,
		Addr2:       pkt.Addr1,
		ParamType:   pkt.ParamType,
		ParamNo:     pkt.ParamNo,
		TotalFrames: 1,
		FrameNo:     0,
		Data:        pkt.Data,
	}
	dc.send(reply)
}

// handleSewing 处理开始/停止缝制
func (dc *DeviceConnection) handleSewing(pkt *Packet) {
	if dc.deviceID == 0 || len(pkt.Data) < 1 {
		return
	}

	var status string
	switch pkt.Data[0] {
	case 0x01:
		status = "working"
		log.Printf("[TCP] Device %s started sewing", dc.deviceCode)
	case 0x00:
		status = "idle"
		log.Printf("[TCP] Device %s stopped sewing", dc.deviceCode)
	default:
		status = "online"
	}

	dc.db.Model(&model.Device{}).Where("id = ?", dc.deviceID).Update("status", status)
}

// handleAlarm 处理报警消息
func (dc *DeviceConnection) handleAlarm(pkt *Packet) {
	if dc.deviceID == 0 || len(pkt.Data) < 2 {
		return
	}

	alarmCode := binary.BigEndian.Uint16(pkt.Data[0:2])

	if alarmCode != 0 {
		// 报警触发
		dc.db.Model(&model.Device{}).Where("id = ?", dc.deviceID).Update("status", "alarm")
		dc.db.Create(&model.AlarmRecord{
			DeviceID:  dc.deviceID,
			AlarmCode: fmt.Sprintf("%d", alarmCode),
			AlarmType: classifyAlarm(alarmCode),
			Status:    "pending",
			StartTime: time.Now(),
		})
		log.Printf("[TCP] Device %s alarm: code=%d", dc.deviceCode, alarmCode)
	} else {
		// 报警解除
		dc.db.Model(&model.Device{}).Where("id = ?", dc.deviceID).Update("status", "online")
		// 关闭未解决的报警
		now := time.Now()
		dc.db.Model(&model.AlarmRecord{}).
			Where("device_id = ? AND status = ?", dc.deviceID, "pending").
			Updates(map[string]interface{}{
				"status":   "resolved",
				"end_time": now,
			})
		log.Printf("[TCP] Device %s alarm cleared", dc.deviceCode)
	}
}

// handleProduction 处理生产数据
func (dc *DeviceConnection) handleProduction(pkt *Packet) {
	if dc.deviceID == 0 {
		return
	}

	// 生产数据82字节记录
	data := pkt.Data
	if len(data) < 82 {
		log.Printf("[TCP] Production data too short: %d bytes (expected 82)", len(data))
		return
	}

	// 解析生产数据字段
	// 偏移量根据协议定义：
	// 0-3:   加工件数 (uint32)
	// 4-11:  针数 (uint64)
	// 12-19: 用线量 mm (uint64)
	// 20-27: 运行时间 ms (uint64)
	// 28-35: 空闲时间 ms (uint64)
	pieces := int(binary.BigEndian.Uint32(data[0:4]))
	stitches := int64(binary.BigEndian.Uint64(data[4:12]))
	threadLenMM := binary.BigEndian.Uint64(data[12:20])
	runTimeMS := binary.BigEndian.Uint64(data[20:28])
	idleTimeMS := binary.BigEndian.Uint64(data[28:36])

	threadLenM := float64(threadLenMM) / 1000.0
	runTimeH := float64(runTimeMS) / 3600000.0
	idleTimeH := float64(idleTimeMS) / 3600000.0

	record := model.ProductionRecord{
		DeviceID:     dc.deviceID,
		Pieces:       pieces,
		Stitches:     stitches,
		ThreadLength: threadLenM,
		RunningTime:  runTimeH,
		IdleTime:     idleTimeH,
		RecordDate:   time.Now(),
	}

	if err := dc.db.Create(&record).Error; err != nil {
		log.Printf("[TCP] Failed to save production record for device %s: %v", dc.deviceCode, err)
	} else {
		log.Printf("[TCP] Production record saved: device=%s pieces=%d stitches=%d", dc.deviceCode, pieces, stitches)
	}
}

// send 发送数据包
func (dc *DeviceConnection) send(pkt *Packet) {
	data := BuildPacket(pkt)
	if _, err := dc.conn.Write(data); err != nil {
		log.Printf("[TCP] Send error to device %s: %v", dc.deviceCode, err)
	}
}

// cleanup 连接关闭时清理
func (dc *DeviceConnection) cleanup() {
	dc.conn.Close()
	if dc.deviceCode != "" {
		dc.connMgr.Unregister(dc.deviceCode)
		// 标记设备离线
		dc.db.Model(&model.Device{}).Where("id = ? AND status != ?", dc.deviceID, "offline").
			Update("status", "offline")
		log.Printf("[TCP] Device disconnected: %s", dc.deviceCode)
	}
}

// 辅助函数

func trimNullBytes(data []byte) string {
	for i, b := range data {
		if b == 0 {
			return string(data[:i])
		}
	}
	return string(data)
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

func mapDeviceType(code uint16) (deviceType, modelName string) {
	// 根据类型编号映射，具体映射关系可能需要根据实际协议文档调整
	switch {
	case code >= 0x0100 && code < 0x0200:
		return "缝纫机", "BM-2000"
	case code >= 0x0200 && code < 0x0300:
		return "缝纫机", "BM-3000"
	case code >= 0x0300 && code < 0x0400:
		return "绣花机", "BM-5000"
	default:
		return "缝纫机", fmt.Sprintf("Unknown(%d)", code)
	}
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
