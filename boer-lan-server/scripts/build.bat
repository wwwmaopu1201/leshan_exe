@echo off

REM 构建 Go 后端
echo Building Go backend...
cd backend
go build -ldflags="-s -w" -o ..\src-tauri\backend-server.exe cmd\server\main.go
cd ..

REM 复制配置文件
echo Copying config files...
if not exist src-tauri\config mkdir src-tauri\config
copy backend\configs\config.yaml src-tauri\config\

REM 构建 Tauri 应用
echo Building Tauri app...
npm run tauri:build

echo Build completed!
