<template>
  <div class="page-container">
    <!-- 搜索栏 -->
    <div class="search-bar">
      <el-form :inline="true" :model="searchForm">
        <el-form-item :label="$t('statistics.dateRange')">
          <el-date-picker
            v-model="searchForm.dateRange"
            type="daterange"
            range-separator="至"
            start-placeholder="开始日期"
            end-placeholder="结束日期"
            value-format="yyyy-MM-dd"
          />
        </el-form-item>
        <el-form-item :label="$t('statistics.employee')">
          <el-select v-model="searchForm.employeeId" clearable placeholder="全部员工">
            <el-option
              v-for="emp in employeeList"
              :key="emp.id"
              :label="emp.name"
              :value="emp.id"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="员工检索">
          <el-input
            v-model.trim="searchForm.employeeKeyword"
            placeholder="姓名/工号"
            clearable
            @keyup.enter.native="handleSearch"
          />
        </el-form-item>
        <el-form-item :label="$t('statistics.device')">
          <device-tree-filter v-model="searchForm.deviceFilter" />
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

    <!-- 统计卡片 -->
    <el-row :gutter="20" class="stat-cards">
      <el-col :span="8">
        <div class="stat-card blue">
          <div class="stat-icon"><i class="el-icon-money"></i></div>
          <div class="stat-info">
            <div class="stat-value">{{ summaryData.totalSalary.toLocaleString() }}</div>
            <div class="stat-label">{{ $t('statistics.totalSalary') }} (元)</div>
          </div>
        </div>
      </el-col>
      <el-col :span="8">
        <div class="stat-card green">
          <div class="stat-icon"><i class="el-icon-s-goods"></i></div>
          <div class="stat-info">
            <div class="stat-value">{{ summaryData.totalPieces.toLocaleString() }}</div>
            <div class="stat-label">{{ $t('statistics.totalPieces') }} (件)</div>
          </div>
        </div>
      </el-col>
      <el-col :span="8">
        <div class="stat-card orange">
          <div class="stat-icon"><i class="el-icon-s-marketing"></i></div>
          <div class="stat-info">
            <div class="stat-value">{{ summaryData.averageSalary.toFixed(2) }}</div>
            <div class="stat-label">{{ $t('statistics.averageSalary') }} (元/人)</div>
          </div>
        </div>
      </el-col>
    </el-row>

    <!-- 图表区域 -->
    <el-row :gutter="20" class="chart-row">
      <el-col :span="12">
        <div class="chart-card">
          <div class="chart-title">员工工资排行</div>
          <div ref="salaryRankChart" class="chart-container"></div>
        </div>
      </el-col>
      <el-col :span="12">
        <div class="chart-card">
          <div class="chart-title">日工资趋势</div>
          <div ref="salaryTrendChart" class="chart-container"></div>
        </div>
      </el-col>
    </el-row>

    <!-- 数据表格 -->
    <div class="card">
      <div class="card-header flex-between">
        <span>{{ $t('statistics.salaryDetail') }}</span>
        <el-dropdown @command="handleExportCommand">
          <el-button type="primary" size="small" icon="el-icon-download">
            {{ $t('statistics.exportExcel') }}<i class="el-icon-arrow-down el-icon--right"></i>
          </el-button>
          <el-dropdown-menu slot="dropdown">
            <el-dropdown-item command="current">导出当前页</el-dropdown-item>
            <el-dropdown-item command="all">导出全部</el-dropdown-item>
            <el-dropdown-item command="merged">导出合并后</el-dropdown-item>
          </el-dropdown-menu>
        </el-dropdown>
      </div>
      <el-table :data="tableData" border v-loading="loading" show-summary>
        <el-table-column prop="employeeName" label="员工姓名" width="120" />
        <el-table-column prop="employeeCode" label="员工编号" width="100" />
        <el-table-column v-if="!hideDeviceColumn" prop="deviceName" label="使用设备" width="120" />
        <el-table-column prop="totalPieces" label="加工件数" width="100" align="right" />
        <el-table-column prop="unitPrice" label="单价(元)" width="100" align="right" />
        <el-table-column prop="salary" label="工资(元)" width="120" align="right">
          <template slot-scope="scope">
            <span class="text-primary">{{ scope.row.salary.toFixed(2) }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="bonus" label="奖金(元)" width="100" align="right" />
        <el-table-column prop="totalAmount" label="合计(元)" width="120" align="right">
          <template slot-scope="scope">
            <span class="text-success">{{ scope.row.totalAmount.toFixed(2) }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="date" label="日期" width="150">
          <template slot-scope="scope">
            <el-link type="primary" @click="openSalaryDetail(scope.row)">
              {{ scope.row.date }}
            </el-link>
          </template>
        </el-table-column>
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
    </div>

    <el-dialog
      :title="detailDialog.title"
      :visible.sync="detailDialog.visible"
      width="980px"
      append-to-body
    >
      <el-table :data="detailDialog.rows" border v-loading="detailDialog.loading" max-height="460">
        <el-table-column type="index" label="序号" width="60" align="center" />
        <el-table-column prop="deviceName" label="设备名称" min-width="120" />
        <el-table-column prop="patternName" label="花型名称" min-width="150" />
        <el-table-column prop="patternStitches" label="花型针数" width="100" align="right" />
        <el-table-column prop="startTime" label="开始时间" width="160" />
        <el-table-column prop="endTime" label="结束时间" width="160" />
        <el-table-column prop="sewCount" label="缝纫次数" width="100" align="right" />
        <el-table-column prop="unitPrice" label="单价(元)" width="100" align="right" />
        <el-table-column prop="totalAmount" label="金额(元)" width="110" align="right">
          <template slot-scope="scope">
            <span class="text-success">{{ Number(scope.row.totalAmount || 0).toFixed(2) }}</span>
          </template>
        </el-table-column>
      </el-table>
    </el-dialog>
  </div>
</template>

<script>
import * as echarts from 'echarts'
import { getSalaryStats, getSalaryDetail, exportStatistics } from '@/api/statistics'
import { getEmployeeList } from '@/api/employee'
import DeviceTreeFilter from '@/components/DeviceTreeFilter.vue'

export default {
  name: 'SalaryStats',
  components: {
    DeviceTreeFilter
  },
  data() {
    return {
      loading: false,
      searchForm: {
        dateRange: [],
        employeeId: '',
        employeeKeyword: '',
        deviceFilter: {
          label: '',
          nodeType: '',
          deviceId: '',
          deviceIds: []
        }
      },
      employeeList: [],
      summaryData: {
        totalSalary: 0,
        totalPieces: 0,
        averageSalary: 0
      },
      tableData: [],
      chartData: {
        salaryRank: [],
        salaryTrend: []
      },
      pagination: {
        page: 1,
        pageSize: 10,
        total: 0
      },
      charts: {},
      hideDeviceColumn: false,
      detailDialog: {
        visible: false,
        loading: false,
        title: '工资明细',
        rows: []
      }
    }
  },
  mounted() {
    this.fetchEmployees()
    this.fetchData()
    window.addEventListener('resize', this.handleResize)
  },
  beforeDestroy() {
    window.removeEventListener('resize', this.handleResize)
    Object.values(this.charts).forEach(chart => chart && chart.dispose())
  },
  methods: {
    async fetchEmployees() {
      try {
        const res = await getEmployeeList({ pageSize: 100 })
        if (res.code === 0) {
          this.employeeList = res.data.list || []
        }
      } catch (error) {
        console.error('Failed to fetch employees:', error)
      }
    },
    async fetchData() {
      this.loading = true
      try {
        const res = await getSalaryStats({
          startDate: this.searchForm.dateRange?.[0],
          endDate: this.searchForm.dateRange?.[1],
          employeeId: this.searchForm.employeeId,
          employeeKeyword: this.searchForm.employeeKeyword,
          deviceId: this.searchForm.deviceFilter.deviceId,
          deviceIds: this.searchForm.deviceFilter.deviceIds.join(','),
          page: this.pagination.page,
          pageSize: this.pagination.pageSize
        })
        if (res.code === 0) {
          this.summaryData = res.data.summary || { totalSalary: 0, totalPieces: 0, averageSalary: 0 }
          this.tableData = res.data.list || []
          this.pagination.total = res.data.total || 0
          this.chartData = {
            salaryRank: res.data.salaryRank || [],
            salaryTrend: res.data.salaryTrend || []
          }
          this.$nextTick(() => {
            this.initCharts()
          })
        }
      } catch (error) {
        console.error('Failed to fetch salary stats:', error)
      } finally {
        this.loading = false
      }
    },
    handleSearch() {
      this.hideDeviceColumn = true
      this.pagination.page = 1
      this.fetchData()
    },
    handleReset() {
      this.hideDeviceColumn = false
      this.searchForm = {
        dateRange: [],
        employeeId: '',
        employeeKeyword: '',
        deviceFilter: {
          label: '',
          nodeType: '',
          deviceId: '',
          deviceIds: []
        }
      }
      this.pagination.page = 1
      this.fetchData()
    },
    handleSizeChange(size) {
      this.pagination.pageSize = size
      this.fetchData()
    },
    handlePageChange(page) {
      this.pagination.page = page
      this.fetchData()
    },
    async openSalaryDetail(row) {
      this.detailDialog.visible = true
      this.detailDialog.loading = true
      this.detailDialog.rows = []
      this.detailDialog.title = `${row.employeeName || '-'} ${row.date || ''} 工资明细`
      try {
        const res = await getSalaryDetail({
          date: row.date,
          employeeId: row.employeeId || this.searchForm.employeeId,
          deviceId: this.searchForm.deviceFilter.deviceId,
          deviceIds: this.searchForm.deviceFilter.deviceIds.join(','),
          startDate: this.searchForm.dateRange?.[0],
          endDate: this.searchForm.dateRange?.[1]
        })
        if (res.code === 0) {
          this.detailDialog.rows = Array.isArray(res.data) ? res.data : []
        }
      } catch (error) {
        console.error('Failed to fetch salary detail:', error)
      } finally {
        this.detailDialog.loading = false
      }
    },
    async handleExport(mode = 'all') {
      try {
        const response = await exportStatistics('salary', {
          startDate: this.searchForm.dateRange?.[0],
          endDate: this.searchForm.dateRange?.[1],
          employeeId: this.searchForm.employeeId,
          employeeKeyword: this.searchForm.employeeKeyword,
          deviceId: this.searchForm.deviceFilter.deviceId,
          deviceIds: this.searchForm.deviceFilter.deviceIds.join(','),
          mode,
          page: this.pagination.page,
          pageSize: this.pagination.pageSize
        })
        this.downloadBlob(response, `salary_stats_${Date.now()}.csv`)
        this.$message.success('导出成功')
      } catch (error) {
        console.error('Failed to export salary stats:', error)
      }
    },
    handleExportCommand(mode) {
      this.handleExport(mode)
    },
    getOrCreateChart(key, ref) {
      if (this.charts[key]) {
        return this.charts[key]
      }
      const chart = echarts.init(ref)
      this.charts[key] = chart
      return chart
    },
    parseFileName(contentDisposition, fallbackName) {
      if (!contentDisposition) return fallbackName
      const utf8Match = contentDisposition.match(/filename\*=UTF-8''([^;]+)/i)
      if (utf8Match && utf8Match[1]) {
        return decodeURIComponent(utf8Match[1])
      }
      const normalMatch = contentDisposition.match(/filename="?([^";]+)"?/i)
      return normalMatch?.[1] || fallbackName
    },
    downloadBlob(response, fallbackName) {
      const blob = response.data instanceof Blob
        ? response.data
        : new Blob([response.data], { type: 'text/csv;charset=utf-8;' })
      const filename = this.parseFileName(response.headers?.['content-disposition'], fallbackName)
      const url = window.URL.createObjectURL(blob)
      const link = document.createElement('a')
      link.href = url
      link.download = filename
      document.body.appendChild(link)
      link.click()
      document.body.removeChild(link)
      window.URL.revokeObjectURL(url)
    },
    initCharts() {
      this.initSalaryRankChart()
      this.initSalaryTrendChart()
    },
    initSalaryRankChart() {
      const chart = this.getOrCreateChart('salaryRank', this.$refs.salaryRankChart)
      const rankData = this.chartData.salaryRank || []
      chart.setOption({
        tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' } },
        grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
        xAxis: { type: 'value' },
        yAxis: {
          type: 'category',
          data: rankData.map(item => item.name)
        },
        series: [{
          type: 'bar',
          data: rankData.map(item => item.value),
          barWidth: 20,
          itemStyle: {
            color: new echarts.graphic.LinearGradient(0, 0, 1, 0, [
              { offset: 0, color: '#409EFF' },
              { offset: 1, color: '#67C23A' }
            ]),
            borderRadius: [0, 4, 4, 0]
          }
        }]
      }, true)
    },
    initSalaryTrendChart() {
      const chart = this.getOrCreateChart('salaryTrend', this.$refs.salaryTrendChart)
      const trendData = this.chartData.salaryTrend || []
      chart.setOption({
        tooltip: { trigger: 'axis' },
        grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
        xAxis: {
          type: 'category',
          data: trendData.map(item => item.date)
        },
        yAxis: { type: 'value' },
        series: [{
          type: 'line',
          data: trendData.map(item => item.value),
          smooth: true,
          lineStyle: { color: '#409EFF', width: 3 },
          itemStyle: { color: '#409EFF' },
          areaStyle: {
            color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
              { offset: 0, color: 'rgba(64, 158, 255, 0.3)' },
              { offset: 1, color: 'rgba(64, 158, 255, 0.05)' }
            ])
          }
        }]
      }, true)
    },
    handleResize() {
      Object.values(this.charts).forEach(chart => chart && chart.resize())
    }
  }
}
</script>

<style lang="scss" scoped>
.stat-cards {
  margin-bottom: 20px;
}

.stat-card {
  display: flex;
  align-items: center;
  padding: 20px;
  border-radius: 8px;
  color: #fff;

  &.blue {
    background: linear-gradient(135deg, #409EFF 0%, #2d8cf0 100%);
  }
  &.green {
    background: linear-gradient(135deg, #67C23A 0%, #5daf34 100%);
  }
  &.orange {
    background: linear-gradient(135deg, #E6A23C 0%, #d69330 100%);
  }

  .stat-icon {
    width: 60px;
    height: 60px;
    background: rgba(255, 255, 255, 0.2);
    border-radius: 50%;
    display: flex;
    align-items: center;
    justify-content: center;
    margin-right: 15px;

    i { font-size: 28px; }
  }

  .stat-info {
    .stat-value {
      font-size: 28px;
      font-weight: bold;
    }
    .stat-label {
      font-size: 14px;
      opacity: 0.9;
    }
  }
}

.chart-row {
  margin-bottom: 20px;
}

.chart-card {
  background: #fff;
  border-radius: 8px;
  padding: 20px;

  .chart-title {
    font-size: 16px;
    font-weight: 600;
    margin-bottom: 15px;
  }

  .chart-container {
    height: 280px;
  }
}

.text-primary {
  color: #409EFF;
  font-weight: bold;
}

.text-success {
  color: #67C23A;
  font-weight: bold;
}
</style>
