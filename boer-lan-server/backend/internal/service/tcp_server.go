package service

import (
	"fmt"
	"net"
	"sync"
	"time"

	"gorm.io/gorm"
)

const (
	TCPPort               = 38400
	ConnectionIdleTimeout = 3 * time.Minute
	OfflineCheckInterval  = 15 * time.Second
)

// ConnectionManager 管理所有设备TCP连接
type ConnectionManager struct {
	mu    sync.RWMutex
	conns map[string]*DeviceConnection // key: deviceCode
}

func NewConnectionManager() *ConnectionManager {
	return &ConnectionManager{
		conns: make(map[string]*DeviceConnection),
	}
}

func (cm *ConnectionManager) Register(code string, dc *DeviceConnection) {
	cm.mu.Lock()
	old := cm.conns[code]
	cm.conns[code] = dc
	cm.mu.Unlock()

	// 如果已有旧连接，关闭它。跳过当前连接本身，避免误伤。
	if old != nil && old != dc {
		old.conn.Close()
	}
}

func (cm *ConnectionManager) Unregister(code string, dc *DeviceConnection) bool {
	cm.mu.Lock()
	defer cm.mu.Unlock()
	current, ok := cm.conns[code]
	if !ok || current != dc {
		return false
	}
	delete(cm.conns, code)
	return true
}

func (cm *ConnectionManager) GetAll() []*DeviceConnection {
	cm.mu.RLock()
	defer cm.mu.RUnlock()
	list := make([]*DeviceConnection, 0, len(cm.conns))
	for _, dc := range cm.conns {
		list = append(list, dc)
	}
	return list
}

func (cm *ConnectionManager) Get(code string) *DeviceConnection {
	cm.mu.RLock()
	defer cm.mu.RUnlock()
	return cm.conns[code]
}

func (s *TCPServer) ConnectionManager() *ConnectionManager {
	return s.connMgr
}

// TCPServer TCP协议服务器
type TCPServer struct {
	db       *gorm.DB
	listener net.Listener
	connMgr  *ConnectionManager
	stopCh   chan struct{}
	once     sync.Once
}

func NewTCPServer(db *gorm.DB) *TCPServer {
	return &TCPServer{
		db:      db,
		connMgr: NewConnectionManager(),
		stopCh:  make(chan struct{}),
	}
}

// Start 启动TCP服务器
func (s *TCPServer) Start() {
	go s.serve()
	go s.offlineChecker()
}

// Stop 优雅关闭
func (s *TCPServer) Stop() {
	s.once.Do(func() {
		close(s.stopCh)
		if s.listener != nil {
			s.listener.Close()
		}
	})
}

func (s *TCPServer) serve() {
	var err error
	s.listener, err = net.Listen("tcp", fmt.Sprintf(":%d", TCPPort))
	if err != nil {
		emitTCPLog(s.db, "error", true, "[TCP] Failed to listen on port %d: %v", TCPPort, err)
		return
	}
	emitTCPLog(s.db, "info", true, "[TCP] Server listening on port %d", TCPPort)

	for {
		conn, err := s.listener.Accept()
		if err != nil {
			select {
			case <-s.stopCh:
				return
			default:
				emitTCPLog(s.db, "error", true, "[TCP] Accept error: %v", err)
				continue
			}
		}
		dc := NewDeviceConnection(conn, s.db, s.connMgr)
		go dc.Handle()
	}
}

// offlineChecker 定时检查连接空闲超时的设备
func (s *TCPServer) offlineChecker() {
	ticker := time.NewTicker(OfflineCheckInterval)
	defer ticker.Stop()

	for {
		select {
		case <-s.stopCh:
			return
		case <-ticker.C:
			now := time.Now()
			for _, dc := range s.connMgr.GetAll() {
				if now.Sub(dc.lastHeartbeat) > ConnectionIdleTimeout {
					emitTCPLog(s.db, "warn", true, "[TCP] Device %s connection idle timeout, closing connection", dc.deviceCode)
					dc.conn.Close()
				}
			}
		}
	}
}
