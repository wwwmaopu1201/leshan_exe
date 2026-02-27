import request from './request'
import { mockGetPatterns, mockGetDownloadQueue, mockGetDownloadLogs } from './mock'

const USE_MOCK = false

// 获取花型文件列表
export function getPatternList(params) {
  if (USE_MOCK) {
    return mockGetPatterns(params)
  }
  return request({
    url: '/pattern/list',
    method: 'get',
    params
  })
}

// 上传花型文件
export function uploadPattern(formData) {
  return request({
    url: '/pattern/upload',
    method: 'post',
    data: formData,
    headers: {
      'Content-Type': 'multipart/form-data'
    }
  })
}

// 删除花型文件
export function deletePattern(id) {
  return request({
    url: `/pattern/${id}`,
    method: 'delete'
  })
}

// 下发花型到设备
export function downloadToDevice(patternId, deviceIds) {
  return request({
    url: '/pattern/download',
    method: 'post',
    data: { patternId, deviceIds }
  })
}

// 批量下发
export function batchDownload(patternIds, deviceIds) {
  return request({
    url: '/pattern/batch-download',
    method: 'post',
    data: { patternIds, deviceIds }
  })
}

// 获取下发队列
export function getDownloadQueue() {
  if (USE_MOCK) {
    return mockGetDownloadQueue()
  }
  return request({
    url: '/pattern/queue',
    method: 'get'
  })
}

// 获取下发日志
export function getDownloadLog(params) {
  if (USE_MOCK) {
    return mockGetDownloadLogs(params)
  }
  return request({
    url: '/pattern/log',
    method: 'get',
    params
  })
}

// 取消下发任务
export function cancelDownload(taskId) {
  return request({
    url: `/pattern/queue/${taskId}`,
    method: 'delete'
  })
}
