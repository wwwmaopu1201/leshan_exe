import request from './request'

// 获取首页统计数据
export function getHomeStats() {
  return request({
    url: '/statistics/home',
    method: 'get'
  })
}

// 获取仪表盘数据
export function getDashboardData(params = {}) {
  const query = typeof params === 'object' && params !== null
    ? params
    : { deviceId: params }
  return request({
    url: '/statistics/dashboard',
    method: 'get',
    params: query
  })
}

// 获取工资统计
export function getSalaryStats(params) {
  return request({
    url: '/statistics/salary',
    method: 'get',
    params
  })
}

// 获取工资详情
export function getSalaryDetail(params) {
  return request({
    url: '/statistics/salary/detail',
    method: 'get',
    params
  })
}

// 获取加工概况
export function getProcessOverview(params) {
  return request({
    url: '/statistics/process',
    method: 'get',
    params
  })
}

// 获取时长统计
export function getDurationStats(params) {
  return request({
    url: '/statistics/duration',
    method: 'get',
    params
  })
}

// 获取报警统计
export function getAlarmStats(params) {
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
