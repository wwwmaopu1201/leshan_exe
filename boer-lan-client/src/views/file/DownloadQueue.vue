<template>
  <div class="page-container">
    <div class="card">
      <div class="card-header flex-between">
        <span>{{ $t('menu.downloadQueue') }}</span>
        <div>
          <el-button size="small" icon="el-icon-refresh" @click="fetchData">
            {{ $t('common.refresh') }}
          </el-button>
          <el-button size="small" type="danger" icon="el-icon-delete" @click="clearCompleted">
            清除已完成
          </el-button>
        </div>
      </div>

      <!-- 统计信息 -->
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
          <span class="stat-label">已完成</span>
          <span class="stat-value success">{{ completedCount }}</span>
        </div>
        <div class="stat-item">
          <span class="stat-label">失败</span>
          <span class="stat-value danger">{{ failedCount }}</span>
        </div>
      </div>

      <!-- 队列列表 -->
      <el-table :data="queueList" border v-loading="loading">
        <el-table-column type="index" label="序号" width="60" align="center" />
        <el-table-column prop="patternName" label="花型文件" min-width="180">
          <template slot-scope="scope">
            <i class="el-icon-document" style="margin-right: 5px; color: #409EFF;"></i>
            {{ scope.row.patternName }}
          </template>
        </el-table-column>
        <el-table-column prop="deviceName" label="目标设备" width="150" />
        <el-table-column prop="status" label="状态" width="120" align="center">
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
        <el-table-column prop="createTime" label="创建时间" width="160" />
        <el-table-column label="操作" width="100" align="center">
          <template slot-scope="scope">
            <el-button
              v-if="scope.row.status === 'waiting' || scope.row.status === 'downloading'"
              type="text"
              size="small"
              class="danger-text"
              @click="handleCancel(scope.row)"
            >
              取消
            </el-button>
            <el-button
              v-else-if="scope.row.status === 'failed'"
              type="text"
              size="small"
              @click="handleRetry(scope.row)"
            >
              重试
            </el-button>
            <span v-else class="text-muted">-</span>
          </template>
        </el-table-column>
      </el-table>
    </div>
  </div>
</template>

<script>
import { getDownloadQueue, cancelDownload } from '@/api/pattern'

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
    completedCount() {
      return this.queueList.filter(q => q.status === 'completed').length
    },
    failedCount() {
      return this.queueList.filter(q => q.status === 'failed').length
    }
  },
  mounted() {
    this.fetchData()
    // Simulate progress updates
    this.progressTimer = setInterval(() => {
      this.updateProgress()
    }, 1000)
  },
  beforeDestroy() {
    if (this.progressTimer) {
      clearInterval(this.progressTimer)
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
    updateProgress() {
      this.queueList.forEach(item => {
        if (item.status === 'downloading' && item.progress < 100) {
          item.progress = Math.min(item.progress + Math.random() * 5, 100)
          if (item.progress >= 100) {
            item.status = 'completed'
            item.progress = 100
          }
        }
      })
    },
    getStatusType(status) {
      const map = {
        waiting: 'warning',
        downloading: 'primary',
        completed: 'success',
        failed: 'danger'
      }
      return map[status] || 'info'
    },
    getStatusText(status) {
      const map = {
        waiting: this.$t('file.waiting'),
        downloading: this.$t('file.downloading'),
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
    handleRetry(row) {
      row.status = 'waiting'
      row.progress = 0
      this.$message.success('已加入队列重新下发')
    },
    clearCompleted() {
      this.queueList = this.queueList.filter(q => q.status !== 'completed')
      this.$message.success('已清除完成的任务')
    }
  }
}
</script>

<style lang="scss" scoped>
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
