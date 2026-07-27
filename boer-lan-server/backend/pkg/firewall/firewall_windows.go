//go:build windows

package firewall

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"syscall"
	"unsafe"
)

const (
	seeMaskNoCloseProcess = 0x00000040
	swHide                = 0
	infinite              = 0xFFFFFFFF
)

type shellExecuteInfo struct {
	Size       uint32
	Mask       uint32
	Window     uintptr
	Verb       *uint16
	File       *uint16
	Parameters *uint16
	Directory  *uint16
	Show       int32
	Instance   uintptr
	IDList     uintptr
	Class      *uint16
	ClassKey   uintptr
	HotKey     uint32
	Icon       uintptr
	Process    syscall.Handle
}

var (
	shell32             = syscall.NewLazyDLL("shell32.dll")
	kernel32            = syscall.NewLazyDLL("kernel32.dll")
	procShellExecuteExW = shell32.NewProc("ShellExecuteExW")
	procWaitForSingle   = kernel32.NewProc("WaitForSingleObject")
	procGetExitCode     = kernel32.NewProc("GetExitCodeProcess")
	procCloseHandle     = kernel32.NewProc("CloseHandle")
)

func Inspect(managementPort, devicePort int) Status {
	management := inspectRule(ManagementRuleName, managementPort)
	device := inspectRule(DeviceRuleName, devicePort)
	needsRepair := !management.Exists || !device.Exists
	message := "局域网防火墙规则已配置"
	if needsRepair {
		message = "Windows 防火墙可能阻止局域网客户端连接"
	}

	return Status{
		Supported:     true,
		NeedsRepair:   needsRepair,
		Management:    management,
		Device:        device,
		Message:       message,
		AllowedScopes: []string{"LocalSubnet"},
	}
}

func inspectRule(name string, port int) RuleStatus {
	status := RuleStatus{Name: name, Port: port}
	if validatePorts(port, port) != nil {
		return status
	}

	output, _ := exec.Command(netshPath(), "advfirewall", "firewall", "show", "rule", "name="+name, "verbose").CombinedOutput()
	status.Exists = ruleOutputMatches(string(output), name, port)
	return status
}

func Repair(managementPort, devicePort int) (Status, error) {
	if err := validatePorts(managementPort, devicePort); err != nil {
		return Inspect(managementPort, devicePort), err
	}

	command := firewallCommand(managementPort, devicePort)
	if err := runElevated("cmd.exe", command); err != nil {
		return Inspect(managementPort, devicePort), err
	}

	status := Inspect(managementPort, devicePort)
	if status.NeedsRepair {
		return status, fmt.Errorf("firewall command completed but required rules were not found")
	}
	return status, nil
}

func netshPath() string {
	if systemRoot := os.Getenv("SystemRoot"); systemRoot != "" {
		return filepath.Join(systemRoot, "System32", "netsh.exe")
	}
	return "netsh.exe"
}

func firewallCommand(managementPort, devicePort int) string {
	netsh := netshPath()
	return fmt.Sprintf(
		`/d /s /c ""%s" advfirewall firewall delete rule name="%s" >nul 2>&1 & "%s" advfirewall firewall add rule name="%s" dir=in action=allow protocol=TCP localport=%d remoteip=LocalSubnet profile=any enable=yes & "%s" advfirewall firewall delete rule name="%s" >nul 2>&1 & "%s" advfirewall firewall add rule name="%s" dir=in action=allow protocol=TCP localport=%d remoteip=LocalSubnet profile=any enable=yes"`,
		netsh, ManagementRuleName,
		netsh, ManagementRuleName, managementPort,
		netsh, DeviceRuleName,
		netsh, DeviceRuleName, devicePort,
	)
}

func runElevated(file, parameters string) error {
	verbPtr, _ := syscall.UTF16PtrFromString("runas")
	filePtr, _ := syscall.UTF16PtrFromString(file)
	parametersPtr, _ := syscall.UTF16PtrFromString(parameters)

	info := shellExecuteInfo{
		Size:       uint32(unsafe.Sizeof(shellExecuteInfo{})),
		Mask:       seeMaskNoCloseProcess,
		Verb:       verbPtr,
		File:       filePtr,
		Parameters: parametersPtr,
		Show:       swHide,
	}

	result, _, callErr := procShellExecuteExW.Call(uintptr(unsafe.Pointer(&info)))
	if result == 0 {
		if callErr == syscall.Errno(1223) {
			return fmt.Errorf("用户取消了管理员授权")
		}
		return fmt.Errorf("failed to request administrator permission: %v", callErr)
	}
	if info.Process == 0 {
		return fmt.Errorf("elevated process handle is unavailable")
	}
	defer procCloseHandle.Call(uintptr(info.Process))

	procWaitForSingle.Call(uintptr(info.Process), infinite)
	var exitCode uint32
	result, _, callErr = procGetExitCode.Call(uintptr(info.Process), uintptr(unsafe.Pointer(&exitCode)))
	if result == 0 {
		return fmt.Errorf("failed to read firewall command result: %v", callErr)
	}
	if exitCode != 0 {
		return fmt.Errorf("firewall command failed with exit code %d", exitCode)
	}
	return nil
}
