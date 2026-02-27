import request from './request'
import {
  mockGetHomeStats,
  mockGetDashboardData,
  mockGetSalaryStats,
  mockGetProcessOverview,
  mockGetDurationStats,
  mockGetAlarmStats
} from './mock'

const USE_MOCK = false

// 获取首页统计数据
export function getHomeStats() {
  if (USE_MOCK) {
    return mockGetHomeStats()
  }
  return request({
    url: '/statistics/home',
    method: 'get'
  })
}

// 获取仪表盘数据
export function getDashboardData(deviceId) {
  if (USE_MOCK) {
    return mockGetDashboardData(deviceId)
  }
  return request({
    url: '/statistics/dashboard',
    method: 'get',
    params: { deviceId }
  })
}

// 获取工资统计
export function getSalaryStats(params) {
  if (USE_MOCK) {
    return mockGetSalaryStats(params)
  }
  return request({
    url: '/statistics/salary',
    method: 'get',
    params
  })
}

// 获取工资详情
export function getSalaryDetail(params) {
  if (USE_MOCK) {
    return mockGetSalaryStats(params)
  }
  return request({
    url: '/statistics/salary/detail',
    method: 'get',
    params
  })
}

// 获取加工概况
export function getProcessOverview(params) {
  if (USE_MOCK) {
    return mockGetProcessOverview(params)
  }
  return request({
    url: '/statistics/process',
    method: 'get',
    params
  })
}

// 获取时长统计
export function getDurationStats(params) {
  if (USE_MOCK) {
    return mockGetDurationStats(params)
  }
  return request({
    url: '/statistics/duration',
    method: 'get',
    params
  })
}

// 获取报警统计
export function getAlarmStats(params) {
  if (USE_MOCK) {
    return mockGetAlarmStats(params)
  }
  return request({
    url: '/statistics/alarm',
    method: 'get',
    params
  })
}

// 导出统计数据
export function exportStatistics(type, params) {
  return request({
    url: `/statistics/export/${type}`,
    method: 'get',
    params,
    responseType: 'blob'
  })
}
