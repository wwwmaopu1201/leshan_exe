# 客户端权限系统更新说明

## 更新概述

客户端已完成权限系统的集成，以适配服务端的新权限管理功能。本次更新主要包括：

1. ✅ Vuex store扩展 - 支持权限存储和查询
2. ✅ 权限工具函数 - 提供完整的权限检查API
3. ✅ 路由权限守卫 - 自动检查用户路由访问权限
4. ✅ 用户禁用检查 - 防止被禁用用户登录和使用系统

## 主要变更文件

### 1. `src/store/index.js` - Vuex Store

**新增state:**
```javascript
permissionsCache: null  // 权限缓存
```

**新增getters:**
- `userPermissions` - 获取用户权限对象
- `hasPermission(key)` - 检查是否有某个权限
- `isUserDisabled` - 检查用户是否被禁用
- `isAdmin` - 检查是否是管理员

### 2. `src/utils/permission.js` - 权限工具（新建）

**提供的API:**
- `hasPermission(key)` - 检查单个权限
- `hasAllPermissions(keys)` - 检查是否拥有所有权限
- `hasAnyPermission(keys)` - 检查是否拥有任意权限
- `getUserPermissions()` - 获取所有权限
- `isUserDisabled()` - 检查用户是否被禁用
- `isAdmin()` - 检查是否是管理员
- `checkRoutePermission(route)` - 路由权限检查

**Vue指令:**
- `v-permission` - 无权限时隐藏元素
- `v-permission-disable` - 无权限时禁用元素

**权限常量:**
```javascript
PERMISSIONS = {
  FILE_MANAGEMENT: 'fileManagement',      // 文件管理
  REMOTE_MONITORING: 'remoteMonitoring',  // 远程监控
  STATISTICS: 'statistics',               // 统计
  DEVICE_MANAGEMENT: 'deviceManagement'   // 设备管理
}
```

### 3. `src/main.js` - 主入口文件

**新增:**
- 导入权限指令
- 注册全局权限指令

### 4. `src/router/index.js` - 路由配置

**改动:**
- 导入权限检查函数
- 为路由添加 `permission` meta字段
- 增强路由守卫，添加：
  - 用户禁用状态检查
  - 路由权限检查

**路由权限示例:**
```javascript
{
  path: 'file/pattern',
  meta: {
    permission: PERMISSIONS.FILE_MANAGEMENT  // 需要文件管理权限
  }
}
```

### 5. `src/views/Login.vue` - 登录页面

**改动:**
- 登录成功后检查用户是否被禁用
- 禁用用户无法登录，显示错误提示

### 6. `src/api/request.js` - API请求拦截器

**改动:**
- 响应拦截器中添加禁用状态检查
- 检测到用户被禁用时自动登出

## 权限系统工作流程

### 1. 登录流程

```
用户登录
  ↓
服务端验证
  ↓
返回用户信息（包含permissions字段）
  ↓
检查disabled状态
  ↓
存储到Vuex store
  ↓
解析permissions并缓存
```

### 2. 权限检查流程

```
用户访问页面/功能
  ↓
路由守卫检查路由权限
  ↓
组件使用v-permission检查元素权限
  ↓
显示/隐藏相应功能
```

### 3. 禁用检查流程

```
登录时检查 → 禁用用户无法登录
  ↓
路由跳转时检查 → 禁用用户被登出
  ↓
API请求时检查 → 禁用用户被登出
```

## 使用示例

### 示例1: 在模板中使用权限指令

```vue
<template>
  <div>
    <!-- 方式1: 隐藏无权限的元素 -->
    <el-button v-permission="'fileManagement'">
      文件管理
    </el-button>

    <!-- 方式2: 禁用无权限的元素 -->
    <el-button v-permission-disable="'remoteMonitoring'">
      远程监控
    </el-button>
  </div>
</template>
```

### 示例2: 在JavaScript中检查权限

```vue
<script>
import { hasPermission, PERMISSIONS } from '@/utils/permission'

export default {
  methods: {
    handleAction() {
      if (!hasPermission(PERMISSIONS.FILE_MANAGEMENT)) {
        this.$message.error('您没有文件管理权限')
        return
      }
      // 执行操作
    }
  }
}
</script>
```

### 示例3: 使用Vuex getter

```vue
<script>
import { mapGetters } from 'vuex'

export default {
  computed: {
    ...mapGetters(['hasPermission']),

    canManageFiles() {
      return this.hasPermission('fileManagement')
    }
  }
}
</script>
```

## 权限配置

### 服务端配置用户权限

在服务端管理后台（http://localhost:8088/admin）中：

1. 登录管理后台
2. 进入"用户管理"
3. 编辑用户，设置权限（JSON格式）：

```json
{
  "fileManagement": true,
  "remoteMonitoring": false,
  "statistics": true,
  "deviceManagement": true
}
```

### 默认权限

如果用户没有配置权限（permissions字段为空），系统默认给予所有权限：

```json
{
  "fileManagement": true,
  "remoteMonitoring": true,
  "statistics": true,
  "deviceManagement": true
}
```

## 与服务端的数据交互

### 登录响应格式

服务端登录接口返回：

```json
{
  "code": 0,
  "data": {
    "token": "eyJhbGc...",
    "user": {
      "ID": 1,
      "username": "admin",
      "nickname": "管理员",
      "role": "admin",
      "disabled": false,
      "permissions": "{\"fileManagement\":true,\"remoteMonitoring\":true,\"statistics\":true,\"deviceManagement\":true}",
      "groupId": 1
    }
  }
}
```

### 权限字段说明

- `permissions` (string) - JSON格式的权限配置
- `disabled` (boolean) - 用户是否被禁用
- `role` (string) - 用户角色（admin/user）
- `groupId` (number) - 所属分组ID

## 兼容性说明

### 向后兼容

本次更新**完全向后兼容**，对于没有配置权限的老用户：

- 默认拥有所有权限
- 所有功能正常可用
- 不影响现有功能

### 渐进式启用

可以逐步为用户配置权限：

1. 初期：所有用户默认全部权限
2. 过渡：为部分用户配置特定权限
3. 最终：为所有用户配置精细化权限

## 测试建议

### 1. 测试权限控制

创建不同权限的测试用户：

```json
// 测试用户1: 只有文件管理权限
{
  "fileManagement": true,
  "remoteMonitoring": false,
  "statistics": false,
  "deviceManagement": false
}

// 测试用户2: 只有统计权限
{
  "fileManagement": false,
  "remoteMonitoring": false,
  "statistics": true,
  "deviceManagement": false
}

// 测试用户3: 全部权限
{
  "fileManagement": true,
  "remoteMonitoring": true,
  "statistics": true,
  "deviceManagement": true
}
```

### 2. 测试禁用功能

1. 登录一个用户
2. 在管理后台禁用该用户
3. 验证：
   - 该用户无法再次登录
   - 已登录的用户下次请求时被登出

### 3. 测试路由权限

1. 使用无文件管理权限的用户登录
2. 尝试访问 `/file/pattern`
3. 应该被阻止并提示"您没有权限访问该页面"

## 后续优化建议

### 1. 菜单动态渲染

根据用户权限动态显示菜单项，建议修改侧边栏组件：

```vue
<script>
import { hasPermission } from '@/utils/permission'

export default {
  computed: {
    visibleMenuItems() {
      return this.menuItems.filter(item => {
        return !item.permission || hasPermission(item.permission)
      })
    }
  }
}
</script>
```

### 2. 权限变更通知

如果需要支持运行时权限变更（无需重新登录）：

- 可以添加WebSocket监听
- 服务端推送权限变更事件
- 客户端实时更新权限缓存

### 3. 更细粒度的权限

可以扩展权限系统支持更细粒度的控制：

```json
{
  "fileManagement": {
    "view": true,
    "upload": true,
    "download": true,
    "delete": false
  }
}
```

## 故障排除

### 问题1: 权限检查不生效

**原因:** 可能是权限缓存未更新

**解决:** 重新登录或清除localStorage

### 问题2: 所有用户都没有权限

**原因:** 服务端返回的permissions格式错误

**解决:** 检查服务端User模型的permissions字段格式

### 问题3: 路由守卫不工作

**原因:** 可能是路由配置的permission字段拼写错误

**解决:** 使用PERMISSIONS常量而不是字符串

## 相关文档

- [权限系统使用指南](./PERMISSION_GUIDE.md) - 详细的使用说明和示例
- [服务端API文档](../boer-lan-server/README_SERVER.md) - 服务端权限管理说明

## 更新日志

### v1.1.0 (2026-03-07)

- ✅ 添加完整的权限管理系统
- ✅ 支持4种基础权限类型
- ✅ 实现用户禁用功能
- ✅ 添加路由权限守卫
- ✅ 提供权限检查工具和指令
- ✅ 完善文档和示例

## 贡献者

- Backend: 服务端权限系统设计与实现
- Frontend: 客户端权限系统集成
- Co-Authored-By: Claude Sonnet 4.5
