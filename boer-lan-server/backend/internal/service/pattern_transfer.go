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
	transferTypeUpload             = 1
	transferTypeDownload           = 2
	patternTransferResponseTimeout = 10 * time.Minute
	patternUploadStartTimeout      = 30 * time.Second
	patternUploadIdleTimeout       = 3 * time.Second
	patternUploadFinishTimeout     = 30 * time.Second
	patternUploadResumeInterval    = 3 * time.Second
)

type deleteAttempt struct {
	label   string
	options DeletePatternOptions
}

type UploadConflictMode string

const (
	UploadConflictModeAsk       UploadConflictMode = "ask"
	UploadConflictModeOverwrite UploadConflictMode = "overwrite"
	UploadConflictModeRename    UploadConflictMode = "rename"
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
	_, err := s.getDeviceConnection(device)
	return err == nil
}

func (s *PatternTransferService) RefreshDevicePatternFiles(device model.Device) ([]model.DevicePatternFile, error) {
	return s.refreshDevicePatternFiles(device, true)
}

func (s *PatternTransferService) RefreshDevicePatternFilesIfIdle(device model.Device) ([]model.DevicePatternFile, error) {
	return s.refreshDevicePatternFiles(device, false)
}

func (s *PatternTransferService) refreshDevicePatternFiles(device model.Device, waitForSession bool) ([]model.DevicePatternFile, error) {
	dc, err := s.getDeviceConnection(device)
	if err != nil {
		return nil, err
	}

	var ch chan *Packet
	var cleanup func()
	if waitForSession {
		ch, cleanup, err = dc.beginPatternSession()
	} else {
		ch, cleanup, err = dc.tryBeginPatternSession()
	}
	if err != nil {
		return nil, err
	}
	defer cleanup()

	emitTCPLog(s.db, "info", true, "[TCP] Request device pattern list: device=%s id=%d", device.Code, device.ID)
	if err := dc.writePacket(buildReadPatternListRequest()); err != nil {
		return nil, err
	}

	ctx, cancel := context.WithTimeout(context.Background(), patternTransferResponseTimeout)
	defer cancel()

	totalFrames := 0
	frameEntries := make(map[int][]PatternListEntry)
	for len(frameEntries) == 0 || len(frameEntries) < totalFrames {
		pkt, err := waitPatternPacket(ctx, ch, func(pkt *Packet) bool {
			return pkt.ParamType == PTPattern &&
				(pkt.ParamNo == PNReadPatternList || pkt.ParamNo == PNRequestServerList)
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
		frameEntries[int(pkt.FrameNo)] = parsePatternListEntries(pkt)
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

	attempts := buildDeleteAttempts(file)
	if len(attempts) == 0 {
		return fmt.Errorf("device delete request has no usable attempt")
	}

	var lastResult byte
	for _, attempt := range attempts {
		emitTCPLog(s.db, "info", true,
			"[TCP] Delete device pattern attempt: device=%s patternNo=%d file=%s mode=%s includeId=%t label=%s",
			device.Code,
			file.PatternNo,
			file.FileName,
			attempt.options.NameEncoding,
			attempt.options.IncludeID,
			attempt.label,
		)

		if err := dc.writePacket(buildDeletePatternCommandWithOptions(attempt.options)); err != nil {
			return err
		}

		ctx, cancel := context.WithTimeout(context.Background(), 8*time.Second)
		pkt, err := waitPatternPacket(ctx, ch, func(pkt *Packet) bool {
			return pkt.ParamType == PTPattern && pkt.ParamNo == PNDeletePatternFile
		})
		cancel()
		if err != nil {
			return err
		}

		result, ok := parseDeletePatternResult(pkt.Data)
		if !ok {
			return fmt.Errorf("device delete response invalid")
		}
		lastResult = result
		if result == 0 {
			return nil
		}
	}

	return fmt.Errorf("device delete failed: %s", describeDeletePatternResult(lastResult))
}

func (s *PatternTransferService) UploadPatternFromDevice(device model.Device, file model.DevicePatternFile, userID uint) (*model.Pattern, error) {
	return s.UploadPatternFromDeviceWithMode(device, file, userID, UploadConflictModeAsk)
}

func (s *PatternTransferService) UploadPatternFromDeviceWithMode(device model.Device, file model.DevicePatternFile, userID uint, conflictMode UploadConflictMode) (*model.Pattern, error) {
	return s.UploadPatternFromDeviceWithOptions(device, file, userID, conflictMode, "")
}

func (s *PatternTransferService) UploadPatternFromDeviceWithOptions(
	device model.Device,
	file model.DevicePatternFile,
	userID uint,
	conflictMode UploadConflictMode,
	targetName string,
) (*model.Pattern, error) {
	dc, err := s.getDeviceConnection(device)
	if err != nil {
		return nil, err
	}

	if refreshed, refreshErr := s.RefreshDevicePatternFilesIfIdle(device); refreshErr == nil {
		if resolved, ok := resolveLiveDevicePatternFile(refreshed, file); ok {
			file = resolved
		}
	} else if isPatternTransferBusy(refreshErr) {
		emitTCPLog(s.db, "info", false, "[TCP] Skip refresh before upload because device is busy: device=%s id=%d", device.Code, device.ID)
	} else {
		emitTCPLog(s.db, "warn", true, "[TCP] Refresh device pattern list before upload failed: device=%s id=%d err=%v", device.Code, device.ID, refreshErr)
	}

	ch, cleanup, err := dc.beginPatternSession()
	if err != nil {
		return nil, err
	}
	defer cleanup()

	emitTCPLog(s.db, "info", true, "[TCP] Request device upload pattern: device=%s id=%d patternNo=%d file=%s", device.Code, device.ID, file.PatternNo, file.FileName)
	if err := dc.writePacket(buildUploadPatternCommand(file.PatternNo, file.FileName)); err != nil {
		return nil, err
	}

	ctx, cancel := context.WithTimeout(context.Background(), patternTransferResponseTimeout)
	defer cancel()

	totalFrames := 0
	frames := make(map[int][]byte)
	uploadFinished := false
	lastResumeFrame := 0
	var lastResumeAt time.Time

	for {
		if uploadFinished && totalFrames > 0 && len(frames) >= totalFrames {
			break
		}

		waitTimeout := patternUploadStartTimeout
		if totalFrames > 0 {
			waitTimeout = patternUploadIdleTimeout
			if len(frames) >= totalFrames && !uploadFinished {
				waitTimeout = patternUploadFinishTimeout
			}
		}
		waitCtx, cancelWait := context.WithTimeout(ctx, waitTimeout)
		pkt, err := waitPatternPacket(waitCtx, ch, func(pkt *Packet) bool {
			return pkt.ParamType == PTPattern && (pkt.ParamNo == PNUploadPatternCommand || pkt.ParamNo == PNUploadPatternData || pkt.ParamNo == PNTransferResume)
		})
		cancelWait()
		if err != nil {
			if isWaitPatternPacketTimeout(err) && totalFrames > 0 && len(frames) < totalFrames {
				missingFrame := firstMissingUploadFrame(totalFrames, frames)
				if missingFrame > 0 && (missingFrame != lastResumeFrame || time.Since(lastResumeAt) >= patternUploadResumeInterval) {
					emitTCPLog(
						s.db,
						"warn",
						true,
						"[TCP] Upload stalled, requesting resume: device=%s patternNo=%d file=%s nextFrame=%d received=%d/%d",
						device.Code,
						file.PatternNo,
						file.FileName,
						missingFrame,
						len(frames),
						totalFrames,
					)
					if err := dc.writePacket(buildTransferResume(transferTypeUpload, uint8(missingFrame))); err != nil {
						return nil, err
					}
					lastResumeFrame = missingFrame
					lastResumeAt = time.Now()
					continue
				}
			}
			return nil, err
		}

		switch pkt.ParamNo {
		case PNUploadPatternCommand:
			status, ok := parseSingleByteResult(pkt.Data)
			if !ok {
				continue
			}
			emitTCPLog(s.db, "info", true, "[TCP] Device upload status: device=%s patternNo=%d file=%s status=%d", device.Code, file.PatternNo, file.FileName, status)
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
		case PNTransferResume:
			resume, ok := parseTransferResume(pkt.Data)
			if !ok {
				continue
			}
			if resume.TransferType == transferTypeUpload {
				reqFrame := resume.FrameNo
				if reqFrame < 1 {
					reqFrame = 1
				}
				emitTCPLog(s.db, "info", true, "[TCP] Device upload resume requested: device=%s patternNo=%d file=%s frame=%d", device.Code, file.PatternNo, file.FileName, reqFrame)
				if err := dc.writePacket(buildTransferResume(transferTypeUpload, reqFrame)); err != nil {
					return nil, err
				}
				lastResumeFrame = int(reqFrame)
				lastResumeAt = time.Now()
			}
		case PNUploadPatternData:
			if totalFrames == 0 {
				totalFrames = int(pkt.TotalFrames)
				if totalFrames <= 0 {
					totalFrames = 1
				}
			}
			frames[int(pkt.FrameNo)] = append([]byte(nil), pkt.Data...)
			emitTCPLog(s.db, "info", false, "[TCP] Device upload data frame: device=%s patternNo=%d frame=%d/%d size=%d", device.Code, file.PatternNo, pkt.FrameNo, pkt.TotalFrames, len(pkt.Data))
			if err := dc.writePacket(buildUploadPatternDataAck(pkt.TotalFrames, pkt.FrameNo, 0)); err != nil {
				return nil, err
			}
			emitTCPLog(s.db, "info", false, "[TCP] Device upload data ack sent: device=%s patternNo=%d frame=%d/%d result=0", device.Code, file.PatternNo, pkt.FrameNo, pkt.TotalFrames)
			lastResumeFrame = 0
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

	pattern, err := s.saveUploadedPattern(device, file, savePath, payload, userID, conflictMode, targetName)
	if err != nil {
		return nil, err
	}

	emitTCPLog(s.db, "info", true, "[TCP] Device upload completed: device=%s patternNo=%d file=%s bytes=%d patternId=%d", device.Code, file.PatternNo, file.FileName, len(payload), pattern.ID)

	return pattern, nil
}

func NormalizeUploadConflictMode(raw string) UploadConflictMode {
	switch UploadConflictMode(strings.ToLower(strings.TrimSpace(raw))) {
	case UploadConflictModeOverwrite:
		return UploadConflictModeOverwrite
	case UploadConflictModeRename:
		return UploadConflictModeRename
	default:
		return UploadConflictModeAsk
	}
}

func ResolveUploadedPatternName(file model.DevicePatternFile, deviceID uint) string {
	patternName := strings.TrimSpace(strings.TrimSuffix(file.FileName, filepath.Ext(file.FileName)))
	if patternName == "" {
		patternName = strings.TrimSpace(file.FileName)
	}
	if patternName == "" {
		patternName = fmt.Sprintf("device_%d_pattern_%d", deviceID, file.PatternNo)
	}
	return patternName
}

func (s *PatternTransferService) saveUploadedPattern(
	device model.Device,
	file model.DevicePatternFile,
	savePath string,
	payload []byte,
	userID uint,
	conflictMode UploadConflictMode,
	targetName string,
) (*model.Pattern, error) {
	if s == nil || s.db == nil {
		return nil, fmt.Errorf("pattern transfer service unavailable")
	}

	baseName := ResolveUploadedPatternName(file, device.ID)
	targetName = strings.TrimSpace(targetName)
	conflictMode = NormalizeUploadConflictMode(string(conflictMode))

	var existing model.Pattern
	existingErr := s.db.Where("name = ?", baseName).First(&existing).Error
	hasExisting := existingErr == nil
	if existingErr != nil && !errors.Is(existingErr, gorm.ErrRecordNotFound) {
		return nil, existingErr
	}

	switch conflictMode {
	case UploadConflictModeAsk:
		if hasExisting {
			return nil, fmt.Errorf("pattern name conflict: %s", baseName)
		}
		return s.createUploadedPattern(file, savePath, payload, userID, baseName)
	case UploadConflictModeRename:
		renameBase := baseName
		if targetName != "" {
			renameBase = targetName
		}
		finalName, err := s.nextAvailablePatternName(renameBase, 0)
		if err != nil {
			return nil, err
		}
		return s.createUploadedPattern(file, savePath, payload, userID, finalName)
	case UploadConflictModeOverwrite:
		if !hasExisting {
			return s.createUploadedPattern(file, savePath, payload, userID, baseName)
		}
		oldPath := strings.TrimSpace(existing.FilePath)
		updates := map[string]interface{}{
			"name":         baseName,
			"pattern_type": file.PatternType,
			"file_name":    file.FileName,
			"file_path":    savePath,
			"file_size":    int64(len(payload)),
			"stitches":     file.Stitches,
			"unit_price":   file.UnitPrice,
			"order_no":     file.OrderNo,
			"uploaded_by":  userID,
		}
		if err := s.db.Model(&existing).Updates(updates).Error; err != nil {
			return nil, err
		}
		if oldPath != "" && oldPath != savePath {
			_ = os.Remove(oldPath)
		}
		if err := s.db.First(&existing, existing.ID).Error; err != nil {
			return nil, err
		}
		return &existing, nil
	default:
		return nil, fmt.Errorf("unsupported upload conflict mode: %s", conflictMode)
	}
}

func (s *PatternTransferService) createUploadedPattern(
	file model.DevicePatternFile,
	savePath string,
	payload []byte,
	userID uint,
	patternName string,
) (*model.Pattern, error) {
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

func (s *PatternTransferService) nextAvailablePatternName(baseName string, excludeID uint) (string, error) {
	baseName = strings.TrimSpace(baseName)
	if baseName == "" {
		baseName = "未命名花型"
	}
	for index := 0; index < 1000; index++ {
		candidate := baseName
		if index > 0 {
			candidate = fmt.Sprintf("%s(%d)", baseName, index)
		}
		query := s.db.Model(&model.Pattern{}).Where("name = ?", candidate)
		if excludeID > 0 {
			query = query.Where("id <> ?", excludeID)
		}
		var count int64
		if err := query.Count(&count).Error; err != nil {
			return "", err
		}
		if count == 0 {
			return candidate, nil
		}
	}
	return "", fmt.Errorf("failed to allocate unique pattern name for %s", baseName)
}

func resolveLiveDevicePatternFile(refreshed []model.DevicePatternFile, fallback model.DevicePatternFile) (model.DevicePatternFile, bool) {
	if len(refreshed) == 0 {
		return model.DevicePatternFile{}, false
	}

	for _, item := range refreshed {
		if fallback.PatternNo > 0 && item.PatternNo == fallback.PatternNo {
			return item, true
		}
	}

	fallbackName := strings.TrimSpace(fallback.FileName)
	if fallbackName == "" {
		return model.DevicePatternFile{}, false
	}
	for _, item := range refreshed {
		if strings.EqualFold(strings.TrimSpace(item.FileName), fallbackName) {
			return item, true
		}
	}
	return model.DevicePatternFile{}, false
}

func buildDeleteAttempts(file model.DevicePatternFile) []deleteAttempt {
	fileName := strings.TrimSpace(file.FileName)
	attempts := make([]deleteAttempt, 0, 5)

	push := func(label string, options DeletePatternOptions) {
		if !options.IncludeID && strings.TrimSpace(options.FileName) == "" {
			return
		}
		if options.IncludeID && options.PatternNo == 0 && strings.TrimSpace(options.FileName) == "" {
			return
		}
		attempts = append(attempts, deleteAttempt{
			label:   label,
			options: options,
		})
	}

	push("纯名称 Unicode变长", DeletePatternOptions{
		PatternNo:    file.PatternNo,
		FileName:     fileName,
		NameEncoding: "unicode",
		IncludeID:    false,
	})
	push("编号+名称 Unicode变长", DeletePatternOptions{
		PatternNo:    file.PatternNo,
		FileName:     fileName,
		NameEncoding: "unicode",
		IncludeID:    true,
	})
	push("编号+名称 Unicode固定44字节", DeletePatternOptions{
		PatternNo:    file.PatternNo,
		FileName:     fileName,
		NameEncoding: "unicodeFixed",
		IncludeID:    true,
	})
	push("纯名称 Unicode固定44字节", DeletePatternOptions{
		PatternNo:    file.PatternNo,
		FileName:     fileName,
		NameEncoding: "unicodeFixed",
		IncludeID:    false,
	})
	push("纯名称 兼容固定44字节", DeletePatternOptions{
		PatternNo:    file.PatternNo,
		FileName:     fileName,
		NameEncoding: "compatibleFixed",
		IncludeID:    false,
	})

	return attempts
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

	commandCtx, cancelCommand := context.WithTimeout(context.Background(), patternTransferResponseTimeout)
	defer cancelCommand()
	commandAck, err := waitPatternPacket(commandCtx, ch, func(pkt *Packet) bool {
		return pkt.ParamType == PTPattern && pkt.ParamNo == PNDownloadPatternCommand
	})
	if err != nil {
		return err
	}

	addr1 := commandAck.Addr1
	addr2 := commandAck.Addr2
	frames := buildDownloadPatternFrames(data, addr1, addr2)
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
			frameCtx, cancelFrame := context.WithTimeout(context.Background(), patternTransferResponseTimeout)
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

	resultCtx, cancelResult := context.WithTimeout(context.Background(), patternTransferResponseTimeout)
	defer cancelResult()
	pkt, err := waitPatternPacket(resultCtx, ch, func(pkt *Packet) bool {
		return pkt.ParamType == PTPattern && pkt.ParamNo == PNDownloadPatternData
	})
	if err != nil {
		return err
	}

	result, echoedName, ok := parseDownloadPatternFinalResult(pkt.Data)
	if !ok {
		return fmt.Errorf("device final download response invalid")
	}
	if result != 0 {
		return fmt.Errorf("device final download result=%d", result)
	}
	if echoedName != "" {
		emitTCPLog(s.db, "info", true, "[TCP] Device download completed: device=%s pattern=%s echoed=%s",
			device.Code, patternName, echoedName)
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

	if dc := s.findDeviceConnection(device); dc != nil {
		return dc, nil
	}

	name := strings.TrimSpace(device.Name)
	code := strings.TrimSpace(device.Code)
	switch {
	case name != "" && code != "":
		return nil, fmt.Errorf("device %s(%s) is not connected", name, code)
	case name != "":
		return nil, fmt.Errorf("device %s is not connected", name)
	case code != "":
		return nil, fmt.Errorf("device %s is not connected", code)
	case device.ID > 0:
		return nil, fmt.Errorf("device #%d is not connected", device.ID)
	default:
		return nil, fmt.Errorf("device is not connected")
	}
}

func (s *PatternTransferService) findDeviceConnection(device model.Device) *DeviceConnection {
	if s == nil || s.connMgr == nil {
		return nil
	}

	code := strings.TrimSpace(device.Code)
	mainboardSN := strings.TrimSpace(device.MainboardSN)
	ip := strings.TrimSpace(device.IP)
	pendingCode := pendingDeviceCodeForIP(ip)

	var matchedByMainboard *DeviceConnection
	var matchedByIP *DeviceConnection
	for _, dc := range s.connMgr.GetAll() {
		if dc == nil {
			continue
		}

		dcCode := strings.TrimSpace(dc.deviceCode)
		if mainboardSN != "" && strings.TrimSpace(dc.deviceFlag) == mainboardSN {
			return dc
		}
		if device.ID > 0 && dc.deviceID == device.ID {
			matchedByMainboard = dc
		}
		if code != "" && dcCode == code && matchedByMainboard == nil {
			matchedByMainboard = dc
		}
		if pendingCode != "" && dcCode == pendingCode {
			matchedByIP = dc
		}
		if ip != "" && extractIP(dc.conn.RemoteAddr().String()) == ip {
			matchedByIP = dc
		}
	}

	if matchedByMainboard != nil {
		return matchedByMainboard
	}
	return matchedByIP
}

func parsePatternListEntries(pkt *Packet) []PatternListEntry {
	if pkt == nil {
		return []PatternListEntry{}
	}

	includeID := pkt.ParamNo != PNRequestServerList
	entries := parsePatternListPayloadWithOptions(pkt.Data, includeID)
	if len(entries) > 0 {
		return entries
	}

	if includeID {
		return parsePatternListPayloadWithOptions(pkt.Data, false)
	}
	return parsePatternListPayloadWithOptions(pkt.Data, true)
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

func isWaitPatternPacketTimeout(err error) bool {
	return err != nil && strings.Contains(strings.ToLower(err.Error()), "timeout waiting for device response")
}

func firstMissingUploadFrame(totalFrames int, frames map[int][]byte) int {
	if totalFrames <= 0 {
		return 0
	}
	for frameNo := 1; frameNo <= totalFrames; frameNo++ {
		if _, ok := frames[frameNo]; !ok {
			return frameNo
		}
	}
	return 0
}

func (s *PatternTransferService) updateDownloadTask(taskID uint, updates map[string]interface{}) error {
	if len(updates) == 0 {
		return nil
	}
	return s.db.Model(&model.DownloadTask{}).Where("id = ?", taskID).Updates(updates).Error
}
