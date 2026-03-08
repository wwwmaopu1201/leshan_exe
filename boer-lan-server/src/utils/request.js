import axios from 'axios'
import { Message } from 'element-ui'

const baseURL = process.env.NODE_ENV === 'development' ? '/api' : 'http://localhost:8088/api'

const request = axios.create({
  baseURL,
  timeout: 10000
})

request.interceptors.request.use(config => {
  const token = localStorage.getItem('token')
  if (token) {
    config.headers.Authorization = `Bearer ${token}`
  }
  return config
})

request.interceptors.response.use(
  response => response.data,
  error => {
    if (error.response?.status === 401) {
      localStorage.removeItem('token')
      window.location.reload()
    }
    Message.error(error.response?.data?.error || '请求失败')
    return Promise.reject(error)
  }
)

export default request
