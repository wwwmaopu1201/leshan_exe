import request from './request'

export function login(data) {
  return request({
    url: '/auth/login',
    method: 'post',
    data,
    suppressErrorMessage: true
  })
}

export function logout() {
  return request({
    url: '/auth/logout',
    method: 'post'
  })
}

export function getUserInfo() {
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
