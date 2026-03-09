<template>
  <div>
    <div class="page-title">服务器概览</div>

    <!-- 统计卡片 -->
    <el-row :gutter="20" class="stats-grid">
      <el-col :span="4">
        <el-card class="stat-card">
          <div class="stat-card-title">设备总数</div>
          <div class="stat-card-value">{{ stats.deviceCount }}</div>
        </el-card>
      </el-col>
      <el-col :span="4">
        <el-card class="stat-card">
          <div class="stat-card-title">在线设备</div>
          <div class="stat-card-value" style="color: #67C23A;">{{ stats.onlineDeviceCount }}</div>
        </el-card>
      </el-col>
      <el-col :span="4">
        <el-card class="stat-card">
          <div class="stat-card-title">客户端账号数</div>
          <div class="stat-card-value">{{ stats.userCount }}</div>
        </el-card>
      </el-col>
      <el-col :span="4">
        <el-card class="stat-card">
          <div class="stat-card-title">员工数量</div>
          <div class="stat-card-value">{{ typeof stats.employeeCount === 'number' ? stats.employeeCount : stats.operatorCount }}</div>
        </el-card>
      </el-col>
      <el-col :span="4">
        <el-card class="stat-card">
          <div class="stat-card-title">分组数量</div>
          <div class="stat-card-value">{{ stats.groupCount }}</div>
        </el-card>
      </el-col>
    </el-row>

    <!-- 调试日志 -->
    <el-card class="debug-log">
      <div slot="header" style="display: flex; justify-content: space-between; align-items: center;">
        <span>调试信息</span>
        <el-button size="small" @click="clearDebugLogs">清空日志</el-button>
      </div>
      <div class="debug-log-item" v-for="log in debugLogs" :key="log.id">
        <span class="debug-log-time">{{ formatTime(log.createdAt) }}</span>
        <span :class="'debug-log-level-' + log.level">[{{ log.level.toUpperCase() }}]</span>
        <span>{{ log.message }}</span>
      </div>
    </el-card>
  </div>
</template>

<script>
export default {
  name: 'Home',
  data() {
    return {
      stats: {
        deviceCount: 0,
        onlineDeviceCount: 0,
        userCount: 0,
        employeeCount: 0,
        operatorCount: 0,
        groupCount: 0
      },
      debugLogs: [],
      timer: null
    }
  },
  mounted() {
    this.loadStats()
    this.loadDebugLogs()
    // 定时刷新
    this.timer = setInterval(() => {
      this.loadStats()
      this.loadDebugLogs()
    }, 5000)
  },
  beforeDestroy() {
    if (this.timer) {
      clearInterval(this.timer)
    }
  },
  methods: {
    async loadStats() {
      try {
        const res = await this.$axios.get('/system/stats')
        if (res.code === 0) {
          this.stats = res.data
        }
      } catch (error) {
        console.error('加载统计信息失败', error)
      }
    },
    async loadDebugLogs() {
      try {
        const res = await this.$axios.get('/system/logs', {
          params: { limit: 50 }
        })
        if (res.code === 0) {
          this.debugLogs = res.data
        }
      } catch (error) {
        console.error('加载调试日志失败', error)
      }
    },
    async clearDebugLogs() {
      try {
        await this.$confirm('确定要清空所有调试日志吗?', '警告', {
          type: 'warning'
        })
        await this.$axios.delete('/system/logs')
        this.debugLogs = []
        this.$message.success('日志已清空')
      } catch (error) {
        if (error !== 'cancel') {
          console.error('清空日志失败', error)
        }
      }
    },
    formatTime(time) {
      if (!time) return ''
      const date = new Date(time)
      return `${date.getHours()}:${String(date.getMinutes()).padStart(2, '0')}:${String(date.getSeconds()).padStart(2, '0')}`
    }
  }
}
</script>

<style scoped>
.page-title {
  font-size: 20px;
  font-weight: bold;
  margin-bottom: 20px;
}

.stats-grid {
  margin-bottom: 20px;
}

.stat-card {
  text-align: center;
}

.stat-card-title {
  color: #909399;
  font-size: 14px;
  margin-bottom: 10px;
}

.stat-card-value {
  font-size: 28px;
  font-weight: bold;
  color: #303133;
}

.debug-log {
  max-height: 400px;
  overflow-y: auto;
}

.debug-log-item {
  padding: 8px 0;
  border-bottom: 1px solid #f0f0f0;
  font-size: 12px;
}

.debug-log-time {
  color: #909399;
  margin-right: 10px;
}

.debug-log-level-info {
  color: #409EFF;
}

.debug-log-level-warn {
  color: #E6A23C;
}

.debug-log-level-error {
  color: #F56C6C;
}
</style>
