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
		permDevice := RequirePermission(db, "deviceManagement", "deviceInfo")
		permRemoteMonitoring := RequirePermission(db, "remoteMonitoring", "deviceManagement")
		permPatternFiles := RequirePermission(db, "fileManagement", "patternFiles")
		permDevicePatternFiles := RequirePermission(db, "fileManagement", "devicePatternFiles")
		permDownloadLog := RequirePermission(db, "fileManagement", "downloadLog")
		permHome := RequirePermission(db, "home")
		permDashboard := RequirePermission(db, "dashboard")
		permSalaryStats := RequirePermission(db, "statistics", "salaryStatistics")
		permStatusStats := RequirePermission(db, "statistics", "statusStatistics")
		permStatsExport := RequirePermission(db, "statistics", "salaryStatistics", "statusStatistics")
		permEmployee := RequirePermission(db, "employeeManagement")
		permAdmin := RequireAdmin()

		// Auth
		protected.POST("/auth/logout", authHandler.Logout)
		protected.GET("/auth/userinfo", authHandler.GetUserInfo)
		protected.PUT("/auth/password", authHandler.ChangePassword)
		protected.PUT("/auth/profile", authHandler.UpdateProfile)
		protected.GET("/auth/login-logs", authHandler.GetLoginLogs)

		// Device
		protected.GET("/device/tree", permDevice, deviceHandler.GetDeviceTree)
		protected.GET("/device/list", permDevice, deviceHandler.GetDeviceList)
		protected.GET("/device/:id", permDevice, deviceHandler.GetDevice)
		protected.POST("/device", permDevice, deviceHandler.CreateDevice)
		protected.PUT("/device/:id", permDevice, deviceHandler.UpdateDevice)
		protected.DELETE("/device/:id", permDevice, deviceHandler.DeleteDevice)
		protected.DELETE("/device/batch", permDevice, deviceHandler.BatchDeleteDevices)
		protected.POST("/device/move", permDevice, deviceHandler.MoveToGroup)
		protected.POST("/device/:id/control/confirm", permRemoteMonitoring, deviceHandler.ConfirmRemoteControl)

		// Device Group
		protected.GET("/device/groups", permDevice, deviceHandler.GetDeviceGroups)
		protected.POST("/device/group", permDevice, deviceHandler.CreateDeviceGroup)
		protected.PUT("/device/group/:id", permDevice, deviceHandler.UpdateDeviceGroup)
		protected.DELETE("/device/group/:id", permDevice, deviceHandler.DeleteDeviceGroup)

		// Pattern
		patternHandler := NewPatternHandler(db)
		protected.GET("/pattern/list", permPatternFiles, patternHandler.GetPatternList)
		protected.GET("/pattern/types", permPatternFiles, patternHandler.GetPatternTypes)
		protected.POST("/pattern/upload", permPatternFiles, patternHandler.UploadPattern)
		protected.PUT("/pattern/:id", permPatternFiles, patternHandler.UpdatePattern)
		protected.POST("/pattern/batch-update", permPatternFiles, patternHandler.BatchUpdatePatterns)
		protected.POST("/pattern/download", permPatternFiles, patternHandler.DownloadToDevice)
		protected.POST("/pattern/batch-download", permPatternFiles, patternHandler.BatchDownload)
		protected.GET("/pattern/queue", permDownloadLog, patternHandler.GetDownloadQueue)
		protected.GET("/pattern/log", permDownloadLog, patternHandler.GetDownloadLog)
		protected.POST("/pattern/queue/:id/pause", permDownloadLog, patternHandler.PauseDownload)
		protected.POST("/pattern/queue/:id/resume", permDownloadLog, patternHandler.ResumeDownload)
		protected.POST("/pattern/queue/pause-all", permDownloadLog, patternHandler.PauseAllDownloads)
		protected.POST("/pattern/queue/resume-all", permDownloadLog, patternHandler.ResumeAllDownloads)
		protected.DELETE("/pattern/queue/completed", permDownloadLog, patternHandler.ClearCompletedDownloads)
		protected.DELETE("/pattern/queue/:id", permDownloadLog, patternHandler.CancelDownload)

		// Device Files (upload back to server)
		protected.GET("/pattern/device-files", permDevicePatternFiles, patternHandler.GetDevicePatternFiles)
		protected.DELETE("/pattern/device-files/:id", permDevicePatternFiles, patternHandler.DeleteDevicePatternFile)
		protected.POST("/pattern/device-files/upload", permDevicePatternFiles, patternHandler.UploadDeviceFilesToServer)
		protected.GET("/pattern/upload-queue", permDevicePatternFiles, patternHandler.GetUploadQueue)
		protected.POST("/pattern/upload-queue/:id/pause", permDevicePatternFiles, patternHandler.PauseUploadTask)
		protected.POST("/pattern/upload-queue/:id/resume", permDevicePatternFiles, patternHandler.ResumeUploadTask)
		protected.DELETE("/pattern/upload-queue/completed", permDevicePatternFiles, patternHandler.ClearCompletedUploads)
		protected.DELETE("/pattern/upload-queue/:id", permDevicePatternFiles, patternHandler.CancelUploadTask)

		protected.DELETE("/pattern/:id", permPatternFiles, patternHandler.DeletePattern)

		// Statistics
		statsHandler := NewStatisticsHandler(db)
		protected.GET("/statistics/home", permHome, statsHandler.GetHomeStats)
		protected.GET("/statistics/dashboard", permDashboard, statsHandler.GetDashboardData)
		protected.GET("/statistics/salary", permSalaryStats, statsHandler.GetSalaryStats)
		protected.GET("/statistics/salary/detail", permSalaryStats, statsHandler.GetSalaryDetail)
		protected.GET("/statistics/process", permStatusStats, statsHandler.GetProcessOverview)
		protected.GET("/statistics/duration", permStatusStats, statsHandler.GetDurationStats)
		protected.GET("/statistics/alarm", permStatusStats, statsHandler.GetAlarmStats)
		protected.GET("/statistics/export/:type", permStatsExport, statsHandler.ExportStatistics)

		// Employee
		employeeHandler := NewEmployeeHandler(db)
		protected.GET("/employee/list", permEmployee, employeeHandler.GetEmployeeList)
		protected.GET("/employee/:id", permEmployee, employeeHandler.GetEmployee)
		protected.POST("/employee", permEmployee, employeeHandler.CreateEmployee)
		protected.PUT("/employee/:id", permEmployee, employeeHandler.UpdateEmployee)
		protected.DELETE("/employee/:id", permEmployee, employeeHandler.DeleteEmployee)
		protected.POST("/employee/import", permEmployee, employeeHandler.ImportEmployees)
		protected.GET("/employee/export", permEmployee, employeeHandler.ExportEmployees)

		// Group Management
		protected.GET("/group/tree", permAdmin, groupHandler.GetGroupTree)
		protected.GET("/group/list", permAdmin, groupHandler.GetGroupList)
		protected.POST("/group", permAdmin, groupHandler.CreateGroup)
		protected.PUT("/group/:id", permAdmin, groupHandler.UpdateGroup)
		protected.DELETE("/group/:id", permAdmin, groupHandler.DeleteGroup)
		protected.POST("/group/sort", permAdmin, groupHandler.SortGroups)

		// User Management
		protected.GET("/role/list", permAdmin, roleHandler.GetRoleList)
		protected.POST("/role", permAdmin, roleHandler.CreateRole)
		protected.PUT("/role/:id", permAdmin, roleHandler.UpdateRole)
		protected.DELETE("/role/:id", permAdmin, roleHandler.DeleteRole)

		// User Management
		protected.GET("/user/list", permAdmin, userHandler.GetUserList)
		protected.GET("/user/all", permAdmin, userHandler.GetAllUsers)
		protected.POST("/user", permAdmin, userHandler.CreateUser)
		protected.PUT("/user/:id", permAdmin, userHandler.UpdateUser)
		protected.DELETE("/user", permAdmin, userHandler.DeleteUser)
		protected.POST("/user/move", permAdmin, userHandler.MoveUsersToGroup)

		// Operator Management
		protected.GET("/operator/list", permAdmin, operatorHandler.GetOperatorList)
		protected.GET("/operator/all", permAdmin, operatorHandler.GetAllOperators)
		protected.POST("/operator", permAdmin, operatorHandler.CreateOperator)
		protected.PUT("/operator/:id", permAdmin, operatorHandler.UpdateOperator)
		protected.DELETE("/operator", permAdmin, operatorHandler.DeleteOperator)
		protected.POST("/operator/move", permAdmin, operatorHandler.MoveOperatorsToGroup)
		protected.POST("/operator/import", permAdmin, operatorHandler.ImportOperators)
		protected.GET("/operator/export", permAdmin, operatorHandler.ExportOperators)

		// System Management
		protected.GET("/system/info", permAdmin, systemHandler.GetServerInfo)
		protected.GET("/system/stats", permAdmin, systemHandler.GetSystemStats)
		protected.GET("/system/network", permAdmin, systemHandler.GetNetworkInfo)
		protected.POST("/system/ping", permAdmin, systemHandler.PingDevice)
		protected.POST("/system/command", permAdmin, systemHandler.ExecuteCommand)

		// Debug Logs
		protected.GET("/system/logs", permAdmin, systemHandler.GetDebugLogs)
		protected.POST("/system/logs", permAdmin, systemHandler.AddDebugLog)
		protected.DELETE("/system/logs", permAdmin, systemHandler.ClearDebugLogs)

		// Server Config
		protected.GET("/system/config", permAdmin, systemHandler.GetServerConfig)
		protected.POST("/system/config", permAdmin, systemHandler.SetServerConfig)
		protected.GET("/system/database/config", permAdmin, systemHandler.GetExternalDBConfig)
		protected.GET("/system/database/sync-status", permAdmin, systemHandler.GetExternalDBSyncStatus)
		protected.POST("/system/database/sync-now", permAdmin, systemHandler.SyncExternalDBNow)
		protected.POST("/system/database/config", permAdmin, systemHandler.SetExternalDBConfig)
		protected.POST("/system/database/test", permAdmin, systemHandler.TestExternalDBConnection)
	}
}
