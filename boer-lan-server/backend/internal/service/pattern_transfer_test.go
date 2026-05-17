package service

import (
	"testing"

	"boer-lan-server/internal/model"
)

func TestPatternListRefreshUsesShortTimeout(t *testing.T) {
	if patternListResponseTimeout >= patternTransferResponseTimeout {
		t.Fatalf("pattern list timeout must be shorter than transfer timeout")
	}
	if patternListResponseTimeout >= patternSessionAcquireTimeout {
		t.Fatalf("pattern list timeout must be shorter than session acquire timeout")
	}
}

func TestTrimKnownPatternFileExtension(t *testing.T) {
	tests := []struct {
		name     string
		fileName string
		want     string
	}{
		{name: "known lowercase extension", fileName: "袖口0.5宽.dst", want: "袖口0.5宽"},
		{name: "known uppercase extension", fileName: "A款加固线.DST", want: "A款加固线"},
		{name: "dot in pattern name is preserved", fileName: "袖口0.5宽", want: "袖口0.5宽"},
		{name: "chinese suffix after dot is preserved", fileName: "A款.加固线", want: "A款.加固线"},
		{name: "unknown extension is preserved", fileName: "样板.v2", want: "样板.v2"},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := TrimKnownPatternFileExtension(tt.fileName); got != tt.want {
				t.Fatalf("TrimKnownPatternFileExtension(%q) = %q, want %q", tt.fileName, got, tt.want)
			}
		})
	}
}

func TestResolveUploadedPatternNamePreservesDescriptiveDots(t *testing.T) {
	file := model.DevicePatternFile{
		PatternNo: 7,
		FileName:  "袖口0.5宽",
	}
	if got := ResolveUploadedPatternName(file, 3); got != "袖口0.5宽" {
		t.Fatalf("ResolveUploadedPatternName() = %q, want %q", got, "袖口0.5宽")
	}
}
