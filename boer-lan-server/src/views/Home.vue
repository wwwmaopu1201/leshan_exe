<template>
  <div class="page-shell">
    <div class="page-header">
      <div class="page-title-block">
        <h2>服务器概览</h2>
        <p>查看设备接入数量、账号规模和服务端最近的调试信息，页面每 5 秒自动刷新。</p>
      </div>
      <div class="action-group">
        <div class="soft-note">
          <i class="el-icon-time"></i>
          <span>最近刷新：{{ lastUpdatedAt || '--' }}</span>
        </div>
      </div>
    </div>

    <div class="summary-grid">
      <div class="summary-card primary">
        <div class="summary-card__icon">
          <i class="el-icon-monitor"></i>
        </div>
        <div class="summary-card__label">设备总数</div>
        <div class="summary-card__value">{{ stats.deviceCount }}</div>
        <div class="summary-card__hint">当前已接入后台的全部设备数量。</div>
      </div>

      <div class="summary-card success">
        <div class="summary-card__icon">
          <i class="el-icon-circle-check"></i>
        </div>
        <div class="summary-card__label">在线设备</div>
        <div class="summary-card__value">{{ stats.onlineDeviceCount }}</div>
        <div class="summary-card__hint">包含在线、缝纫中和空闲状态设备。</div>
      </div>

      <div class="summary-card indigo">
        <div class="summary-card__icon">
          <i class="el-icon-user"></i>
        </div>
        <div class="summary-card__label">客户端账号数</div>
        <div class="summary-card__value">{{ stats.userCount }}</div>
        <div class="summary-card__hint">服务端已创建的客户端登录账号数量。</div>
      </div>

      <div class="summary-card warning">
        <div class="summary-card__icon">
          <i class="el-icon-s-custom"></i>
        </div>
        <div class="summary-card__label">员工数</div>
        <div class="summary-card__value">{{ employeeCount }}</div>
        <div class="summary-card__hint">来源于客户端侧员工/操作员相关数据。</div>
      </div>

      <div class="summary-card info">
        <div class="summary-card__icon">
          <i class="el-icon-folder-opened"></i>
        </div>
        <div class="summary-card__label">分组数</div>
        <div class="summary-card__value">{{ stats.groupCount }}</div>
        <div class="summary-card__hint">用于按工厂或区域管理账号和设备。</div>
      </div>
    </div>

    <div class="surface-card home-log-card">
      <div class="section-title">
        <div>
          <h3>调试信息</h3>
          <p>按时间倒序显示最近调试日志，可用于核对设备接入、协议通信和系统异常。</p>
        </div>
        <div class="action-group">
          <el-select v-model="logLevel" clearable size="small" placeholder="全部级别" @change="loadDebugLogs">
            <el-option label="INFO" value="info" />
            <el-option label="WARN" value="warn" />
            <el-option label="ERROR" value="error" />
          </el-select>
          <el-button size="small" icon="el-icon-refresh" @click="reloadAll">
            刷新
          </el-button>
          <el-button size="small" type="danger" icon="el-icon-delete" @click="clearDebugLogs">
            清空日志
          </el-button>
        </div>
      </div>

      <div v-if="logLoading && !debugLogs.length" class="ghost-empty">
        <i class="el-icon-loading"></i>
        <span>正在加载调试日志...</span>
      </div>

      <template v-else>
        <div v-if="debugLogs.length" class="debug-list">
          <div v-for="log in debugLogs" :key="log.id" class="debug-item">
            <div class="debug-item__meta">
              <span class="debug-time">{{ formatTime(log.createdAt) }}</span>
              <span :class="['status-pill', levelClass(log.level)]">{{ levelText(log.level) }}</span>
              <span v-if="log.source" class="debug-source">{{ log.source }}</span>
            </div>
            <div class="debug-item__message">{{ log.message || '-' }}</div>
          </div>
        </div>
        <div v-else class="ghost-empty">
          <i class="el-icon-document-remove"></i>
          <span>暂无调试数据</span>
        </div>
      </template>
    </div>
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
      timer: null,
      logLoading: false,
      logLevel: '',
      lastUpdatedAt: ''
    }
  },
  computed: {
    employeeCount() {
      return typeof this.stats.employeeCount === 'number'
        ? this.stats.employeeCount
        : this.stats.operatorCount
    }
  },
  mounted() {
    this.reloadAll()
    this.timer = setInterval(() => {
      this.reloadAll({ silent: true })
    }, 5000)
  },
  beforeDestroy() {
    if (this.timer) {
      clearInterval(this.timer)
    }
  },
  methods: {
    async reloadAll(options = {}) {
      await Promise.all([
        this.loadStats(),
        this.loadDebugLogs(options)
      ])
      this.lastUpdatedAt = this.formatClock(new Date())
    },
    async loadStats() {
      try {
        const res = await this.$axios.get('/system/stats')
        if (res.code === 0) {
          this.stats = {
            ...this.stats,
            ...res.data
          }
        }
      } catch (error) {
        console.error('加载统计信息失败', error)
      }
    },
    async loadDebugLogs(options = {}) {
      if (!options.silent) {
        this.logLoading = true
      }
      try {
        const res = await this.$axios.get('/system/logs', {
          params: {
            limit: 80,
            level: this.logLevel || undefined
          }
        })
        if (res.code === 0) {
          this.debugLogs = Array.isArray(res.data) ? res.data : []
        }
      } catch (error) {
        console.error('加载调试日志失败', error)
      } finally {
        if (!options.silent) {
          this.logLoading = false
        }
      }
    },
    async clearDebugLogs() {
      try {
        await this.$confirm('确定要清空所有调试日志吗？', '警告', {
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
    levelClass(level) {
      const normalized = String(level || '').toLowerCase()
      const map = {
        info: 'info',
        warn: 'warning',
        error: 'danger'
      }
      return map[normalized] || 'info'
    },
    levelText(level) {
      return String(level || 'info').toUpperCase()
    },
    formatClock(time) {
      const date = time instanceof Date ? time : new Date(time)
      const pad = value => String(value).padStart(2, '0')
      return `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`
    },
    formatTime(time) {
      if (!time) return '--'
      const date = new Date(time)
      if (Number.isNaN(date.getTime())) return '--'
      const pad = value => String(value).padStart(2, '0')
      return `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`
    }
  }
}
</script>

<style scoped>
.home-log-card {
  min-height: 520px;
}

.debug-list {
  display: grid;
  gap: 12px;
}

.debug-item {
  padding: 16px 18px;
  border-radius: 20px;
  border: 1px solid rgba(219, 228, 240, 0.92);
  background: linear-gradient(180deg, #ffffff 0%, #f8fbff 100%);
}

.debug-item__meta {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}

.debug-time {
  color: #73849a;
  font-family: Menlo, Monaco, Consolas, monospace;
  font-size: 12px;
}

.debug-source {
  min-height: 28px;
  padding: 0 10px;
  border-radius: 999px;
  background: rgba(47, 109, 246, 0.08);
  color: #2f6df6;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  font-size: 12px;
}

.debug-item__message {
  margin-top: 10px;
  color: #22324d;
  line-height: 1.8;
  word-break: break-word;
}
</style>
