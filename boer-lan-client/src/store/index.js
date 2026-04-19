import Vue from 'vue'
import Vuex from 'vuex'

Vue.use(Vuex)

const DEFAULT_PERMISSIONS = {
  home: true,
  dashboard: true,
  employeeManagement: true,
  fileManagement: true,
  remoteMonitoring: true,
  statistics: true,
  deviceManagement: true
}

function getDefaultPermissions() {
  return { ...DEFAULT_PERMISSIONS }
}

function parsePermissions(rawPermissions) {
  if (!rawPermissions) {
    return null
  }

  try {
    const permissions = typeof rawPermissions === 'string'
      ? JSON.parse(rawPermissions)
      : rawPermissions

    if (Array.isArray(permissions)) {
      return permissions.reduce((acc, key) => {
        if (typeof key === 'string' && key.trim()) {
          acc[key.trim()] = true
        }
        return acc
      }, {})
    }

    if (permissions && typeof permissions === 'object') {
      return Object.keys(permissions).reduce((acc, key) => {
        if (permissions[key] === true) {
          acc[key] = true
        }
        return acc
      }, {})
    }
  } catch (error) {
    console.error('解析用户权限失败:', error)
  }

  return null
}

function normalizePermissions(rawPermissions) {
  const parsedPermissions = parsePermissions(rawPermissions)
  if (!parsedPermissions) {
    return getDefaultPermissions()
  }

  const normalizedPermissions = {
    ...parsedPermissions
  }

  normalizedPermissions.home = parsedPermissions.home === true
  normalizedPermissions.dashboard = parsedPermissions.dashboard === true
  normalizedPermissions.employeeManagement = parsedPermissions.employeeManagement === true
  normalizedPermissions.deviceManagement =
    parsedPermissions.deviceManagement === true ||
    parsedPermissions.deviceInfo === true
  normalizedPermissions.remoteMonitoring =
    parsedPermissions.remoteMonitoring === true ||
    parsedPermissions.deviceManagement === true ||
    parsedPermissions.deviceInfo === true
  normalizedPermissions.fileManagement =
    parsedPermissions.fileManagement === true ||
    parsedPermissions.patternFiles === true ||
    parsedPermissions.devicePatternFiles === true ||
    parsedPermissions.downloadLog === true
  normalizedPermissions.statistics =
    parsedPermissions.statistics === true ||
    parsedPermissions.salaryStatistics === true ||
    parsedPermissions.statusStatistics === true

  return normalizedPermissions
}

export default new Vuex.Store({
  state: {
    // User info
    user: null,
    token: localStorage.getItem('token') || '',

    // Server connection
    serverConfig: {
      ip: localStorage.getItem('serverIp') || '',
      port: '8088'
    },

    // Device tree
    deviceTree: [],
    selectedDevice: null,

    // Sidebar
    sidebarCollapsed: false,

    // Language
    language: localStorage.getItem('language') || 'zh-CN',

    // Permissions cache
    permissionsCache: null
  },

  getters: {
    isLoggedIn: state => !!state.token,
    currentUser: state => state.user,
    serverUrl: state => `http://${state.serverConfig.ip}:${state.serverConfig.port}`,

    // 用户权限对象
    userPermissions: state => {
      if (state.permissionsCache) {
        return state.permissionsCache
      }

      if (!state.user || !state.user.permissions) {
        return getDefaultPermissions()
      }

      return normalizePermissions(state.user.permissions)
    },

    // 检查是否有某个权限
    hasPermission: (state, getters) => (permissionKey) => {
      const permissions = getters.userPermissions
      return permissions[permissionKey] === true
    },

    // 用户是否被禁用
    isUserDisabled: state => {
      return state.user?.disabled === true
    },

    // 是否是管理员
    isAdmin: state => {
      return state.user?.role === 'admin'
    }
  },

  mutations: {
    SET_TOKEN(state, token) {
      state.token = token
      localStorage.setItem('token', token)
    },

    SET_USER(state, user) {
      state.user = user
      state.permissionsCache = normalizePermissions(user?.permissions)
    },

    SET_PERMISSIONS_CACHE(state, permissions) {
      state.permissionsCache = permissions
    },

    SET_SERVER_CONFIG(state, config) {
      const normalizedConfig = {
        ip: config.ip,
        port: '8088'
      }
      state.serverConfig = normalizedConfig
      localStorage.setItem('serverIp', normalizedConfig.ip)
      localStorage.setItem('serverPort', normalizedConfig.port)
    },

    SET_DEVICE_TREE(state, tree) {
      state.deviceTree = tree
    },

    SET_SELECTED_DEVICE(state, device) {
      state.selectedDevice = device
    },

    TOGGLE_SIDEBAR(state) {
      state.sidebarCollapsed = !state.sidebarCollapsed
    },

    SET_LANGUAGE(state, lang) {
      state.language = lang
      localStorage.setItem('language', lang)
    },

    LOGOUT(state) {
      state.token = ''
      state.user = null
      state.permissionsCache = null
      localStorage.removeItem('token')
    }
  },

  actions: {
    login({ commit }, { token, user }) {
      commit('SET_TOKEN', token)
      commit('SET_USER', user)
    },

    logout({ commit }) {
      commit('LOGOUT')
    },

    updateServerConfig({ commit }, config) {
      commit('SET_SERVER_CONFIG', config)
    },

    selectDevice({ commit }, device) {
      commit('SET_SELECTED_DEVICE', device)
    },

    setLanguage({ commit }, lang) {
      commit('SET_LANGUAGE', lang)
    }
  }
})
