# 博尔局域网管理软件 - 服务端架构方案

## 技术架构

### Go + Tauri 方案

采用 Go 语言开发后端 API 服务，使用 Tauri 打包成桌面应用。

```
+---------------------------+
|     Tauri 桌面应用         |
+---------------------------+
|    前端 UI (Vue/React)    |
+---------------------------+
|    Tauri Runtime (Rust)   |
+---------------------------+
|    Go Sidecar (API服务)   |
+---------------------------+
|    MySQL 数据库            |
+---------------------------+
```

## 方案优势

| 特性 | Go + Tauri | Electron + Node.js |
|------|------------|-------------------|
| 安装包大小 | ~10-20MB | ~100MB+ |
| 内存占用 | 低 (~50MB) | 高 (~150MB+) |
| CPU 占用 | 低 | 中等 |
| 启动速度 | 快 | 较慢 |
| 跨平台支持 | Windows/macOS/Linux | Windows/macOS/Linux |
| 数据库性能 | 优秀 | 良好 |

## 项目结构

```
boer-lan-server/
├── cmd/
│   └── server/
│       └── main.go              # 程序入口
├── internal/
│   ├── api/                     # API 路由和处理器
│   │   ├── router.go            # 路由配置
│   │   ├── middleware.go        # 中间件
│   │   ├── auth.go              # 认证接口
│   │   ├── device.go            # 设备管理接口
│   │   ├── pattern.go           # 花型文件接口
│   │   ├── statistics.go        # 统计接口
│   │   └── employee.go          # 员工管理接口
│   ├── model/                   # 数据模型
│   │   └── models.go
│   ├── service/                 # 业务逻辑层
│   │   ├── device_service.go
│   │   ├── pattern_service.go
│   │   └── statistics_service.go
│   └── repository/              # 数据访问层
│       └── repository.go
├── pkg/
│   └── utils/
│       ├── jwt.go               # JWT 工具
│       └── password.go          # 密码工具
├── configs/
│   └── config.yaml              # 配置文件
├── scripts/
│   ├── init.sql                 # 数据库建表脚本
│   └── test_data.sql            # 测试数据脚本
├── uploads/                     # 上传文件目录
│   └── patterns/
├── go.mod
└── go.sum
```

## Tauri Sidecar 配置

### 1. tauri.conf.json 配置

```json
{
  "bundle": {
    "externalBin": [
      "binaries/boer-server"
    ]
  }
}
```

### 2. 目录结构

```
boer-lan-server-tauri/
├── src-tauri/
│   ├── binaries/
│   │   ├── boer-server-x86_64-pc-windows-msvc.exe    # Windows
│   │   ├── boer-server-x86_64-apple-darwin           # macOS Intel
│   │   ├── boer-server-aarch64-apple-darwin          # macOS Apple Silicon
│   │   └── boer-server-x86_64-unknown-linux-gnu      # Linux
│   ├── src/
│   │   └── main.rs
│   └── tauri.conf.json
└── src/                          # 前端代码（可选，服务端可用简单管理界面）
```

### 3. Rust 代码调用 Sidecar

```rust
use tauri::api::process::Command;

#[tauri::command]
async fn start_server() -> Result<(), String> {
    Command::new_sidecar("boer-server")
        .expect("failed to create sidecar command")
        .spawn()
        .expect("failed to spawn sidecar");
    Ok(())
}
```

## 构建流程

### 1. 编译 Go 服务

```bash
# Windows
GOOS=windows GOARCH=amd64 go build -o binaries/boer-server-x86_64-pc-windows-msvc.exe ./cmd/server

# macOS Intel
GOOS=darwin GOARCH=amd64 go build -o binaries/boer-server-x86_64-apple-darwin ./cmd/server

# macOS Apple Silicon
GOOS=darwin GOARCH=arm64 go build -o binaries/boer-server-aarch64-apple-darwin ./cmd/server

# Linux
GOOS=linux GOARCH=amd64 go build -o binaries/boer-server-x86_64-unknown-linux-gnu ./cmd/server
```

### 2. 构建 Tauri 应用

```bash
npm run tauri build
```

## API 接口设计

### 基础信息

- **Base URL**: `http://localhost:8080/api`
- **认证方式**: JWT Bearer Token
- **响应格式**: JSON

### 响应结构

```json
{
  "code": 0,           // 0 成功，非0 失败
  "data": {},          // 响应数据
  "message": "success" // 消息
}
```

### 接口列表

#### 认证模块
| 方法 | 路径 | 描述 |
|------|------|------|
| POST | /auth/login | 用户登录 |
| POST | /auth/logout | 用户登出 |
| GET | /auth/userinfo | 获取用户信息 |
| PUT | /auth/password | 修改密码 |

#### 设备管理
| 方法 | 路径 | 描述 |
|------|------|------|
| GET | /device/tree | 获取设备树 |
| GET | /device/list | 获取设备列表 |
| GET | /device/:id | 获取设备详情 |
| POST | /device | 创建设备 |
| PUT | /device/:id | 更新设备 |
| DELETE | /device/:id | 删除设备 |
| DELETE | /device/batch | 批量删除设备 |
| POST | /device/move | 移动设备到分组 |
| GET | /device/groups | 获取设备分组 |
| POST | /device/group | 创建分组 |
| PUT | /device/group/:id | 更新分组 |
| DELETE | /device/group/:id | 删除分组 |

#### 花型文件管理
| 方法 | 路径 | 描述 |
|------|------|------|
| GET | /pattern/list | 获取花型列表 |
| POST | /pattern/upload | 上传花型文件 |
| DELETE | /pattern/:id | 删除花型 |
| POST | /pattern/download | 下发到设备 |
| POST | /pattern/batch-download | 批量下发 |
| GET | /pattern/queue | 获取下发队列 |
| GET | /pattern/log | 获取下发日志 |
| DELETE | /pattern/queue/:id | 取消下发任务 |

#### 统计模块
| 方法 | 路径 | 描述 |
|------|------|------|
| GET | /statistics/home | 首页统计数据 |
| GET | /statistics/dashboard | 数据看板 |
| GET | /statistics/salary | 工资统计 |
| GET | /statistics/salary/detail | 工资详情 |
| GET | /statistics/process | 加工概况 |
| GET | /statistics/duration | 时长统计 |
| GET | /statistics/alarm | 报警统计 |

#### 员工管理
| 方法 | 路径 | 描述 |
|------|------|------|
| GET | /employee/list | 获取员工列表 |
| GET | /employee/:id | 获取员工详情 |
| POST | /employee | 创建员工 |
| PUT | /employee/:id | 更新员工 |
| DELETE | /employee/:id | 删除员工 |

## 数据库设计

### 主要表结构

1. **users** - 用户表
2. **device_groups** - 设备分组表
3. **devices** - 设备表
4. **patterns** - 花型文件表
5. **download_tasks** - 下发任务表
6. **employees** - 员工表
7. **employee_devices** - 员工设备绑定表
8. **production_records** - 生产记录表
9. **alarm_records** - 报警记录表
10. **salary_records** - 工资记录表

详细建表语句见 `scripts/init.sql`

## 部署说明

### 服务端部署

1. 安装 MySQL 数据库
2. 执行 `scripts/init.sql` 创建数据库和表
3. 执行 `scripts/test_data.sql` 插入测试数据（可选）
4. 修改 `configs/config.yaml` 配置数据库连接
5. 运行 Tauri 打包后的安装程序

### 客户端部署

1. 运行 Tauri 打包后的客户端安装程序
2. 配置服务器 IP 和端口
3. 使用账号密码登录

## 开发环境

### 后端开发

```bash
cd boer-lan-server

# 安装依赖
go mod tidy

# 运行开发服务器
go run cmd/server/main.go
```

### 服务端 Tauri 开发

```bash
cd boer-lan-server-tauri

# 安装依赖
npm install

# 开发模式
npm run tauri dev

# 构建
npm run tauri build
```

## 配置文件

### config.yaml

```yaml
server:
  port: 8080
  mode: debug  # debug / release

database:
  host: 127.0.0.1
  port: 3306
  username: root
  password: your_password
  database: boer_lan
  charset: utf8mb4

jwt:
  secret: your-jwt-secret-key
  expire: 24  # hours
```

## 注意事项

1. **数据库兼容性**: 目前使用 MySQL，如需支持 SQL Server，需要修改驱动和部分 SQL 语法
2. **跨平台构建**: 需要在对应平台上构建或使用交叉编译
3. **文件上传**: 上传目录需要有写入权限
4. **安全性**: 生产环境需要修改 JWT Secret 和数据库密码
