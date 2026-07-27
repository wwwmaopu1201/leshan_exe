package firewall

import "testing"

func TestRuleOutputMatches(t *testing.T) {
	output := `Rule Name: Boer LAN Server - Management TCP
LocalPort: 8088`
	if !ruleOutputMatches(output, ManagementRuleName, 8088) {
		t.Fatal("expected matching rule output")
	}
	if ruleOutputMatches(output, ManagementRuleName, 9999) {
		t.Fatal("unexpected match for stale port")
	}
	if ruleOutputMatches(output, DeviceRuleName, 8088) {
		t.Fatal("unexpected match for different rule name")
	}
	if ruleOutputMatches(output+"\nEnabled: No", ManagementRuleName, 8088) {
		t.Fatal("unexpected match for disabled English rule")
	}
	if ruleOutputMatches(output+"\n已启用: 否", ManagementRuleName, 8088) {
		t.Fatal("unexpected match for disabled Chinese rule")
	}
}
