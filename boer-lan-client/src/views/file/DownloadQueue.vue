<template>
  <div class="page-container">
    <div class="card">
      <div class="card-header flex-between">
        <span>{{ $t('menu.downloadQueue') }}</span>
        <div class="header-actions">
          <el-button size="small" icon="el-icon-refresh" @click="fetchData">
            {{ $t('common.refresh') }}
          </el-button>
          <el-button size="small" icon="el-icon-video-pause" @click="handlePauseAll">
            全部暂停
          </el-button>
          <el-button size="small" icon="el-icon-video-play" @click="handleResumeAll">
            全部继续
          </el-button>
          <el-button size="small" type="danger" icon="el-icon-delete" @click="clearCompleted">
            清除已完成
          </el-button>
        </div>
      </div>

      <div class="queue-stats">
        <div class="stat-item">
          <span class="stat-label">总任务</span>
          <span class="stat-value">{{ queueList.length }}</span>
        </div>
        <div class="stat-item">
          <span class="stat-label">等待中</span>
          <span class="stat-value warning">{{ waitingCount }}</span>
        </div>
        <div class="stat-item">
          <span class="stat-label">下发中</span>
          <span class="stat-value primary">{{ downloadingCount }}</span>
        </div>
        <div class="stat-item">
          <span class="stat-label">已暂停</span>
          <span class="stat-value info">{{ pausedCount }}</span>
        </div>
        <div class="stat-item">
          <span class="stat-label">失败</span>
          <span class="stat-value danger">{{ failedCount }}</span>
        </div>
      </div>

      <el-table :data="queueList" border v-loading="loading">
        <el-table-column type="index" label="序号" width="60" align="center" />
        <el-table-column prop="patternName" label="花型文件" min-width="180">
          <template slot-scope="scope">
            <i class="el-icon-document" style="margin-right: 5px; color: #409EFF;"></i>
            {{ scope.row.patternName }}
          </template>
        </el-table-column>
        <el-table-column prop="patternType" label="花型类型" width="130" />
        <el-table-column prop="orderNo" label="订单号" width="120" />
        <el-table-column prop="deviceName" label="目标设备" width="150" />
        <el-table-column prop="status" label="状态" width="100" align="center">
          <template slot-scope="scope">
            <el-tag :type="getStatusType(scope.row.status)" size="small">
              {{ getStatusText(scope.row.status) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="progress" label="进度" width="200">
          <template slot-scope="scope">
            <el-progress
              :percentage="scope.row.progress"
              :status="getProgressStatus(scope.row.status)"
              :stroke-width="16"
              text-inside
            />
          </template>
        </el-table-column>
        <el-table-column prop="message" label="备注" min-width="120" />
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
      queueList: []
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
    failedCount() {
      return this.queueList.filter(q => q.status === 'failed').length
    }
  },
  mounted() {
    this.fetchData()
    this.refreshTimer = setInterval(() => {
      this.fetchData()
    }, 5000)
  },
  beforeDestroy() {
    if (this.refreshTimer) {
      clearInterval(this.refreshTimer)
    }
  },
  methods: {
    async fetchData() {
      this.loading = true
      try {
        const res = await getDownloadQueue()
        if (res.code === 0) {
          this.queueList = res.data.list || []
        }
      } catch (error) {
        console.error('Failed to fetch download queue:', error)
      } finally {
        this.loading = false
      }
    },
    getStatusType(status) {
      const map = {
        waiting: 'warning',
        downloading: 'primary',
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
          this.$message.success(`已暂停 ${res.data.affected || 0} 个任务`)
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
          this.$message.success(`已恢复 ${res.data.affected || 0} 个任务`)
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
          this.$message.success(`已清理 ${res.data.affected || 0} 条历史任务`)
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
.header-actions {
  display: flex;
  gap: 8px;
}

.queue-stats {
  display: flex;
  gap: 30px;
  padding: 20px;
  background: #f5f7fa;
  border-radius: 8px;
  margin-bottom: 20px;

  .stat-item {
    display: flex;
    flex-direction: column;
    align-items: center;

    .stat-label {
      color: #909399;
      font-size: 14px;
      margin-bottom: 5px;
    }

    .stat-value {
      font-size: 24px;
      font-weight: bold;
      color: #303133;

      &.primary { color: #409EFF; }
      &.success { color: #67C23A; }
      &.warning { color: #E6A23C; }
      &.danger { color: #F56C6C; }
      &.info { color: #909399; }
    }
  }
}

.danger-text {
  color: #F56C6C !important;
}

.text-muted {
  color: #909399;
}
</style>
