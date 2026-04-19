<template>
  <div class="app-shell">
    <header class="app-topbar">
      <div class="app-topbar__left">
        <i class="el-icon-office-building"></i>
        <span class="app-topbar__title">局域网管理软件服务端</span>
      </div>

      <div class="app-topbar__actions">
        <div class="server-chip">
          <span class="server-chip__label">服务器 IP</span>
          <strong>{{ serverIpText }}</strong>
        </div>
        <div class="server-chip">
          <span class="server-chip__label">管理端口</span>
          <strong>{{ serverInfo.port || '-' }}</strong>
        </div>
        <div class="server-chip">
          <span class="server-chip__label">设备 TCP 端口</span>
          <strong>{{ serverInfo.tcpPort || '-' }}</strong>
        </div>
        <el-button size="small" @click="logout">退出登录</el-button>
      </div>
    </header>

    <div class="app-body">
      <aside class="app-sidebar">
        <el-menu
          :default-active="currentPath"
          class="sidebar-menu"
          background-color="transparent"
          text-color="#4c5768"
          active-text-color="#3388ff"
          @select="handleMenuSelect"
        >
          <el-menu-item v-for="item in menuItems" :key="item.path" :index="item.path">
            <i :class="item.icon"></i>
            <span>{{ item.label }}</span>
          </el-menu-item>
        </el-menu>
      </aside>
      <div class="app-main">
        <main class="app-content">
          <router-view />
        </main>
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
        { path: '/home', label: '主界面', icon: 'el-icon-data-analysis' },
        { path: '/tools', label: '辅助工具', icon: 'el-icon-s-tools' },
        { path: '/database', label: '连接数据库', icon: 'el-icon-connection' },
        { path: '/patterns', label: '花型列表', icon: 'el-icon-document' },
        { path: '/pattern-stats', label: '花型统计', icon: 'el-icon-data-line' },
        { path: '/roles', label: '权限角色', icon: 'el-icon-s-check' },
        { path: '/users', label: '账号管理', icon: 'el-icon-user' },
        { path: '/devices', label: '设备管理', icon: 'el-icon-monitor' }
      ],
      serverInfo: {
        ips: [],
        port: 8088,
        tcpPort: 38400,
        workDir: '',
        dataDir: '',
        os: '',
        arch: '',
        version: ''
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
      if (path !== this.$route.path) {
        this.$router.push(path)
      }
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
          this.serverInfo = {
            ...this.serverInfo,
            ...res.data
          }
        }
      } catch (error) {
        console.error('加载服务器信息失败', error)
      }
    }
  }
}
</script>

<style lang="scss" scoped>
.app-shell {
  height: 100%;
  display: flex;
  flex-direction: column;
  width: 100%;
  background: #f5f7fa;
}

.app-topbar {
  height: 46px;
  padding: 0 12px;
  background: linear-gradient(180deg, #338cf9 0%, #287de8 100%);
  color: #ffffff;
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.app-topbar__left,
.app-topbar__actions {
  display: flex;
  align-items: center;
  gap: 10px;
  min-width: 0;
}

.app-topbar__title {
  font-size: 13px;
  font-weight: 700;
}

.app-body {
  flex: 1;
  min-height: 0;
  display: flex;
}

.app-sidebar {
  width: 206px;
  background: #ffffff;
  border-right: 1px solid #dbe3ec;
  color: #303133;
}

.sidebar-menu {
  padding-top: 8px;
  border: none;
  height: 100%;
}

.sidebar-menu ::v-deep .el-menu-item {
  height: 38px;
  line-height: 38px;
  margin: 0 8px 4px;
  border-radius: 2px;
  color: #4d596a !important;
  font-size: 12px;
}

.sidebar-menu ::v-deep .el-menu-item i {
  width: 18px;
  margin-right: 8px;
  font-size: 14px;
  color: inherit;
}

.sidebar-menu ::v-deep .el-menu-item:hover {
  background: #edf5ff !important;
  color: #3388ff !important;
}

.sidebar-menu ::v-deep .el-menu-item.is-active {
  background: #e8f2ff !important;
  color: #3388ff !important;
}

.app-main {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.server-chip {
  min-height: 26px;
  padding: 0 8px;
  border-radius: 2px;
  background: rgba(255, 255, 255, 0.14);
  display: inline-flex;
  flex-direction: column;
  justify-content: center;
  gap: 1px;

  strong {
    color: #ffffff;
    font-size: 12px;
    font-weight: 500;
  }
}

.server-chip__label {
  color: rgba(255, 255, 255, 0.78);
  font-size: 11px;
}

.app-content {
  flex: 1;
  min-height: 0;
  padding: 8px;
  overflow-y: auto;
}

@media (max-width: 1120px) {
  .app-topbar__actions {
    gap: 6px;
  }
}

@media (max-width: 920px) {
  .app-shell {
    flex-direction: column;
  }

  .app-sidebar {
    width: 100%;
    border-right: none;
    border-bottom: 1px solid #e6e6e6;
  }
}
</style>
