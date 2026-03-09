import Vue from 'vue'
import VueRouter from 'vue-router'
import MainLayout from '@/layouts/MainLayout.vue'
import store from '@/store'
import { checkRoutePermission, PERMISSIONS } from '@/utils/permission'
import { Message } from 'element-ui'
import { getUserInfo } from '@/api/auth'

Vue.use(VueRouter)

const routes = [
  {
    path: '/login',
    name: 'Login',
    component: () => import('@/views/Login.vue'),
    meta: { requiresAuth: false }
  },
  {
    path: '/',
    component: MainLayout,
    redirect: '/home',
    meta: { requiresAuth: true },
    children: [
      {
        path: 'home',
        name: 'Home',
        component: () => import('@/views/Home.vue'),
        meta: {
          title: 'menu.home',
          icon: 'el-icon-s-home',
          permission: PERMISSIONS.HOME
        }
      },
      {
        path: 'dashboard',
        name: 'Dashboard',
        component: () => import('@/views/Dashboard.vue'),
        meta: {
          title: 'menu.dashboard',
          icon: 'el-icon-data-board',
          permission: PERMISSIONS.DASHBOARD
        }
      },
      // 设备管理
      {
        path: 'device/list',
        name: 'DeviceList',
        component: () => import('@/views/device/DeviceList.vue'),
        meta: {
          title: 'menu.deviceList',
          parent: 'menu.device',
          permission: PERMISSIONS.DEVICE_MANAGEMENT
        }
      },
      {
        path: 'device/group',
        name: 'DeviceGroup',
        component: () => import('@/views/device/DeviceGroup.vue'),
        meta: {
          title: 'menu.deviceGroup',
          parent: 'menu.device',
          permission: PERMISSIONS.DEVICE_MANAGEMENT
        }
      },
      {
        path: 'device/monitor',
        name: 'RemoteMonitor',
        component: () => import('@/views/device/RemoteMonitor.vue'),
        meta: {
          title: 'menu.remoteMonitor',
          parent: 'menu.device',
          permission: PERMISSIONS.REMOTE_MONITORING
        }
      },
      // 花型管理
      {
        path: 'file/pattern',
        name: 'PatternList',
        component: () => import('@/views/file/PatternList.vue'),
        meta: {
          title: 'menu.patternList',
          parent: 'menu.file',
          permission: PERMISSIONS.FILE_MANAGEMENT
        }
      },
      {
        path: 'file/queue',
        name: 'DownloadQueue',
        component: () => import('@/views/file/DownloadQueue.vue'),
        meta: {
          title: 'menu.downloadQueue',
          parent: 'menu.file',
          permission: PERMISSIONS.FILE_MANAGEMENT
        }
      },
      {
        path: 'file/log',
        name: 'DownloadLog',
        component: () => import('@/views/file/DownloadLog.vue'),
        meta: {
          title: 'menu.downloadLog',
          parent: 'menu.file',
          permission: PERMISSIONS.FILE_MANAGEMENT
        }
      },
      // 数据统计
      {
        path: 'statistics/salary',
        name: 'SalaryStats',
        component: () => import('@/views/statistics/SalaryStats.vue'),
        meta: {
          title: 'menu.salaryStats',
          parent: 'menu.statistics',
          permission: PERMISSIONS.STATISTICS
        }
      },
      {
        path: 'statistics/process',
        name: 'ProcessOverview',
        component: () => import('@/views/statistics/ProcessOverview.vue'),
        meta: {
          title: 'menu.processOverview',
          parent: 'menu.statistics',
          permission: PERMISSIONS.STATISTICS
        }
      },
      {
        path: 'statistics/duration',
        name: 'DurationStats',
        component: () => import('@/views/statistics/DurationStats.vue'),
        meta: {
          title: 'menu.durationStats',
          parent: 'menu.statistics',
          permission: PERMISSIONS.STATISTICS
        }
      },
      {
        path: 'statistics/alarm',
        name: 'AlarmStats',
        component: () => import('@/views/statistics/AlarmStats.vue'),
        meta: {
          title: 'menu.alarmStats',
          parent: 'menu.statistics',
          permission: PERMISSIONS.STATISTICS
        }
      },
      // 员工管理
      {
        path: 'employee/list',
        name: 'EmployeeList',
        component: () => import('@/views/employee/EmployeeList.vue'),
        meta: {
          title: 'menu.employeeList',
          parent: 'menu.employee',
          permission: PERMISSIONS.EMPLOYEE_MANAGEMENT
        }
      },
      // 个人中心
      {
        path: 'profile/info',
        name: 'BasicInfo',
        component: () => import('@/views/profile/BasicInfo.vue'),
        meta: { title: 'menu.basicInfo', parent: 'menu.profile' }
      },
      {
        path: 'profile/password',
        name: 'ChangePassword',
        component: () => import('@/views/profile/ChangePassword.vue'),
        meta: { title: 'menu.changePassword', parent: 'menu.profile' }
      },
      {
        path: 'profile/language',
        name: 'LanguageSwitch',
        component: () => import('@/views/profile/LanguageSwitch.vue'),
        meta: { title: 'menu.languageSwitch', parent: 'menu.profile' }
      },
      // 服务支持
      {
        path: 'support/contact',
        name: 'Contact',
        component: () => import('@/views/support/Contact.vue'),
        meta: { title: 'menu.contact', parent: 'menu.support' }
      },
      {
        path: 'support/manual',
        name: 'Manual',
        component: () => import('@/views/support/Manual.vue'),
        meta: { title: 'menu.manual', parent: 'menu.support' }
      }
    ]
  },
  // 重定向旧路径
  { path: '/device', redirect: '/device/list' },
  { path: '/file', redirect: '/file/pattern' },
  { path: '/statistics', redirect: '/statistics/salary' },
  { path: '/employee', redirect: '/employee/list' },
  { path: '/profile', redirect: '/profile/info' },
  { path: '/support', redirect: '/support/contact' }
]

const router = new VueRouter({
  mode: 'hash',
  base: '/',
  routes
})

// Navigation guard
router.beforeEach((to, from, next) => {
  const token = localStorage.getItem('token')

  // 需要登录认证的路由
  if (to.matched.some(record => record.meta.requiresAuth !== false)) {
    if (!token) {
      // 未登录，跳转到登录页
      next({ name: 'Login' })
      return
    }

    const proceed = async () => {
      // 页面刷新后从后端拉取用户信息，确保权限与禁用状态有效
      if (!store.state.user) {
        try {
          const res = await getUserInfo()
          if (res.code === 0 && res.data) {
            store.commit('SET_USER', res.data)
          } else {
            throw new Error('获取账号信息失败')
          }
        } catch (error) {
          store.dispatch('logout')
          next({ name: 'Login' })
          return
        }
      }

      // 检查用户是否被禁用
      if (store.getters.isUserDisabled) {
        Message.error('您的账号已被禁用，请联系管理员')
        store.dispatch('logout')
        next({ name: 'Login' })
        return
      }

      // 检查路由权限
      if (!checkRoutePermission(to)) {
        Message.error('您没有权限访问该页面')
        const fallbackPath = '/profile/info'
        if (to.path !== fallbackPath) {
          next({ path: fallbackPath })
        } else {
          next(false)
        }
        return
      }

      next()
    }

    proceed()
  } else {
    next()
  }
})

export default router
