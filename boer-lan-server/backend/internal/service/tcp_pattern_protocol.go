package service

import (
	"encoding/binary"
	"fmt"
	"strings"
	"unicode/utf16"
)

const (
	PTPattern uint16 = 0x0B2A

	PNDownloadPatternCommand uint16 = 0x0004
	PNDownloadPatternData    uint16 = 0x0005
	PNCommunicationError     uint16 = 0x0006
	PNUploadPatternCommand   uint16 = 0x0007
	PNDeletePatternFile      uint16 = 0x0008
	PNReadPatternList        uint16 = 0x0009
	PNUploadPatternData      uint16 = 0x000D
	PNTransferResume         uint16 = 0x000E
	PNRequestServerList      uint16 = 0x000F

	patternNameFixedBytes = 44
	patternChunkSize      = 256
)

type PatternListEntry struct {
	PatternNo uint
	FileName  string
}

type DeletePatternOptions struct {
	PatternNo    uint
	FileName     string
	NameEncoding string
	IncludeID    bool
	Addr1        uint32
	Addr2        uint32
}

type TransferResume struct {
	TransferType uint8
	FrameNo      uint8
}

func isPatternCommand(pkt *Packet) bool {
	if pkt == nil || pkt.ParamType != PTPattern {
		return false
	}

	switch pkt.ParamNo {
	case PNDownloadPatternCommand,
		PNDownloadPatternData,
		PNCommunicationError,
		PNUploadPatternCommand,
		PNDeletePatternFile,
		PNReadPatternList,
		PNUploadPatternData,
		PNTransferResume,
		PNRequestServerList:
		return true
	default:
		return false
	}
}

func buildPatternCommand(paramNo uint16, data []byte) *Packet {
	payload := append([]byte(nil), data...)
	return &Packet{
		ParamType:   PTPattern,
		ParamNo:     paramNo,
		TotalFrames: 1,
		FrameNo:     1,
		Data:        payload,
	}
}

func buildPatternResult(paramNo uint16, result byte, totalFrames, frameNo uint8) *Packet {
	if totalFrames == 0 {
		totalFrames = 1
	}
	if frameNo == 0 {
		frameNo = 1
	}
	return &Packet{
		ParamType:   PTPattern,
		ParamNo:     paramNo,
		TotalFrames: totalFrames,
		FrameNo:     frameNo,
		Data:        []byte{result},
	}
}

func buildReadPatternListRequest() *Packet {
	return buildPatternCommand(PNReadPatternList, nil)
}

func buildRequestServerPatternListRequest() *Packet {
	return buildPatternCommand(PNRequestServerList, nil)
}

func buildDownloadPatternCommand(patternName string) *Packet {
	return buildPatternCommand(PNDownloadPatternCommand, encodeUTF16LE(patternName))
}

func buildUploadPatternCommand(patternNo uint, patternName string) *Packet {
	payload := make([]byte, 2)
	binary.BigEndian.PutUint16(payload, uint16(patternNo))
	payload = append(payload, encodeUTF16LE(patternName)...)
	return buildPatternCommand(PNUploadPatternCommand, payload)
}

func buildDeletePatternCommand(patternNo uint, patternName string) *Packet {
	return buildDeletePatternCommandWithOptions(DeletePatternOptions{
		PatternNo:    patternNo,
		FileName:     patternName,
		NameEncoding: "unicode",
		IncludeID:    false,
	})
}

func buildDeletePatternCommandWithOptions(options DeletePatternOptions) *Packet {
	nameEncoding := strings.TrimSpace(options.NameEncoding)
	if nameEncoding == "" {
		nameEncoding = "unicode"
	}

	payload := make([]byte, 0, patternNameFixedBytes+2)
	if options.IncludeID {
		idBytes := make([]byte, 2)
		binary.BigEndian.PutUint16(idBytes, uint16(options.PatternNo))
		payload = append(payload, idBytes...)
	}
	if strings.TrimSpace(options.FileName) != "" {
		payload = append(payload, encodeDeletePatternName(options.FileName, nameEncoding)...)
	}

	pkt := buildPatternCommand(PNDeletePatternFile, payload)
	pkt.Addr1 = options.Addr1
	pkt.Addr2 = options.Addr2
	return pkt
}

func buildDeletePatternResponseFromRequestPayload(requestPayload []byte, result byte) *Packet {
	payload := append(append([]byte(nil), requestPayload...), result)
	return buildPatternCommand(PNDeletePatternFile, payload)
}

func encodeDeletePatternName(value string, mode string) []byte {
	switch mode {
	case "unicodeFixed":
		return encodeUTF16LEFixed(value, patternNameFixedBytes)
	case "compatibleFixed":
		return encodeFixedString(value, patternNameFixedBytes)
	default:
		return encodeUTF16LE(value)
	}
}

func encodeUTF16LEFixed(value string, byteLength int) []byte {
	buffer := make([]byte, byteLength)
	raw := encodeUTF16LE(value)
	copy(buffer, raw[:minInt(len(raw), byteLength)])
	return buffer
}

func encodeFixedString(value string, byteLength int) []byte {
	buffer := make([]byte, byteLength)
	copy(buffer, []byte(value))
	return buffer
}

func buildPatternListResponse(paramNo uint16, entries []PatternListEntry, includeID bool) []*Packet {
	entryBuffers := make([][]byte, 0, len(entries))
	for _, entry := range entries {
		entryBuffers = append(entryBuffers, encodePatternListEntry(entry, includeID))
	}

	chunks := make([][]byte, 0)
	current := make([]byte, 0, 920)
	for _, entry := range entryBuffers {
		if len(current)+len(entry) > 920 && len(current) > 0 {
			chunks = append(chunks, current)
			current = make([]byte, 0, 920)
		}
		current = append(current, entry...)
	}
	if len(current) > 0 {
		chunks = append(chunks, current)
	}
	if len(chunks) == 0 {
		chunks = append(chunks, nil)
	}

	packets := make([]*Packet, 0, len(chunks))
	for index, chunk := range chunks {
		packets = append(packets, &Packet{
			ParamType:   PTPattern,
			ParamNo:     paramNo,
			TotalFrames: uint8(len(chunks)),
			FrameNo:     uint8(index + 1),
			Data:        append([]byte(nil), chunk...),
		})
	}
	return packets
}

func encodePatternListEntry(entry PatternListEntry, includeID bool) []byte {
	nameBytes := encodeUTF16LEFixed(entry.FileName, patternNameFixedBytes)
	if !includeID {
		return nameBytes
	}
	nameBytes = encodeFixedString(entry.FileName, patternNameFixedBytes)
	prefix := make([]byte, 2)
	binary.BigEndian.PutUint16(prefix, uint16(entry.PatternNo))
	return append(prefix, nameBytes...)
}

func buildUploadPatternDataAck(totalFrames, frameNo uint8, result byte) *Packet {
	return buildPatternResult(PNUploadPatternData, result, totalFrames, frameNo)
}

func buildDownloadPatternResult(result byte) *Packet {
	return buildPatternCommand(PNDownloadPatternData, []byte{result})
}

func buildCommunicationErrorResult(totalFrames, frameNo uint8, result byte) *Packet {
	return buildPatternResult(PNCommunicationError, result, totalFrames, frameNo)
}

func buildTransferResume(transferType, frameNo uint8) *Packet {
	return buildPatternCommand(PNTransferResume, []byte{transferType, frameNo})
}

func describeDeletePatternResult(result byte) string {
	switch result {
	case 0:
		return "删除成功"
	case 1:
		return "删除失败"
	case 2:
		return "设备返回结果 2，协议未定义"
	default:
		return fmt.Sprintf("设备返回未定义删除结果 %d", result)
	}
}

func parsePatternListPayload(data []byte) []PatternListEntry {
	return parsePatternListPayloadWithOptions(data, true)
}

func parsePatternListPayloadWithOptions(data []byte, includeID bool) []PatternListEntry {
	entrySize := patternNameFixedBytes
	if includeID {
		entrySize += 2
	}
	if len(data) == 0 || len(data)%entrySize != 0 {
		return []PatternListEntry{}
	}

	entries := make([]PatternListEntry, 0, len(data)/entrySize)
	for offset := 0; offset+entrySize <= len(data); offset += entrySize {
		entry := PatternListEntry{}
		nameOffset := offset
		if includeID {
			entry.PatternNo = uint(binary.BigEndian.Uint16(data[offset : offset+2]))
			nameOffset += 2
		}
		entry.FileName = strings.TrimSpace(normalizeProtocolText(data[nameOffset : nameOffset+patternNameFixedBytes]))
		entries = append(entries, entry)
	}
	return entries
}

func buildDownloadPatternFrames(data []byte) []*Packet {
	if len(data) == 0 {
		return []*Packet{{
			ParamType:   PTPattern,
			ParamNo:     PNDownloadPatternData,
			TotalFrames: 1,
			FrameNo:     1,
			Data:        nil,
		}}
	}

	totalFrames := (len(data) + patternChunkSize - 1) / patternChunkSize
	frames := make([]*Packet, 0, totalFrames)
	for index := 0; index < totalFrames; index++ {
		start := index * patternChunkSize
		end := start + patternChunkSize
		if end > len(data) {
			end = len(data)
		}
		frames = append(frames, &Packet{
			ParamType:   PTPattern,
			ParamNo:     PNDownloadPatternData,
			TotalFrames: uint8(totalFrames),
			FrameNo:     uint8(index + 1),
			Data:        append([]byte(nil), data[start:end]...),
		})
	}
	return frames
}

func parseSingleByteResult(data []byte) (byte, bool) {
	if len(data) < 1 {
		return 0, false
	}
	return data[0], true
}

func parseDeletePatternResult(data []byte) (byte, bool) {
	if len(data) < 1 {
		return 0, false
	}
	return data[len(data)-1], true
}

func parseDownloadCommandAck(data []byte) string {
	return decodeUTF16LECString(data)
}

func parseDownloadPatternFinalResult(data []byte) (byte, string, bool) {
	if len(data) == 0 {
		return 0, "", false
	}
	if len(data) == 1 {
		return data[0], "", true
	}

	if echoedName := decodeUTF16LECString(data[:len(data)-1]); echoedName != "" {
		return data[len(data)-1], echoedName, true
	}

	return data[len(data)-1], "", true
}

func parseUploadCommandPayload(data []byte) (uint, string, bool) {
	if len(data) < 2 {
		return 0, "", false
	}
	return uint(binary.BigEndian.Uint16(data[:2])), decodeUTF16LECString(data[2:]), true
}

func parseTransferResume(data []byte) (TransferResume, bool) {
	if len(data) < 2 {
		return TransferResume{}, false
	}
	return TransferResume{
		TransferType: data[0],
		FrameNo:      data[1],
	}, true
}

func encodeUTF16LE(value string) []byte {
	if value == "" {
		return nil
	}
	encoded := utf16.Encode([]rune(value))
	buf := make([]byte, len(encoded)*2)
	for i, code := range encoded {
		binary.LittleEndian.PutUint16(buf[i*2:], code)
	}
	return buf
}

func decodeUTF16LECString(data []byte) string {
	if len(data) == 0 {
		return ""
	}

	end := len(data)
	if end%2 != 0 {
		end--
	}
	for offset := 0; offset+1 < end; offset += 2 {
		word := binary.LittleEndian.Uint16(data[offset : offset+2])
		if word == 0x0000 || word == 0xFDFD || word == 0xFFFF {
			end = offset
			break
		}
	}
	if end <= 0 {
		return ""
	}

	words := make([]uint16, 0, end/2)
	for offset := 0; offset+1 < end; offset += 2 {
		words = append(words, binary.LittleEndian.Uint16(data[offset:offset+2]))
	}
	return strings.TrimSpace(string(utf16.Decode(words)))
}

func decodeUTF16LEFixed(data []byte) string {
	return decodeUTF16LECString(data)
}
