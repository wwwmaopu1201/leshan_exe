#!/bin/bash

# 构建 Go 后端
echo "Building Go backend..."
cd backend
go build -ldflags="-s -w" -o ../src-tauri/backend-server cmd/server/main.go
cd ..

# 复制配置文件
echo "Copying config files..."
mkdir -p src-tauri/config
cp backend/configs/config.yaml src-tauri/config/

# 构建 Tauri 应用
echo "Building Tauri app..."
npm run tauri:build

echo "Build completed!"
