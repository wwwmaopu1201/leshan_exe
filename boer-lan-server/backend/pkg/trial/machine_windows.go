//go:build windows

package trial

import (
	"fmt"
	"os"
	"os/user"
	"strings"

	"golang.org/x/sys/windows/registry"
)

func LegacyMachineHash() (string, error) {
	hostname, _ := os.Hostname()
	currentUser, _ := user.Current()
	username := ""
	if currentUser != nil {
		username = currentUser.Username
	}
	machineGuid := ""
	key, err := registry.OpenKey(registry.LOCAL_MACHINE, `SOFTWARE\Microsoft\Cryptography`, registry.QUERY_VALUE|registry.WOW64_64KEY)
	if err == nil {
		defer key.Close()
		machineGuid, _, _ = key.GetStringValue("MachineGuid")
	}
	return hashParts(machineGuid, hostname, username), nil
}

func MachineHash() (string, error) {
	key, err := registry.OpenKey(registry.LOCAL_MACHINE, `SOFTWARE\Microsoft\Cryptography`, registry.QUERY_VALUE|registry.WOW64_64KEY)
	if err != nil {
		return "", fmt.Errorf("read machine identity: %w", err)
	}
	defer key.Close()
	guid, _, err := key.GetStringValue("MachineGuid")
	if err != nil || strings.TrimSpace(guid) == "" {
		return "", fmt.Errorf("machine identity unavailable")
	}
	return hashParts("windows-machine-v2", strings.ToLower(strings.TrimSpace(guid))), nil
}
