package trial

import (
	"testing"
	"time"

	appversion "boer-lan-server/pkg/version"
)

func TestEnsureResetsTrialWhenUpgradingVersion(t *testing.T) {
	tempDir := t.TempDir()
	t.Setenv("DATA_DIR", tempDir)
	t.Setenv("APP_VERSION", "1.0.13")

	statePath, err := resolveStatePath()
	if err != nil {
		t.Fatalf("resolveStatePath failed: %v", err)
	}

	machineHash, err := MachineHash()
	if err != nil {
		t.Fatalf("MachineHash failed: %v", err)
	}

	firstSeenAt := time.Now().Add(-2 * time.Hour).Unix()
	lastSeenAt := time.Now().Add(-5 * time.Minute).Unix()

	if err := writeState(statePath, &State{
		MachineHash:   machineHash,
		FirstSeenAt:   firstSeenAt,
		LastSeenAt:    lastSeenAt,
		LaunchCount:   7,
		PolicyVersion: trialPolicyVersion,
		AppVersion:    "1.0.12",
	}); err != nil {
		t.Fatalf("writeState failed: %v", err)
	}

	beforeEnsure := time.Now().Unix()
	status, err := Ensure()
	afterEnsure := time.Now().Unix()
	if err != nil {
		t.Fatalf("Ensure failed: %v", err)
	}
	if !status.Valid {
		t.Fatalf("expected valid trial after reset, got invalid: %s", status.Message)
	}

	state, exists, err := readState(statePath)
	if err != nil {
		t.Fatalf("readState failed: %v", err)
	}
	if !exists {
		t.Fatal("expected state file to exist")
	}
	if state.AppVersion != appversion.Resolve() {
		t.Fatalf("expected app version %q, got %q", appversion.Resolve(), state.AppVersion)
	}
	if state.LaunchCount != 1 {
		t.Fatalf("expected launch count to reset to 1, got %d", state.LaunchCount)
	}
	if state.FirstSeenAt <= firstSeenAt {
		t.Fatalf("expected firstSeenAt to reset after %d, got %d", firstSeenAt, state.FirstSeenAt)
	}
	if state.FirstSeenAt < beforeEnsure || state.FirstSeenAt > afterEnsure {
		t.Fatalf("expected firstSeenAt to reset within ensure window [%d,%d], got %d", beforeEnsure, afterEnsure, state.FirstSeenAt)
	}
}

func TestEnsureKeepsTrialForSameVersion(t *testing.T) {
	tempDir := t.TempDir()
	t.Setenv("DATA_DIR", tempDir)
	t.Setenv("APP_VERSION", "1.0.13")

	statePath, err := resolveStatePath()
	if err != nil {
		t.Fatalf("resolveStatePath failed: %v", err)
	}

	machineHash, err := MachineHash()
	if err != nil {
		t.Fatalf("MachineHash failed: %v", err)
	}

	firstSeenAt := time.Now().Add(-2 * time.Hour).Unix()
	lastSeenAt := time.Now().Add(-5 * time.Minute).Unix()

	if err := writeState(statePath, &State{
		MachineHash:   machineHash,
		FirstSeenAt:   firstSeenAt,
		LastSeenAt:    lastSeenAt,
		LaunchCount:   3,
		PolicyVersion: trialPolicyVersion,
		AppVersion:    "V1.0.13",
	}); err != nil {
		t.Fatalf("writeState failed: %v", err)
	}

	status, err := Ensure()
	if err != nil {
		t.Fatalf("Ensure failed: %v", err)
	}
	if !status.Valid {
		t.Fatalf("expected valid trial, got invalid: %s", status.Message)
	}

	state, exists, err := readState(statePath)
	if err != nil {
		t.Fatalf("readState failed: %v", err)
	}
	if !exists {
		t.Fatal("expected state file to exist")
	}
	if state.AppVersion != appversion.Resolve() {
		t.Fatalf("expected app version %q, got %q", appversion.Resolve(), state.AppVersion)
	}
	if state.FirstSeenAt != firstSeenAt {
		t.Fatalf("expected firstSeenAt to stay %d, got %d", firstSeenAt, state.FirstSeenAt)
	}
	if state.LaunchCount != 4 {
		t.Fatalf("expected launch count to increment to 4, got %d", state.LaunchCount)
	}
}

func TestEnsureKeepsTrialWhenDowngradingVersion(t *testing.T) {
	tempDir := t.TempDir()
	t.Setenv("DATA_DIR", tempDir)
	t.Setenv("APP_VERSION", "1.0.12")

	statePath, err := resolveStatePath()
	if err != nil {
		t.Fatalf("resolveStatePath failed: %v", err)
	}

	machineHash, err := MachineHash()
	if err != nil {
		t.Fatalf("MachineHash failed: %v", err)
	}

	firstSeenAt := time.Now().Add(-2 * time.Hour).Unix()
	lastSeenAt := time.Now().Add(-5 * time.Minute).Unix()

	if err := writeState(statePath, &State{
		MachineHash:   machineHash,
		FirstSeenAt:   firstSeenAt,
		LastSeenAt:    lastSeenAt,
		LaunchCount:   5,
		PolicyVersion: trialPolicyVersion,
		AppVersion:    "1.0.13",
	}); err != nil {
		t.Fatalf("writeState failed: %v", err)
	}

	status, err := Ensure()
	if err != nil {
		t.Fatalf("Ensure failed: %v", err)
	}
	if !status.Valid {
		t.Fatalf("expected valid trial after downgrade reset, got invalid: %s", status.Message)
	}

	state, exists, err := readState(statePath)
	if err != nil {
		t.Fatalf("readState failed: %v", err)
	}
	if !exists {
		t.Fatal("expected state file to exist")
	}
	if state.AppVersion != "1.0.13" {
		t.Fatalf("expected stored app version to remain at newest seen version %q, got %q", "1.0.13", state.AppVersion)
	}
	if state.LaunchCount != 6 {
		t.Fatalf("expected launch count to increment to 6, got %d", state.LaunchCount)
	}
	if state.FirstSeenAt != firstSeenAt {
		t.Fatalf("expected firstSeenAt to stay %d, got %d", firstSeenAt, state.FirstSeenAt)
	}
}
