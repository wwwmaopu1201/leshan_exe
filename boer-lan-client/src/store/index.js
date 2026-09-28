import Vue from 'vue'
import Vuex from 'vuex'
import { normalizeServerVersion } from '@/utils/server-version'

Vue.use(Vuex)

const DEFAULT_SERVER_PORT = '8088'

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
        acc[key] = permissions[key] === true
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

  const normalizedPermissions = { ...parsedPermissions }
  const children = {
    deviceManagement: ['deviceInfo', 'remoteMonitoring'],
    fileManagement: ['patternFiles', 'devicePatternFiles', 'downloadLog'],
    statistics: ['salaryStatistics', 'statusStatistics']
  }
  Object.entries(children).forEach(([parent, keys]) => {
    const detailed = keys.some(key => {
      if (parent === 'deviceManagement' && key === 'remoteMonitoring' && parsedPermissions[key] === true && parsedPermissions[parent] === true && !Object.prototype.hasOwnProperty.call(parsedPermissions, 'deviceInfo')) return false
      return Object.prototype.hasOwnProperty.call(parsedPermissions, key)
    })
    keys.forEach(key => { normalizedPermissions[key] = detailed ? parsedPermissions[key] === true : parsedPermissions[parent] === true })
    normalizedPermissions[parent] = keys.some(key => normalizedPermissions[key])
  })

  return normalizedPermissions
}

function normalizeServerPort(value) {
  const port = String(value || '').trim()
  const portNumber = Number(port)
  if (!/^\d+$/.test(port) || !Number.isInteger(portNumber) || portNumber < 1 || portNumber > 65535) {
    return DEFAULT_SERVER_PORT
  }
  return port
}

export default new Vuex.Store({
  state: {
    // User info
    user: null,
    token: sessionStorage.getItem('token') || '',

    // Server connection
    serverConfig: {
      ip: localStorage.getItem('serverIp') || '',
      port: normalizeServerPort(localStorage.getItem('serverPort') || DEFAULT_SERVER_PORT)
    },
    serverVersion: '',

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
      if (getters.isAdmin) return true
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
      sessionStorage.setItem('token', token)
    },

    SET_USER(state, user) {
      state.user = user
      state.permissionsCache = normalizePermissions(user?.permissions)
    },

    SET_SERVER_VERSION(state, version) {
      state.serverVersion = normalizeServerVersion(version)
    },

    SET_PERMISSIONS_CACHE(state, permissions) {
      state.permissionsCache = permissions
    },

    SET_SERVER_CONFIG(state, config) {
      const previousServer = `${state.serverConfig?.ip || ''}:${state.serverConfig?.port || ''}`
      const normalizedConfig = {
        ip: String(config.ip || '').trim(),
        port: normalizeServerPort(config.port || DEFAULT_SERVER_PORT)
      }
      state.serverConfig = normalizedConfig
      const nextServer = `${normalizedConfig.ip}:${normalizedConfig.port}`
      if (previousServer !== nextServer) {
        state.serverVersion = ''
      }
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
      state.serverVersion = ''
      state.permissionsCache = null
      sessionStorage.removeItem('token')
    }
  },

  actions: {
    login({ commit }, { token, user, serverVersion }) {
      commit('SET_TOKEN', token)
      commit('SET_USER', user)
      commit('SET_SERVER_VERSION', serverVersion)
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
