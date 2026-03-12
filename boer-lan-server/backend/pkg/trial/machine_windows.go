//go:build windows

package trial

import (
	"os"
	"os/user"

	"golang.org/x/sys/windows/registry"
)

func MachineHash() (string, error) {
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
