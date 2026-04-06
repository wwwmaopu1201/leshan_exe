package service

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
)

// 包头标识
const (
	HeaderByte1  = 0x44
	HeaderByte2  = 0x54
	HeaderSize   = 23 // 2(header) + 4(addr1) + 4(addr2) + 3(reserved) + 2(paramType) + 2(paramNo) + 1(totalFrames) + 1(frameNo) + 4(length)
	CRC16Size    = 2
	MaxFrameSize = 1024 * 1024
)

// ParamType + ParamNo 指令常量
const (
	// 注册
	PTRegister uint16 = 0x0B2A
	PNRegister uint16 = 0x0002
	// 心跳
	PTHeartbeat uint16 = 0x0B2A
	PNHeartbeat uint16 = 0x0001
	// 时间同步
	PTTimeSync uint16 = 0x0B2A
	PNTimeSync uint16 = 0x0003
	// 设备信息（设备型号+编号+名称）
	PTDeviceInfo uint16 = 0x1302
	PNDeviceInfo uint16 = 0x10FA
	// 主板SN
	PTMainboardSN uint16 = 0x1302
	PNMainboardSN uint16 = 0x157C
	// 开始/停止缝制
	PTSewing            uint16 = 0x0B29
	PNSewing            uint16 = 0x0032
	PTRealtimeStatus    uint16 = 0x0B29
	PNRealtimeStatus    uint16 = 0x0033
	PTThreadTrim        uint16 = 0x0B29
	PNThreadTrim        uint16 = 0x0034
	PTReadHighPoint     uint16 = 0x0B29
	PNReadHighPoint     uint16 = 0x0035
	PTSetHighPoint      uint16 = 0x0B29
	PNSetHighPoint      uint16 = 0x0036
	PTReadLowPoint      uint16 = 0x0B29
	PNReadLowPoint      uint16 = 0x0037
	PTSetLowPoint       uint16 = 0x0B29
	PNSetLowPoint       uint16 = 0x0038
	PTSetSpeed          uint16 = 0x0B29
	PNSetSpeed          uint16 = 0x0039
	PTSewingRange       uint16 = 0x0B29
	PNSewingRange       uint16 = 0x0015
	PTPatternInfo       uint16 = 0x0B29
	PNPatternInfo       uint16 = 0x0016
	PTBottomThreadCount uint16 = 0x0B29
	PNBottomThreadCount uint16 = 0x0006
	PTProductionCount   uint16 = 0x0B29
	PNProductionCount   uint16 = 0x0003
	PTOilPrompt         uint16 = 0x0B29
	PNOilPrompt         uint16 = 0x0029
	// 报警
	PTAlarm     uint16 = 0x0B97
	PNAlarm     uint16 = 0x0001
	PTIdleAlarm uint16 = 0x0B97
	PNIdleAlarm uint16 = 0x0002
	// 生产数据
	PTProduction    uint16 = 0x0B2A
	PNProduction    uint16 = 0x000C
	PNProductionOld uint16 = 0x000B
	PTNeedleCount   uint16 = 0x0B01
	PNNeedleCount   uint16 = 0x001F
	PTCurrentSpeed  uint16 = 0x1302
	PNCurrentSpeed  uint16 = 0x107C
	PTMaxSpeed      uint16 = 0x1301
	PNMaxSpeed      uint16 = 0x00A3
)

// Packet 协议数据包
type Packet struct {
	Addr1       uint32
	Addr2       uint32
	Reserved    [3]byte
	ParamType   uint16
	ParamNo     uint16
	TotalFrames uint8
	FrameNo     uint8
	Data        []byte
	Raw         []byte
	ParseError  string
	Recovered   string
}

// ParsePacket 从reader中读取并解析一个完整的协议包。
// 协议规则与 xieyi_test 保持一致：
// 1. 多字节字段使用大端。
// 2. length 字段表示 payload + CRC16 的总长度，而不是纯 payload 长度。
// 3. CRC16 使用 Modbus，对 header + payload 计算，CRC 自身不参与计算。
func ParsePacket(reader io.Reader) (*Packet, error) {
	// 寻找包头
	if err := findHeader(reader); err != nil {
		return nil, err
	}

	// 读取固定头部（包头之后的21字节）
	hdr := make([]byte, HeaderSize-2)
	if _, err := io.ReadFull(reader, hdr); err != nil {
		return nil, fmt.Errorf("read header: %w", err)
	}

	p := &Packet{}
	p.Addr1 = binary.BigEndian.Uint32(hdr[0:4])
	p.Addr2 = binary.BigEndian.Uint32(hdr[4:8])
	copy(p.Reserved[:], hdr[8:11])
	p.ParamType = binary.BigEndian.Uint16(hdr[11:13])
	p.ParamNo = binary.BigEndian.Uint16(hdr[13:15])
	p.TotalFrames = hdr[15]
	p.FrameNo = hdr[16]
	lengthField := binary.BigEndian.Uint32(hdr[17:21])

	if lengthField < CRC16Size {
		return nil, fmt.Errorf("invalid length field %d: smaller than CRC size", lengthField)
	}
	if lengthField > 65538 {
		return nil, fmt.Errorf("data length too large: %d", lengthField)
	}

	// 读取 payload + CRC16。
	tail := make([]byte, lengthField)
	if _, err := io.ReadFull(reader, tail); err != nil {
		return nil, fmt.Errorf("read payload+crc: %w", err)
	}

	payloadLen := int(lengthField) - CRC16Size
	p.Data = tail[:payloadLen]
	p.Raw = make([]byte, HeaderSize+int(lengthField))
	p.Raw[0] = HeaderByte1
	p.Raw[1] = HeaderByte2
	copy(p.Raw[2:], hdr)
	copy(p.Raw[HeaderSize:], tail)

	// 校验CRC16：对 header(2) + hdr(21) + payload 整体计算
	rawForCRC := make([]byte, 2+len(hdr)+payloadLen)
	rawForCRC[0] = HeaderByte1
	rawForCRC[1] = HeaderByte2
	copy(rawForCRC[2:], hdr)
	copy(rawForCRC[2+len(hdr):], p.Data)

	expectedCRC := binary.BigEndian.Uint16(tail[payloadLen:])
	actualCRC := CRC16Modbus(rawForCRC)
	if expectedCRC != actualCRC {
		return nil, fmt.Errorf("CRC mismatch: expected 0x%04X, got 0x%04X", expectedCRC, actualCRC)
	}

	return p, nil
}

type FrameParser struct {
	buffer []byte
}

func NewFrameParser() *FrameParser {
	return &FrameParser{
		buffer: make([]byte, 0, 4096),
	}
}

func (p *FrameParser) PendingSnapshot(limit int) []byte {
	if limit <= 0 || len(p.buffer) <= limit {
		return append([]byte(nil), p.buffer...)
	}
	return append([]byte(nil), p.buffer[:limit]...)
}

func (p *FrameParser) PendingLen() int {
	return len(p.buffer)
}

func (p *FrameParser) Push(chunk []byte) []*Packet {
	if len(chunk) == 0 {
		return nil
	}

	p.buffer = append(p.buffer, chunk...)
	packets := make([]*Packet, 0, 4)
	header := []byte{HeaderByte1, HeaderByte2}

	for len(p.buffer) >= HeaderSize+CRC16Size {
		headIndex := bytes.Index(p.buffer, header)
		if headIndex < 0 {
			p.buffer = p.buffer[:0]
			break
		}
		if headIndex > 0 {
			p.buffer = p.buffer[headIndex:]
		}
		if len(p.buffer) < HeaderSize+CRC16Size {
			break
		}

		lengthField := binary.BigEndian.Uint32(p.buffer[19:23])
		packetLength := HeaderSize + int(lengthField)
		if packetLength < HeaderSize+CRC16Size || packetLength > MaxFrameSize {
			packets = append(packets, &Packet{
				Raw:        append([]byte(nil), p.buffer[:minInt(len(p.buffer), HeaderSize+32)]...),
				ParseError: fmt.Sprintf("invalid frame length %d", lengthField),
			})
			p.buffer = p.buffer[2:]
			continue
		}
		if len(p.buffer) < packetLength {
			break
		}

		raw := append([]byte(nil), p.buffer[:packetLength]...)
		p.buffer = p.buffer[packetLength:]

		pkt, err := parsePacketBuffer(raw)
		if err != nil {
			packets = append(packets, &Packet{
				Raw:        raw[:minInt(len(raw), HeaderSize+32)],
				ParseError: err.Error(),
			})
			continue
		}
		packets = append(packets, pkt)
	}

	return packets
}

func parsePacketBuffer(raw []byte) (*Packet, error) {
	if len(raw) < HeaderSize+CRC16Size {
		return nil, fmt.Errorf("frame too short")
	}
	if raw[0] != HeaderByte1 || raw[1] != HeaderByte2 {
		return nil, fmt.Errorf("invalid frame head")
	}

	lengthField := binary.BigEndian.Uint32(raw[19:23])
	if int(lengthField) != len(raw)-HeaderSize {
		return nil, fmt.Errorf("frame length mismatch")
	}

	payloadLen := int(lengthField) - CRC16Size
	if payloadLen < 0 {
		return nil, fmt.Errorf("invalid payload length %d", payloadLen)
	}

	expectedCRC := binary.BigEndian.Uint16(raw[len(raw)-CRC16Size:])
	actualCRC := CRC16Modbus(raw[:len(raw)-CRC16Size])
	if expectedCRC != actualCRC {
		if pkt := tryRecoverUploadPatternCommandFrame(raw, lengthField, expectedCRC, actualCRC); pkt != nil {
			return pkt, nil
		}
		return nil, fmt.Errorf("crc mismatch: expected 0x%04X, got 0x%04X", expectedCRC, actualCRC)
	}

	pkt := &Packet{
		Addr1:       binary.BigEndian.Uint32(raw[2:6]),
		Addr2:       binary.BigEndian.Uint32(raw[6:10]),
		ParamType:   binary.BigEndian.Uint16(raw[13:15]),
		ParamNo:     binary.BigEndian.Uint16(raw[15:17]),
		TotalFrames: raw[17],
		FrameNo:     raw[18],
		Data:        append([]byte(nil), raw[HeaderSize:len(raw)-CRC16Size]...),
		Raw:         raw,
	}
	copy(pkt.Reserved[:], raw[10:13])
	return pkt, nil
}

func tryRecoverUploadPatternCommandFrame(raw []byte, lengthField uint32, expectedCRC, actualCRC uint16) *Packet {
	if len(raw) < HeaderSize+CRC16Size {
		return nil
	}

	paramType := binary.BigEndian.Uint16(raw[13:15])
	paramNo := binary.BigEndian.Uint16(raw[15:17])
	if paramType != PTPattern || paramNo != PNUploadPatternCommand {
		return nil
	}

	totalFrames := raw[17]
	frameNo := raw[18]
	if totalFrames != 1 || (frameNo != 0 && frameNo != 1) {
		return nil
	}

	if lengthField < 2 || lengthField > 256 {
		return nil
	}

	recoveredPayload := append([]byte(nil), raw[HeaderSize:]...)
	if len(recoveredPayload) != int(lengthField) {
		return nil
	}
	if !isLikelyUTF16LEText(recoveredPayload[2:]) {
		return nil
	}

	pkt := &Packet{
		Addr1:       binary.BigEndian.Uint32(raw[2:6]),
		Addr2:       binary.BigEndian.Uint32(raw[6:10]),
		ParamType:   paramType,
		ParamNo:     paramNo,
		TotalFrames: totalFrames,
		FrameNo:     frameNo,
		Data:        recoveredPayload,
		Raw:         raw,
		Recovered:   fmt.Sprintf("上传花型指令 CRC 异常，已按无CRC兼容处理（expected 0x%04X, got 0x%04X）", expectedCRC, actualCRC),
	}
	copy(pkt.Reserved[:], raw[10:13])
	return pkt
}

// BuildPacket 将Packet序列化为字节流（含CRC16）
func BuildPacket(p *Packet) []byte {
	dataLen := len(p.Data)
	lengthField := dataLen + CRC16Size
	totalFrames := p.TotalFrames
	if totalFrames == 0 {
		totalFrames = 1
	}
	frameNo := p.FrameNo
	if frameNo == 0 {
		frameNo = 1
	}
	buf := make([]byte, HeaderSize+dataLen+CRC16Size)

	buf[0] = HeaderByte1
	buf[1] = HeaderByte2
	binary.BigEndian.PutUint32(buf[2:6], p.Addr1)
	binary.BigEndian.PutUint32(buf[6:10], p.Addr2)
	copy(buf[10:13], p.Reserved[:])
	binary.BigEndian.PutUint16(buf[13:15], p.ParamType)
	binary.BigEndian.PutUint16(buf[15:17], p.ParamNo)
	buf[17] = totalFrames
	buf[18] = frameNo
	binary.BigEndian.PutUint32(buf[19:23], uint32(lengthField))
	copy(buf[23:23+dataLen], p.Data)

	crc := CRC16Modbus(buf[:HeaderSize+dataLen])
	binary.BigEndian.PutUint16(buf[HeaderSize+dataLen:], crc)

	return buf
}

func buildProtocolCommand(paramType, paramNo uint16, data []byte) *Packet {
	payload := append([]byte(nil), data...)
	return &Packet{
		ParamType:   paramType,
		ParamNo:     paramNo,
		TotalFrames: 1,
		FrameNo:     1,
		Data:        payload,
	}
}

// CRC16Modbus 计算CRC16-MODBUS校验值
func CRC16Modbus(data []byte) uint16 {
	crc := uint16(0xFFFF)
	for _, b := range data {
		crc ^= uint16(b)
		for i := 0; i < 8; i++ {
			if crc&0x0001 != 0 {
				crc = (crc >> 1) ^ 0xA001
			} else {
				crc >>= 1
			}
		}
	}
	return crc
}

// findHeader 在字节流中寻找 0x44 0x54 包头
func findHeader(reader io.Reader) error {
	buf := make([]byte, 1)
	sample := make([]byte, 0, 32)
	bytesRead := 0
	for {
		if _, err := io.ReadFull(reader, buf); err != nil {
			return wrapHeaderReadError(err, sample, bytesRead)
		}
		bytesRead++
		if len(sample) < cap(sample) {
			sample = append(sample, buf[0])
		}
		if buf[0] != HeaderByte1 {
			continue
		}
		if _, err := io.ReadFull(reader, buf); err != nil {
			return wrapHeaderReadError(err, sample, bytesRead)
		}
		bytesRead++
		if len(sample) < cap(sample) {
			sample = append(sample, buf[0])
		}
		if buf[0] == HeaderByte2 {
			return nil
		}
	}
}

func wrapHeaderReadError(err error, sample []byte, bytesRead int) error {
	if err == nil {
		return nil
	}
	if bytesRead == 0 {
		return err
	}
	if len(sample) == 0 {
		return fmt.Errorf("read %d bytes before header match: %w", bytesRead, err)
	}
	return fmt.Errorf("read %d bytes before header match, sample=% X: %w", bytesRead, sample, err)
}

func minInt(a, b int) int {
	if a < b {
		return a
	}
	return b
}
