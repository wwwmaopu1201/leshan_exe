package service

import (
	"context"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"

	"boer-lan-server/internal/model"

	"gorm.io/gorm"
)

const (
	transferTypeUpload   = 1
	transferTypeDownload = 2
)

type PatternTransferService struct {
	db      *gorm.DB
	connMgr *ConnectionManager
}

func NewPatternTransferService(db *gorm.DB, connMgr *ConnectionManager) *PatternTransferService {
	return &PatternTransferService{
		db:      db,
		connMgr: connMgr,
	}
}

func (s *PatternTransferService) IsDeviceConnected(device model.Device) bool {
	if s == nil || s.connMgr == nil {
		return false
	}
	return s.connMgr.Get(strings.TrimSpace(device.Code)) != nil
}

func (s *PatternTransferService) RefreshDevicePatternFiles(device model.Device) ([]model.DevicePatternFile, error) {
	dc, err := s.getDeviceConnection(device)
	if err != nil {
		return nil, err
	}

	ch, cleanup, err := dc.beginPatternSession()
	if err != nil {
		return nil, err
	}
	defer cleanup()

	emitTCPLog(s.db, "info", true, "[TCP] Request device pattern list: device=%s id=%d", device.Code, device.ID)
	if err := dc.writePacket(buildReadPatternListRequest()); err != nil {
		return nil, err
	}

	ctx, cancel := context.WithTimeout(context.Background(), 8*time.Second)
	defer cancel()

	totalFrames := 0
	frameEntries := make(map[int][]PatternListEntry)
	for len(frameEntries) == 0 || len(frameEntries) < totalFrames {
		pkt, err := waitPatternPacket(ctx, ch, func(pkt *Packet) bool {
			return pkt.ParamType == PTPattern && pkt.ParamNo == PNReadPatternList
		})
		if err != nil {
			return nil, err
		}

		if totalFrames == 0 {
			totalFrames = int(pkt.TotalFrames)
			if totalFrames <= 0 {
				totalFrames = 1
			}
		}
		frameEntries[int(pkt.FrameNo)] = parsePatternListPayload(pkt.Data)
	}

	entries := make([]PatternListEntry, 0)
	for frameNo := 1; frameNo <= totalFrames; frameNo++ {
		entries = append(entries, frameEntries[frameNo]...)
	}

	records := make([]model.DevicePatternFile, 0, len(entries))
	for _, entry := range entries {
		fileName := strings.TrimSpace(entry.FileName)
		if fileName == "" {
			fileName = fmt.Sprintf("pattern_%d", entry.PatternNo)
		}
		records = append(records, model.DevicePatternFile{
			DeviceID:  device.ID,
			PatternNo: entry.PatternNo,
			FileName:  fileName,
		})
	}

	if err := s.db.Transaction(func(tx *gorm.DB) error {
		if err := tx.Where("device_id = ?", device.ID).Delete(&model.DevicePatternFile{}).Error; err != nil {
			return err
		}
		if len(records) == 0 {
			return nil
		}
		return tx.Create(&records).Error
	}); err != nil {
		return nil, err
	}

	return records, nil
}

func (s *PatternTransferService) DeleteDevicePatternFile(device model.Device, file model.DevicePatternFile) error {
	dc, err := s.getDeviceConnection(device)
	if err != nil {
		return err
	}

	ch, cleanup, err := dc.beginPatternSession()
	if err != nil {
		return err
	}
	defer cleanup()

	if err := dc.writePacket(buildDeletePatternCommand(file.PatternNo, file.FileName)); err != nil {
		return err
	}

	ctx, cancel := context.WithTimeout(context.Background(), 8*time.Second)
	defer cancel()

	pkt, err := waitPatternPacket(ctx, ch, func(pkt *Packet) bool {
		return pkt.ParamType == PTPattern && pkt.ParamNo == PNDeletePatternFile
	})
	if err != nil {
		return err
	}

	result, ok := parseDeletePatternResult(pkt.Data)
	if !ok {
		return fmt.Errorf("device delete response invalid")
	}
	if result != 0 {
		return fmt.Errorf("device delete rejected with result=%d", result)
	}

	return nil
}

func (s *PatternTransferService) UploadPatternFromDevice(device model.Device, file model.DevicePatternFile, userID uint) (*model.Pattern, error) {
	dc, err := s.getDeviceConnection(device)
	if err != nil {
		return nil, err
	}

	ch, cleanup, err := dc.beginPatternSession()
	if err != nil {
		return nil, err
	}
	defer cleanup()

	if err := dc.writePacket(buildUploadPatternCommand(file.PatternNo, file.FileName)); err != nil {
		return nil, err
	}

	ctx, cancel := context.WithTimeout(context.Background(), 25*time.Second)
	defer cancel()

	totalFrames := 0
	frames := make(map[int][]byte)
	uploadFinished := false

	for {
		if uploadFinished && totalFrames > 0 && len(frames) >= totalFrames {
			break
		}

		pkt, err := waitPatternPacket(ctx, ch, func(pkt *Packet) bool {
			return pkt.ParamType == PTPattern && (pkt.ParamNo == PNUploadPatternCommand || pkt.ParamNo == PNUploadPatternData)
		})
		if err != nil {
			return nil, err
		}

		switch pkt.ParamNo {
		case PNUploadPatternCommand:
			status, ok := parseSingleByteResult(pkt.Data)
			if !ok {
				continue
			}
			switch status {
			case 0:
				continue
			case 2:
				uploadFinished = true
			case 1:
				return nil, fmt.Errorf("device refused upload request")
			case 3:
				return nil, fmt.Errorf("device upload failed")
			case 4:
				return nil, fmt.Errorf("device upload timeout")
			default:
				return nil, fmt.Errorf("device upload finished with result=%d", status)
			}
		case PNUploadPatternData:
			if totalFrames == 0 {
				totalFrames = int(pkt.TotalFrames)
				if totalFrames <= 0 {
					totalFrames = 1
				}
			}
			frames[int(pkt.FrameNo)] = append([]byte(nil), pkt.Data...)
			if err := dc.writePacket(buildPatternResult(PNUploadPatternData, 0, pkt.TotalFrames, pkt.FrameNo)); err != nil {
				return nil, err
			}
		}
	}

	if totalFrames == 0 {
		return nil, fmt.Errorf("device did not send upload data")
	}

	payload := make([]byte, 0)
	for frameNo := 1; frameNo <= totalFrames; frameNo++ {
		chunk, ok := frames[frameNo]
		if !ok {
			return nil, fmt.Errorf("missing upload frame %d/%d", frameNo, totalFrames)
		}
		payload = append(payload, chunk...)
	}

	if err := os.MkdirAll(filepath.Join("uploads", "patterns"), 0755); err != nil {
		return nil, err
	}

	fileName := fmt.Sprintf(
		"device_%d_%d_%s.dst",
		device.ID,
		file.PatternNo,
		time.Now().Format("20060102150405"),
	)
	savePath := filepath.Join("uploads", "patterns", fileName)
	if err := os.WriteFile(savePath, payload, 0644); err != nil {
		return nil, err
	}

	patternName := strings.TrimSpace(strings.TrimSuffix(file.FileName, filepath.Ext(file.FileName)))
	if patternName == "" {
		patternName = strings.TrimSpace(file.FileName)
	}
	if patternName == "" {
		patternName = fmt.Sprintf("device_%d_pattern_%d", device.ID, file.PatternNo)
	}

	pattern := &model.Pattern{
		Name:        patternName,
		PatternType: file.PatternType,
		FileName:    file.FileName,
		FilePath:    savePath,
		FileSize:    int64(len(payload)),
		Stitches:    file.Stitches,
		UnitPrice:   file.UnitPrice,
		OrderNo:     file.OrderNo,
		UploadedBy:  userID,
	}
	if err := s.db.Create(pattern).Error; err != nil {
		return nil, err
	}

	return pattern, nil
}

func (s *PatternTransferService) ExecuteDownloadTask(taskID uint) error {
	var task model.DownloadTask
	if err := s.db.First(&task, taskID).Error; err != nil {
		return err
	}

	var device model.Device
	if err := s.db.First(&device, task.DeviceID).Error; err != nil {
		return err
	}
	var pattern model.Pattern
	if err := s.db.First(&pattern, task.PatternID).Error; err != nil {
		return err
	}

	dc, err := s.getDeviceConnection(device)
	if err != nil {
		return err
	}

	if strings.TrimSpace(pattern.FilePath) == "" || strings.HasPrefix(pattern.FilePath, "device://") {
		return fmt.Errorf("pattern file path is unavailable")
	}

	data, err := os.ReadFile(pattern.FilePath)
	if err != nil {
		return err
	}

	ch, cleanup, err := dc.beginPatternSession()
	if err != nil {
		return err
	}
	defer cleanup()

	patternName := strings.TrimSpace(pattern.Name)
	if patternName == "" {
		patternName = strings.TrimSpace(strings.TrimSuffix(pattern.FileName, filepath.Ext(pattern.FileName)))
	}
	if patternName == "" {
		patternName = fmt.Sprintf("pattern_%d", pattern.ID)
	}

	if err := s.updateDownloadTask(task.ID, map[string]interface{}{
		"status":   "downloading",
		"progress": 2,
		"message":  "等待设备确认下载指令",
	}); err != nil {
		return err
	}

	if err := dc.writePacket(buildDownloadPatternCommand(patternName)); err != nil {
		return err
	}

	commandCtx, cancelCommand := context.WithTimeout(context.Background(), 8*time.Second)
	defer cancelCommand()
	if _, err := waitPatternPacket(commandCtx, ch, func(pkt *Packet) bool {
		return pkt.ParamType == PTPattern && pkt.ParamNo == PNDownloadPatternCommand
	}); err != nil {
		return err
	}

	frames := buildDownloadPatternFrames(data)
	totalFrames := len(frames)

	for index, frame := range frames {
		progress := 5
		if totalFrames > 0 {
			progress = 5 + int(float64(index)*80/float64(totalFrames))
		}
		if err := s.updateDownloadTask(task.ID, map[string]interface{}{
			"progress": progress,
			"message":  fmt.Sprintf("正在下发 %d/%d", index+1, totalFrames),
		}); err != nil {
			return err
		}

		if err := dc.writePacket(frame); err != nil {
			return err
		}

		for {
			frameCtx, cancelFrame := context.WithTimeout(context.Background(), 8*time.Second)
			pkt, err := waitPatternPacket(frameCtx, ch, func(pkt *Packet) bool {
				return pkt.ParamType == PTPattern &&
					(pkt.ParamNo == PNCommunicationError || pkt.ParamNo == PNTransferResume)
			})
			cancelFrame()
			if err != nil {
				return err
			}

			if pkt.ParamNo == PNTransferResume {
				resume, ok := parseTransferResume(pkt.Data)
				if ok && resume.TransferType == transferTypeDownload {
					reqFrame := int(resume.FrameNo)
					if reqFrame < 1 {
						reqFrame = 1
					}
					if reqFrame <= totalFrames {
						if err := dc.writePacket(frames[reqFrame-1]); err != nil {
							return err
						}
					}
				}
				continue
			}

			result, ok := parseSingleByteResult(pkt.Data)
			if !ok {
				continue
			}
			if result != 0 {
				return fmt.Errorf("device rejected frame %d with result=%d", index+1, result)
			}
			break
		}
	}

	if err := s.updateDownloadTask(task.ID, map[string]interface{}{
		"progress": 95,
		"message":  "等待设备完成写入",
	}); err != nil {
		return err
	}

	resultCtx, cancelResult := context.WithTimeout(context.Background(), 8*time.Second)
	defer cancelResult()
	pkt, err := waitPatternPacket(resultCtx, ch, func(pkt *Packet) bool {
		return pkt.ParamType == PTPattern && pkt.ParamNo == PNDownloadPatternData
	})
	if err != nil {
		return err
	}

	result, ok := parseSingleByteResult(pkt.Data)
	if !ok {
		return fmt.Errorf("device final download response invalid")
	}
	if result != 0 {
		return fmt.Errorf("device final download result=%d", result)
	}

	return s.updateDownloadTask(task.ID, map[string]interface{}{
		"status":   "completed",
		"progress": 100,
		"message":  "下发完成",
	})
}

func (s *PatternTransferService) getDeviceConnection(device model.Device) (*DeviceConnection, error) {
	if s == nil || s.connMgr == nil {
		return nil, fmt.Errorf("pattern transfer service unavailable")
	}

	code := strings.TrimSpace(device.Code)
	if code == "" {
		return nil, fmt.Errorf("device code is empty")
	}

	dc := s.connMgr.Get(code)
	if dc == nil {
		return nil, fmt.Errorf("device %s is not connected", code)
	}
	return dc, nil
}

func waitPatternPacket(ctx context.Context, ch <-chan *Packet, match func(*Packet) bool) (*Packet, error) {
	for {
		select {
		case <-ctx.Done():
			if errors.Is(ctx.Err(), context.DeadlineExceeded) {
				return nil, fmt.Errorf("timeout waiting for device response")
			}
			return nil, ctx.Err()
		case pkt, ok := <-ch:
			if !ok {
				return nil, fmt.Errorf("device session closed")
			}
			if match == nil || match(pkt) {
				return pkt, nil
			}
		}
	}
}

func (s *PatternTransferService) updateDownloadTask(taskID uint, updates map[string]interface{}) error {
	if len(updates) == 0 {
		return nil
	}
	return s.db.Model(&model.DownloadTask{}).Where("id = ?", taskID).Updates(updates).Error
}
