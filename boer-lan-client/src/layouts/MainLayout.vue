<template>
  <div class="main-layout">
    <header class="topbar">
      <div class="topbar-left">
        <img src="@/assets/images/logo.png" alt="Logo" class="topbar-logo" />
        <span class="topbar-title">局域网管理软件客户端</span>
      </div>
      <div class="topbar-right">
        <div class="server-tag" v-if="serverAddress">
          <i class="el-icon-link"></i>
          <span>{{ serverAddress }}</span>
        </div>
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
            <el-avatar class="topbar-avatar" :size="24" :src="avatarSrc" fit="cover"></el-avatar>
            <div class="user-copy">
              <span class="username">{{ user?.username || 'Admin' }}</span>
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

    <div class="shell-body">
      <aside class="sidebar">
        <el-menu
          :default-active="activeMenu"
          class="sidebar-menu"
          background-color="transparent"
          text-color="#4c5768"
          active-text-color="#3388ff"
          :collapse="false"
          :unique-opened="true"
          router
        >
          <el-menu-item v-if="canAccess.home" index="/home">
            <i class="menu-icon el-icon-s-home"></i>
            <span slot="title">{{ $t('menu.home') }}</span>
          </el-menu-item>

          <el-menu-item v-if="canAccess.dashboard" index="/dashboard">
            <i class="menu-icon el-icon-data-board"></i>
            <span slot="title">{{ $t('menu.dashboard') }}</span>
          </el-menu-item>

          <el-submenu v-if="canAccess.deviceSection" index="/device">
            <template slot="title">
              <i class="menu-icon el-icon-monitor"></i>
              <span>{{ $t('menu.device') }}</span>
            </template>
            <el-menu-item v-if="canAccess.deviceManagement" index="/device/list">
              {{ $t('menu.deviceList') }}
            </el-menu-item>
            <el-menu-item v-if="canAccess.deviceManagement" index="/device/types">
              {{ $t('menu.deviceTypeManager') }}
            </el-menu-item>
            <el-menu-item v-if="canAccess.remoteMonitoring" index="/device/monitor">
              {{ $t('menu.remoteMonitor') }}
            </el-menu-item>
          </el-submenu>

          <el-submenu v-if="canAccess.fileManagement" index="/file">
            <template slot="title">
              <i class="menu-icon el-icon-folder"></i>
              <span>{{ $t('menu.file') }}</span>
            </template>
            <el-menu-item index="/file/pattern">{{ $t('menu.patternList') }}</el-menu-item>
            <el-menu-item index="/file/pattern-types">{{ $t('menu.patternTypeManager') }}</el-menu-item>
            <el-menu-item index="/file/order-nos">{{ $t('menu.orderNoManager') }}</el-menu-item>
            <el-menu-item index="/file/queue">{{ $t('menu.downloadQueue') }}</el-menu-item>
            <el-menu-item index="/file/log">{{ $t('menu.downloadLog') }}</el-menu-item>
          </el-submenu>

          <el-submenu v-if="canAccess.statistics" index="/statistics">
            <template slot="title">
              <i class="menu-icon el-icon-s-data"></i>
              <span>{{ $t('menu.statistics') }}</span>
            </template>
            <el-menu-item index="/statistics/salary">{{ $t('menu.salaryStats') }}</el-menu-item>
            <el-menu-item index="/statistics/process">{{ $t('menu.processOverview') }}</el-menu-item>
            <el-menu-item index="/statistics/duration">{{ $t('menu.durationStats') }}</el-menu-item>
            <el-menu-item index="/statistics/alarm">{{ $t('menu.alarmStats') }}</el-menu-item>
          </el-submenu>

          <el-submenu v-if="canAccess.employeeManagement" index="/employee">
            <template slot="title">
              <i class="menu-icon el-icon-user"></i>
              <span>{{ $t('menu.employee') }}</span>
            </template>
            <el-menu-item index="/employee/list">{{ $t('menu.employeeList') }}</el-menu-item>
          </el-submenu>

          <el-submenu index="/profile">
            <template slot="title">
              <i class="menu-icon el-icon-user-solid"></i>
              <span>{{ $t('menu.profile') }}</span>
            </template>
            <el-menu-item index="/profile/info">{{ $t('menu.basicInfo') }}</el-menu-item>
            <el-menu-item index="/profile/password">{{ $t('menu.changePassword') }}</el-menu-item>
          </el-submenu>

          <el-submenu index="/support">
            <template slot="title">
              <i class="menu-icon el-icon-service"></i>
              <span>{{ $t('menu.support') }}</span>
            </template>
            <el-menu-item index="/support/contact">{{ $t('menu.contact') }}</el-menu-item>
            <el-menu-item index="/support/manual">{{ $t('menu.manual') }}</el-menu-item>
          </el-submenu>
        </el-menu>
      </aside>

      <div class="main-container">
        <main class="content">
          <router-view />
        </main>
      </div>
    </div>
  </div>
</template>

<script>
import defaultAvatar from '@/assets/images/default-avatar.svg'
import { mapState, mapActions } from 'vuex'

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
    ...mapState(['user', 'language', 'serverConfig']),
    avatarSrc() {
      const avatar = String(this.user?.avatar || '').trim()
      if (!avatar) {
        return defaultAvatar
      }
      if (avatar.startsWith('http://') || avatar.startsWith('https://') || avatar.startsWith('data:')) {
        return avatar
      }
      if (!this.serverAddress) {
        return defaultAvatar
      }
      return `http://${this.serverAddress}${avatar}?v=${encodeURIComponent(avatar)}`
    },
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
    activeMenu() {
      return this.$route.path
    },
    currentLang() {
      return this.language || 'zh-CN'
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
    ...mapActions(['logout', 'setLanguage']),
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
  height: 100%;
  min-height: 768px;
  display: flex;
  flex-direction: column;
  background: #eef2f6;
}

.topbar {
  height: 46px;
  padding: 0 12px;
  background: linear-gradient(180deg, #338cf9 0%, #287de8 100%);
  color: #ffffff;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  box-shadow: inset 0 -1px 0 rgba(0, 0, 0, 0.08);
}

.topbar-left,
.topbar-right {
  display: flex;
  align-items: center;
  gap: 10px;
  min-width: 0;
}

.topbar-logo {
  width: 18px;
  height: 18px;
}

.topbar-title {
  font-size: 13px;
  font-weight: 700;
  letter-spacing: 0.02em;
}

.shell-body {
  flex: 1;
  min-height: 0;
  min-width: 0;
  display: flex;
}

.sidebar {
  width: 206px;
  flex-shrink: 0;
  background: #ffffff;
  border-right: 1px solid #dbe3ec;
  transition: width 0.28s ease;
  overflow: hidden;

  &.collapsed {
    width: 58px;
  }
}

.sidebar-menu {
  height: 100%;
  border: none;
  overflow-y: auto;
  padding: 8px 0;

  &:not(.el-menu--collapse) {
    width: 100%;
  }

  ::v-deep .el-submenu__title,
  ::v-deep .el-menu-item {
    height: 38px;
    line-height: 38px;
    margin: 0 8px 4px;
    border-radius: 2px;
    padding-left: 12px !important;
    color: #4d596a !important;
    font-size: 12px;
    transition: background-color 0.2s ease;
  }

  ::v-deep .el-submenu__title:hover,
  ::v-deep .el-menu-item:hover {
    background: #edf5ff !important;
    color: #3388ff !important;
  }

  ::v-deep .el-menu-item.is-active {
    background: #e8f2ff !important;
    color: #3388ff !important;
  }

  ::v-deep .el-submenu.is-opened > .el-submenu__title {
    background: transparent !important;
  }

  ::v-deep .el-menu--inline {
    background: #ffffff !important;
  }

  ::v-deep .el-menu--inline .el-menu-item {
    height: 34px;
    line-height: 34px;
    margin: 0 8px 3px 18px;
    padding-left: 20px !important;
    border-radius: 2px;
    background: transparent;
  }

  ::v-deep .el-menu--collapse .el-submenu__title,
  ::v-deep .el-menu--collapse .el-menu-item {
    padding: 0 !important;
    justify-content: center;
  }

  ::v-deep .el-menu--collapse .menu-icon {
    margin-right: 0;
  }
}

.menu-icon {
  width: 18px;
  height: 18px;
  margin-right: 8px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  color: currentColor;
  font-size: 14px;
  vertical-align: middle;
}

.main-container {
  flex: 1;
  display: flex;
  flex-direction: column;
  min-width: 0;
}

.server-tag {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  min-height: 26px;
  padding: 0 10px;
  border-radius: 2px;
  background: rgba(255, 255, 255, 0.14);
  color: #ffffff;
  font-size: 12px;
}

.lang-switch {
  display: inline-flex;
  padding: 2px;
  background: rgba(255, 255, 255, 0.14);
  border-radius: 2px;

  button {
    min-width: 38px;
    height: 22px;
    border: none;
    border-radius: 2px;
    background: transparent;
    color: rgba(255, 255, 255, 0.86);
    font-size: 12px;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.2s ease;

    &.active {
      background: rgba(255, 255, 255, 0.24);
      color: #ffffff;
    }
  }
}

.user-info {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 2px 8px 2px 4px;
  border-radius: 2px;
  background: rgba(255, 255, 255, 0.14);
  color: #ffffff;
  cursor: pointer;
}

.topbar-avatar {
  flex-shrink: 0;
  background: #eef3f8;
}

.topbar-avatar ::v-deep img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  object-position: center;
  display: block;
}

.user-copy {
  display: flex;
  align-items: center;
}

.content {
  flex: 1;
  min-height: 0;
  overflow: auto;
  padding: 8px;
}

</style>
