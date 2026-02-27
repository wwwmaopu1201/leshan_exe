// Mock数据 - 开发阶段使用

// 模拟延迟
const delay = (ms = 300) => new Promise(resolve => setTimeout(resolve, ms))

// Mock用户
const mockUsers = [
  { id: 1, username: 'admin', password: 'admin123', nickname: '管理员', role: 'admin' },
  { id: 2, username: 'user', password: 'user123', nickname: '普通用户', role: 'user' }
]

// Mock设备树
export const mockDeviceTree = [
  {
    id: 1,
    label: '全部设备',
    children: [
      {
        id: 2,
        label: 'A车间',
        children: [
          { id: 5, label: 'A-001', type: 'device', status: 'online', model: 'BM-2000' },
          { id: 6, label: 'A-002', type: 'device', status: 'working', model: 'BM-2000' },
          { id: 7, label: 'A-003', type: 'device', status: 'offline', model: 'BM-3000' }
        ]
      },
      {
        id: 3,
        label: 'B车间',
        children: [
          { id: 8, label: 'B-001', type: 'device', status: 'working', model: 'BM-3000' },
          { id: 9, label: 'B-002', type: 'device', status: 'alarm', model: 'BM-2000' }
        ]
      },
      {
        id: 4,
        label: 'C车间',
        children: [
          { id: 10, label: 'C-001', type: 'device', status: 'idle', model: 'BM-3000' },
          { id: 11, label: 'C-002', type: 'device', status: 'working', model: 'BM-2000' }
        ]
      }
    ]
  }
]

// Mock设备列表
export const mockDevices = [
  { id: 1, code: 'A-001', name: '缝纫机A-001', type: '缝纫机', model: 'BM-2000', status: 'online', ip: '192.168.1.101', group: 'A车间' },
  { id: 2, code: 'A-002', name: '缝纫机A-002', type: '缝纫机', model: 'BM-2000', status: 'working', ip: '192.168.1.102', group: 'A车间' },
  { id: 3, code: 'A-003', name: '缝纫机A-003', type: '缝纫机', model: 'BM-3000', status: 'offline', ip: '192.168.1.103', group: 'A车间' },
  { id: 4, code: 'B-001', name: '缝纫机B-001', type: '缝纫机', model: 'BM-3000', status: 'working', ip: '192.168.1.104', group: 'B车间' },
  { id: 5, code: 'B-002', name: '缝纫机B-002', type: '缝纫机', model: 'BM-2000', status: 'alarm', ip: '192.168.1.105', group: 'B车间' },
  { id: 6, code: 'C-001', name: '缝纫机C-001', type: '缝纫机', model: 'BM-3000', status: 'idle', ip: '192.168.1.106', group: 'C车间' },
  { id: 7, code: 'C-002', name: '缝纫机C-002', type: '缝纫机', model: 'BM-2000', status: 'working', ip: '192.168.1.107', group: 'C车间' }
]

// Mock花型文件
export const mockPatterns = [
  { id: 1, name: 'Pattern-001.dst', size: '1.2MB', uploadTime: '2024-01-15 10:30:00', status: 'completed' },
  { id: 2, name: 'Pattern-002.dst', size: '0.8MB', uploadTime: '2024-01-15 11:20:00', status: 'completed' },
  { id: 3, name: 'Pattern-003.dst', size: '2.1MB', uploadTime: '2024-01-16 09:15:00', status: 'completed' },
  { id: 4, name: 'Pattern-004.dst', size: '1.5MB', uploadTime: '2024-01-16 14:00:00', status: 'downloading' },
  { id: 5, name: 'Pattern-005.dst', size: '0.9MB', uploadTime: '2024-01-17 08:30:00', status: 'waiting' }
]

// Mock员工
export const mockEmployees = [
  { id: 1, code: 'E001', name: '张三', department: '生产部', position: '操作员', phone: '13800138001' },
  { id: 2, code: 'E002', name: '李四', department: '生产部', position: '操作员', phone: '13800138002' },
  { id: 3, code: 'E003', name: '王五', department: '生产部', position: '组长', phone: '13800138003' },
  { id: 4, code: 'E004', name: '赵六', department: '质检部', position: '质检员', phone: '13800138004' },
  { id: 5, code: 'E005', name: '钱七', department: '技术部', position: '工程师', phone: '13800138005' }
]

// Mock首页统计数据
export const mockHomeStats = {
  totalDevices: 50,
  onlineDevices: 42,
  offlineDevices: 5,
  alarmDevices: 3,
  weeklyEfficiency: [
    { date: '周一', value: 85 },
    { date: '周二', value: 88 },
    { date: '周三', value: 82 },
    { date: '周四', value: 90 },
    { date: '周五', value: 87 },
    { date: '周六', value: 75 },
    { date: '周日', value: 60 }
  ],
  patternUsage: [
    { name: 'Pattern-001', value: 35 },
    { name: 'Pattern-002', value: 28 },
    { name: 'Pattern-003', value: 22 },
    { name: '其他', value: 15 }
  ],
  modelRatio: [
    { name: 'BM-2000', value: 30 },
    { name: 'BM-3000', value: 15 },
    { name: 'BM-5000', value: 5 }
  ],
  topProduction: [
    { name: 'A-001', value: 1200 },
    { name: 'B-002', value: 1100 },
    { name: 'C-001', value: 980 }
  ],
  productionByHour: [
    { hour: '08:00', value: 120 },
    { hour: '09:00', value: 180 },
    { hour: '10:00', value: 200 },
    { hour: '11:00', value: 190 },
    { hour: '12:00', value: 80 },
    { hour: '13:00', value: 150 },
    { hour: '14:00', value: 210 },
    { hour: '15:00', value: 195 },
    { hour: '16:00', value: 185 },
    { hour: '17:00', value: 160 }
  ]
}

// Mock仪表盘数据
export const mockDashboardData = {
  totalPieces: 12580,
  threadLength: 8520.5,
  spindleSpeed: 3500,
  runningTime: 8.5,
  processingTime: 6.2,
  utilizationRate: 72.9,
  hourlyProduction: [
    { hour: '08:00', value: 150 },
    { hour: '09:00', value: 180 },
    { hour: '10:00', value: 200 },
    { hour: '11:00', value: 190 },
    { hour: '12:00', value: 100 },
    { hour: '13:00', value: 160 },
    { hour: '14:00', value: 210 },
    { hour: '15:00', value: 195 }
  ]
}

// Mock登录
export const mockLogin = async (username, password) => {
  await delay(500)
  const user = mockUsers.find(u => u.username === username && u.password === password)
  if (user) {
    return {
      code: 0,
      data: {
        token: 'mock-token-' + Date.now(),
        user: {
          id: user.id,
          username: user.username,
          nickname: user.nickname,
          role: user.role
        }
      },
      message: 'success'
    }
  }
  throw new Error('用户名或密码错误')
}

// Mock获取设备树
export const mockGetDeviceTree = async () => {
  await delay(300)
  return {
    code: 0,
    data: mockDeviceTree,
    message: 'success'
  }
}

// Mock获取设备列表
export const mockGetDevices = async (params = {}) => {
  await delay(300)
  let list = [...mockDevices]

  // 过滤
  if (params.keyword) {
    list = list.filter(d =>
      d.name.includes(params.keyword) ||
      d.code.includes(params.keyword)
    )
  }
  if (params.status) {
    list = list.filter(d => d.status === params.status)
  }

  return {
    code: 0,
    data: {
      list,
      total: list.length
    },
    message: 'success'
  }
}

// Mock获取首页统计
export const mockGetHomeStats = async () => {
  await delay(300)
  return {
    code: 0,
    data: mockHomeStats,
    message: 'success'
  }
}

// Mock获取仪表盘数据
export const mockGetDashboardData = async (deviceId) => {
  await delay(300)
  return {
    code: 0,
    data: mockDashboardData,
    message: 'success'
  }
}

// Mock获取员工列表
export const mockGetEmployees = async (params = {}) => {
  await delay(300)
  let list = [...mockEmployees]

  if (params.keyword) {
    list = list.filter(e =>
      e.name.includes(params.keyword) ||
      e.code.includes(params.keyword)
    )
  }

  return {
    code: 0,
    data: {
      list,
      total: list.length
    },
    message: 'success'
  }
}

// Mock获取花型列表
export const mockGetPatterns = async (params = {}) => {
  await delay(300)
  return {
    code: 0,
    data: {
      list: mockPatterns,
      total: mockPatterns.length
    },
    message: 'success'
  }
}

// Mock下发队列
export const mockDownloadQueue = [
  { id: 1, patternName: 'Pattern-001.dst', deviceCode: 'A-001', deviceName: '缝纫机A-001', progress: 100, status: 'completed', createTime: '2024-01-17 10:30:00' },
  { id: 2, patternName: 'Pattern-002.dst', deviceCode: 'A-002', deviceName: '缝纫机A-002', progress: 65, status: 'downloading', createTime: '2024-01-17 10:35:00' },
  { id: 3, patternName: 'Pattern-003.dst', deviceCode: 'B-001', deviceName: '缝纫机B-001', progress: 30, status: 'downloading', createTime: '2024-01-17 10:40:00' },
  { id: 4, patternName: 'Pattern-004.dst', deviceCode: 'C-001', deviceName: '缝纫机C-001', progress: 0, status: 'waiting', createTime: '2024-01-17 10:45:00' },
  { id: 5, patternName: 'Pattern-005.dst', deviceCode: 'C-002', deviceName: '缝纫机C-002', progress: 0, status: 'waiting', createTime: '2024-01-17 10:50:00' }
]

// Mock下发日志
export const mockDownloadLogs = [
  { id: 1, patternName: 'Pattern-001.dst', deviceCode: 'A-001', deviceName: '缝纫机A-001', status: 'success', operator: '张三', startTime: '2024-01-17 09:00:00', endTime: '2024-01-17 09:02:30', message: '下发成功' },
  { id: 2, patternName: 'Pattern-002.dst', deviceCode: 'A-002', deviceName: '缝纫机A-002', status: 'success', operator: '李四', startTime: '2024-01-17 09:10:00', endTime: '2024-01-17 09:12:15', message: '下发成功' },
  { id: 3, patternName: 'Pattern-001.dst', deviceCode: 'B-001', deviceName: '缝纫机B-001', status: 'failed', operator: '王五', startTime: '2024-01-17 09:20:00', endTime: '2024-01-17 09:20:30', message: '设备离线' },
  { id: 4, patternName: 'Pattern-003.dst', deviceCode: 'A-003', deviceName: '缝纫机A-003', status: 'success', operator: '赵六', startTime: '2024-01-16 14:00:00', endTime: '2024-01-16 14:03:00', message: '下发成功' },
  { id: 5, patternName: 'Pattern-002.dst', deviceCode: 'B-002', deviceName: '缝纫机B-002', status: 'success', operator: '钱七', startTime: '2024-01-16 15:00:00', endTime: '2024-01-16 15:02:00', message: '下发成功' }
]

// Mock获取下发队列
export const mockGetDownloadQueue = async () => {
  await delay(300)
  return {
    code: 0,
    data: {
      list: mockDownloadQueue,
      total: mockDownloadQueue.length
    },
    message: 'success'
  }
}

// Mock获取下发日志
export const mockGetDownloadLogs = async (params = {}) => {
  await delay(300)
  return {
    code: 0,
    data: {
      list: mockDownloadLogs,
      total: mockDownloadLogs.length
    },
    message: 'success'
  }
}

// Mock工资统计数据
export const mockSalaryStats = {
  summary: {
    totalEmployees: 25,
    totalSalary: 125800,
    avgSalary: 5032,
    totalBonus: 8500
  },
  list: [
    { id: 1, employeeCode: 'E001', employeeName: '张三', department: '生产部', pieces: 1200, unitPrice: 3.5, salary: 4200, bonus: 500, totalAmount: 4700 },
    { id: 2, employeeCode: 'E002', employeeName: '李四', department: '生产部', pieces: 1350, unitPrice: 3.5, salary: 4725, bonus: 600, totalAmount: 5325 },
    { id: 3, employeeCode: 'E003', employeeName: '王五', department: '生产部', pieces: 1100, unitPrice: 3.5, salary: 3850, bonus: 400, totalAmount: 4250 },
    { id: 4, employeeCode: 'E004', employeeName: '赵六', department: '质检部', pieces: 0, unitPrice: 0, salary: 4500, bonus: 300, totalAmount: 4800 },
    { id: 5, employeeCode: 'E005', employeeName: '钱七', department: '技术部', pieces: 0, unitPrice: 0, salary: 6000, bonus: 500, totalAmount: 6500 }
  ],
  chartData: [
    { name: '张三', value: 4700 },
    { name: '李四', value: 5325 },
    { name: '王五', value: 4250 },
    { name: '赵六', value: 4800 },
    { name: '钱七', value: 6500 }
  ]
}

// Mock获取工资统计
export const mockGetSalaryStats = async (params = {}) => {
  await delay(300)
  return {
    code: 0,
    data: mockSalaryStats,
    message: 'success'
  }
}

// Mock加工概况数据
export const mockProcessOverview = {
  summary: {
    totalPieces: 15800,
    totalStitches: 2580000,
    totalThread: 12500.5,
    avgEfficiency: 85.6
  },
  byDevice: [
    { deviceCode: 'A-001', deviceName: '缝纫机A-001', pieces: 2500, stitches: 420000, efficiency: 88 },
    { deviceCode: 'A-002', deviceName: '缝纫机A-002', pieces: 2800, stitches: 480000, efficiency: 92 },
    { deviceCode: 'A-003', deviceName: '缝纫机A-003', pieces: 1800, stitches: 300000, efficiency: 75 },
    { deviceCode: 'B-001', deviceName: '缝纫机B-001', pieces: 2600, stitches: 440000, efficiency: 86 },
    { deviceCode: 'B-002', deviceName: '缝纫机B-002', pieces: 2200, stitches: 380000, efficiency: 82 },
    { deviceCode: 'C-001', deviceName: '缝纫机C-001', pieces: 2100, stitches: 350000, efficiency: 80 },
    { deviceCode: 'C-002', deviceName: '缝纫机C-002', pieces: 1800, stitches: 210000, efficiency: 78 }
  ],
  trend: [
    { date: '01-11', pieces: 2100 },
    { date: '01-12', pieces: 2350 },
    { date: '01-13', pieces: 2200 },
    { date: '01-14', pieces: 2450 },
    { date: '01-15', pieces: 2300 },
    { date: '01-16', pieces: 2150 },
    { date: '01-17', pieces: 2250 }
  ]
}

// Mock获取加工概况
export const mockGetProcessOverview = async (params = {}) => {
  await delay(300)
  return {
    code: 0,
    data: mockProcessOverview,
    message: 'success'
  }
}

// Mock时长统计数据
export const mockDurationStats = {
  summary: {
    totalRunning: 168.5,
    totalIdle: 42.3,
    totalProcessing: 126.2,
    avgUtilization: 75.0
  },
  byDevice: [
    { deviceCode: 'A-001', deviceName: '缝纫机A-001', running: 24.5, idle: 5.5, processing: 19.0, utilization: 77.6 },
    { deviceCode: 'A-002', deviceName: '缝纫机A-002', running: 26.0, idle: 4.0, processing: 22.0, utilization: 84.6 },
    { deviceCode: 'A-003', deviceName: '缝纫机A-003', running: 20.0, idle: 8.0, processing: 12.0, utilization: 60.0 },
    { deviceCode: 'B-001', deviceName: '缝纫机B-001', running: 25.0, idle: 5.0, processing: 20.0, utilization: 80.0 },
    { deviceCode: 'B-002', deviceName: '缝纫机B-002', running: 23.0, idle: 7.0, processing: 16.0, utilization: 69.6 },
    { deviceCode: 'C-001', deviceName: '缝纫机C-001', running: 25.0, idle: 6.3, processing: 18.7, utilization: 74.8 },
    { deviceCode: 'C-002', deviceName: '缝纫机C-002', running: 25.0, idle: 6.5, processing: 18.5, utilization: 74.0 }
  ],
  trend: [
    { date: '01-11', running: 23, idle: 6, processing: 17 },
    { date: '01-12', running: 24, idle: 5, processing: 19 },
    { date: '01-13', running: 22, idle: 7, processing: 15 },
    { date: '01-14', running: 25, idle: 4, processing: 21 },
    { date: '01-15', running: 24, idle: 6, processing: 18 },
    { date: '01-16', running: 26, idle: 5, processing: 21 },
    { date: '01-17', running: 24.5, idle: 5.3, processing: 19.2 }
  ]
}

// Mock获取时长统计
export const mockGetDurationStats = async (params = {}) => {
  await delay(300)
  return {
    code: 0,
    data: mockDurationStats,
    message: 'success'
  }
}

// Mock报警统计数据
export const mockAlarmStats = {
  summary: {
    totalAlarms: 45,
    resolvedAlarms: 38,
    pendingAlarms: 7,
    avgResolutionTime: 15.5
  },
  byType: [
    { type: '断线报警', count: 18 },
    { type: '缺料报警', count: 12 },
    { type: '设备故障', count: 8 },
    { type: '通讯异常', count: 5 },
    { type: '其他', count: 2 }
  ],
  byDevice: [
    { deviceCode: 'A-001', deviceName: '缝纫机A-001', count: 5 },
    { deviceCode: 'A-002', deviceName: '缝纫机A-002', count: 3 },
    { deviceCode: 'A-003', deviceName: '缝纫机A-003', count: 12 },
    { deviceCode: 'B-001', deviceName: '缝纫机B-001', count: 8 },
    { deviceCode: 'B-002', deviceName: '缝纫机B-002', count: 10 },
    { deviceCode: 'C-001', deviceName: '缝纫机C-001', count: 4 },
    { deviceCode: 'C-002', deviceName: '缝纫机C-002', count: 3 }
  ],
  list: [
    { id: 1, deviceCode: 'B-002', deviceName: '缝纫机B-002', type: '断线报警', code: 'E001', description: '上线断裂', status: 'pending', startTime: '2024-01-17 10:30:00', endTime: null, duration: null },
    { id: 2, deviceCode: 'A-003', deviceName: '缝纫机A-003', type: '设备故障', code: 'E005', description: '电机过热', status: 'pending', startTime: '2024-01-17 09:45:00', endTime: null, duration: null },
    { id: 3, deviceCode: 'A-001', deviceName: '缝纫机A-001', type: '断线报警', code: 'E001', description: '底线断裂', status: 'resolved', startTime: '2024-01-17 08:30:00', endTime: '2024-01-17 08:45:00', duration: 15 },
    { id: 4, deviceCode: 'B-001', deviceName: '缝纫机B-001', type: '缺料报警', code: 'E002', description: '底线不足', status: 'resolved', startTime: '2024-01-16 16:00:00', endTime: '2024-01-16 16:20:00', duration: 20 },
    { id: 5, deviceCode: 'C-001', deviceName: '缝纫机C-001', type: '通讯异常', code: 'E003', description: '网络连接中断', status: 'resolved', startTime: '2024-01-16 14:00:00', endTime: '2024-01-16 14:10:00', duration: 10 }
  ],
  trend: [
    { date: '01-11', count: 8 },
    { date: '01-12', count: 5 },
    { date: '01-13', count: 7 },
    { date: '01-14', count: 6 },
    { date: '01-15', count: 9 },
    { date: '01-16', count: 6 },
    { date: '01-17', count: 4 }
  ]
}

// Mock获取报警统计
export const mockGetAlarmStats = async (params = {}) => {
  await delay(300)
  return {
    code: 0,
    data: mockAlarmStats,
    message: 'success'
  }
}
