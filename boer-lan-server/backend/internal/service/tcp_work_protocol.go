package service

import (
	"strings"

	"boer-lan-server/internal/model"

	"gorm.io/gorm"
)

const (
	workUserIDBytes           = 11
	workUserNameBytes         = 16
	workCurrentUserPayloadLen = workUserIDBytes + workUserNameBytes
	workStartAckResultBytes   = 1
	workStartAckPayloadLen    = workStartAckResultBytes
)

func parseWorkStartUserID(data []byte) string {
	if len(data) == 0 {
		return ""
	}
	if len(data) > workUserIDBytes {
		data = data[:workUserIDBytes]
	}
	return normalizeProtocolText(data)
}

func parseCurrentUserResult(data []byte) (byte, bool) {
	if len(data) == 0 {
		return 0, false
	}
	return data[len(data)-1], true
}

func buildUpdateCurrentUserIDCommand(employeeCode, employeeName string) *Packet {
	return buildProtocolCommand(PTWorkUser, PNUpdateCurrentUserID, encodeCurrentUserPayload(employeeCode, employeeName))
}

func buildWorkStartAckReply(request *Packet, result byte) *Packet {
	return buildProtocolReply(request, encodeWorkStartAckPayload(result))
}

func encodeWorkStartAckPayload(result byte) []byte {
	return []byte{result}
}

func encodeCurrentUserPayload(employeeCode, employeeName string) []byte {
	payload := make([]byte, 0, workCurrentUserPayloadLen)
	payload = append(payload, encodeFixedString(strings.TrimSpace(employeeCode), workUserIDBytes)...)
	payload = append(payload, encodeUTF16LEFixed(strings.TrimSpace(employeeName), workUserNameBytes)...)
	return payload
}

func findEmployeeByCode(db *gorm.DB, code string) (model.Employee, error) {
	code = strings.TrimSpace(code)
	if db == nil || code == "" {
		return model.Employee{}, gorm.ErrRecordNotFound
	}

	var employee model.Employee
	if err := db.Where("LOWER(code) = ?", strings.ToLower(code)).First(&employee).Error; err != nil {
		return model.Employee{}, err
	}
	return employee, nil
}
