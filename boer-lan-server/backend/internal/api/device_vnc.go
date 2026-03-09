package api

import (
	"crypto/rand"
	"encoding/hex"
	"io"
	"net"
	"net/http"
	"strconv"
	"strings"
	"sync"
	"time"

	"boer-lan-server/internal/model"
	"boer-lan-server/pkg/utils"

	"github.com/gin-gonic/gin"
	"golang.org/x/net/websocket"
)

const defaultVNCPort = 5900
const remoteControlTokenTTL = 2 * time.Minute

type remoteControlGrant struct {
	DeviceID  uint
	ExpiresAt time.Time
}

var remoteControlTokenStore sync.Map

func (h *DeviceHandler) ConfirmRemoteControl(c *gin.Context) {
	var req struct {
		Code         string `json:"code" binding:"required"`
		Acknowledged bool   `json:"acknowledged"`
	}

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "参数错误",
		})
		return
	}

	code := strings.TrimSpace(req.Code)
	if code == "" {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "确认口令不能为空",
		})
		return
	}
	if len([]rune(code)) < 4 || len([]rune(code)) > 32 {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "确认口令长度需在4-32位",
		})
		return
	}
	if !req.Acknowledged {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "请先确认设备端已授权",
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

	scope := h.getCurrentUserScope(c)
	if !h.canAccessDevice(scope, device) {
		c.JSON(http.StatusForbidden, gin.H{
			"code":    403,
			"message": "无权操作该设备",
		})
		return
	}
	if strings.EqualFold(strings.TrimSpace(device.Status), "offline") {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "设备离线，无法开启远程控制",
		})
		return
	}

	token, err := generateRemoteControlToken()
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": "生成控制令牌失败",
		})
		return
	}

	expiresAt := time.Now().Add(remoteControlTokenTTL)
	remoteControlTokenStore.Store(token, remoteControlGrant{
		DeviceID:  device.ID,
		ExpiresAt: expiresAt,
	})

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"controlToken": token,
			"expiresAt":    expiresAt.Unix(),
		},
		"message": "success",
	})
}

func (h *DeviceHandler) ProxyVNCWebSocket(c *gin.Context) {
	token := extractAuthToken(c)
	if token == "" {
		c.JSON(http.StatusUnauthorized, gin.H{
			"code":    401,
			"message": "未提供认证信息",
		})
		return
	}

	claims, err := utils.ParseToken(token)
	if err != nil {
		c.JSON(http.StatusUnauthorized, gin.H{
			"code":    401,
			"message": "认证无效或已过期",
		})
		return
	}
	active, activeErr := checkUserActiveStatus(h.db, claims.UserID)
	if activeErr != nil {
		c.JSON(http.StatusUnauthorized, gin.H{
			"code":    401,
			"message": "账号认证失效，请重新登录",
		})
		return
	}
	if !active {
		c.JSON(http.StatusForbidden, gin.H{
			"code":    403,
			"message": "账号已被禁用",
		})
		return
	}

	allowed, permErr := hasPermissionForUser(h.db, claims.UserID, claims.Role, "remoteMonitoring", "deviceManagement")
	if permErr != nil {
		c.JSON(http.StatusUnauthorized, gin.H{
			"code":    401,
			"message": "账号认证失效，请重新登录",
		})
		return
	}
	if !allowed {
		c.JSON(http.StatusForbidden, gin.H{
			"code":    403,
			"message": "当前账号无权访问远程监控",
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

	scope, scopeErr := loadUserGroupScope(h.db, claims.UserID, claims.Role)
	if scopeErr != nil {
		c.JSON(http.StatusUnauthorized, gin.H{
			"code":    401,
			"message": "账号认证失效，请重新登录",
		})
		return
	}
	if !scope.All {
		if device.GroupID == nil || !containsGroupID(scope.GroupIDs, *device.GroupID) {
			c.JSON(http.StatusForbidden, gin.H{
				"code":    403,
				"message": "无权访问该设备",
			})
			return
		}
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
	mode := strings.ToLower(strings.TrimSpace(c.DefaultQuery("mode", "monitor")))
	allowControl := false
	if mode == "control" {
		controlToken := strings.TrimSpace(c.Query("controlToken"))
		if controlToken == "" || !consumeRemoteControlGrant(device.ID, controlToken) {
			c.JSON(http.StatusUnauthorized, gin.H{
				"code":    401,
				"message": "远程控制授权已失效，请重新确认",
			})
			return
		}
		allowControl = true
	}

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
			if allowControl {
				go func() {
					_, _ = io.Copy(tcpConn, ws)
					if conn, ok := tcpConn.(*net.TCPConn); ok {
						_ = conn.CloseWrite()
					}
					done <- struct{}{}
				}()
			} else {
				// 监控模式仅允许设备 -> 客户端方向数据，客户端输入会被丢弃。
				go func() {
					_, _ = io.Copy(io.Discard, ws)
					done <- struct{}{}
				}()
			}

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

func generateRemoteControlToken() (string, error) {
	buf := make([]byte, 16)
	if _, err := rand.Read(buf); err != nil {
		return "", err
	}
	return hex.EncodeToString(buf), nil
}

func consumeRemoteControlGrant(deviceID uint, token string) bool {
	raw, ok := remoteControlTokenStore.Load(token)
	if !ok {
		return false
	}

	grant, ok := raw.(remoteControlGrant)
	if !ok {
		remoteControlTokenStore.Delete(token)
		return false
	}
	if grant.DeviceID != deviceID || time.Now().After(grant.ExpiresAt) {
		remoteControlTokenStore.Delete(token)
		return false
	}

	// 一次性授权，消费后立即失效。
	remoteControlTokenStore.Delete(token)
	return true
}
