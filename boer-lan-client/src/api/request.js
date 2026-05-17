import axios from 'axios'
import store from '@/store'
import router from '@/router'
import { Message } from 'element-ui'
import { translateApiMessage } from '@/utils/i18n-normalizer'

// 创建axios实例
const service = axios.create({
  timeout: 2 * 60 * 1000
})

function resolveErrorMessage(error) {
  const responseData = error?.response?.data
  const responseMessage = responseData?.message || responseData?.error
  if (typeof responseMessage === 'string' && responseMessage.trim()) {
    return translateApiMessage(responseMessage.trim())
  }

  if (error?.message && !String(error.message).startsWith('Request failed with status code')) {
    if (error.message.includes('timeout')) {
      return translateApiMessage('请求超时')
    }
    if (error.message.includes('Network Error')) {
      return translateApiMessage('网络连接失败')
    }
  }

  if (error?.response) {
    switch (error.response.status) {
      case 400:
        return translateApiMessage('请求参数错误')
      case 401:
        return translateApiMessage(store.state.token ? '登录已过期，请重新登录' : '账号或密码错误')
      case 403:
        return translateApiMessage('没有权限访问')
      case 404:
        return translateApiMessage('请求的资源不存在')
      case 500:
        return translateApiMessage('服务器错误')
      default:
        return translateApiMessage('请求失败')
    }
  }

  return translateApiMessage('请求失败')
}

// 请求拦截器
service.interceptors.request.use(
  config => {
    // 动态设置baseURL
    const serverUrl = store.getters.serverUrl
    if (serverUrl && serverUrl !== 'http://:') {
      config.baseURL = serverUrl + '/api'
    }

    // 添加token
    const token = store.state.token
    if (token) {
      config.headers['Authorization'] = `Bearer ${token}`
    }
    config.headers['Accept-Language'] = store.state.language || localStorage.getItem('language') || 'zh-CN'

    return config
  },
  error => {
    console.error('Request error:', error)
    return Promise.reject(error)
  }
)

// 响应拦截器
service.interceptors.response.use(
  response => {
    if (response.config?.responseType === 'blob' || response.config?.responseType === 'arraybuffer') {
      return response
    }

    const res = response.data

    // 假设后端返回格式为 { code: 0, data: {}, message: '' }
    if (res.code !== 0 && res.code !== 200) {
      const message = translateApiMessage(res.message || res.error || '请求失败')
      const businessError = new Error(message)
      businessError.userMessage = message
      businessError.response = response
      businessError.config = response.config

      if (!response.config?.suppressErrorMessage) {
        Message.error(message)
      }

      if (res.code === 401 && store.state.token) {
        store.dispatch('logout')
        router.push('/login')
      }

      return Promise.reject(businessError)
    }

    // 检查用户是否被禁用（仅在登录后检查）
    if (store.state.token && store.getters.isUserDisabled) {
      Message.error(translateApiMessage('您的账号已被禁用，请联系管理员'))
      store.dispatch('logout')
      router.push('/login')
      return Promise.reject(new Error(translateApiMessage('账号已被禁用')))
    }

    return res
  },
  error => {
    console.error('Response error:', error)

    const message = resolveErrorMessage(error)
    error.userMessage = message

    if (error.response?.status === 401 && store.state.token) {
      store.dispatch('logout')
      router.push('/login')
    }

    if (!error.config?.suppressErrorMessage) {
      Message.error(message)
    }

    return Promise.reject(error)
  }
)

export default service
