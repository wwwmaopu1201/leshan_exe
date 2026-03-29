package trial

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	appversion "boer-lan-server/pkg/version"
)

const (
	trialDuration      = 72 * time.Hour
	rollbackLeeway     = 10 * time.Minute
	trialPolicyVersion = 4
	stateFolderName    = "BoerLAN"
	stateFileName      = "server-trial-state.json"
)

type State struct {
	MachineHash   string `json:"machineHash"`
	FirstSeenAt   int64  `json:"firstSeenAt"`
	LastSeenAt    int64  `json:"lastSeenAt"`
	LaunchCount   int64  `json:"launchCount"`
	PolicyVersion int    `json:"policyVersion,omitempty"`
	AppVersion    string `json:"appVersion,omitempty"`
}

type Status struct {
	Valid            bool
	Message          string
	ExpiresAt        time.Time
	Remaining        time.Duration
	StatePath        string
	MachineHash      string
	FirstSeenAt      time.Time
	LastSeenAt       time.Time
	TimeRollbackHint bool
}

func Ensure() (*Status, error) {
	statePath, err := resolveStatePath()
	if err != nil {
		return nil, err
	}

	machineHash, err := MachineHash()
	if err != nil {
		return nil, fmt.Errorf("failed to resolve machine hash: %w", err)
	}

	currentVersion := appversion.Resolve()
	now := time.Now()
	state, exists, err := readState(statePath)
	if err != nil {
		return nil, err
	}

	if !exists {
		state = &State{
			MachineHash:   machineHash,
			FirstSeenAt:   now.Unix(),
			LastSeenAt:    now.Unix(),
			LaunchCount:   1,
			PolicyVersion: trialPolicyVersion,
			AppVersion:    currentVersion,
		}
		if err := writeState(statePath, state); err != nil {
			return nil, err
		}
		return buildStatus(statePath, state, now, false), nil
	}

	if reason := trialResetReason(state, currentVersion); reason != "" {
		log.Printf("Resetting trial state: %s", reason)
		resetState(state, machineHash, currentVersion, now)
	}

	if state.MachineHash != machineHash {
		status := buildStatus(statePath, state, now, false)
		status.Valid = false
		status.Message = "试用授权已绑定到其他设备，无法继续使用"
		return status, errors.New(status.Message)
	}

	lastSeen := time.Unix(state.LastSeenAt, 0)
	if now.Before(lastSeen.Add(-rollbackLeeway)) {
		status := buildStatus(statePath, state, now, true)
		status.Valid = false
		status.Message = "检测到系统时间被回拨，试用已失效"
		return status, errors.New(status.Message)
	}

	status := buildStatus(statePath, state, now, false)
	if now.After(status.ExpiresAt) {
		status.Valid = false
		status.Message = "试用已过期，请联系供应商"
		return status, errors.New(status.Message)
	}

	state.LastSeenAt = now.Unix()
	state.LaunchCount++
	if err := writeState(statePath, state); err != nil {
		return nil, err
	}

	status = buildStatus(statePath, state, now, false)
	return status, nil
}

func trialResetReason(state *State, currentVersion string) string {
	if state.PolicyVersion < trialPolicyVersion {
		return fmt.Sprintf("policy version changed %d -> %d", state.PolicyVersion, trialPolicyVersion)
	}

	storedVersion := appversion.Normalize(state.AppVersion)
	if storedVersion == currentVersion {
		return ""
	}
	if storedVersion == "" {
		return fmt.Sprintf("missing app version, current=%s", currentVersion)
	}
	return fmt.Sprintf("app version changed %s -> %s", storedVersion, currentVersion)
}

func resetState(state *State, machineHash, currentVersion string, now time.Time) {
	state.MachineHash = machineHash
	state.FirstSeenAt = now.Unix()
	state.LastSeenAt = now.Unix()
	state.LaunchCount = 0
	state.PolicyVersion = trialPolicyVersion
	state.AppVersion = currentVersion
}

func StartExpiryWatcher(status *Status) {
	if status == nil || !status.Valid || status.Remaining <= 0 {
		return
	}

	time.AfterFunc(status.Remaining, func() {
		log.Printf("Trial expired after %s, exiting backend", trialDuration)
		os.Exit(1)
	})
}

func formatRemaining(remaining time.Duration) string {
	if remaining <= 0 {
		return "不足 1 分钟"
	}

	if remaining >= 24*time.Hour {
		days := int((remaining + 24*time.Hour - 1) / (24 * time.Hour))
		return fmt.Sprintf("%d 天", days)
	}

	if remaining >= time.Hour {
		hours := int((remaining + time.Hour - 1) / time.Hour)
		return fmt.Sprintf("%d 小时", hours)
	}

	minutes := int((remaining + time.Minute - 1) / time.Minute)
	if minutes < 1 {
		minutes = 1
	}
	return fmt.Sprintf("%d 分钟", minutes)
}

func buildStatus(statePath string, state *State, now time.Time, rollback bool) *Status {
	firstSeen := time.Unix(state.FirstSeenAt, 0)
	expiresAt := firstSeen.Add(trialDuration)
	remaining := time.Until(expiresAt)
	if remaining < 0 {
		remaining = 0
	}
	return &Status{
		Valid:            !now.After(expiresAt) && !rollback,
		Message:          fmt.Sprintf("试用剩余 %s", formatRemaining(remaining)),
		ExpiresAt:        expiresAt,
		Remaining:        remaining,
		StatePath:        statePath,
		MachineHash:      state.MachineHash,
		FirstSeenAt:      firstSeen,
		LastSeenAt:       time.Unix(state.LastSeenAt, 0),
		TimeRollbackHint: rollback,
	}
}

func readState(path string) (*State, bool, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return nil, false, nil
		}
		return nil, false, fmt.Errorf("failed to read trial state: %w", err)
	}

	var state State
	if err := json.Unmarshal(data, &state); err != nil {
		return nil, false, fmt.Errorf("failed to parse trial state: %w", err)
	}
	return &state, true, nil
}

func writeState(path string, state *State) error {
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		return fmt.Errorf("failed to create trial state dir: %w", err)
	}

	data, err := json.Marshal(state)
	if err != nil {
		return fmt.Errorf("failed to encode trial state: %w", err)
	}

	if err := os.WriteFile(path, data, 0644); err != nil {
		return fmt.Errorf("failed to write trial state: %w", err)
	}
	return nil
}

func resolveStatePath() (string, error) {
	dataDir := strings.TrimSpace(os.Getenv("DATA_DIR"))
	if dataDir != "" {
		return filepath.Join(dataDir, stateFileName), nil
	}

	if runtime.GOOS == "windows" {
		localAppData := strings.TrimSpace(os.Getenv("LOCALAPPDATA"))
		if localAppData != "" {
			return filepath.Join(localAppData, stateFolderName, stateFileName), nil
		}
	}

	configDir, err := os.UserConfigDir()
	if err != nil {
		return "", fmt.Errorf("failed to resolve config dir: %w", err)
	}
	return filepath.Join(configDir, stateFolderName, stateFileName), nil
}

func hashParts(parts ...string) string {
	h := sha256.New()
	for _, part := range parts {
		_, _ = h.Write([]byte(part))
		_, _ = h.Write([]byte{'|'})
	}
	return hex.EncodeToString(h.Sum(nil))
}
