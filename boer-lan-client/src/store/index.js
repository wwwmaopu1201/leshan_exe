import Vue from 'vue'
import Vuex from 'vuex'

Vue.use(Vuex)

export default new Vuex.Store({
  state: {
    // User info
    user: null,
    token: localStorage.getItem('token') || '',

    // Server connection
    serverConfig: {
      ip: localStorage.getItem('serverIp') || '',
      port: localStorage.getItem('serverPort') || '8088'
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
      // 使用缓存避免重复解析
      if (state.permissionsCache) {
        return state.permissionsCache
      }

      if (!state.user || !state.user.permissions) {
        return {
          home: true,
          dashboard: true,
          employeeManagement: true,
          fileManagement: true,
          remoteMonitoring: true,
          statistics: true,
          deviceManagement: true
        }
      }

      try {
        const permissions = typeof state.user.permissions === 'string'
          ? JSON.parse(state.user.permissions)
          : state.user.permissions
        if (Array.isArray(permissions)) {
          return permissions.reduce((acc, key) => {
            acc[key] = true
            return acc
          }, {})
        }
        return permissions
      } catch (error) {
        console.error('解析用户权限失败:', error)
        return {
          home: true,
          dashboard: true,
          employeeManagement: true,
          fileManagement: true,
          remoteMonitoring: true,
          statistics: true,
          deviceManagement: true
        }
      }
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
      // 清除权限缓存，强制重新解析
      state.permissionsCache = null
    },

    SET_PERMISSIONS_CACHE(state, permissions) {
      state.permissionsCache = permissions
    },

    SET_SERVER_CONFIG(state, config) {
      state.serverConfig = config
      localStorage.setItem('serverIp', config.ip)
      localStorage.setItem('serverPort', config.port)
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
