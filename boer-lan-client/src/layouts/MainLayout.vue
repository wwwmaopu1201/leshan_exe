<template>
  <div class="main-layout">
    <!-- 侧边栏 -->
    <div class="sidebar" :class="{ collapsed: isCollapsed }">
      <div class="logo">
        <img src="@/assets/images/logo.png" alt="Logo" class="logo-img" v-if="!isCollapsed" />
        <span class="logo-text" v-if="!isCollapsed">博尔管理系统</span>
      </div>
      <el-menu
        :default-active="activeMenu"
        class="sidebar-menu"
        background-color="transparent"
        text-color="#fff"
        active-text-color="#fff"
        :collapse="isCollapsed"
        :unique-opened="true"
        router
      >
        <el-menu-item index="/home">
          <i class="el-icon-s-home"></i>
          <span slot="title">{{ $t('menu.home') }}</span>
        </el-menu-item>

        <el-menu-item index="/dashboard">
          <i class="el-icon-data-board"></i>
          <span slot="title">{{ $t('menu.dashboard') }}</span>
        </el-menu-item>

        <el-submenu index="/device">
          <template slot="title">
            <i class="el-icon-monitor"></i>
            <span>{{ $t('menu.device') }}</span>
          </template>
          <el-menu-item index="/device/list">{{ $t('menu.deviceList') }}</el-menu-item>
          <el-menu-item index="/device/group">{{ $t('menu.deviceGroup') }}</el-menu-item>
          <el-menu-item index="/device/monitor">{{ $t('menu.remoteMonitor') }}</el-menu-item>
        </el-submenu>

        <el-submenu index="/file">
          <template slot="title">
            <i class="el-icon-folder"></i>
            <span>{{ $t('menu.file') }}</span>
          </template>
          <el-menu-item index="/file/pattern">{{ $t('menu.patternList') }}</el-menu-item>
          <el-menu-item index="/file/queue">{{ $t('menu.downloadQueue') }}</el-menu-item>
          <el-menu-item index="/file/log">{{ $t('menu.downloadLog') }}</el-menu-item>
        </el-submenu>

        <el-submenu index="/statistics">
          <template slot="title">
            <i class="el-icon-s-data"></i>
            <span>{{ $t('menu.statistics') }}</span>
          </template>
          <el-menu-item index="/statistics/salary">{{ $t('menu.salaryStats') }}</el-menu-item>
          <el-menu-item index="/statistics/process">{{ $t('menu.processOverview') }}</el-menu-item>
          <el-menu-item index="/statistics/duration">{{ $t('menu.durationStats') }}</el-menu-item>
          <el-menu-item index="/statistics/alarm">{{ $t('menu.alarmStats') }}</el-menu-item>
        </el-submenu>

        <el-submenu index="/employee">
          <template slot="title">
            <i class="el-icon-user"></i>
            <span>{{ $t('menu.employee') }}</span>
          </template>
          <el-menu-item index="/employee/list">{{ $t('menu.employeeList') }}</el-menu-item>
        </el-submenu>

        <el-submenu index="/profile">
          <template slot="title">
            <i class="el-icon-user-solid"></i>
            <span>{{ $t('menu.profile') }}</span>
          </template>
          <el-menu-item index="/profile/info">{{ $t('menu.basicInfo') }}</el-menu-item>
          <el-menu-item index="/profile/password">{{ $t('menu.changePassword') }}</el-menu-item>
          <el-menu-item index="/profile/language">{{ $t('menu.languageSwitch') }}</el-menu-item>
        </el-submenu>

        <el-submenu index="/support">
          <template slot="title">
            <i class="el-icon-service"></i>
            <span>{{ $t('menu.support') }}</span>
          </template>
          <el-menu-item index="/support/contact">{{ $t('menu.contact') }}</el-menu-item>
          <el-menu-item index="/support/manual">{{ $t('menu.manual') }}</el-menu-item>
        </el-submenu>
      </el-menu>
    </div>

    <!-- 主内容区 -->
    <div class="main-container">
      <!-- 顶部导航栏 -->
      <div class="header">
        <div class="header-left">
          <i
            :class="isCollapsed ? 'el-icon-s-unfold' : 'el-icon-s-fold'"
            class="collapse-btn"
            @click="toggleSidebar"
          ></i>
          <el-breadcrumb separator="/">
            <el-breadcrumb-item
              v-for="(item, index) in breadcrumbs"
              :key="index"
              :to="item.path"
            >
              {{ item.title }}
            </el-breadcrumb-item>
          </el-breadcrumb>
        </div>
        <div class="header-right">
          <el-dropdown trigger="click" @command="handleCommand">
            <div class="user-info">
              <el-avatar :size="32" icon="el-icon-user-solid"></el-avatar>
              <span class="username">{{ user?.username || 'Admin' }}</span>
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
      </div>

      <!-- 内容区 -->
      <div class="content">
        <router-view />
      </div>
    </div>
  </div>
</template>

<script>
import { mapState, mapMutations, mapActions } from 'vuex'

export default {
  name: 'MainLayout',
  computed: {
    ...mapState(['user', 'sidebarCollapsed']),
    isCollapsed() {
      return this.sidebarCollapsed
    },
    activeMenu() {
      return this.$route.path
    },
    breadcrumbs() {
      const matched = this.$route.matched.filter(item => item.meta && item.meta.title)
      return matched.map(item => ({
        path: item.path,
        title: this.$t(item.meta.title)
      }))
    }
  },
  methods: {
    ...mapMutations(['TOGGLE_SIDEBAR']),
    ...mapActions(['logout']),
    toggleSidebar() {
      this.TOGGLE_SIDEBAR()
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
}

.sidebar {
  width: 220px;
  height: 100%;
  background: linear-gradient(180deg, #1e3c72 0%, #2a5298 100%);
  transition: width 0.3s;
  overflow: hidden;

  &.collapsed {
    width: 64px;

    .logo {
      padding: 20px 0;
      justify-content: center;
    }
  }

  .logo {
    height: 60px;
    display: flex;
    align-items: center;
    padding: 0 20px;
    border-bottom: 1px solid rgba(255, 255, 255, 0.1);

    .logo-img {
      width: 32px;
      height: 32px;
      margin-right: 10px;
    }

    .logo-text {
      color: #fff;
      font-size: 16px;
      font-weight: bold;
      white-space: nowrap;
    }
  }

  .sidebar-menu {
    height: calc(100% - 60px);
    border: none;
    overflow-y: auto;

    &:not(.el-menu--collapse) {
      width: 220px;
    }
  }
}

.main-container {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.header {
  height: 60px;
  background-color: #3b9dfc;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 20px;
  box-shadow: 0 1px 4px rgba(0, 0, 0, 0.1);

  .header-left {
    display: flex;
    align-items: center;

    .collapse-btn {
      font-size: 20px;
      color: #fff;
      cursor: pointer;
      margin-right: 20px;

      &:hover {
        opacity: 0.8;
      }
    }

    .el-breadcrumb {
      ::v-deep .el-breadcrumb__inner,
      ::v-deep .el-breadcrumb__separator {
        color: rgba(255, 255, 255, 0.8);
      }

      ::v-deep .el-breadcrumb__inner.is-link:hover {
        color: #fff;
      }
    }
  }

  .header-right {
    .user-info {
      display: flex;
      align-items: center;
      cursor: pointer;
      color: #fff;

      .username {
        margin: 0 8px;
        font-size: 14px;
      }

      &:hover {
        opacity: 0.9;
      }
    }
  }
}

.content {
  flex: 1;
  overflow: auto;
  background-color: #f5f7fa;
}
</style>
