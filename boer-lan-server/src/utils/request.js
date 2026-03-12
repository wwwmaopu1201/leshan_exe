import axios from 'axios'
import { Message } from 'element-ui'
import { invoke } from '@tauri-apps/api/core'

const defaultHost = '127.0.0.1'
const defaultPort = 8088

function buildBaseURL(port = defaultPort) {
  return `http://${defaultHost}:${port}/api`
}

const request = axios.create({
  baseURL: import.meta.env.DEV ? '/api' : buildBaseURL(),
  timeout: 10000
})

function normalizePort(value) {
  const port = Number(value)
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    throw new Error(`invalid backend port: ${value}`)
  }
  return port
}

export async function initRequestBaseURL(retries = 40) {
  if (import.meta.env.DEV) {
    request.defaults.baseURL = '/api'
    return request.defaults.baseURL
  }

  for (let i = 0; i < retries; i++) {
    try {
      const port = normalizePort(await invoke('get_backend_port'))
      request.defaults.baseURL = buildBaseURL(port)
      return request.defaults.baseURL
    } catch (error) {
      console.log(`Waiting for backend port... (${i + 1}/${retries})`, error)
      await new Promise(resolve => setTimeout(resolve, 500))
    }
  }

  request.defaults.baseURL = buildBaseURL()
  return request.defaults.baseURL
}

export function getRequestBaseURL() {
  return request.defaults.baseURL
}

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

    if (!error.config?.suppressErrorMessage) {
      Message.error(error.response?.data?.error || '请求失败')
    }

    return Promise.reject(error)
  }
)

export default request
