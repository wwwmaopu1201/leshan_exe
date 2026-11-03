package service

import (
	"boer-lan-server/pkg/sqlitequeue"
	"context"
	"fmt"
	"log"
	"os"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"boer-lan-server/internal/model"

	"gorm.io/gorm"
)

const (
	debugOutputEnabledConfigKey = "debug_output_enabled"
	tcpDebugSource              = "tcp"
)

type pendingDebugLog struct {
	db    *gorm.DB
	entry model.DebugLog
}

var debugLogQueue = make(chan pendingDebugLog, 1024)
var debugLogWorker sync.Once
var droppedDebugLogs atomic.Uint64

// Devices retransmit identical packets; one database copy per window is enough
// because the file log above still records every occurrence.
const repeatedDebugLogWindow = time.Minute

var recentDebugLogsMu sync.Mutex
var recentDebugLogs = make(map[string]time.Time)

func isRepeatedDebugLog(message string, now time.Time) bool {
	recentDebugLogsMu.Lock()
	defer recentDebugLogsMu.Unlock()
	if last, ok := recentDebugLogs[message]; ok && now.Sub(last) < repeatedDebugLogWindow {
		return true
	}
	if len(recentDebugLogs) >= 4096 {
		for key, last := range recentDebugLogs {
			if now.Sub(last) >= repeatedDebugLogWindow {
				delete(recentDebugLogs, key)
			}
		}
		if len(recentDebugLogs) >= 4096 {
			recentDebugLogs = make(map[string]time.Time)
		}
	}
	recentDebugLogs[message] = now
	return false
}

func emitTCPLog(db *gorm.DB, level string, persist bool, format string, args ...interface{}) {
	message := fmt.Sprintf(format, args...)
	log.Print(message)
	if persist && shouldMirrorTCPToStdout() {
		fmt.Fprintf(os.Stdout, "%s %s\n", time.Now().Format("2006/01/02 15:04:05.000000"), message)
	}

	if !persist || db == nil || isRepeatedDebugLog(message, time.Now()) {
		return
	}

	entry := model.DebugLog{
		Level:   normalizeDebugLevel(level),
		Source:  tcpDebugSource,
		Message: message,
	}
	debugLogWorker.Do(func() { go persistDebugLogs() })
	select {
	case debugLogQueue <- pendingDebugLog{db: db, entry: entry}:
	default:
		// The file log above is already saved. Diagnostic database copies must
		// never delay device packets or displace business data under overload.
		droppedDebugLogs.Add(1)
	}
}

func persistDebugLogs() {
	lastReport := time.Now()
	for first := range debugLogQueue {
		batch := []pendingDebugLog{first}
		// Coalesce bursts into a small transaction rather than one per packet.
		timer := time.NewTimer(25 * time.Millisecond)
	collect:
		for len(batch) < 64 {
			select {
			case next := <-debugLogQueue:
				batch = append(batch, next)
			case <-timer.C:
				break collect
			}
		}
		timer.Stop()
		groups := make(map[*gorm.DB][]model.DebugLog)
		for _, item := range batch {
			groups[item.db] = append(groups[item.db], item.entry)
		}
		for db, entries := range groups {
			ctx, cancel := context.WithTimeout(sqlitequeue.Priority(context.Background(), -20), 250*time.Millisecond)
			background := db.WithContext(ctx)
			if isDebugOutputEnabled(background, db) {
				if err := background.Create(&entries).Error; err != nil {
					droppedDebugLogs.Add(uint64(len(entries)))
				}
			}
			cancel()
		}
		if time.Since(lastReport) >= 10*time.Second {
			if count := droppedDebugLogs.Swap(0); count > 0 {
				log.Printf("[TCP] skipped %d database debug-log copies under load; file logs retained", count)
			}
			lastReport = time.Now()
		}
	}
}

type debugOutputState struct {
	enabled   bool
	checkedAt time.Time
}

var debugOutputCacheMu sync.Mutex
var debugOutputCache = make(map[*gorm.DB]debugOutputState)

// The switch rarely changes; avoid one config read per persisted batch.
func isDebugOutputEnabled(db *gorm.DB, key *gorm.DB) bool {
	debugOutputCacheMu.Lock()
	defer debugOutputCacheMu.Unlock()
	if state, ok := debugOutputCache[key]; ok && time.Since(state.checkedAt) < 5*time.Second {
		return state.enabled
	}
	enabled := readDebugOutputEnabled(db)
	debugOutputCache[key] = debugOutputState{enabled: enabled, checkedAt: time.Now()}
	return enabled
}

func readDebugOutputEnabled(db *gorm.DB) bool {
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
