<template>
  <div class="page-container">
    <div class="queue-shell">
      <div class="card queue-hero">
        <div class="queue-hero-actions">
          <el-button size="small" icon="el-icon-refresh" @click="fetchData">
            {{ $t('common.refresh') }}
          </el-button>
          <el-button
            size="small"
            icon="el-icon-video-pause"
            :disabled="!waitingCount && !downloadingCount"
            @click="handlePauseAll"
          >
            全部暂停
          </el-button>
          <el-button
            size="small"
            icon="el-icon-video-play"
            :disabled="!pausedCount"
            @click="handleResumeAll"
          >
            全部继续
          </el-button>
          <el-button
            size="small"
            type="danger"
            icon="el-icon-delete"
            :disabled="!completedCount"
            @click="clearCompleted"
          >
            清除已完成
          </el-button>
        </div>

        <div class="queue-overview">
          <div class="queue-stat total">
            <div class="queue-stat-label">总任务</div>
            <div class="queue-stat-value">{{ queueList.length }}</div>
          </div>
          <div class="queue-stat waiting">
            <div class="queue-stat-label">等待中</div>
            <div class="queue-stat-value">{{ waitingCount }}</div>
          </div>
          <div class="queue-stat running">
            <div class="queue-stat-label">下发中</div>
            <div class="queue-stat-value">{{ downloadingCount }}</div>
          </div>
          <div class="queue-stat paused">
            <div class="queue-stat-label">已暂停</div>
            <div class="queue-stat-value">{{ pausedCount }}</div>
          </div>
          <div class="queue-stat completed">
            <div class="queue-stat-label">已完成</div>
            <div class="queue-stat-value">{{ completedCount }}</div>
          </div>
          <div class="queue-stat failed">
            <div class="queue-stat-label">失败</div>
            <div class="queue-stat-value">{{ failedCount }}</div>
          </div>
        </div>
      </div>

      <div class="card queue-table-card">
        <el-table
          v-loading="loading"
          :data="queueList"
          border
          empty-text="暂无下发任务"
          :row-class-name="getRowClassName"
        >
          <el-table-column type="index" label="序号" width="60" align="center" />
          <el-table-column prop="patternName" label="花型文件" min-width="180">
            <template slot-scope="scope">
              <div class="pattern-cell">
                <i class="el-icon-document"></i>
                <span>{{ scope.row.patternName || '-' }}</span>
              </div>
            </template>
          </el-table-column>
          <el-table-column prop="patternType" label="花型类型" width="130" />
          <el-table-column prop="orderNo" label="订单号" width="130" />
          <el-table-column prop="deviceName" label="目标设备" width="160" />
          <el-table-column prop="status" label="状态" width="110" align="center">
            <template slot-scope="scope">
              <el-tag :type="getStatusType(scope.row.status)" size="small" effect="plain">
                {{ getStatusText(scope.row.status) }}
              </el-tag>
            </template>
          </el-table-column>
          <el-table-column prop="progress" label="进度" width="220">
            <template slot-scope="scope">
              <el-progress
                :percentage="Number(scope.row.progress || 0)"
                :status="getProgressStatus(scope.row.status)"
                :stroke-width="16"
                text-inside
              />
            </template>
          </el-table-column>
          <el-table-column prop="message" label="备注" min-width="170">
            <template slot-scope="scope">
              {{ scope.row.message || '-' }}
            </template>
          </el-table-column>
          <el-table-column prop="createTime" label="创建时间" width="170" />
          <el-table-column label="操作" width="160" align="center">
            <template slot-scope="scope">
              <template v-if="scope.row.status === 'waiting' || scope.row.status === 'downloading'">
                <el-button type="text" size="small" @click="handlePause(scope.row)">
                  暂停
                </el-button>
                <el-button
                  type="text"
                  size="small"
                  class="danger-text"
                  @click="handleCancel(scope.row)"
                >
                  取消
                </el-button>
              </template>
              <template v-else-if="scope.row.status === 'paused'">
                <el-button type="text" size="small" @click="handleResume(scope.row)">
                  继续
                </el-button>
                <el-button
                  type="text"
                  size="small"
                  class="danger-text"
                  @click="handleCancel(scope.row)"
                >
                  取消
                </el-button>
              </template>
              <template v-else-if="scope.row.status === 'failed'">
                <el-button type="text" size="small" @click="handleRetry(scope.row)">
                  重试提示
                </el-button>
              </template>
              <span v-else class="text-muted">-</span>
            </template>
          </el-table-column>
        </el-table>
      </div>
    </div>
  </div>
</template>

<script>
import {
  getDownloadQueue,
  cancelDownload,
  pauseDownload,
  resumeDownload,
  pauseAllDownloads,
  resumeAllDownloads,
  clearCompletedDownloads
} from '@/api/pattern'

export default {
  name: 'DownloadQueue',
  data() {
    return {
      loading: false,
      queueList: [],
      refreshTimer: null,
      lastUpdatedAt: ''
    }
  },
  computed: {
    waitingCount() {
      return this.queueList.filter(q => q.status === 'waiting').length
    },
    downloadingCount() {
      return this.queueList.filter(q => q.status === 'downloading').length
    },
    pausedCount() {
      return this.queueList.filter(q => q.status === 'paused').length
    },
    completedCount() {
      return this.queueList.filter(q => q.status === 'completed').length
    },
    failedCount() {
      return this.queueList.filter(q => q.status === 'failed').length
    }
  },
  mounted() {
    this.fetchData()
    this.refreshTimer = setInterval(() => {
      this.fetchData({ silent: true })
    }, 5000)
  },
  beforeDestroy() {
    if (this.refreshTimer) {
      clearInterval(this.refreshTimer)
    }
  },
  methods: {
    async fetchData(options = {}) {
      if (!options.silent) {
        this.loading = true
      }
      try {
        const res = await getDownloadQueue()
        if (res.code === 0) {
          this.queueList = Array.isArray(res.data) ? res.data : (res.data?.list || [])
          this.lastUpdatedAt = this.formatTime(new Date())
        }
      } catch (error) {
        console.error('Failed to fetch download queue:', error)
      } finally {
        if (!options.silent) {
          this.loading = false
        }
      }
    },
    formatTime(date) {
      const target = date instanceof Date ? date : new Date(date)
      const pad = value => String(value).padStart(2, '0')
      return `${pad(target.getHours())}:${pad(target.getMinutes())}:${pad(target.getSeconds())}`
    },
    getStatusType(status) {
      const map = {
        waiting: 'warning',
        downloading: '',
        paused: 'info',
        completed: 'success',
        failed: 'danger'
      }
      return map[status] || 'info'
    },
    getStatusText(status) {
      const map = {
        waiting: this.$t('file.waiting'),
        downloading: this.$t('file.downloading'),
        paused: '已暂停',
        completed: this.$t('file.completed'),
        failed: this.$t('file.downloadFailed')
      }
      return map[status] || status
    },
    getProgressStatus(status) {
      if (status === 'completed') return 'success'
      if (status === 'failed') return 'exception'
      return null
    },
    getRowClassName({ row }) {
      if (row.status === 'failed') return 'row-failed'
      if (row.status === 'paused') return 'row-paused'
      return ''
    },
    async handlePause(row) {
      try {
        const res = await pauseDownload(row.id)
        if (res.code === 0) {
          this.$message.success('任务已暂停')
          this.fetchData()
        }
      } catch (error) {
        console.error('Pause download failed:', error)
      }
    },
    async handleResume(row) {
      try {
        const res = await resumeDownload(row.id)
        if (res.code === 0) {
          this.$message.success('任务已恢复')
          this.fetchData()
        }
      } catch (error) {
        console.error('Resume download failed:', error)
      }
    },
    handleCancel(row) {
      this.$confirm('确定要取消该下发任务吗？', '提示', {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        type: 'warning'
      }).then(async () => {
        try {
          const res = await cancelDownload(row.id)
          if (res.code === 0) {
            this.$message.success('已取消')
            this.fetchData()
          } else {
            this.$message.error(res.message || '取消失败')
          }
        } catch (error) {
          console.error('Cancel download failed:', error)
          this.$message.error('取消失败')
        }
      }).catch(() => {})
    },
    async handlePauseAll() {
      try {
        const res = await pauseAllDownloads()
        if (res.code === 0) {
          this.$message.success(`已暂停 ${res.data?.affected || 0} 个任务`)
          this.fetchData()
        }
      } catch (error) {
        console.error('Pause all downloads failed:', error)
      }
    },
    async handleResumeAll() {
      try {
        const res = await resumeAllDownloads()
        if (res.code === 0) {
          this.$message.success(`已恢复 ${res.data?.affected || 0} 个任务`)
          this.fetchData()
        }
      } catch (error) {
        console.error('Resume all downloads failed:', error)
      }
    },
    async clearCompleted() {
      try {
        const res = await clearCompletedDownloads()
        if (res.code === 0) {
          this.$message.success(`已清理 ${res.data?.affected || 0} 条历史任务`)
          this.fetchData()
        }
      } catch (error) {
        console.error('Clear completed downloads failed:', error)
      }
    },
    handleRetry(row) {
      this.$message.info(`任务「${row.patternName}」已失败，请在花型列表重新下发`)
    }
  }
}
</script>

<style lang="scss" scoped>
.queue-shell {
  display: grid;
  gap: 18px;
}

.queue-hero-actions {
  display: flex;
  flex-wrap: wrap;
  justify-content: flex-end;
  gap: 10px;
  margin-bottom: 16px;
}

.queue-overview {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(170px, 1fr));
  gap: 14px;
}

.queue-stat {
  min-height: 120px;
  padding: 18px 18px 16px;
  border-radius: 20px;
  border: 1px solid rgba(219, 228, 240, 0.85);
  background: linear-gradient(180deg, #ffffff 0%, #f8fbff 100%);

  &.total {
    background: linear-gradient(135deg, rgba(47, 109, 246, 0.13), rgba(255, 255, 255, 0.96));
  }

  &.waiting {
    background: linear-gradient(135deg, rgba(227, 163, 45, 0.16), rgba(255, 255, 255, 0.96));
  }

  &.running {
    background: linear-gradient(135deg, rgba(47, 109, 246, 0.14), rgba(255, 255, 255, 0.96));
  }

  &.paused {
    background: linear-gradient(135deg, rgba(138, 152, 173, 0.16), rgba(255, 255, 255, 0.96));
  }

  &.completed {
    background: linear-gradient(135deg, rgba(47, 180, 110, 0.14), rgba(255, 255, 255, 0.96));
  }

  &.failed {
    background: linear-gradient(135deg, rgba(239, 90, 90, 0.14), rgba(255, 255, 255, 0.96));
  }
}

.queue-stat-label {
  color: #6f8098;
  font-size: 13px;
}

.queue-stat-value {
  margin-top: 16px;
  font-size: 32px;
  line-height: 1;
  font-weight: 700;
  color: #23324b;
}

.queue-stat-desc {
  margin-top: 12px;
  color: #8090a8;
  line-height: 1.6;
}

.queue-note {
  margin-top: 16px;
  padding: 14px 18px;
  border-radius: 18px;
  background: #f5f8ff;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  color: #5d708d;
}

.queue-note-main {
  display: inline-flex;
  align-items: center;
  gap: 10px;

  i {
    color: #2f6df6;
    font-size: 16px;
  }
}

.queue-note-time {
  white-space: nowrap;
  color: #8293aa;
}

.queue-table-card {
  overflow: hidden;
}

.queue-table-tip {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  min-height: 38px;
  padding: 0 14px;
  border-radius: 999px;
  background: #f4f7fc;
  color: #667891;

  i {
    color: #2f6df6;
  }
}

.pattern-cell {
  display: inline-flex;
  align-items: center;
  gap: 8px;

  i {
    color: #2f6df6;
    font-size: 16px;
  }
}

.danger-text {
  color: #ef5a5a !important;
}

::v-deep .row-failed td {
  background: rgba(239, 90, 90, 0.045);
}

::v-deep .row-paused td {
  background: rgba(138, 152, 173, 0.05);
}

@media (max-width: 1080px) {
  .queue-note,
  .section-title {
    align-items: flex-start;
    flex-direction: column;
  }

  .queue-hero-actions {
    width: 100%;
    justify-content: flex-start;
  }
}
</style>
