<template>
  <div class="main-container">
    <!-- 侧边栏 -->
    <div class="sidebar">
      <div class="sidebar-header">
        博尔局域网服务器
      </div>
      <el-menu
        :default-active="currentPath"
        class="sidebar-menu"
        background-color="#304156"
        text-color="#fff"
        active-text-color="#409EFF"
        @select="handleMenuSelect"
      >
        <el-menu-item v-for="item in menuItems" :key="item.path" :index="item.path">
          <i :class="item.icon"></i>
          <span>{{ item.label }}</span>
        </el-menu-item>
      </el-menu>
    </div>

    <!-- 主内容区 -->
    <div class="main-content">
      <!-- 顶部栏 -->
      <div class="top-header">
        <div class="server-info">
          <span>服务器端口: <strong>{{ serverInfo.port }}</strong></span>
          <span>服务器IP: <strong>{{ serverIpText }}</strong></span>
          <span>设备TCP端口: <strong>{{ serverInfo.tcpPort || '-' }}</strong></span>
        </div>
        <div>
          <el-button @click="logout" size="small">退出登录</el-button>
        </div>
      </div>

      <!-- 内容区域 -->
      <div class="content-area">
        <router-view />
      </div>
    </div>
  </div>
</template>

<script>
export default {
  name: 'Layout',
  data() {
    return {
      menuItems: [
        { path: '/home', label: '主界面', icon: 'el-icon-data-line' },
        { path: '/tools', label: '辅助工具', icon: 'el-icon-setting' },
        { path: '/database', label: '连接数据库', icon: 'el-icon-connection' },
        { path: '/groups', label: '分组管理', icon: 'el-icon-folder' },
        { path: '/roles', label: '权限角色', icon: 'el-icon-s-check' },
        { path: '/users', label: '账号管理', icon: 'el-icon-user' },
        { path: '/operators', label: '操作员管理', icon: 'el-icon-user-solid' },
        { path: '/devices', label: '设备管理', icon: 'el-icon-monitor' }
      ],
      serverInfo: {
        ips: [],
        port: 8088,
        tcpPort: 38400,
        workDir: '',
        dataDir: '',
        os: '',
        arch: ''
      }
    }
  },
  computed: {
    currentPath() {
      return this.$route.path
    },
    serverIpText() {
      const ips = Array.isArray(this.serverInfo.ips) ? this.serverInfo.ips.filter(Boolean) : []
      return ips.length ? ips.join(', ') : '-'
    }
  },
  mounted() {
    this.loadServerInfo()
  },
  methods: {
    handleMenuSelect(path) {
      this.$router.push(path)
    },
    logout() {
      localStorage.removeItem('token')
      this.$message.success('已退出登录')
      this.$router.push('/login')
    },
    async loadServerInfo() {
      try {
        const res = await this.$axios.get('/system/info')
        if (res.code === 0) {
          this.serverInfo = res.data
        }
      } catch (error) {
        console.error('加载服务器信息失败', error)
      }
    }
  }
}
</script>

<style scoped>
.main-container {
  display: flex;
  height: 100%;
}

.sidebar {
  width: 200px;
  background: #304156;
  color: white;
}

.sidebar-header {
  padding: 20px;
  background: #263445;
  text-align: center;
  font-weight: bold;
  font-size: 16px;
}

.sidebar-menu {
  border: none;
}

.sidebar-menu .el-menu-item {
  height: 48px;
  line-height: 48px;
}

.sidebar-menu .el-menu-item i {
  margin-right: 8px;
}

.main-content {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.top-header {
  height: 60px;
  background: white;
  border-bottom: 1px solid #e6e6e6;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 20px;
}

.server-info {
  display: flex;
  align-items: center;
  gap: 20px;
}

.content-area {
  flex: 1;
  padding: 20px;
  overflow-y: auto;
  background: #f0f2f5;
}
</style>
