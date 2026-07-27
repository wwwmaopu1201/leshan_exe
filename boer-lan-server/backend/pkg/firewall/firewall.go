package firewall

import (
	"fmt"
	"strings"
)

const (
	ManagementRuleName = "Boer LAN Server - Management TCP"
	DeviceRuleName     = "Boer LAN Server - Device TCP"
)

type RuleStatus struct {
	Name   string `json:"name"`
	Port   int    `json:"port"`
	Exists bool   `json:"exists"`
}

type Status struct {
	Supported     bool       `json:"supported"`
	NeedsRepair   bool       `json:"needsRepair"`
	Management    RuleStatus `json:"management"`
	Device        RuleStatus `json:"device"`
	Message       string     `json:"message"`
	AllowedScopes []string   `json:"allowedScopes"`
}

func validatePorts(managementPort, devicePort int) error {
	if managementPort < 1 || managementPort > 65535 {
		return fmt.Errorf("invalid management port: %d", managementPort)
	}
	if devicePort < 1 || devicePort > 65535 {
		return fmt.Errorf("invalid device port: %d", devicePort)
	}
	return nil
}

func ruleOutputMatches(output, ruleName string, port int) bool {
	normalized := strings.ToLower(string(output))
	disabledMarkers := []string{
		"enabled: no",
		"enabled:no",
		"已启用: 否",
		"已启用:否",
		"啟用: 否",
		"啟用:否",
	}
	for _, marker := range disabledMarkers {
		if strings.Contains(normalized, marker) {
			return false
		}
	}
	return strings.Contains(normalized, strings.ToLower(ruleName)) &&
		strings.Contains(normalized, fmt.Sprintf("%d", port))
}
