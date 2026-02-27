package api

import (
	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

func SetupRouter(r *gin.Engine, db *gorm.DB, jwtSecret string, jwtExpire int) {
	// API group
	api := r.Group("/api")

	// Auth routes (public)
	authHandler := NewAuthHandler(db, jwtSecret, jwtExpire)
	api.POST("/auth/login", authHandler.Login)

	// Protected routes
	protected := api.Group("")
	protected.Use(AuthMiddleware(jwtSecret))
	{
		// Auth
		protected.POST("/auth/logout", authHandler.Logout)
		protected.GET("/auth/userinfo", authHandler.GetUserInfo)
		protected.PUT("/auth/password", authHandler.ChangePassword)
		protected.PUT("/auth/profile", authHandler.UpdateProfile)
		protected.GET("/auth/login-logs", authHandler.GetLoginLogs)

		// Device
		deviceHandler := NewDeviceHandler(db)
		protected.GET("/device/tree", deviceHandler.GetDeviceTree)
		protected.GET("/device/list", deviceHandler.GetDeviceList)
		protected.GET("/device/:id", deviceHandler.GetDevice)
		protected.POST("/device", deviceHandler.CreateDevice)
		protected.PUT("/device/:id", deviceHandler.UpdateDevice)
		protected.DELETE("/device/:id", deviceHandler.DeleteDevice)
		protected.DELETE("/device/batch", deviceHandler.BatchDeleteDevices)
		protected.POST("/device/move", deviceHandler.MoveToGroup)

		// Device Group
		protected.GET("/device/groups", deviceHandler.GetDeviceGroups)
		protected.POST("/device/group", deviceHandler.CreateDeviceGroup)
		protected.PUT("/device/group/:id", deviceHandler.UpdateDeviceGroup)
		protected.DELETE("/device/group/:id", deviceHandler.DeleteDeviceGroup)

		// Pattern
		patternHandler := NewPatternHandler(db)
		protected.GET("/pattern/list", patternHandler.GetPatternList)
		protected.POST("/pattern/upload", patternHandler.UploadPattern)
		protected.DELETE("/pattern/:id", patternHandler.DeletePattern)
		protected.POST("/pattern/download", patternHandler.DownloadToDevice)
		protected.POST("/pattern/batch-download", patternHandler.BatchDownload)
		protected.GET("/pattern/queue", patternHandler.GetDownloadQueue)
		protected.GET("/pattern/log", patternHandler.GetDownloadLog)
		protected.DELETE("/pattern/queue/:id", patternHandler.CancelDownload)

		// Statistics
		statsHandler := NewStatisticsHandler(db)
		protected.GET("/statistics/home", statsHandler.GetHomeStats)
		protected.GET("/statistics/dashboard", statsHandler.GetDashboardData)
		protected.GET("/statistics/salary", statsHandler.GetSalaryStats)
		protected.GET("/statistics/salary/detail", statsHandler.GetSalaryDetail)
		protected.GET("/statistics/process", statsHandler.GetProcessOverview)
		protected.GET("/statistics/duration", statsHandler.GetDurationStats)
		protected.GET("/statistics/alarm", statsHandler.GetAlarmStats)

		// Employee
		employeeHandler := NewEmployeeHandler(db)
		protected.GET("/employee/list", employeeHandler.GetEmployeeList)
		protected.GET("/employee/:id", employeeHandler.GetEmployee)
		protected.POST("/employee", employeeHandler.CreateEmployee)
		protected.PUT("/employee/:id", employeeHandler.UpdateEmployee)
		protected.DELETE("/employee/:id", employeeHandler.DeleteEmployee)
	}
}
