import request from './request'

// 获取花型文件列表
export function getPatternList(params) {
  return request({
    url: '/pattern/list',
    method: 'get',
    params
  })
}

// 获取花型类型列表
export function getPatternTypes() {
  return request({
    url: '/pattern/types',
    method: 'get'
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

// 编辑花型信息
export function updatePattern(id, data) {
  return request({
    url: `/pattern/${id}`,
    method: 'put',
    data
  })
}

// 批量编辑花型信息
export function batchUpdatePatterns(data) {
  return request({
    url: '/pattern/batch-update',
    method: 'post',
    data
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
  return request({
    url: '/pattern/queue',
    method: 'get'
  })
}

// 获取下发日志
export function getDownloadLog(params) {
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

// 暂停下发任务
export function pauseDownload(taskId) {
  return request({
    url: `/pattern/queue/${taskId}/pause`,
    method: 'post'
  })
}

// 继续下发任务
export function resumeDownload(taskId) {
  return request({
    url: `/pattern/queue/${taskId}/resume`,
    method: 'post'
  })
}

// 全部暂停
export function pauseAllDownloads() {
  return request({
    url: '/pattern/queue/pause-all',
    method: 'post'
  })
}

// 全部继续
export function resumeAllDownloads() {
  return request({
    url: '/pattern/queue/resume-all',
    method: 'post'
  })
}

// 清除已完成任务
export function clearCompletedDownloads() {
  return request({
    url: '/pattern/queue/completed',
    method: 'delete'
  })
}

// 获取设备文件（设备端花型文件）
export function getDevicePatternFiles(params) {
  return request({
    url: '/pattern/device-files',
    method: 'get',
    params
  })
}

// 删除设备文件
export function deleteDevicePatternFile(id) {
  return request({
    url: `/pattern/device-files/${id}`,
    method: 'delete'
  })
}

// 设备文件回传到服务器
export function uploadDeviceFilesToServer(data) {
  return request({
    url: '/pattern/device-files/upload',
    method: 'post',
    data
  })
}

// 获取上传队列
export function getUploadQueue(params) {
  return request({
    url: '/pattern/upload-queue',
    method: 'get',
    params
  })
}

// 暂停上传任务
export function pauseUploadTask(id) {
  return request({
    url: `/pattern/upload-queue/${id}/pause`,
    method: 'post'
  })
}

// 恢复上传任务
export function resumeUploadTask(id) {
  return request({
    url: `/pattern/upload-queue/${id}/resume`,
    method: 'post'
  })
}

// 取消上传任务
export function cancelUploadTask(id) {
  return request({
    url: `/pattern/upload-queue/${id}`,
    method: 'delete'
  })
}

// 清理上传历史任务
export function clearCompletedUploads() {
  return request({
    url: '/pattern/upload-queue/completed',
    method: 'delete'
  })
}
