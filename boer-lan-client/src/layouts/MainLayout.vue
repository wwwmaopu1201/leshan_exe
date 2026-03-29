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
  background:
    radial-gradient(circle at top left, rgba(63, 130, 255, 0.14), transparent 30%),
    linear-gradient(180deg, #f7f9fe 0%, #eef3fb 100%);
}

.sidebar {
  width: 248px;
  height: 100%;
  padding: 18px 14px;
  background: linear-gradient(180deg, #0f2042 0%, #163766 58%, #0d5fa8 100%);
  box-shadow: 18px 0 38px rgba(10, 27, 58, 0.18);
  transition: width 0.28s ease;
  overflow: hidden;

  &.collapsed {
    width: 84px;

    .logo {
      justify-content: center;
      padding: 12px 0 20px;
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
  padding: 12px 10px 20px;

  .logo-img {
    width: 42px;
    height: 42px;
    margin-right: 12px;
    border-radius: 12px;
    box-shadow: 0 10px 24px rgba(18, 104, 210, 0.3);
  }

  .logo-copy {
    display: flex;
    flex-direction: column;
    gap: 2px;
    min-width: 0;
  }

  .logo-title {
    font-size: 16px;
    font-weight: 700;
    color: #ffffff;
    letter-spacing: 0.04em;
  }

  .logo-subtitle {
    font-size: 11px;
    color: rgba(220, 232, 255, 0.72);
    letter-spacing: 0.08em;
  }
}

.sidebar-menu {
  height: calc(100% - 88px);
  border: none;
  overflow-y: auto;
  padding-right: 4px;

  &:not(.el-menu--collapse) {
    width: 220px;
  }

  ::v-deep .el-submenu__title,
  ::v-deep .el-menu-item {
    height: 48px;
    line-height: 48px;
    margin-bottom: 8px;
    border-radius: 14px;
    padding-left: 14px !important;
    color: #d8e4ff !important;
    transition: all 0.22s ease;
  }

  ::v-deep .el-submenu__title:hover,
  ::v-deep .el-menu-item:hover {
    background: rgba(255, 255, 255, 0.09) !important;
    color: #ffffff !important;
  }

  ::v-deep .el-submenu.is-opened > .el-submenu__title,
  ::v-deep .el-menu-item.is-active {
    background: linear-gradient(135deg, rgba(67, 139, 255, 0.95), rgba(61, 192, 255, 0.88)) !important;
    box-shadow: 0 12px 26px rgba(41, 122, 228, 0.26);
    color: #ffffff !important;
  }

  ::v-deep .el-menu--inline {
    background: transparent !important;
  }

  ::v-deep .el-menu--inline .el-menu-item {
    height: 42px;
    line-height: 42px;
    margin: 4px 0 0 12px;
    padding-left: 52px !important;
    border-radius: 12px;
    background: rgba(255, 255, 255, 0.04);
  }

  ::v-deep .el-menu--collapse .el-submenu__title,
  ::v-deep .el-menu--collapse .el-menu-item {
    padding: 0 !important;
    justify-content: center;
  }
}

.menu-icon {
  width: 28px;
  height: 28px;
  margin-right: 10px;
  border-radius: 10px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  background: rgba(255, 255, 255, 0.09);
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
  height: 76px;
  margin: 18px 18px 0 18px;
  padding: 0 24px;
  border-radius: 22px;
  background: rgba(255, 255, 255, 0.9);
  box-shadow: 0 18px 40px rgba(65, 91, 137, 0.08);
  border: 1px solid rgba(213, 224, 239, 0.84);
  display: flex;
  align-items: center;
  justify-content: space-between;
  backdrop-filter: blur(14px);
}

.header-left {
  display: flex;
  align-items: center;
  gap: 16px;
  min-width: 0;
}

.collapse-btn {
  width: 42px;
  height: 42px;
  border: none;
  border-radius: 14px;
  background: linear-gradient(135deg, #f0f5ff, #e5eefc);
  color: #1f3f7a;
  font-size: 18px;
  cursor: pointer;
  transition: transform 0.2s ease, box-shadow 0.2s ease;

  &:hover {
    transform: translateY(-1px);
    box-shadow: 0 10px 18px rgba(78, 109, 160, 0.14);
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
  padding: 6px 10px;
  border-radius: 999px;
  background: #f2f6fd;
  color: #6780a8;
  font-size: 12px;
}

.header-right {
  display: flex;
  align-items: center;
  gap: 14px;
}

.lang-switch {
  display: inline-flex;
  padding: 4px;
  background: #edf3fb;
  border-radius: 999px;

  button {
    min-width: 48px;
    height: 32px;
    border: none;
    border-radius: 999px;
    background: transparent;
    color: #60789f;
    font-size: 12px;
    font-weight: 700;
    cursor: pointer;
    transition: all 0.2s ease;

    &.active {
      background: #ffffff;
      color: #1a4280;
      box-shadow: 0 8px 16px rgba(84, 109, 156, 0.16);
    }
  }
}

.user-info {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 7px 10px 7px 8px;
  border-radius: 18px;
  background: #f7faff;
  color: #23426f;
  cursor: pointer;
}

.user-copy {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.username {
  font-size: 14px;
  font-weight: 700;
}

.user-role {
  font-size: 11px;
  color: #7a91b4;
}

.content {
  flex: 1;
  min-height: 0;
  overflow: auto;
  padding: 12px 18px 18px;
}

::v-deep .el-breadcrumb {
  line-height: 1;
}

::v-deep .el-breadcrumb__inner,
::v-deep .el-breadcrumb__separator {
  color: #5c7399;
}

::v-deep .el-breadcrumb__inner.is-link:hover {
  color: #2d5ea6;
}

@media (max-width: 1200px) {
  .header {
    padding: 0 18px;
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
    margin: 12px 12px 0 96px;
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
