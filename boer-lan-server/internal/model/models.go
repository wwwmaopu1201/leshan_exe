package model

import (
	"time"

	"gorm.io/gorm"
)

// User 用户模型
type User struct {
	gorm.Model
	Username string `gorm:"size:50;uniqueIndex;not null" json:"username"`
	Password string `gorm:"size:255;not null" json:"-"`
	Nickname string `gorm:"size:50" json:"nickname"`
	Email    string `gorm:"size:100" json:"email"`
	Phone    string `gorm:"size:20" json:"phone"`
	Role     string `gorm:"size:20;default:user" json:"role"` // admin, user
	Avatar   string `gorm:"size:255" json:"avatar"`
}

// DeviceGroup 设备分组
type DeviceGroup struct {
	gorm.Model
	Name     string        `gorm:"size:100;not null" json:"name"`
	ParentID *uint         `gorm:"index" json:"parentId"`
	Parent   *DeviceGroup  `gorm:"foreignKey:ParentID" json:"parent,omitempty"`
	Children []DeviceGroup `gorm:"foreignKey:ParentID" json:"children,omitempty"`
	Devices  []Device      `gorm:"foreignKey:GroupID" json:"devices,omitempty"`
}

// Device 设备模型
type Device struct {
	gorm.Model
	Code       string       `gorm:"size:50;uniqueIndex;not null" json:"code"`
	Name       string       `gorm:"size:100;not null" json:"name"`
	Type       string       `gorm:"size:50" json:"type"`      // 缝纫机, 绣花机
	ModelName  string       `gorm:"size:50" json:"model"`     // BM-2000, BM-3000
	IP         string       `gorm:"size:50" json:"ip"`
	Status     string       `gorm:"size:20;default:offline" json:"status"` // online, offline, working, idle, alarm
	GroupID    *uint        `gorm:"index" json:"groupId"`
	Group      *DeviceGroup `gorm:"foreignKey:GroupID" json:"group,omitempty"`
	LastOnline time.Time    `json:"lastOnline"`
}

// Pattern 花型文件
type Pattern struct {
	gorm.Model
	Name       string `gorm:"size:255;not null" json:"name"`
	FileName   string `gorm:"size:255;not null" json:"fileName"`
	FilePath   string `gorm:"size:500" json:"filePath"`
	FileSize   int64  `json:"fileSize"`
	Stitches   int    `json:"stitches"`   // 针数
	Colors     int    `json:"colors"`     // 色数
	Width      float64 `json:"width"`     // 宽度mm
	Height     float64 `json:"height"`    // 高度mm
	UploadedBy uint   `gorm:"index" json:"uploadedBy"`
}

// DownloadTask 下发任务
type DownloadTask struct {
	gorm.Model
	PatternID  uint   `gorm:"index;not null" json:"patternId"`
	DeviceID   uint   `gorm:"index;not null" json:"deviceId"`
	Status     string `gorm:"size:20;default:waiting" json:"status"` // waiting, downloading, completed, failed
	Progress   int    `json:"progress"`
	Message    string `gorm:"size:500" json:"message"`
	OperatorID uint   `gorm:"index" json:"operatorId"`
}

// Employee 员工模型
type Employee struct {
	gorm.Model
	Code       string `gorm:"size:50;uniqueIndex;not null" json:"code"`
	Name       string `gorm:"size:50;not null" json:"name"`
	Department string `gorm:"size:50" json:"department"`
	Position   string `gorm:"size:50" json:"position"`
	Phone      string `gorm:"size:20" json:"phone"`
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
	DeviceID    uint      `gorm:"index;not null" json:"deviceId"`
	AlarmType   string    `gorm:"size:50" json:"alarmType"`   // 断线, 张力, 电机, 传感器
	AlarmCode   string    `gorm:"size:20" json:"alarmCode"`
	Description string    `gorm:"size:500" json:"description"`
	Duration    int       `json:"duration"`                         // 持续时长(秒)
	Status      string    `gorm:"size:20;default:pending" json:"status"` // pending, resolved
	StartTime   time.Time `json:"startTime"`
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
