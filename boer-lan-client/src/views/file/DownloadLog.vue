<template>
  <div class="page-container">
    <div class="search-bar">
      <el-form :inline="true" :model="searchForm">
        <el-form-item label="花型文件">
          <el-input
            v-model="searchForm.patternName"
            placeholder="文件名"
            clearable
            @keyup.enter.native="handleSearch"
          />
        </el-form-item>
        <el-form-item label="花型类型">
          <el-select
            v-model="searchForm.patternType"
            placeholder="全部类型"
            clearable
            filterable
          >
            <el-option
              v-for="item in patternTypeOptions"
              :key="item"
              :label="item"
              :value="item"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="订单号">
          <el-input
            v-model="searchForm.orderNo"
            placeholder="订单编号"
            clearable
            @keyup.enter.native="handleSearch"
          />
        </el-form-item>
        <el-form-item label="目标设备">
          <el-input
            v-model="searchForm.deviceName"
            placeholder="设备名称"
            clearable
            @keyup.enter.native="handleSearch"
          />
        </el-form-item>
        <el-form-item label="状态">
          <el-select v-model="searchForm.status" clearable>
            <el-option label="全部" value="" />
            <el-option label="成功" value="completed" />
            <el-option label="失败" value="failed" />
          </el-select>
        </el-form-item>
        <el-form-item label="时间范围">
          <el-date-picker
            v-model="searchForm.dateRange"
            type="daterange"
            range-separator="至"
            start-placeholder="开始日期"
            end-placeholder="结束日期"
            value-format="yyyy-MM-dd"
          />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" icon="el-icon-search" @click="handleSearch">
            {{ $t('common.search') }}
          </el-button>
          <el-button icon="el-icon-refresh" @click="handleReset">
            {{ $t('common.reset') }}
          </el-button>
        </el-form-item>
      </el-form>
    </div>

    <el-card ref="tableCard" shadow="never" class="card page-table-card">
      <div class="section-title">
        <div>
          <h3>下发日志</h3>
          <p>按花型、订单号、设备和时间追踪每一次下发结果。</p>
        </div>
      </div>

      <div class="table-actions flex-between">
        <div class="action-group">
          <el-button icon="el-icon-download" @click="handleExport">
            {{ $t('common.export') }}
          </el-button>
        </div>
        <div>
          <el-button icon="el-icon-refresh" circle @click="fetchData" />
        </div>
      </div>

      <el-table ref="tableRef" :data="logList" border v-loading="loading" :height="tableHeight" empty-text="暂无数据">
        <el-table-column type="index" label="序号" width="60" align="center" />
        <el-table-column prop="patternName" label="花型文件" min-width="160">
          <template slot-scope="scope">
            <i class="el-icon-document" style="margin-right: 5px; color: #409EFF;"></i>
            {{ scope.row.patternName }}
          </template>
        </el-table-column>
        <el-table-column prop="patternType" label="花型类型" width="120" />
        <el-table-column prop="stitches" label="针数" width="90" />
        <el-table-column prop="size" label="文件大小" width="100" />
        <el-table-column prop="deviceName" label="下发设备" width="140" />
        <el-table-column prop="unitPrice" label="工价" width="100" align="right">
          <template slot-scope="scope">
            {{ formatPrice(scope.row.unitPrice) }}
          </template>
        </el-table-column>
        <el-table-column prop="orderNo" label="订单号" width="120" />
        <el-table-column prop="status" label="结果" width="100" align="center">
          <template slot-scope="scope">
            <el-tag :type="scope.row.status === 'completed' ? 'success' : 'danger'" size="small">
              {{ getStatusText(scope.row.status) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="message" label="备注" min-width="120">
          <template slot-scope="scope">
            <span :class="{ 'text-danger': scope.row.status === 'failed' }">
              {{ scope.row.message || '-' }}
            </span>
          </template>
        </el-table-column>
        <el-table-column prop="createTime" label="下发时间" width="170" />
      </el-table>

      <el-pagination
        :current-page="pagination.page"
        :page-size="pagination.pageSize"
        :total="pagination.total"
        :page-sizes="[10, 20, 50, 100]"
        layout="total, sizes, prev, pager, next, jumper"
        @size-change="handleSizeChange"
        @current-change="handlePageChange"
      />
    </el-card>
  </div>
</template>

<script>
import { getDownloadLog, getPatternTypes } from '@/api/pattern'
import { saveTextWithDialog } from '@/utils/file-export'

export default {
  name: 'DownloadLog',
  data() {
    return {
      loading: false,
      logList: [],
      patternTypeOptions: [],
      searchForm: {
        patternName: '',
        patternType: '',
        orderNo: '',
        deviceName: '',
        status: '',
        dateRange: []
      },
      pagination: {
        page: 1,
        pageSize: 10,
        total: 0
      },
      tableHeight: 320
    }
  },
  mounted() {
    this.fetchPatternTypes()
    this.fetchData()
    window.addEventListener('resize', this.syncTableHeight)
  },
  beforeDestroy() {
    window.removeEventListener('resize', this.syncTableHeight)
  },
  methods: {
    formatPrice(value) {
      const num = Number(value || 0)
      return num.toFixed(3)
    },
    async fetchPatternTypes() {
      try {
        const res = await getPatternTypes()
        if (res.code === 0) {
          this.patternTypeOptions = res.data || []
        }
      } catch (error) {
        console.error('Failed to fetch pattern types:', error)
      }
    },
    async fetchData() {
      this.loading = true
      try {
        const res = await getDownloadLog({
          patternName: this.searchForm.patternName,
          patternType: this.searchForm.patternType,
          orderNo: this.searchForm.orderNo,
          deviceName: this.searchForm.deviceName,
          status: this.searchForm.status,
          startDate: this.searchForm.dateRange?.[0],
          endDate: this.searchForm.dateRange?.[1],
          page: this.pagination.page,
          pageSize: this.pagination.pageSize
        })
        if (res.code === 0) {
          this.logList = res.data.list || []
          this.pagination.total = res.data.total || 0
          this.$nextTick(() => {
            this.syncTableHeight()
          })
        }
      } catch (error) {
        console.error('Failed to fetch download log:', error)
      } finally {
        this.loading = false
      }
    },
    handleSearch() {
      this.pagination.page = 1
      this.fetchData()
    },
    handleReset() {
      this.searchForm = {
        patternName: '',
        patternType: '',
        orderNo: '',
        deviceName: '',
        status: '',
        dateRange: []
      }
      this.handleSearch()
    },
    handleSizeChange(size) {
      this.pagination.pageSize = size
      this.fetchData()
    },
    handlePageChange(page) {
      this.pagination.page = page
      this.fetchData()
    },
    syncTableHeight() {
      this.$nextTick(() => {
        const card = this.$refs.tableCard && this.$refs.tableCard.$el
        const table = this.$refs.tableRef && this.$refs.tableRef.$el
        if (!card || !table) return
        const body = card.querySelector('.el-card__body')
        if (!body) return
        let occupied = 0
        Array.from(body.children).forEach(child => {
          if (child === table) return
          occupied += child.offsetHeight
        })
        this.tableHeight = Math.max(240, body.clientHeight - occupied - 16)
      })
    },
    getStatusText(status) {
      const map = {
        completed: '成功',
        failed: '失败',
        waiting: '等待中',
        downloading: '下发中',
        paused: '已暂停'
      }
      return map[status] || status
    },
    async handleExport() {
      if (!this.logList.length) {
        this.$message.warning('暂无可导出的日志')
        return
      }

      const headers = ['花型文件', '花型类型', '针数', '文件大小', '下发设备', '工价', '订单号', '结果', '备注', '下发时间']
      const rows = this.logList.map(item => ([
        item.patternName || '',
        item.patternType || '',
        item.stitches || 0,
        item.size || '',
        item.deviceName || '',
        this.formatPrice(item.unitPrice),
        item.orderNo || '',
        this.getStatusText(item.status),
        item.message || '',
        item.createTime || ''
      ]))
      const csv = [headers, ...rows]
        .map(row => row.map(col => `\"${String(col).replace(/\"/g, '\"\"')}\"`).join(','))
        .join('\n')

      try {
        const saved = await saveTextWithDialog(`\uFEFF${csv}`, `download_log_${Date.now()}.csv`, {
          mimeType: 'text/csv;charset=utf-8;',
          description: 'CSV 文件',
          extensions: ['csv']
        })
        if (saved === null) {
          return
        }
        this.$message.success(saved ? '导出文件已保存' : '导出成功')
      } catch (error) {
        console.error('Failed to export download log:', error)
        this.$message.error('导出失败')
      }
    }
  }
}
</script>

<style lang="scss" scoped>
.action-group {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
}

.text-danger {
  color: #F56C6C;
}
</style>
