import request from './request'

// 获取员工列表
export function getEmployeeList(params) {
  return request({
    url: '/employee/list',
    method: 'get',
    params
  })
}

// 获取员工详情
export function getEmployeeDetail(id) {
  return request({
    url: `/employee/${id}`,
    method: 'get'
  })
}

// 创建员工
export function createEmployee(data) {
  return request({
    url: '/employee',
    method: 'post',
    data
  })
}

// 更新员工
export function updateEmployee(id, data) {
  return request({
    url: `/employee/${id}`,
    method: 'put',
    data
  })
}

// 删除员工
export function deleteEmployee(id) {
  return request({
    url: `/employee/${id}`,
    method: 'delete'
  })
}

// 批量导入员工
export function importEmployees(employees) {
  return request({
    url: '/employee/import',
    method: 'post',
    data: { employees }
  })
}

// 导出员工列表
export function exportEmployees(params) {
  return request({
    url: '/employee/export',
    method: 'get',
    params
  })
}
