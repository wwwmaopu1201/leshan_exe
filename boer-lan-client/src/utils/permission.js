import store from '@/store'

/**
 * 权限键定义
 * - home: 首页权限
 * - dashboard: 看板权限
 * - employeeManagement: 员工管理权限
 * - fileManagement: 文件管理权限
 * - remoteMonitoring: 远程监控权限
 * - statistics: 统计权限
 * - deviceManagement: 设备管理权限
 */
export const PERMISSIONS = {
  HOME: 'home',
  DASHBOARD: 'dashboard',
  EMPLOYEE_MANAGEMENT: 'employeeManagement',
  FILE_MANAGEMENT: 'fileManagement',
  REMOTE_MONITORING: 'remoteMonitoring',
  STATISTICS: 'statistics',
  DEVICE_MANAGEMENT: 'deviceManagement'
}

/**
 * 检查是否有某个权限
 * @param {string} permissionKey - 权限键
 * @returns {boolean}
 */
export function hasPermission(permissionKey) {
  return store.getters.hasPermission(permissionKey)
}

/**
 * 检查是否有多个权限（需要全部拥有）
 * @param {string[]} permissionKeys - 权限键数组
 * @returns {boolean}
 */
export function hasAllPermissions(permissionKeys) {
  return permissionKeys.every(key => hasPermission(key))
}

/**
 * 检查是否有任意一个权限（拥有其中一个即可）
 * @param {string[]} permissionKeys - 权限键数组
 * @returns {boolean}
 */
export function hasAnyPermission(permissionKeys) {
  return permissionKeys.some(key => hasPermission(key))
}

/**
 * 获取当前用户的所有权限
 * @returns {Object}
 */
export function getUserPermissions() {
  return store.getters.userPermissions
}

/**
 * 检查用户是否被禁用
 * @returns {boolean}
 */
export function isUserDisabled() {
  return store.getters.isUserDisabled
}

/**
 * 检查是否是管理员
 * @returns {boolean}
 */
export function isAdmin() {
  return store.getters.isAdmin
}

/**
 * Vue指令：v-permission
 * 用法：<el-button v-permission="'fileManagement'">文件管理</el-button>
 */
export function installPermissionDirective(Vue) {
  Vue.directive('permission', {
    inserted(el, binding) {
      const permission = binding.value
      if (!hasPermission(permission)) {
        el.style.display = 'none'
      }
    },
    update(el, binding) {
      const permission = binding.value
      if (!hasPermission(permission)) {
        el.style.display = 'none'
      } else {
        el.style.display = ''
      }
    }
  })
}

/**
 * Vue指令：v-permission-disable
 * 用法：<el-button v-permission-disable="'fileManagement'">文件管理</el-button>
 * 没有权限时禁用按钮而不是隐藏
 */
export function installPermissionDisableDirective(Vue) {
  Vue.directive('permission-disable', {
    inserted(el, binding) {
      const permission = binding.value
      if (!hasPermission(permission)) {
        el.disabled = true
        el.classList.add('is-disabled')
      }
    },
    update(el, binding) {
      const permission = binding.value
      if (!hasPermission(permission)) {
        el.disabled = true
        el.classList.add('is-disabled')
      } else {
        el.disabled = false
        el.classList.remove('is-disabled')
      }
    }
  })
}

/**
 * 路由权限检查函数
 * @param {Object} route - 路由对象
 * @returns {boolean} - 是否有权限访问
 */
export function checkRoutePermission(route) {
  // 如果路由没有定义权限要求，默认允许访问
  if (!route.meta || !route.meta.permission) {
    return true
  }

  const requiredPermission = route.meta.permission

  // 如果是数组，检查是否拥有其中任意一个权限
  if (Array.isArray(requiredPermission)) {
    return hasAnyPermission(requiredPermission)
  }

  // 如果是字符串，检查是否拥有该权限
  return hasPermission(requiredPermission)
}

export default {
  hasPermission,
  hasAllPermissions,
  hasAnyPermission,
  getUserPermissions,
  isUserDisabled,
  isAdmin,
  checkRoutePermission,
  PERMISSIONS
}
