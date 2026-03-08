package api

import (
	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

func SetupRouter(r *gin.Engine, db *gorm.DB, jwtSecret string, jwtExpire int, serverPort int) {
	// API group
	api := r.Group("/api")

	// Initialize handlers
	deviceHandler := NewDeviceHandler(db)
	groupHandler := NewGroupHandler(db)
	userHandler := NewUserHandler(db)
	operatorHandler := NewOperatorHandler(db)
	roleHandler := NewRoleHandler(db)
	systemHandler := NewSystemHandler(db, serverPort)

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
		protected.GET("/pattern/types", patternHandler.GetPatternTypes)
		protected.POST("/pattern/upload", patternHandler.UploadPattern)
		protected.PUT("/pattern/:id", patternHandler.UpdatePattern)
		protected.POST("/pattern/batch-update", patternHandler.BatchUpdatePatterns)
		protected.POST("/pattern/download", patternHandler.DownloadToDevice)
		protected.POST("/pattern/batch-download", patternHandler.BatchDownload)
		protected.GET("/pattern/queue", patternHandler.GetDownloadQueue)
		protected.GET("/pattern/log", patternHandler.GetDownloadLog)
		protected.POST("/pattern/queue/:id/pause", patternHandler.PauseDownload)
		protected.POST("/pattern/queue/:id/resume", patternHandler.ResumeDownload)
		protected.POST("/pattern/queue/pause-all", patternHandler.PauseAllDownloads)
		protected.POST("/pattern/queue/resume-all", patternHandler.ResumeAllDownloads)
		protected.DELETE("/pattern/queue/completed", patternHandler.ClearCompletedDownloads)
		protected.DELETE("/pattern/queue/:id", patternHandler.CancelDownload)

		// Device Files (upload back to server)
		protected.GET("/pattern/device-files", patternHandler.GetDevicePatternFiles)
		protected.DELETE("/pattern/device-files/:id", patternHandler.DeleteDevicePatternFile)
		protected.POST("/pattern/device-files/upload", patternHandler.UploadDeviceFilesToServer)
		protected.GET("/pattern/upload-queue", patternHandler.GetUploadQueue)
		protected.POST("/pattern/upload-queue/:id/pause", patternHandler.PauseUploadTask)
		protected.POST("/pattern/upload-queue/:id/resume", patternHandler.ResumeUploadTask)
		protected.DELETE("/pattern/upload-queue/completed", patternHandler.ClearCompletedUploads)
		protected.DELETE("/pattern/upload-queue/:id", patternHandler.CancelUploadTask)

		protected.DELETE("/pattern/:id", patternHandler.DeletePattern)

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
		protected.POST("/employee/import", employeeHandler.ImportEmployees)
		protected.GET("/employee/export", employeeHandler.ExportEmployees)

		// Group Management
		protected.GET("/group/tree", groupHandler.GetGroupTree)
		protected.GET("/group/list", groupHandler.GetGroupList)
		protected.POST("/group", groupHandler.CreateGroup)
		protected.PUT("/group/:id", groupHandler.UpdateGroup)
		protected.DELETE("/group/:id", groupHandler.DeleteGroup)
		protected.POST("/group/sort", groupHandler.SortGroups)

		// User Management
		protected.GET("/role/list", roleHandler.GetRoleList)
		protected.POST("/role", roleHandler.CreateRole)
		protected.PUT("/role/:id", roleHandler.UpdateRole)
		protected.DELETE("/role/:id", roleHandler.DeleteRole)

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
		protected.GET("/system/database/config", systemHandler.GetExternalDBConfig)
		protected.GET("/system/database/sync-status", systemHandler.GetExternalDBSyncStatus)
		protected.POST("/system/database/sync-now", systemHandler.SyncExternalDBNow)
		protected.POST("/system/database/config", systemHandler.SetExternalDBConfig)
		protected.POST("/system/database/test", systemHandler.TestExternalDBConnection)
	}
}
