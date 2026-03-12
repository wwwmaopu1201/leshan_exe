//go:build !windows

package trial

import (
	"os"
	"os/user"
	"runtime"
)

func MachineHash() (string, error) {
	hostname, _ := os.Hostname()
	currentUser, _ := user.Current()
	username := ""
	homeDir := ""
	if currentUser != nil {
		username = currentUser.Username
		homeDir = currentUser.HomeDir
	}
	return hashParts(runtime.GOOS, hostname, username, homeDir), nil
}
