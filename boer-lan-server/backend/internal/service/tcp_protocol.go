package service

import (
	"encoding/binary"
	"fmt"
	"io"
)

// 包头标识
const (
	HeaderByte1 = 0x44
	HeaderByte2 = 0x54
	HeaderSize  = 23 // 2(header) + 4(addr1) + 4(addr2) + 3(reserved) + 2(paramType) + 2(paramNo) + 1(totalFrames) + 1(frameNo) + 4(length)
	CRC16Size   = 2
)

// ParamType + ParamNo 指令常量
const (
	// 注册
	PTRegister    uint16 = 0x0B2A
	PNRegister    uint16 = 0x0002
	// 心跳
	PTHeartbeat   uint16 = 0x0B2A
	PNHeartbeat   uint16 = 0x0001
	// 时间同步
	PTTimeSync    uint16 = 0x0B2A
	PNTimeSync    uint16 = 0x0003
	// 设备信息（设备型号+编号+名称）
	PTDeviceInfo  uint16 = 0x1302
	PNDeviceInfo  uint16 = 0x10FA
	// 主板SN
	PTMainboardSN uint16 = 0x1302
	PNMainboardSN uint16 = 0x157C
	// 开始/停止缝制
	PTSewing      uint16 = 0x0B29
	PNSewing      uint16 = 0x0032
	// 报警
	PTAlarm       uint16 = 0x0B97
	PNAlarm       uint16 = 0x0001
	// 生产数据
	PTProduction  uint16 = 0x0B2A
	PNProduction  uint16 = 0x000C
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
}

// ParsePacket 从reader中读取并解析一个完整的协议包
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
	dataLen := binary.BigEndian.Uint32(hdr[17:21])

	if dataLen > 65536 {
		return nil, fmt.Errorf("data length too large: %d", dataLen)
	}

	// 读取数据 + CRC16
	payload := make([]byte, dataLen+CRC16Size)
	if _, err := io.ReadFull(reader, payload); err != nil {
		return nil, fmt.Errorf("read data+crc: %w", err)
	}

	p.Data = payload[:dataLen]

	// 校验CRC16：对 header(2) + hdr(21) + data(dataLen) 整体计算
	rawForCRC := make([]byte, 2+len(hdr)+int(dataLen))
	rawForCRC[0] = HeaderByte1
	rawForCRC[1] = HeaderByte2
	copy(rawForCRC[2:], hdr)
	copy(rawForCRC[2+len(hdr):], p.Data)

	expectedCRC := binary.BigEndian.Uint16(payload[dataLen:])
	actualCRC := CRC16Modbus(rawForCRC)
	if expectedCRC != actualCRC {
		return nil, fmt.Errorf("CRC mismatch: expected 0x%04X, got 0x%04X", expectedCRC, actualCRC)
	}

	return p, nil
}

// BuildPacket 将Packet序列化为字节流（含CRC16）
func BuildPacket(p *Packet) []byte {
	dataLen := len(p.Data)
	buf := make([]byte, HeaderSize+dataLen+CRC16Size)

	buf[0] = HeaderByte1
	buf[1] = HeaderByte2
	binary.BigEndian.PutUint32(buf[2:6], p.Addr1)
	binary.BigEndian.PutUint32(buf[6:10], p.Addr2)
	copy(buf[10:13], p.Reserved[:])
	binary.BigEndian.PutUint16(buf[13:15], p.ParamType)
	binary.BigEndian.PutUint16(buf[15:17], p.ParamNo)
	buf[17] = p.TotalFrames
	buf[18] = p.FrameNo
	binary.BigEndian.PutUint32(buf[19:23], uint32(dataLen))
	copy(buf[23:23+dataLen], p.Data)

	crc := CRC16Modbus(buf[:HeaderSize+dataLen])
	binary.BigEndian.PutUint16(buf[HeaderSize+dataLen:], crc)

	return buf
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
	for {
		if _, err := io.ReadFull(reader, buf); err != nil {
			return err
		}
		if buf[0] != HeaderByte1 {
			continue
		}
		if _, err := io.ReadFull(reader, buf); err != nil {
			return err
		}
		if buf[0] == HeaderByte2 {
			return nil
		}
	}
}
