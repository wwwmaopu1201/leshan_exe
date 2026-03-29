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
}

.app-sidebar {
  width: 240px;
  padding: 18px 16px;
  display: flex;
  flex-direction: column;
  background:
    radial-gradient(circle at top left, rgba(255, 255, 255, 0.16), transparent 28%),
    linear-gradient(180deg, #0f2042 0%, #0d5fa8 100%);
  color: #ffffff;
}

.sidebar-brand {
  min-height: 76px;
  padding: 16px;
  border-radius: 22px;
  display: flex;
  align-items: center;
  gap: 14px;
  background: rgba(255, 255, 255, 0.1);
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.08);
}

.sidebar-brand__icon {
  width: 48px;
  height: 48px;
  border-radius: 18px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  background: rgba(255, 255, 255, 0.16);
  font-size: 22px;
}

.sidebar-brand__copy {
  display: flex;
  flex-direction: column;
  gap: 4px;

  strong {
    font-size: 18px;
    font-weight: 700;
  }

  span {
    color: rgba(216, 230, 255, 0.82);
    font-size: 12px;
  }
}

.sidebar-menu {
  margin-top: 18px;
  border: none;
  flex: 1;
}

.sidebar-menu ::v-deep .el-menu-item {
  height: 48px;
  line-height: 48px;
  margin-bottom: 8px;
  border-radius: 16px;
  color: #d8e6ff !important;
}

.sidebar-menu ::v-deep .el-menu-item i {
  width: 30px;
  margin-right: 10px;
  font-size: 18px;
  color: inherit;
}

.sidebar-menu ::v-deep .el-menu-item:hover {
  background: rgba(255, 255, 255, 0.12) !important;
}

.sidebar-menu ::v-deep .el-menu-item.is-active {
  background: linear-gradient(135deg, rgba(255, 255, 255, 0.22), rgba(255, 255, 255, 0.12)) !important;
  box-shadow: 0 14px 22px rgba(2, 14, 41, 0.18);
}

.sidebar-footer {
  padding: 14px 16px;
  border-radius: 18px;
  background: rgba(255, 255, 255, 0.1);
}

.sidebar-footer__title {
  color: rgba(216, 230, 255, 0.82);
  font-size: 12px;
}

.sidebar-footer__value {
  margin-top: 6px;
  font-size: 16px;
  font-weight: 700;
}

.app-main {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.app-header {
  min-height: 72px;
  padding: 14px 24px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 18px;
  background: rgba(255, 255, 255, 0.7);
  border-bottom: 1px solid rgba(221, 229, 240, 0.9);
  backdrop-filter: blur(16px);
}

.app-header__title {
  min-width: 0;

  h1 {
    margin: 0;
    font-size: 24px;
    color: #22324d;
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
  min-height: 46px;
  padding: 8px 14px;
  border-radius: 16px;
  border: 1px solid rgba(219, 228, 240, 0.92);
  background: rgba(255, 255, 255, 0.96);
  display: inline-flex;
  flex-direction: column;
  justify-content: center;
  gap: 4px;

  strong {
    color: #22324d;
    font-size: 14px;
  }
}

.server-chip__label {
  color: #8190a5;
  font-size: 12px;
}

.app-content {
  flex: 1;
  min-height: 0;
  padding: 12px 24px 24px;
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
  .app-sidebar {
    width: 212px;
  }
}
</style>
