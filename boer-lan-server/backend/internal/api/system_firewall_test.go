package api

import (
	"net/http/httptest"
	"testing"

	"github.com/gin-gonic/gin"
)

func TestRequestIsLocalConsole(t *testing.T) {
	gin.SetMode(gin.TestMode)
	tests := []struct {
		name       string
		remoteAddr string
		origin     string
		want       bool
	}{
		{name: "tauri", remoteAddr: "127.0.0.1:50000", origin: "http://tauri.localhost", want: true},
		{name: "win7 shell", remoteAddr: "127.0.0.1:50000", origin: "http://127.0.0.1:43117", want: true},
		{name: "development", remoteAddr: "[::1]:50000", origin: "http://localhost:45173", want: true},
		{name: "local command without origin", remoteAddr: "127.0.0.1:50000", want: true},
		{name: "browser csrf", remoteAddr: "127.0.0.1:50000", origin: "https://example.com", want: false},
		{name: "remote caller", remoteAddr: "192.168.1.20:50000", origin: "http://192.168.1.20", want: false},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			req := httptest.NewRequest("POST", "/api/system/firewall/repair", nil)
			req.RemoteAddr = tt.remoteAddr
			if tt.origin != "" {
				req.Header.Set("Origin", tt.origin)
			}
			ctx, _ := gin.CreateTestContext(httptest.NewRecorder())
			ctx.Request = req
			if got := requestIsLocalConsole(ctx); got != tt.want {
				t.Fatalf("requestIsLocalConsole() = %v, want %v", got, tt.want)
			}
		})
	}
}
