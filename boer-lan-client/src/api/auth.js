import request from './request'
import { getVersion } from '@tauri-apps/api/app'
import { isTauri } from '@tauri-apps/api/core'
import clientPackage from '../../package.json'

const CLIENT_VERSION = clientPackage.version || ''
let resolvedClientVersion = ''

async function getClientVersion() {
  if (resolvedClientVersion) {
    return resolvedClientVersion
  }
  if (isTauri()) {
    try {
      resolvedClientVersion = await getVersion()
      return resolvedClientVersion
    } catch (error) {
      console.warn('Failed to read Tauri app version:', error)
    }
  }
  resolvedClientVersion = CLIENT_VERSION
  return resolvedClientVersion
}

export async function login(data) {
  const clientVersion = await getClientVersion()
  return request({
    url: '/auth/login',
    method: 'post',
    data: {
      ...(data || {}),
      clientVersion
    },
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

// 上传头像
export function uploadAvatar(formData) {
  return request({
    url: '/auth/avatar',
    method: 'post',
    data: formData,
    headers: {
      'Content-Type': 'multipart/form-data'
    }
  })
}

// 获取登录记录
export function getLoginLogs() {
  return request({
    url: '/auth/login-logs',
    method: 'get'
  })
}
