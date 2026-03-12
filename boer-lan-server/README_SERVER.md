# 博尔局域网服务器

## 概述

博尔局域网服务器是一个功能完整的设备管理系统，支持设备监控、用户管理、分组管理等功能。

## 主要特性

### 1. 数据库
- **默认使用 SQLite**：无需安装额外数据库软件，开箱即用
- **支持 MySQL**：可通过配置切换到MySQL数据库
- 自动创建数据目录和默认数据

### 2. 管理界面
基于PDF文档设计的完整服务端管理界面，包括：

#### 主界面
- 服务器IP和端口显示
- 设备、用户、操作员统计信息
- 实时调试日志输出

#### 辅助工具
- 网络诊断（查看本机IP、Ping设备）
- 服务器配置信息查看

#### 分组管理
- 创建、编辑、删除分组（支持两级分组）
- 分组树形展示
- 分组排序功能

#### 客户端用户管理
- 用户列表查看
- 新建、编辑、删除用户
- 用户权限设置（文件管理、远程监控、统计、设备管理）
- 用户分组管理
- 移动用户到其他分组

#### 操作员管理
- 操作员列表查看
- 新建、编辑、删除操作员
- 限制登录功能
- 批量导入/导出操作员

#### 设备管理
- 设备列表查看
- 设备状态监控（在线/离线/工作中/空闲/报警）
- Ping设备测试连通性
- 设备分组管理

## 快速开始

### 1. 启动服务器

```bash
cd boer-lan-server
go run cmd/server/main.go
```

### 2. 访问管理界面

打开浏览器访问：
```
http://localhost:8088/admin
```

### 3. 默认登录账号

**管理员账号：**
- 用户名：`admin`
- 密码：`admin123`

**默认操作员：**
- 用户名：`operator`
- 密码：`123`

## Windows 打包

### 便携版（免安装）

- GitHub Actions 工作流 `Build Server Windows Package` 会额外产出 `server-windows-portable` 免安装产物
- 下载 artifact 后解压，直接运行 `Boer-LAN-Server.exe`
- 如需发布附件，也会额外生成 `Boer-LAN-Server-windows-portable.zip`
- 便携版会自动携带 `backend-server.exe` 和 `config/config.yaml`

### 本地打包

```bash
cd boer-lan-server
npm run tauri:build
npm run package:portable:win
```

## 配置说明

配置文件位置：`configs/config.yaml`

### 数据库配置

#### 使用SQLite（默认）
```yaml
database:
  type: sqlite
  path: ./data/boer-lan.db
```

#### 使用MySQL
```yaml
database:
  type: mysql
  host: localhost
  port: 3306
  username: root
  password: your_password
  database: boer_lan
  charset: utf8mb4
```

### 服务器配置

```yaml
server:
  port: 8088
  mode: debug  # debug, release, test
```

### JWT配置

```yaml
jwt:
  secret: boer-lan-secret-key-2024
  expire: 24  # hours
```

## API接口

### 认证相关
- `POST /api/auth/login` - 登录
- `POST /api/auth/logout` - 登出
- `GET /api/auth/userinfo` - 获取用户信息

### 分组管理
- `GET /api/group/tree` - 获取分组树
- `GET /api/group/list` - 获取分组列表
- `POST /api/group` - 创建分组
- `PUT /api/group/:id` - 更新分组
- `DELETE /api/group/:id` - 删除分组
- `POST /api/group/sort` - 批量排序分组

### 用户管理
- `GET /api/user/list` - 获取用户列表
- `GET /api/user/all` - 获取所有用户
- `POST /api/user` - 创建用户
- `PUT /api/user/:id` - 更新用户
- `DELETE /api/user` - 删除用户
- `POST /api/user/move` - 移动用户到其他分组

### 操作员管理
- `GET /api/operator/list` - 获取操作员列表
- `GET /api/operator/all` - 获取所有操作员
- `POST /api/operator` - 创建操作员
- `PUT /api/operator/:id` - 更新操作员
- `DELETE /api/operator` - 删除操作员
- `POST /api/operator/move` - 移动操作员到其他分组
- `POST /api/operator/import` - 批量导入操作员
- `GET /api/operator/export` - 导出操作员列表

### 设备管理
- `GET /api/device/list` - 获取设备列表
- `GET /api/device/tree` - 获取设备树
- `GET /api/device/:id` - 获取设备详情
- `POST /api/device` - 创建设备
- `PUT /api/device/:id` - 更新设备
- `DELETE /api/device/:id` - 删除设备
- `POST /api/device/move` - 移动设备到其他分组

### 系统管理
- `GET /api/system/info` - 获取服务器信息
- `GET /api/system/stats` - 获取统计信息
- `GET /api/system/network` - 获取网络信息
- `POST /api/system/ping` - Ping设备
- `GET /api/system/logs` - 获取调试日志
- `POST /api/system/logs` - 添加调试日志
- `DELETE /api/system/logs` - 清空调试日志
- `GET /api/system/config` - 获取服务器配置
- `POST /api/system/config` - 设置服务器配置

## 数据模型

### Group（分组）
- 支持两级分组结构（一级分组 -> 二级分组）
- 统一管理用户、设备、操作员
- 支持自定义排序

### User（客户端用户）
- 用于登录客户端程序
- 支持权限管理（JSON格式）
- 支持禁用功能
- 关联分组

### Operator（操作员）
- 用于登录模板机
- 支持限制登录
- 关联分组

### Device（设备）
- 设备基本信息（编号、名称、IP等）
- 设备状态（在线、离线、工作中、空闲、报警）
- 支持分组内排序
- 关联分组

## 开发说明

### 目录结构

```
boer-lan-server/
├── cmd/
│   └── server/
│       └── main.go          # 主程序入口
├── internal/
│   ├── api/                 # API处理器
│   │   ├── auth.go
│   │   ├── device.go
│   │   ├── group.go
│   │   ├── user.go
│   │   ├── operator.go
│   │   ├── system.go
│   │   └── router.go
│   └── model/
│       └── models.go        # 数据模型
├── pkg/
│   └── utils/               # 工具函数
├── web/
│   └── admin/               # 管理界面
│       ├── index.html
│       └── app.js
├── configs/
│   └── config.yaml          # 配置文件
├── data/                    # SQLite数据库目录（自动创建）
└── go.mod
```

### 添加新功能

1. 在 `internal/model/models.go` 中定义数据模型
2. 在 `internal/api/` 中创建对应的处理器
3. 在 `internal/api/router.go` 中注册路由
4. 在 `web/admin/` 中添加前端界面

## 注意事项

1. **数据库文件位置**：默认在 `./data/boer-lan.db`，首次运行会自动创建
2. **端口占用**：默认使用8088端口，如需修改请编辑 `configs/config.yaml`
3. **跨域配置**：已配置CORS中间件，允许跨域请求
4. **数据安全**：请定期备份 `data` 目录下的数据库文件

## 与PDF文档的对应关系

本实现完全按照《星火数控局域网服务器程序说明书》PDF文档设计：

| PDF章节 | 对应功能 | 实现位置 |
|---------|---------|----------|
| 主界面 | 服务器信息、统计、调试日志 | /admin 主界面页 |
| 辅助功能 | 网络诊断工具 | 辅助工具页 |
| 分组管理 - 组别管理 | 创建、编辑、删除分组 | 分组管理页 |
| 分组管理 - 客户端用户管理 | 用户CRUD、权限设置 | 用户管理页 |
| 分组管理 - 设备管理 | 设备查看、Ping测试 | 设备管理页 |
| 分组管理 - 操作员管理 | 操作员CRUD、导入导出 | 操作员管理页 |
| 连接数据库 | SQLite/MySQL切换 | config.yaml |

## 更新日志

### v1.0.0 (2026-03-07)
- ✅ 将数据库从MySQL改为SQLite（默认）
- ✅ 实现完整的服务端管理界面
- ✅ 添加分组管理功能（两级分组）
- ✅ 添加客户端用户管理（含权限设置）
- ✅ 添加操作员管理（含批量导入导出）
- ✅ 添加系统管理和辅助工具
- ✅ 实时调试日志显示
- ✅ 设备监控和Ping测试

## 技术栈

**后端：**
- Go 1.21+
- Gin Web Framework
- GORM ORM
- SQLite3（默认）/ MySQL
- JWT认证

**前端：**
- Vue 3
- Element Plus UI
- Axios

## 许可证

版权所有 © 2026 博尔科技
