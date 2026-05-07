package service

import "testing"

func TestPatternListRefreshUsesShortTimeout(t *testing.T) {
	if patternListResponseTimeout >= patternTransferResponseTimeout {
		t.Fatalf("pattern list timeout must be shorter than transfer timeout")
	}
	if patternListResponseTimeout >= patternSessionAcquireTimeout {
		t.Fatalf("pattern list timeout must be shorter than session acquire timeout")
	}
}
