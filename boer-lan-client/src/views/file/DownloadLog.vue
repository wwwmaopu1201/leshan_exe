<template>
  <div class="page-container">
    <!-- 搜索栏 -->
    <div class="search-bar">
      <el-form :inline="true" :model="searchForm">
        <el-form-item label="花型文件">
          <el-input
            v-model="searchForm.patternName"
            placeholder="文件名"
            clearable
          />
        </el-form-item>
        <el-form-item label="目标设备">
          <el-input
            v-model="searchForm.deviceName"
            placeholder="设备名称"
            clearable
          />
        </el-form-item>
        <el-form-item label="状态">
          <el-select v-model="searchForm.status" clearable>
            <el-option label="全部" value="" />
            <el-option label="成功" value="success" />
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

    <!-- 表格 -->
    <div class="card">
      <div class="table-actions flex-between">
        <div>
          <el-button icon="el-icon-download" @click="handleExport">
            {{ $t('common.export') }}
          </el-button>
        </div>
        <div>
          <el-button icon="el-icon-refresh" circle @click="fetchData" />
        </div>
      </div>

      <el-table :data="logList" border v-loading="loading">
        <el-table-column type="index" label="序号" width="60" align="center" />
        <el-table-column prop="patternName" label="花型文件" min-width="180">
          <template slot-scope="scope">
            <i class="el-icon-document" style="margin-right: 5px; color: #409EFF;"></i>
            {{ scope.row.patternName }}
          </template>
        </el-table-column>
        <el-table-column prop="deviceName" label="目标设备" width="150" />
        <el-table-column prop="status" label="结果" width="100" align="center">
          <template slot-scope="scope">
            <el-tag :type="scope.row.status === 'success' ? 'success' : 'danger'" size="small">
              {{ scope.row.status === 'success' ? '成功' : '失败' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="message" label="备注" min-width="150">
          <template slot-scope="scope">
            <span :class="{ 'text-danger': scope.row.status === 'failed' }">
              {{ scope.row.message || '-' }}
            </span>
          </template>
        </el-table-column>
        <el-table-column prop="duration" label="耗时" width="100" />
        <el-table-column prop="operator" label="操作人" width="100" />
        <el-table-column prop="createTime" label="下发时间" width="160" />
      </el-table>

      <!-- 分页 -->
      <el-pagination
        :current-page="pagination.page"
        :page-size="pagination.pageSize"
        :total="pagination.total"
        :page-sizes="[10, 20, 50, 100]"
        layout="total, sizes, prev, pager, next, jumper"
        @size-change="handleSizeChange"
        @current-change="handlePageChange"
      />
    </div>
  </div>
</template>

<script>
import { getDownloadLog } from '@/api/pattern'

export default {
  name: 'DownloadLog',
  data() {
    return {
      loading: false,
      logList: [],
      searchForm: {
        patternName: '',
        deviceName: '',
        status: '',
        dateRange: []
      },
      pagination: {
        page: 1,
        pageSize: 10,
        total: 7
      }
    }
  },
  mounted() {
    this.fetchData()
  },
  methods: {
    async fetchData() {
      this.loading = true
      try {
        const res = await getDownloadLog({
          patternName: this.searchForm.patternName,
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
    handleExport() {
      this.$message.success('导出功能开发中')
    }
  }
}
</script>

<style lang="scss" scoped>
.text-danger {
  color: #F56C6C;
}
</style>
