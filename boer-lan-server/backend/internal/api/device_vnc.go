package api

import (
	"io"
	"net"
	"net/http"
	"strconv"
	"strings"
	"time"

	"boer-lan-server/internal/model"
	"boer-lan-server/pkg/utils"

	"github.com/gin-gonic/gin"
	"golang.org/x/net/websocket"
)

const defaultVNCPort = 5900

func (h *DeviceHandler) ProxyVNCWebSocket(c *gin.Context) {
	token := extractAuthToken(c)
	if token == "" {
		c.JSON(http.StatusUnauthorized, gin.H{
			"code":    401,
			"message": "未提供认证信息",
		})
		return
	}

	if _, err := utils.ParseToken(token); err != nil {
		c.JSON(http.StatusUnauthorized, gin.H{
			"code":    401,
			"message": "认证无效或已过期",
		})
		return
	}

	var device model.Device
	if err := h.db.First(&device, c.Param("id")).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{
			"code":    404,
			"message": "设备不存在",
		})
		return
	}

	host := strings.TrimSpace(device.IP)
	if host == "" {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "设备未配置IP地址",
		})
		return
	}

	port, err := parseVNCPort(c.DefaultQuery("port", strconv.Itoa(defaultVNCPort)))
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "VNC端口不合法",
		})
		return
	}

	target := net.JoinHostPort(host, strconv.Itoa(port))

	server := websocket.Server{
		Handler: websocket.Handler(func(ws *websocket.Conn) {
			defer ws.Close()
			ws.PayloadType = websocket.BinaryFrame

			tcpConn, dialErr := net.DialTimeout("tcp", target, 5*time.Second)
			if dialErr != nil {
				return
			}
			defer tcpConn.Close()

			if conn, ok := tcpConn.(*net.TCPConn); ok {
				_ = conn.SetKeepAlive(true)
				_ = conn.SetKeepAlivePeriod(30 * time.Second)
			}

			done := make(chan struct{}, 2)

			go func() {
				_, _ = io.Copy(tcpConn, ws)
				if conn, ok := tcpConn.(*net.TCPConn); ok {
					_ = conn.CloseWrite()
				}
				done <- struct{}{}
			}()

			go func() {
				_, _ = io.Copy(ws, tcpConn)
				done <- struct{}{}
			}()

			<-done
		}),
		Handshake: func(config *websocket.Config, r *http.Request) error {
			return nil
		},
	}

	server.ServeHTTP(c.Writer, c.Request)
}

func extractAuthToken(c *gin.Context) string {
	if token := strings.TrimSpace(c.Query("token")); token != "" {
		return token
	}

	authHeader := strings.TrimSpace(c.GetHeader("Authorization"))
	if authHeader == "" {
		return ""
	}
	parts := strings.SplitN(authHeader, " ", 2)
	if len(parts) == 2 && strings.EqualFold(parts[0], "Bearer") {
		return strings.TrimSpace(parts[1])
	}
	return ""
}

func parseVNCPort(raw string) (int, error) {
	port, err := strconv.Atoi(strings.TrimSpace(raw))
	if err != nil {
		return 0, err
	}
	if port < 1 || port > 65535 {
		return 0, strconv.ErrRange
	}
	return port, nil
}
