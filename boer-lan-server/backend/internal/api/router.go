package api

import (
	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

func SetupRouter(r *gin.Engine, db *gorm.DB, jwtSecret string, jwtExpire int) {
	// API group
	api := r.Group("/api")

	// Initialize handlers
	deviceHandler := NewDeviceHandler(db)
	groupHandler := NewGroupHandler(db)
	userHandler := NewUserHandler(db)
	operatorHandler := NewOperatorHandler(db)
	systemHandler := NewSystemHandler(db, 8088) // TODO: 从配置读取端口

	// Auth routes (public)
	authHandler := NewAuthHandler(db, jwtSecret, jwtExpire)
	api.POST("/auth/login", authHandler.Login)
	api.GET("/device/vnc/ws/:id", deviceHandler.ProxyVNCWebSocket)

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
		protected.GET("/statistics/export/:type", statsHandler.ExportStatistics)

		// Employee
		employeeHandler := NewEmployeeHandler(db)
		protected.GET("/employee/list", employeeHandler.GetEmployeeList)
		protected.GET("/employee/:id", employeeHandler.GetEmployee)
		protected.POST("/employee", employeeHandler.CreateEmployee)
		protected.PUT("/employee/:id", employeeHandler.UpdateEmployee)
		protected.DELETE("/employee/:id", employeeHandler.DeleteEmployee)

		// Group Management
		protected.GET("/group/tree", groupHandler.GetGroupTree)
		protected.GET("/group/list", groupHandler.GetGroupList)
		protected.POST("/group", groupHandler.CreateGroup)
		protected.PUT("/group/:id", groupHandler.UpdateGroup)
		protected.DELETE("/group/:id", groupHandler.DeleteGroup)
		protected.POST("/group/sort", groupHandler.SortGroups)

		// User Management
		protected.GET("/user/list", userHandler.GetUserList)
		protected.GET("/user/all", userHandler.GetAllUsers)
		protected.POST("/user", userHandler.CreateUser)
		protected.PUT("/user/:id", userHandler.UpdateUser)
		protected.DELETE("/user", userHandler.DeleteUser)
		protected.POST("/user/move", userHandler.MoveUsersToGroup)

		// Operator Management
		protected.GET("/operator/list", operatorHandler.GetOperatorList)
		protected.GET("/operator/all", operatorHandler.GetAllOperators)
		protected.POST("/operator", operatorHandler.CreateOperator)
		protected.PUT("/operator/:id", operatorHandler.UpdateOperator)
		protected.DELETE("/operator", operatorHandler.DeleteOperator)
		protected.POST("/operator/move", operatorHandler.MoveOperatorsToGroup)
		protected.POST("/operator/import", operatorHandler.ImportOperators)
		protected.GET("/operator/export", operatorHandler.ExportOperators)

		// System Management
		protected.GET("/system/info", systemHandler.GetServerInfo)
		protected.GET("/system/stats", systemHandler.GetSystemStats)
		protected.GET("/system/network", systemHandler.GetNetworkInfo)
		protected.POST("/system/ping", systemHandler.PingDevice)
		protected.POST("/system/command", systemHandler.ExecuteCommand)

		// Debug Logs
		protected.GET("/system/logs", systemHandler.GetDebugLogs)
		protected.POST("/system/logs", systemHandler.AddDebugLog)
		protected.DELETE("/system/logs", systemHandler.ClearDebugLogs)

		// Server Config
		protected.GET("/system/config", systemHandler.GetServerConfig)
		protected.POST("/system/config", systemHandler.SetServerConfig)
	}
}
