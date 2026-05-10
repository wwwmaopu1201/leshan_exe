<template>
  <div class="page-shell">
    <div class="page-header">
      <div class="page-title-block">
        <h2>登录日志</h2>
        <p>查看服务端账号登录记录、登录 IP、登录状态和登录时间。</p>
      </div>
    </div>

    <div class="filter-panel">
      <el-form :inline="true" :model="searchForm">
        <el-form-item label="账号/IP">
          <el-input
            v-model.trim="searchForm.keyword"
            clearable
            placeholder="输入账号或登录 IP"
            @keyup.enter.native="handleSearch"
          />
        </el-form-item>
        <el-form-item label="登录状态">
          <el-select v-model="searchForm.status" clearable placeholder="全部状态">
            <el-option label="成功" value="成功" />
            <el-option label="失败" value="失败" />
          </el-select>
        </el-form-item>
        <el-form-item label="登录时间">
          <el-date-picker
            v-model="searchForm.dateRange"
            type="daterange"
            value-format="yyyy-MM-dd"
            range-separator="至"
            start-placeholder="开始日期"
            end-placeholder="结束日期"
          />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" icon="el-icon-search" @click="handleSearch">搜索</el-button>
          <el-button icon="el-icon-refresh" @click="handleReset">重置</el-button>
        </el-form-item>
      </el-form>
    </div>

    <el-card shadow="never" class="surface-card">
      <div class="action-row">
        <div class="soft-note">
          <i class="el-icon-document"></i>
          <span>共 {{ pagination.total }} 条登录记录</span>
        </div>
        <el-button icon="el-icon-refresh" @click="loadData">刷新</el-button>
      </div>

      <el-table
        :data="tableData"
        v-loading="loading"
        border
        style="width: 100%; margin-top: 18px;"
      >
        <el-table-column label="序号" width="70" align="center">
          <template slot-scope="{ $index }">
            {{ (pagination.page - 1) * pagination.pageSize + $index + 1 }}
          </template>
        </el-table-column>
        <el-table-column prop="username" label="账号" min-width="140" />
        <el-table-column prop="ip" label="登录IP" min-width="140" />
        <el-table-column prop="status" label="登录状态" width="110" align="center">
          <template slot-scope="{ row }">
            <span :class="['status-pill', row.status === '成功' ? 'success' : 'danger']">
              {{ row.status || '-' }}
            </span>
          </template>
        </el-table-column>
        <el-table-column prop="loginTime" label="登录时间" width="180" />
        <el-table-column prop="device" label="客户端信息" min-width="260" show-overflow-tooltip />
      </el-table>

      <el-pagination
        :current-page.sync="pagination.page"
        :page-size.sync="pagination.pageSize"
        :page-sizes="[10, 20, 50, 100]"
        :total="pagination.total"
        layout="total, sizes, prev, pager, next, jumper"
        class="table-pagination"
        @size-change="handleSizeChange"
        @current-change="handlePageChange"
      />
    </el-card>
  </div>
</template>

<script>
const defaultSearchForm = () => ({
  keyword: '',
  status: '',
  dateRange: []
})

export default {
  name: 'LoginLogs',
  data() {
    return {
      loading: false,
      tableData: [],
      searchForm: defaultSearchForm(),
      pagination: {
        page: 1,
        pageSize: 20,
        total: 0
      }
    }
  },
  mounted() {
    this.loadData()
  },
  methods: {
    async loadData() {
      this.loading = true
      try {
        const res = await this.$axios.get('/auth/login-logs', {
          params: {
            keyword: this.searchForm.keyword || undefined,
            status: this.searchForm.status || undefined,
            startDate: this.searchForm.dateRange?.[0] || undefined,
            endDate: this.searchForm.dateRange?.[1] || undefined,
            page: this.pagination.page,
            pageSize: this.pagination.pageSize
          }
        })
        if (res.code === 0) {
          this.tableData = Array.isArray(res.data?.list) ? res.data.list : []
          this.pagination.total = Number(res.data?.total || 0)
        }
      } catch (error) {
        console.error('加载登录日志失败', error)
        this.$message.error('加载登录日志失败')
      } finally {
        this.loading = false
      }
    },
    handleSearch() {
      this.pagination.page = 1
      this.loadData()
    },
    handleReset() {
      this.searchForm = defaultSearchForm()
      this.pagination.page = 1
      this.loadData()
    },
    handleSizeChange(size) {
      this.pagination.pageSize = size
      this.pagination.page = 1
      this.loadData()
    },
    handlePageChange(page) {
      this.pagination.page = page
      this.loadData()
    }
  }
}
</script>

<style lang="scss" scoped>
.table-pagination {
  margin-top: 16px;
  text-align: right;
}
</style>
