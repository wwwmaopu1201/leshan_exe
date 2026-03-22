package service

import (
	"encoding/binary"
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
	payload := make([]byte, 2)
	binary.BigEndian.PutUint16(payload, uint16(patternNo))
	payload = append(payload, encodeUTF16LE(patternName)...)
	return buildPatternCommand(PNDeletePatternFile, payload)
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

func parsePatternListPayload(data []byte) []PatternListEntry {
	entrySize := patternNameFixedBytes + 2
	if len(data) == 0 || len(data)%entrySize != 0 {
		return []PatternListEntry{}
	}

	entries := make([]PatternListEntry, 0, len(data)/entrySize)
	for offset := 0; offset+entrySize <= len(data); offset += entrySize {
		patternNo := binary.BigEndian.Uint16(data[offset : offset+2])
		name := decodeUTF16LEFixed(data[offset+2 : offset+2+patternNameFixedBytes])
		entries = append(entries, PatternListEntry{
			PatternNo: uint(patternNo),
			FileName:  name,
		})
	}
	return entries
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
