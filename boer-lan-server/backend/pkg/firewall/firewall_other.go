//go:build !windows

package firewall

func Inspect(managementPort, devicePort int) Status {
	return Status{
		Supported:   false,
		NeedsRepair: false,
		Management:  RuleStatus{Name: ManagementRuleName, Port: managementPort},
		Device:      RuleStatus{Name: DeviceRuleName, Port: devicePort},
		Message:     "当前系统不需要 Windows 防火墙规则",
	}
}

func Repair(managementPort, devicePort int) (Status, error) {
	return Inspect(managementPort, devicePort), nil
}
