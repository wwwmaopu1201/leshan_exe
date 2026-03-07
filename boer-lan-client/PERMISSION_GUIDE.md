# 权限系统使用指南

## 概述

客户端已经完整集成了服务端的权限系统，支持基于用户权限的功能访问控制。

## 权限类型

系统定义了以下4种权限：

| 权限键 | 说明 | 影响的功能 |
|-------|------|-----------|
| `fileManagement` | 文件管理权限 | 花型文件管理、下载队列、下载日志 |
| `remoteMonitoring` | 远程监控权限 | 设备远程监控、VNC连接 |
| `statistics` | 统计权限 | 工资统计、加工统计、时长统计、报警统计 |
| `deviceManagement` | 设备管理权限 | 设备列表、设备分组 |

## 使用方法

### 1. 在Vue组件中检查权限

#### 方法1：使用v-permission指令（隐藏元素）

```vue
<template>
  <div>
    <!-- 没有权限时，按钮会被隐藏 -->
    <el-button v-permission="'fileManagement'">
      文件管理
    </el-button>

    <!-- 也可以使用PERMISSIONS常量 -->
    <el-button v-permission="PERMISSIONS.REMOTE_MONITORING">
      远程监控
    </el-button>
  </div>
</template>

<script>
import { PERMISSIONS } from '@/utils/permission'

export default {
  data() {
    return {
      PERMISSIONS
    }
  }
}
</script>
```

#### 方法2：使用v-permission-disable指令（禁用元素）

```vue
<template>
  <!-- 没有权限时，按钮会被禁用但不会隐藏 -->
  <el-button v-permission-disable="'fileManagement'">
    文件管理
  </el-button>
</template>
```

#### 方法3：使用computed计算属性

```vue
<template>
  <div>
    <el-button v-if="canManageFiles">文件管理</el-button>
    <el-button :disabled="!canRemoteMonitor">远程监控</el-button>
  </div>
</template>

<script>
import { mapGetters } from 'vuex'

export default {
  computed: {
    ...mapGetters(['hasPermission']),

    canManageFiles() {
      return this.hasPermission('fileManagement')
    },

    canRemoteMonitor() {
      return this.hasPermission('remoteMonitoring')
    }
  }
}
</script>
```

#### 方法4：使用方法调用

```vue
<template>
  <div>
    <el-button @click="handleFileManagement">文件管理</el-button>
  </div>
</template>

<script>
import { hasPermission, PERMISSIONS } from '@/utils/permission'

export default {
  methods: {
    handleFileManagement() {
      if (!hasPermission(PERMISSIONS.FILE_MANAGEMENT)) {
        this.$message.error('您没有文件管理权限')
        return
      }

      // 执行文件管理操作
      this.openFileManager()
    }
  }
}
</script>
```

### 2. 在路由中使用权限

路由权限已经在 `src/router/index.js` 中配置，会自动检查用户是否有权限访问某个路由。

```javascript
{
  path: 'file/pattern',
  name: 'PatternList',
  component: () => import('@/views/file/PatternList.vue'),
  meta: {
    title: 'menu.patternList',
    parent: 'menu.file',
    permission: PERMISSIONS.FILE_MANAGEMENT  // 需要文件管理权限
  }
}
```

### 3. 在JavaScript代码中检查权限

```javascript
import {
  hasPermission,
  hasAllPermissions,
  hasAnyPermission,
  getUserPermissions,
  isAdmin,
  PERMISSIONS
} from '@/utils/permission'

// 检查单个权限
if (hasPermission(PERMISSIONS.FILE_MANAGEMENT)) {
  console.log('有文件管理权限')
}

// 检查是否拥有所有指定权限
if (hasAllPermissions([
  PERMISSIONS.FILE_MANAGEMENT,
  PERMISSIONS.DEVICE_MANAGEMENT
])) {
  console.log('同时拥有文件管理和设备管理权限')
}

// 检查是否拥有任意一个权限
if (hasAnyPermission([
  PERMISSIONS.FILE_MANAGEMENT,
  PERMISSIONS.STATISTICS
])) {
  console.log('至少拥有文件管理或统计权限之一')
}

// 获取所有权限
const permissions = getUserPermissions()
console.log('用户权限:', permissions)

// 检查是否是管理员
if (isAdmin()) {
  console.log('当前用户是管理员')
}
```

### 4. 在菜单中使用权限

建议在菜单组件中使用权限过滤，只显示用户有权限的菜单项：

```vue
<template>
  <el-menu>
    <el-menu-item
      v-for="item in visibleMenuItems"
      :key="item.path"
      :index="item.path"
    >
      {{ item.title }}
    </el-menu-item>
  </el-menu>
</template>

<script>
import { hasPermission } from '@/utils/permission'

export default {
  data() {
    return {
      menuItems: [
        { path: '/file/pattern', title: '文件管理', permission: 'fileManagement' },
        { path: '/device/monitor', title: '远程监控', permission: 'remoteMonitoring' },
        { path: '/statistics/salary', title: '统计', permission: 'statistics' },
        { path: '/device/list', title: '设备管理', permission: 'deviceManagement' }
      ]
    }
  },
  computed: {
    visibleMenuItems() {
      return this.menuItems.filter(item => {
        // 如果没有定义权限要求，默认显示
        if (!item.permission) return true
        // 检查是否有权限
        return hasPermission(item.permission)
      })
    }
  }
}
</script>
```

## 权限配置

### 服务端配置

在服务端管理后台（http://localhost:8088/admin），管理员可以为每个用户配置权限：

1. 进入"用户管理"页面
2. 编辑用户
3. 设置权限（JSON格式）：

```json
{
  "fileManagement": true,
  "remoteMonitoring": false,
  "statistics": true,
  "deviceManagement": true
}
```

### 客户端获取权限

客户端登录后会自动从服务端获取用户信息（包括权限），存储在Vuex store中。

## 用户禁用功能

除了权限控制，系统还支持禁用用户：

- 管理员可以在服务端管理后台禁用某个用户
- 被禁用的用户无法登录
- 已登录的用户如果被禁用，下次请求时会被强制登出

## 完整示例

以下是一个完整的组件示例，展示了多种权限检查方式：

```vue
<template>
  <div class="permission-demo">
    <h2>权限系统演示</h2>

    <!-- 使用指令控制可见性 -->
    <el-button v-permission="PERMISSIONS.FILE_MANAGEMENT" type="primary">
      文件管理（有权限才显示）
    </el-button>

    <!-- 使用指令控制禁用状态 -->
    <el-button v-permission-disable="PERMISSIONS.REMOTE_MONITORING" type="success">
      远程监控（无权限时禁用）
    </el-button>

    <!-- 使用computed计算属性 -->
    <el-button v-if="canViewStatistics" type="info">
      统计（computed控制）
    </el-button>

    <!-- 显示当前权限 -->
    <div class="permissions-info">
      <h3>当前用户权限：</h3>
      <ul>
        <li>文件管理：{{ permissions.fileManagement ? '✓' : '✗' }}</li>
        <li>远程监控：{{ permissions.remoteMonitoring ? '✓' : '✗' }}</li>
        <li>统计：{{ permissions.statistics ? '✓' : '✗' }}</li>
        <li>设备管理：{{ permissions.deviceManagement ? '✓' : '✗' }}</li>
      </ul>
      <p>管理员：{{ isAdmin ? '是' : '否' }}</p>
    </div>
  </div>
</template>

<script>
import { mapGetters } from 'vuex'
import { PERMISSIONS } from '@/utils/permission'

export default {
  name: 'PermissionDemo',
  data() {
    return {
      PERMISSIONS
    }
  },
  computed: {
    ...mapGetters([
      'hasPermission',
      'userPermissions',
      'isAdmin'
    ]),

    permissions() {
      return this.userPermissions
    },

    canViewStatistics() {
      return this.hasPermission(PERMISSIONS.STATISTICS)
    }
  },
  methods: {
    handleAction() {
      if (!this.hasPermission(PERMISSIONS.FILE_MANAGEMENT)) {
        this.$message.error('您没有文件管理权限')
        return
      }

      // 执行操作
      this.$message.success('操作成功')
    }
  }
}
</script>

<style scoped>
.permission-demo {
  padding: 20px;
}

.permissions-info {
  margin-top: 20px;
  padding: 15px;
  background: #f5f5f5;
  border-radius: 4px;
}
</style>
```

## 注意事项

1. **安全性**：客户端权限检查只是为了用户体验，真正的权限控制在服务端
2. **默认权限**：如果用户没有配置权限，默认拥有所有权限
3. **管理员**：管理员角色（role: 'admin'）可能需要特殊处理
4. **权限更新**：用户权限更新后需要重新登录才能生效

## 调试

在开发过程中，可以在浏览器控制台查看当前用户权限：

```javascript
// 查看当前用户信息
console.log('当前用户:', this.$store.state.user)

// 查看当前权限
console.log('当前权限:', this.$store.getters.userPermissions)

// 测试权限
console.log('有文件管理权限?', this.$store.getters.hasPermission('fileManagement'))
```

## 相关文件

- `src/store/index.js` - Vuex状态管理，包含权限getter
- `src/utils/permission.js` - 权限工具函数和指令
- `src/router/index.js` - 路由权限配置
- `src/api/request.js` - API请求拦截器，检查用户状态
- `src/views/Login.vue` - 登录页面，检查禁用状态
