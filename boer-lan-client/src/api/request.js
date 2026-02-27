import axios from 'axios'
import store from '@/store'
import router from '@/router'
import { Message } from 'element-ui'

// 创建axios实例
const service = axios.create({
  timeout: 30000
})

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
    const res = response.data

    // 假设后端返回格式为 { code: 0, data: {}, message: '' }
    if (res.code !== 0 && res.code !== 200) {
      Message.error(res.message || '请求失败')

      // Token过期或无效
      if (res.code === 401) {
        store.dispatch('logout')
        router.push('/login')
      }

      return Promise.reject(new Error(res.message || '请求失败'))
    }

    return res
  },
  error => {
    console.error('Response error:', error)

    if (error.response) {
      switch (error.response.status) {
        case 401:
          Message.error('登录已过期，请重新登录')
          store.dispatch('logout')
          router.push('/login')
          break
        case 403:
          Message.error('没有权限访问')
          break
        case 404:
          Message.error('请求的资源不存在')
          break
        case 500:
          Message.error('服务器错误')
          break
        default:
          Message.error(error.response.data?.message || '请求失败')
      }
    } else if (error.message.includes('timeout')) {
      Message.error('请求超时')
    } else if (error.message.includes('Network Error')) {
      Message.error('网络连接失败')
    } else {
      Message.error(error.message || '请求失败')
    }

    return Promise.reject(error)
  }
)

export default service
