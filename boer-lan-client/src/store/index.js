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
    language: localStorage.getItem('language') || 'zh-CN'
  },

  getters: {
    isLoggedIn: state => !!state.token,
    currentUser: state => state.user,
    serverUrl: state => `http://${state.serverConfig.ip}:${state.serverConfig.port}`
  },

  mutations: {
    SET_TOKEN(state, token) {
      state.token = token
      localStorage.setItem('token', token)
    },

    SET_USER(state, user) {
      state.user = user
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
