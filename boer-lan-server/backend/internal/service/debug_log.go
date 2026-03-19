package service

import (
	"fmt"
	"log"
	"os"
	"strconv"
	"strings"
	"time"

	"boer-lan-server/internal/model"

	"gorm.io/gorm"
)

const (
	debugOutputEnabledConfigKey = "debug_output_enabled"
	tcpDebugSource              = "tcp"
)

func emitTCPLog(db *gorm.DB, level string, persist bool, format string, args ...interface{}) {
	message := fmt.Sprintf(format, args...)
	log.Print(message)
	if persist && shouldMirrorTCPToStdout() {
		fmt.Fprintf(os.Stdout, "%s %s\n", time.Now().Format("2006/01/02 15:04:05.000000"), message)
	}

	if !persist || db == nil || !isDebugOutputEnabled(db) {
		return
	}

	entry := model.DebugLog{
		Level:   normalizeDebugLevel(level),
		Source:  tcpDebugSource,
		Message: message,
	}
	if err := db.Create(&entry).Error; err != nil {
		log.Printf("[TCP] failed to persist debug log: %v", err)
	}
}

func isDebugOutputEnabled(db *gorm.DB) bool {
	var config model.ServerConfig
	if err := db.Where("key = ?", debugOutputEnabledConfigKey).First(&config).Error; err != nil {
		return true
	}

	value := strings.ToLower(strings.TrimSpace(config.Value))
	if value == "" {
		return true
	}
	switch value {
	case "1", "true", "yes", "on":
		return true
	case "0", "false", "no", "off":
		return false
	default:
		return true
	}
}

func normalizeDebugLevel(level string) string {
	switch strings.ToLower(strings.TrimSpace(level)) {
	case "warn", "warning":
		return "warn"
	case "error":
		return "error"
	default:
		return "info"
	}
}

func shouldMirrorTCPToStdout() bool {
	quietMode := parseEnvBoolWithDefault("QUIET_MODE", true)
	logToStdout := parseEnvBoolWithDefault("LOG_TO_STDOUT", !quietMode)
	if logToStdout {
		return false
	}

	return parseEnvBoolWithDefault("TCP_LOG_TO_STDOUT", quietMode)
}

func parseEnvBoolWithDefault(name string, fallback bool) bool {
	value := strings.TrimSpace(os.Getenv(name))
	if value == "" {
		return fallback
	}

	parsed, err := strconv.ParseBool(value)
	if err != nil {
		return fallback
	}
	return parsed
}
