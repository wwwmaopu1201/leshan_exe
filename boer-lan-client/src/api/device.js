import request from './request'

// 获取设备树
export function getDeviceTree() {
  return request({
    url: '/device/tree',
    method: 'get'
  })
}

// 获取设备列表
export function getDeviceList(params) {
  return request({
    url: '/device/list',
    method: 'get',
    params
  })
}

// 获取设备详情
export function getDeviceDetail(id) {
  return request({
    url: `/device/${id}`,
    method: 'get'
  })
}

// 创建设备
export function createDevice(data) {
  return request({
    url: '/device',
    method: 'post',
    data
  })
}

// 更新设备
export function updateDevice(id, data) {
  return request({
    url: `/device/${id}`,
    method: 'put',
    data
  })
}

// 删除设备（业务语义：移出分组并保留设备）
export function deleteDevice(id) {
  return request({
    url: `/device/${id}`,
    method: 'delete'
  })
}

// 批量删除设备（业务语义：批量移出分组并保留设备）
export function batchDeleteDevices(ids) {
  return request({
    url: '/device/batch',
    method: 'delete',
    data: { ids }
  })
}

// 移动设备到分组
export function moveToGroup(deviceIds, groupId) {
  return request({
    url: '/device/move',
    method: 'post',
    data: { deviceIds, groupId }
  })
}

// 获取设备分组
export function getDeviceGroups() {
  return request({
    url: '/device/groups',
    method: 'get'
  })
}

// 创建设备分组
export function createDeviceGroup(data) {
  return request({
    url: '/device/group',
    method: 'post',
    data
  })
}

// 更新设备分组
export function updateDeviceGroup(id, data) {
  return request({
    url: `/device/group/${id}`,
    method: 'put',
    data
  })
}

// 删除设备分组
export function deleteDeviceGroup(id) {
  return request({
    url: `/device/group/${id}`,
    method: 'delete'
  })
}

// 远程控制确认（获取一次性控制令牌）
export function confirmRemoteControl(deviceId, data) {
  return request({
    url: `/device/${deviceId}/control/confirm`,
    method: 'post',
    data
  })
}
