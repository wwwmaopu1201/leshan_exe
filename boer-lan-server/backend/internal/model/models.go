package model

import (
	"time"

	"gorm.io/gorm"
)

// User 用户模型（客户端用户）
type User struct {
	gorm.Model
	Username    string `gorm:"size:50;uniqueIndex;not null" json:"username"`
	Password    string `gorm:"size:255;not null" json:"-"`
	Nickname    string `gorm:"size:50" json:"nickname"`
	Email       string `gorm:"size:100" json:"email"`
	Phone       string `gorm:"size:20" json:"phone"`
	Role        string `gorm:"size:50;default:user" json:"role"` // 角色名称
	Avatar      string `gorm:"size:255" json:"avatar"`
	Disabled    bool   `gorm:"default:false" json:"disabled"` // 是否禁用
	Permissions string `gorm:"type:text" json:"permissions"`  // JSON格式的权限设置
	GroupID     *uint  `gorm:"index" json:"groupId"`          // 所属分组
	GroupIDs    string `gorm:"type:text" json:"groupIds"`     // 可见分组ID列表（JSON数组）
	Group       *Group `gorm:"foreignKey:GroupID" json:"group,omitempty"`
}

// Role 权限角色模型（服务器端客户端账号管理）
type Role struct {
	gorm.Model
	Name            string `gorm:"size:50;uniqueIndex;not null" json:"name"`
	Remark          string `gorm:"size:255" json:"remark"`
	Permissions     string `gorm:"type:text;not null" json:"permissions"` // JSON格式
	ParentChildLink bool   `gorm:"default:true" json:"parentChildLink"`   // 父子联动勾选
}

// Operator 操作员模型（用于登录模板机）
type Operator struct {
	gorm.Model
	Username string `gorm:"size:50;uniqueIndex;not null" json:"username"`
	Password string `gorm:"size:255;not null" json:"-"`
	Nickname string `gorm:"size:50" json:"nickname"`
	Disabled bool   `gorm:"default:false" json:"disabled"` // 限制登录
	GroupID  *uint  `gorm:"index" json:"groupId"`          // 所属分组
	Group    *Group `gorm:"foreignKey:GroupID" json:"group,omitempty"`
}

// Group 分组模型（统一管理用户、设备、操作员）
type Group struct {
	gorm.Model
	Name      string     `gorm:"size:100;not null" json:"name"`
	ParentID  *uint      `gorm:"index" json:"parentId"`
	Parent    *Group     `gorm:"foreignKey:ParentID" json:"parent,omitempty"`
	Children  []Group    `gorm:"foreignKey:ParentID" json:"children,omitempty"`
	SortOrder int        `gorm:"default:0" json:"sortOrder"` // 排序
	Users     []User     `gorm:"foreignKey:GroupID" json:"users,omitempty"`
	Operators []Operator `gorm:"foreignKey:GroupID" json:"operators,omitempty"`
	Devices   []Device   `gorm:"foreignKey:GroupID" json:"devices,omitempty"`
}

// Device 设备模型
type Device struct {
	gorm.Model
	Code         string    `gorm:"size:50;uniqueIndex;not null" json:"code"`
	Name         string    `gorm:"size:100;not null" json:"name"`
	InitialName  string    `gorm:"size:100" json:"initialName"` // 初始名称
	Type         string    `gorm:"size:50" json:"type"`         // 缝纫机, 绣花机
	ModelName    string    `gorm:"size:50" json:"model"`        // BM-2000, BM-3000
	EmployeeCode string    `gorm:"size:50" json:"employeeCode"` // 当前员工工号
	EmployeeName string    `gorm:"size:50" json:"employeeName"` // 当前员工姓名
	MainboardSN  string    `gorm:"size:100" json:"mainboardSn"` // 主板编号
	Remark       string    `gorm:"size:255" json:"remark"`      // 备注
	IP           string    `gorm:"size:50" json:"ip"`
	Status       string    `gorm:"size:20;default:offline" json:"status"` // online, offline, working, idle, alarm
	GroupID      *uint     `gorm:"index" json:"groupId"`
	Group        *Group    `gorm:"foreignKey:GroupID" json:"group,omitempty"`
	SortOrder    int       `gorm:"default:0" json:"sortOrder"` // 分组内排序
	LastOnline   time.Time `json:"lastOnline"`
}

// Pattern 花型文件
type Pattern struct {
	gorm.Model
	Name        string  `gorm:"size:255;not null" json:"name"`
	PatternType string  `gorm:"size:100;index" json:"patternType"`
	FileName    string  `gorm:"size:255;not null" json:"fileName"`
	FilePath    string  `gorm:"size:500" json:"filePath"`
	FileSize    int64   `json:"fileSize"`
	Stitches    int     `json:"stitches"`                                      // 针数
	Colors      int     `json:"colors"`                                        // 色数
	Width       float64 `json:"width"`                                         // 宽度mm
	Height      float64 `json:"height"`                                        // 高度mm
	UnitPrice   float64 `gorm:"type:decimal(12,3);default:0" json:"unitPrice"` // 工价
	OrderNo     string  `gorm:"size:100;index" json:"orderNo"`                 // 订单编号
	UploadedBy  uint    `gorm:"index" json:"uploadedBy"`
}

// DownloadTask 下发任务
type DownloadTask struct {
	gorm.Model
	PatternID  uint   `gorm:"index;not null" json:"patternId"`
	DeviceID   uint   `gorm:"index;not null" json:"deviceId"`
	Status     string `gorm:"size:20;default:waiting" json:"status"` // waiting, downloading, paused, completed, failed
	Progress   int    `json:"progress"`
	Message    string `gorm:"size:500" json:"message"`
	OperatorID uint   `gorm:"index" json:"operatorId"`
}

// DevicePatternFile 设备本地花型文件
type DevicePatternFile struct {
	gorm.Model
	DeviceID    uint    `gorm:"index;not null" json:"deviceId"`
	PatternNo   uint    `gorm:"index;default:0" json:"patternNo"`
	FileName    string  `gorm:"size:255;not null" json:"fileName"`
	PatternType string  `gorm:"size:100;index" json:"patternType"`
	FileSize    int64   `json:"fileSize"`
	Stitches    int     `json:"stitches"`
	UnitPrice   float64 `gorm:"type:decimal(12,3);default:0" json:"unitPrice"`
	OrderNo     string  `gorm:"size:100;index" json:"orderNo"`
	FilePath    string  `gorm:"size:500" json:"filePath"`
}

// UploadTask 设备文件回传任务
type UploadTask struct {
	gorm.Model
	DeviceFileID uint   `gorm:"index;not null" json:"deviceFileId"`
	PatternID    *uint  `gorm:"index" json:"patternId"`
	DeviceID     uint   `gorm:"index;not null" json:"deviceId"`
	Status       string `gorm:"size:20;default:waiting" json:"status"` // waiting, uploading, paused, completed, failed, canceled
	Progress     int    `json:"progress"`
	Message      string `gorm:"size:500" json:"message"`
	OperatorID   uint   `gorm:"index" json:"operatorId"`
}

// Employee 员工模型
type Employee struct {
	gorm.Model
	Code       string `gorm:"size:50;uniqueIndex;not null" json:"code"`
	Name       string `gorm:"size:50;not null" json:"name"`
	Department string `gorm:"size:50" json:"department"`
	Position   string `gorm:"size:50" json:"position"`
	Phone      string `gorm:"size:20" json:"phone"`
	Remark     string `gorm:"size:255" json:"remark"`
}

// EmployeeDevice 员工设备绑定
type EmployeeDevice struct {
	gorm.Model
	EmployeeID uint `gorm:"index;not null" json:"employeeId"`
	DeviceID   uint `gorm:"index;not null" json:"deviceId"`
}

// ProductionRecord 生产记录
type ProductionRecord struct {
	gorm.Model
	DeviceID     uint      `gorm:"index;not null" json:"deviceId"`
	EmployeeID   uint      `gorm:"index" json:"employeeId"`
	PatternID    uint      `gorm:"index" json:"patternId"`
	Pieces       int       `json:"pieces"`       // 加工件数
	Stitches     int64     `json:"stitches"`     // 针数
	ThreadLength float64   `json:"threadLength"` // 用线量(m)
	RunningTime  float64   `json:"runningTime"`  // 运行时长(h)
	IdleTime     float64   `json:"idleTime"`     // 空闲时长(h)
	RecordDate   time.Time `gorm:"index" json:"recordDate"`
}

// AlarmRecord 报警记录
type AlarmRecord struct {
	gorm.Model
	DeviceID    uint       `gorm:"index;not null" json:"deviceId"`
	AlarmType   string     `gorm:"size:50" json:"alarmType"` // 断线, 张力, 电机, 传感器
	AlarmCode   string     `gorm:"size:20" json:"alarmCode"`
	Description string     `gorm:"size:500" json:"description"`
	Duration    int        `json:"duration"`                              // 持续时长(秒)
	Status      string     `gorm:"size:20;default:pending" json:"status"` // pending, resolved
	StartTime   time.Time  `json:"startTime"`
	EndTime     *time.Time `json:"endTime"`
}

// SalaryRecord 工资记录
type SalaryRecord struct {
	gorm.Model
	EmployeeID  uint      `gorm:"index;not null" json:"employeeId"`
	DeviceID    uint      `gorm:"index" json:"deviceId"`
	Pieces      int       `json:"pieces"`
	UnitPrice   float64   `json:"unitPrice"`
	Salary      float64   `json:"salary"`
	Bonus       float64   `json:"bonus"`
	TotalAmount float64   `json:"totalAmount"`
	RecordDate  time.Time `gorm:"index" json:"recordDate"`
}

// LoginLog 登录记录
type LoginLog struct {
	ID        uint      `gorm:"primaryKey" json:"id"`
	UserID    uint      `gorm:"index;not null" json:"userId"`
	IP        string    `gorm:"size:50" json:"ip"`
	Device    string    `gorm:"size:200" json:"device"`
	Status    string    `gorm:"size:20" json:"status"`
	LoginTime time.Time `gorm:"type:timestamp;not null;default:CURRENT_TIMESTAMP" json:"loginTime"`
}

// ServerConfig 服务器配置
type ServerConfig struct {
	gorm.Model
	Key   string `gorm:"size:100;uniqueIndex;not null" json:"key"`
	Value string `gorm:"type:text" json:"value"`
	Desc  string `gorm:"size:255" json:"desc"`
}

// DebugLog 调试日志
type DebugLog struct {
	ID        uint      `gorm:"primaryKey" json:"id"`
	Level     string    `gorm:"size:20" json:"level"` // info, warn, error
	Message   string    `gorm:"type:text" json:"message"`
	Source    string    `gorm:"size:100" json:"source"` // 来源模块
	CreatedAt time.Time `gorm:"type:timestamp;not null;default:CURRENT_TIMESTAMP" json:"createdAt"`
}
