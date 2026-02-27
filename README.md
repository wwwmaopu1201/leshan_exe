# 博尔局域网管理软件

基于 Vue2 + ElementUI + Tauri 开发的工业设备局域网管理客户端软件。

## 项目结构

```
.
├── boer-lan-client/          # 前端 Tauri 客户端
│   ├── src/                  # Vue 源码
│   │   ├── api/             # API 接口
│   │   ├── assets/          # 静态资源
│   │   ├── components/      # 公共组件
│   │   ├── i18n/            # 国际化
│   │   ├── layouts/         # 布局组件
│   │   ├── router/          # 路由配置
│   │   ├── store/           # Vuex 状态管理
│   │   ├── utils/           # 工具函数
│   │   └── views/           # 页面组件
│   ├── src-tauri/           # Tauri Rust 桥接层
│   ├── package.json
│   └── vite.config.js
│
└── boer-lan-server/          # Go 后端服务
    ├── cmd/server/          # 入口文件
    ├── internal/            # 内部模块
    │   ├── api/            # API 路由和处理器
    │   ├── model/          # 数据模型
    │   ├── service/        # 业务逻辑
    │   └── repository/     # 数据访问层
    ├── pkg/utils/           # 工具包
    ├── configs/             # 配置文件
    ├── scripts/             # SQL 脚本
    └── go.mod
```

## 技术栈

### 前端
- **框架**: Vue 2.7.x
- **UI库**: Element UI 2.15.x
- **图表**: ECharts 5.x
- **桌面**: Tauri 2.x
- **状态管理**: Vuex 3.x
- **路由**: Vue Router 3.x
- **国际化**: vue-i18n 8.x
- **HTTP**: Axios
- **构建**: Vite

### 后端
- **语言**: Go 1.21+
- **框架**: Gin
- **ORM**: GORM
- **数据库**: MySQL

## 快速开始

### 前端开发

1. 进入前端目录
```bash
cd boer-lan-client
```

2. 安装依赖
```bash
npm install
```

3. 启动开发服务器（仅前端预览）
```bash
npm run dev
```

4. 启动 Tauri 开发模式
```bash
npm run tauri:dev
```

5. 构建桌面应用
```bash
npm run tauri:build
```

### 后端开发

1. 进入后端目录
```bash
cd boer-lan-server
```

2. 配置数据库
   - 修改 `configs/config.yaml` 中的数据库连接信息
   - 执行 `scripts/init.sql` 初始化数据库

3. 安装依赖
```bash
go mod tidy
```

4. 启动服务
```bash
go run cmd/server/main.go
```

5. 编译
```bash
go build -o server cmd/server/main.go
```

## 默认账号

- 用户名: `admin`
- 密码: `admin123`

## 功能模块

1. **登录模块** - 服务器连接配置、用户登录
2. **首页** - 设备状态统计、图表展示
3. **数据看板** - 单设备数据监控、仪表盘
4. **设备管理** - 设备列表、分组、远程监控
5. **花型管理** - 文件上传、下发、队列、日志
6. **数据统计** - 工资、加工、时长、报警统计
7. **员工管理** - 员工信息、设备绑定
8. **个人中心** - 信息修改、密码修改、语言切换
9. **服务支持** - 联系客服、操作说明

## 主题颜色

- 主色调: `#409EFF` (蓝色)
- 侧边栏: 深蓝渐变 `#1e3c72 -> #2a5298`
- 顶部导航: `#3b9dfc`
- 背景色: `#f5f7fa`
- 卡片背景: `#ffffff`

## API 接口

所有 API 以 `/api` 为前缀，需要携带 JWT Token（登录接口除外）。

### 认证接口
- `POST /api/auth/login` - 登录
- `POST /api/auth/logout` - 登出
- `GET /api/auth/userinfo` - 获取用户信息
- `PUT /api/auth/password` - 修改密码

### 设备接口
- `GET /api/device/tree` - 获取设备树
- `GET /api/device/list` - 获取设备列表
- `POST /api/device` - 创建设备
- `PUT /api/device/:id` - 更新设备
- `DELETE /api/device/:id` - 删除设备

### 花型接口
- `GET /api/pattern/list` - 获取花型列表
- `POST /api/pattern/upload` - 上传花型
- `POST /api/pattern/download` - 下发花型
- `GET /api/pattern/queue` - 获取下发队列
- `GET /api/pattern/log` - 获取下发日志

### 统计接口
- `GET /api/statistics/home` - 首页统计
- `GET /api/statistics/dashboard` - 仪表盘数据
- `GET /api/statistics/salary` - 工资统计
- `GET /api/statistics/process` - 加工概况
- `GET /api/statistics/duration` - 时长统计
- `GET /api/statistics/alarm` - 报警统计

### 员工接口
- `GET /api/employee/list` - 获取员工列表
- `POST /api/employee` - 创建员工
- `PUT /api/employee/:id` - 更新员工
- `DELETE /api/employee/:id` - 删除员工

## 国际化

支持中文和英文两种语言，可在个人中心切换。
