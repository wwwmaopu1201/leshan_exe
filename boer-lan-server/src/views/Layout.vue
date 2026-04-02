<template>
  <div class="app-shell">
    <aside class="app-sidebar">
      <div class="sidebar-brand">
        <div class="sidebar-brand__icon">
          <i class="el-icon-office-building"></i>
        </div>
        <div class="sidebar-brand__copy">
          <strong>博尔局域网服务器</strong>
          <span>管理后台</span>
        </div>
      </div>

      <el-menu
        :default-active="currentPath"
        class="sidebar-menu"
        background-color="transparent"
        text-color="#d8e6ff"
        active-text-color="#ffffff"
        @select="handleMenuSelect"
      >
        <el-menu-item v-for="item in menuItems" :key="item.path" :index="item.path">
          <i :class="item.icon"></i>
          <span>{{ item.label }}</span>
        </el-menu-item>
      </el-menu>

      <div class="sidebar-footer">
        <div class="sidebar-footer__title">运行版本</div>
        <div class="sidebar-footer__value">{{ serverInfo.version || '1.0.9' }}</div>
      </div>
    </aside>

    <div class="app-main">
      <header class="app-header">
        <div class="app-header__title">
          <h1>{{ currentTitle }}</h1>
        </div>

        <div class="app-header__actions">
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

      <main class="app-content">
        <router-view />
      </main>
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
    currentTitle() {
      const matched = this.menuItems.find(item => item.path === this.$route.path)
      return matched ? matched.label : '服务器后台'
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
  display: flex;
  width: 100%;
  height: 100%;
  background: #f5f7fa;
}

.app-sidebar {
  width: 240px;
  padding: 12px 0;
  display: flex;
  flex-direction: column;
  background: #ffffff;
  border-right: 1px solid #e6e6e6;
  color: #303133;
}

.sidebar-brand {
  min-height: 64px;
  padding: 0 16px 12px;
  display: flex;
  align-items: center;
  gap: 12px;
  border-bottom: 1px solid #ebeef5;
}

.sidebar-brand__icon {
  width: 36px;
  height: 36px;
  border-radius: 4px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  background: #ecf5ff;
  color: #409eff;
  font-size: 18px;
}

.sidebar-brand__copy {
  display: flex;
  flex-direction: column;
  gap: 2px;

  strong {
    font-size: 16px;
    font-weight: 600;
  }

  span {
    color: #909399;
    font-size: 12px;
  }
}

.sidebar-menu {
  margin-top: 12px;
  border: none;
  flex: 1;
}

.sidebar-menu ::v-deep .el-menu-item {
  height: 44px;
  line-height: 44px;
  margin: 0 12px 4px;
  border-radius: 4px;
  color: #303133 !important;
}

.sidebar-menu ::v-deep .el-menu-item i {
  width: 24px;
  margin-right: 10px;
  font-size: 16px;
  color: inherit;
}

.sidebar-menu ::v-deep .el-menu-item:hover {
  background: #ecf5ff !important;
  color: #409eff !important;
}

.sidebar-menu ::v-deep .el-menu-item.is-active {
  background: #ecf5ff !important;
  color: #409eff !important;
}

.sidebar-footer {
  margin: 12px 16px 0;
  padding: 12px;
  border-radius: 4px;
  background: #f5f7fa;
  border: 1px solid #ebeef5;
}

.sidebar-footer__title {
  color: #909399;
  font-size: 12px;
}

.sidebar-footer__value {
  margin-top: 6px;
  font-size: 14px;
  font-weight: 500;
}

.app-main {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.app-header {
  min-height: 60px;
  padding: 12px 20px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 18px;
  background: #ffffff;
  border-bottom: 1px solid #ebeef5;
}

.app-header__title {
  min-width: 0;

  h1 {
    margin: 0;
    font-size: 20px;
    color: #303133;
    font-weight: 500;
  }
}

.app-header__actions {
  display: flex;
  align-items: center;
  justify-content: flex-end;
  gap: 10px;
  flex-wrap: wrap;
}

.server-chip {
  min-height: 40px;
  padding: 8px 12px;
  border-radius: 4px;
  border: 1px solid #dcdfe6;
  background: #ffffff;
  display: inline-flex;
  flex-direction: column;
  justify-content: center;
  gap: 4px;

  strong {
    color: #303133;
    font-size: 14px;
    font-weight: 500;
  }
}

.server-chip__label {
  color: #909399;
  font-size: 12px;
}

.app-content {
  flex: 1;
  min-height: 0;
  padding: 0;
  overflow-y: auto;
}

@media (max-width: 1120px) {
  .app-header {
    align-items: flex-start;
    flex-direction: column;
  }

  .app-header__actions {
    width: 100%;
    justify-content: flex-start;
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
