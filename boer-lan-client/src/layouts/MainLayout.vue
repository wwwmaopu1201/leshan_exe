<template>
  <div class="main-layout">
    <aside class="sidebar" :class="{ collapsed: isCollapsed }">
      <div class="logo">
        <img src="@/assets/images/logo.png" alt="Logo" class="logo-img" />
        <div v-if="!isCollapsed" class="logo-copy">
          <span class="logo-title">博尔管理系统</span>
          <span class="logo-subtitle">Boer LAN Client</span>
        </div>
      </div>

      <el-menu
        :default-active="activeMenu"
        class="sidebar-menu"
        background-color="transparent"
        text-color="#d8e4ff"
        active-text-color="#ffffff"
        :collapse="isCollapsed"
        :unique-opened="true"
        router
      >
        <el-menu-item v-if="canAccess.home" index="/home">
          <span class="menu-icon"><i class="el-icon-s-home"></i></span>
          <span slot="title">{{ $t('menu.home') }}</span>
        </el-menu-item>

        <el-menu-item v-if="canAccess.dashboard" index="/dashboard">
          <span class="menu-icon"><i class="el-icon-data-board"></i></span>
          <span slot="title">{{ $t('menu.dashboard') }}</span>
        </el-menu-item>

        <el-submenu v-if="canAccess.deviceSection" index="/device">
          <template slot="title">
            <span class="menu-icon"><i class="el-icon-monitor"></i></span>
            <span>{{ $t('menu.device') }}</span>
          </template>
          <el-menu-item v-if="canAccess.deviceManagement" index="/device/list">
            {{ $t('menu.deviceList') }}
          </el-menu-item>
          <el-menu-item v-if="canAccess.remoteMonitoring" index="/device/monitor">
            {{ $t('menu.remoteMonitor') }}
          </el-menu-item>
        </el-submenu>

        <el-submenu v-if="canAccess.fileManagement" index="/file">
          <template slot="title">
            <span class="menu-icon"><i class="el-icon-folder"></i></span>
            <span>{{ $t('menu.file') }}</span>
          </template>
          <el-menu-item index="/file/pattern">{{ $t('menu.patternList') }}</el-menu-item>
          <el-menu-item index="/file/queue">{{ $t('menu.downloadQueue') }}</el-menu-item>
          <el-menu-item index="/file/log">{{ $t('menu.downloadLog') }}</el-menu-item>
        </el-submenu>

        <el-submenu v-if="canAccess.statistics" index="/statistics">
          <template slot="title">
            <span class="menu-icon"><i class="el-icon-s-data"></i></span>
            <span>{{ $t('menu.statistics') }}</span>
          </template>
          <el-menu-item index="/statistics/salary">{{ $t('menu.salaryStats') }}</el-menu-item>
          <el-menu-item index="/statistics/process">{{ $t('menu.processOverview') }}</el-menu-item>
          <el-menu-item index="/statistics/duration">{{ $t('menu.durationStats') }}</el-menu-item>
          <el-menu-item index="/statistics/alarm">{{ $t('menu.alarmStats') }}</el-menu-item>
        </el-submenu>

        <el-submenu v-if="canAccess.employeeManagement" index="/employee">
          <template slot="title">
            <span class="menu-icon"><i class="el-icon-user"></i></span>
            <span>{{ $t('menu.employee') }}</span>
          </template>
          <el-menu-item index="/employee/list">{{ $t('menu.employeeList') }}</el-menu-item>
        </el-submenu>

        <el-submenu index="/profile">
          <template slot="title">
            <span class="menu-icon"><i class="el-icon-user-solid"></i></span>
            <span>{{ $t('menu.profile') }}</span>
          </template>
          <el-menu-item index="/profile/info">{{ $t('menu.basicInfo') }}</el-menu-item>
          <el-menu-item index="/profile/password">{{ $t('menu.changePassword') }}</el-menu-item>
        </el-submenu>

        <el-submenu index="/support">
          <template slot="title">
            <span class="menu-icon"><i class="el-icon-service"></i></span>
            <span>{{ $t('menu.support') }}</span>
          </template>
          <el-menu-item index="/support/contact">{{ $t('menu.contact') }}</el-menu-item>
          <el-menu-item index="/support/manual">{{ $t('menu.manual') }}</el-menu-item>
        </el-submenu>
      </el-menu>
    </aside>

    <div class="main-container">
      <header class="header">
        <div class="header-left">
          <button class="collapse-btn" type="button" @click="toggleSidebar">
            <i :class="isCollapsed ? 'el-icon-s-unfold' : 'el-icon-s-fold'"></i>
          </button>
          <div class="header-info">
            <el-breadcrumb separator="/">
              <el-breadcrumb-item
                v-for="(item, index) in breadcrumbs"
                :key="`${item.title}-${index}`"
                :to="item.path || undefined"
              >
                {{ item.title }}
              </el-breadcrumb-item>
            </el-breadcrumb>
            <div class="server-tag" v-if="serverAddress">
              <i class="el-icon-link"></i>
              <span>{{ serverAddress }}</span>
            </div>
          </div>
        </div>

        <div class="header-right">
          <div class="lang-switch" role="group" aria-label="language switch">
            <button
              v-for="item in languageOptions"
              :key="item.value"
              type="button"
              :class="{ active: currentLang === item.value }"
              @click="changeLang(item.value)"
            >
              {{ item.label }}
            </button>
          </div>

          <el-dropdown trigger="click" @command="handleCommand">
            <div class="user-info">
              <el-avatar :size="34" icon="el-icon-user-solid"></el-avatar>
              <div class="user-copy">
                <span class="username">{{ user?.username || 'Admin' }}</span>
                <span class="user-role">{{ currentLangLabel }}</span>
              </div>
              <i class="el-icon-arrow-down"></i>
            </div>
            <el-dropdown-menu slot="dropdown">
              <el-dropdown-item command="profile">
                <i class="el-icon-user"></i> {{ $t('menu.profile') }}
              </el-dropdown-item>
              <el-dropdown-item command="password">
                <i class="el-icon-lock"></i> {{ $t('menu.changePassword') }}
              </el-dropdown-item>
              <el-dropdown-item divided command="logout">
                <i class="el-icon-switch-button"></i> 退出登录
              </el-dropdown-item>
            </el-dropdown-menu>
          </el-dropdown>
        </div>
      </header>

      <main class="content">
        <router-view />
      </main>
    </div>
  </div>
</template>

<script>
import { mapState, mapMutations, mapActions } from 'vuex'

export default {
  name: 'MainLayout',
  data() {
    return {
      languageOptions: [
        { label: '中', value: 'zh-CN' },
        { label: 'EN', value: 'en-US' }
      ]
    }
  },
  computed: {
    ...mapState(['user', 'sidebarCollapsed', 'language', 'serverConfig']),
    canAccess() {
      const hasPermission = this.$store.getters.hasPermission
      const deviceManagement = hasPermission('deviceManagement')
      const remoteMonitoring = hasPermission('remoteMonitoring')
      return {
        home: hasPermission('home'),
        dashboard: hasPermission('dashboard'),
        employeeManagement: hasPermission('employeeManagement'),
        fileManagement: hasPermission('fileManagement'),
        statistics: hasPermission('statistics'),
        deviceManagement,
        remoteMonitoring,
        deviceSection: deviceManagement || remoteMonitoring
      }
    },
    isCollapsed() {
      return this.sidebarCollapsed
    },
    activeMenu() {
      return this.$route.path
    },
    currentLang() {
      return this.language || 'zh-CN'
    },
    currentLangLabel() {
      return this.currentLang === 'zh-CN' ? '简体中文' : 'English'
    },
    serverAddress() {
      const ip = String(this.serverConfig?.ip || '').trim()
      const port = String(this.serverConfig?.port || '').trim()
      if (!ip || !port) {
        return ''
      }
      return `${ip}:${port}`
    },
    breadcrumbs() {
      const matched = this.$route.matched.filter(item => item.meta && item.meta.title)
      const items = []

      matched.forEach(item => {
        const parentTitle = item.meta.parent ? this.$t(item.meta.parent) : ''
        if (parentTitle && !items.find(entry => entry.title === parentTitle)) {
          items.push({ title: parentTitle, path: '' })
        }
        items.push({
          title: this.$t(item.meta.title),
          path: item.path
        })
      })

      return items
    }
  },
  methods: {
    ...mapMutations(['TOGGLE_SIDEBAR']),
    ...mapActions(['logout', 'setLanguage']),
    toggleSidebar() {
      this.TOGGLE_SIDEBAR()
    },
    changeLang(lang) {
      if (lang === this.currentLang) {
        return
      }
      this.$i18n.locale = lang
      this.setLanguage(lang)
    },
    handleCommand(command) {
      switch (command) {
        case 'profile':
          this.$router.push('/profile/info')
          break
        case 'password':
          this.$router.push('/profile/password')
          break
        case 'logout':
          this.handleLogout()
          break
      }
    },
    handleLogout() {
      this.$confirm('确定要退出登录吗？', '提示', {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        type: 'warning'
      }).then(() => {
        this.logout()
        this.$router.push('/login')
      }).catch(() => {})
    }
  }
}
</script>

<style lang="scss" scoped>
.main-layout {
  display: flex;
  width: 100%;
  height: 100vh;
  background: #f5f7fa;
}

.sidebar {
  width: 248px;
  height: 100%;
  padding: 12px 0;
  background: #ffffff;
  border-right: 1px solid #e6e6e6;
  transition: width 0.28s ease;
  overflow: hidden;

  &.collapsed {
    width: 84px;

    .logo {
      justify-content: center;
      padding: 12px 0;
    }

    .logo-img {
      margin-right: 0;
    }
  }
}

.logo {
  display: flex;
  align-items: center;
  min-height: 68px;
  padding: 0 16px 12px;
  border-bottom: 1px solid #ebeef5;

  .logo-img {
    width: 36px;
    height: 36px;
    margin-right: 12px;
    border-radius: 4px;
  }

  .logo-copy {
    display: flex;
    flex-direction: column;
    gap: 2px;
    min-width: 0;
  }

  .logo-title {
    font-size: 16px;
    font-weight: 600;
    color: #303133;
  }

  .logo-subtitle {
    font-size: 11px;
    color: #909399;
  }
}

.sidebar-menu {
  height: calc(100% - 80px);
  border: none;
  overflow-y: auto;
  padding: 12px 0;

  &:not(.el-menu--collapse) {
    width: 100%;
  }

  ::v-deep .el-submenu__title,
  ::v-deep .el-menu-item {
    height: 44px;
    line-height: 44px;
    margin: 0 12px 4px;
    border-radius: 4px;
    padding-left: 16px !important;
    color: #303133 !important;
    transition: background-color 0.2s ease;
  }

  ::v-deep .el-submenu__title:hover,
  ::v-deep .el-menu-item:hover {
    background: #ecf5ff !important;
    color: #409eff !important;
  }

  ::v-deep .el-submenu.is-opened > .el-submenu__title,
  ::v-deep .el-menu-item.is-active {
    background: #ecf5ff !important;
    color: #409eff !important;
  }

  ::v-deep .el-menu--inline {
    background: #ffffff !important;
  }

  ::v-deep .el-menu--inline .el-menu-item {
    height: 40px;
    line-height: 40px;
    margin: 0 12px 4px 24px;
    padding-left: 28px !important;
    border-radius: 4px;
    background: transparent;
  }

  ::v-deep .el-menu--collapse .el-submenu__title,
  ::v-deep .el-menu--collapse .el-menu-item {
    padding: 0 !important;
    justify-content: center;
  }
}

.menu-icon {
  width: 20px;
  height: 20px;
  margin-right: 10px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  color: currentColor;
  font-size: 16px;
  vertical-align: middle;
}

.main-container {
  flex: 1;
  display: flex;
  flex-direction: column;
  min-width: 0;
}

.header {
  min-height: 60px;
  padding: 0 20px;
  background: #ffffff;
  border-bottom: 1px solid #ebeef5;
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 16px;
  min-width: 0;
}

.collapse-btn {
  width: 32px;
  height: 32px;
  border: 1px solid #dcdfe6;
  border-radius: 4px;
  background: #ffffff;
  color: #606266;
  font-size: 16px;
  cursor: pointer;
  transition: border-color 0.2s ease, color 0.2s ease;

  &:hover {
    color: #409eff;
    border-color: #c6e2ff;
  }
}

.header-info {
  display: flex;
  flex-direction: column;
  gap: 8px;
  min-width: 0;
}

.server-tag {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  width: fit-content;
  max-width: 100%;
  padding: 4px 10px;
  border-radius: 12px;
  background: #f4f4f5;
  color: #606266;
  font-size: 12px;
}

.header-right {
  display: flex;
  align-items: center;
  gap: 14px;
}

.lang-switch {
  display: inline-flex;
  padding: 2px;
  background: #f5f7fa;
  border: 1px solid #dcdfe6;
  border-radius: 4px;

  button {
    min-width: 44px;
    height: 28px;
    border: none;
    border-radius: 4px;
    background: transparent;
    color: #606266;
    font-size: 12px;
    font-weight: 500;
    cursor: pointer;
    transition: all 0.2s ease;

    &.active {
      background: #409eff;
      color: #ffffff;
    }
  }
}

.user-info {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 6px 10px 6px 8px;
  border: 1px solid #dcdfe6;
  border-radius: 4px;
  background: #ffffff;
  color: #303133;
  cursor: pointer;
}

.user-copy {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.username {
  font-size: 14px;
  font-weight: 500;
}

.user-role {
  font-size: 11px;
  color: #909399;
}

.content {
  flex: 1;
  min-height: 0;
  overflow: auto;
  padding: 0;
}

::v-deep .el-breadcrumb {
  line-height: 1;
}

::v-deep .el-breadcrumb__inner,
::v-deep .el-breadcrumb__separator {
  color: #909399;
}

::v-deep .el-breadcrumb__inner.is-link:hover {
  color: #409eff;
}

@media (max-width: 1200px) {
  .header {
    padding: 0 16px;
  }

  .lang-switch {
    display: none;
  }
}

@media (max-width: 768px) {
  .sidebar {
    position: fixed;
    z-index: 20;
    left: 0;
    top: 0;
    bottom: 0;
  }

  .header {
    height: auto;
    min-height: 72px;
    padding: 14px;
    align-items: flex-start;
  }

  .header-left,
  .header-right {
    width: 100%;
  }

  .header-right {
    justify-content: flex-end;
  }

  .content {
    padding: 10px;
  }
}
</style>
