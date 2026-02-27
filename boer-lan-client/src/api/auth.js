import request from './request'
import { mockLogin } from './mock'

// 是否使用Mock数据（开发阶段可设为true，生产环境设为false）
const USE_MOCK = false

export function login(data) {
  if (USE_MOCK) {
    return mockLogin(data.username, data.password)
  }
  return request({
    url: '/auth/login',
    method: 'post',
    data
  })
}

export function logout() {
  if (USE_MOCK) {
    return Promise.resolve({ code: 0, message: 'success' })
  }
  return request({
    url: '/auth/logout',
    method: 'post'
  })
}

export function getUserInfo() {
  if (USE_MOCK) {
    return Promise.resolve({
      code: 0,
      data: {
        id: 1,
        username: 'admin',
        nickname: '管理员',
        role: 'admin'
      },
      message: 'success'
    })
  }
  return request({
    url: '/auth/userinfo',
    method: 'get'
  })
}

// 修改密码
export function changePassword(data) {
  return request({
    url: '/auth/password',
    method: 'put',
    data
  })
}

// 更新用户资料
export function updateProfile(data) {
  return request({
    url: '/auth/profile',
    method: 'put',
    data
  })
}

// 获取登录记录
export function getLoginLogs() {
  return request({
    url: '/auth/login-logs',
    method: 'get'
  })
}
