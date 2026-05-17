package api

import (
	"testing"

	"boer-lan-server/internal/model"
)

func TestPatternDownloadFileNameUsesDeviceFileExtension(t *testing.T) {
	tests := []struct {
		name     string
		pattern  model.Pattern
		filePath string
		want     string
	}{
		{
			name:     "uses extension from stored device file name",
			pattern:  model.Pattern{Name: "袖口0.5宽", FileName: "设备文件.DST"},
			filePath: "uploads/patterns/device_1_2_20260517120000.dst",
			want:     "袖口0.5宽.DST",
		},
		{
			name:     "falls back to saved file path extension",
			pattern:  model.Pattern{Name: "A款.加固线", FileName: "A款.加固线"},
			filePath: "uploads/patterns/device_1_2_20260517120000.dst",
			want:     "A款.加固线.dst",
		},
		{
			name:     "does not treat descriptive dot as extension",
			pattern:  model.Pattern{Name: "样板.v2", FileName: "样板.v2"},
			filePath: "uploads/patterns/device_1_2_20260517120000",
			want:     "样板.v2",
		},
		{
			name:     "does not duplicate existing extension",
			pattern:  model.Pattern{Name: "后片.vdt", FileName: "设备文件.vdt"},
			filePath: "uploads/patterns/device_1_2_20260517120000.vdt",
			want:     "后片.vdt",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := patternDownloadFileName(tt.pattern, tt.filePath); got != tt.want {
				t.Fatalf("patternDownloadFileName() = %q, want %q", got, tt.want)
			}
		})
	}
}
