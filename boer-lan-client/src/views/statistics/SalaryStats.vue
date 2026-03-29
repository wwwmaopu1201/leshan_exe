<template>
  <div class="page-container">
    <div class="stats-layout">
      <aside class="stats-side">
        <device-tree-panel
          v-model="searchForm.deviceFilter"
          title="设备树筛选"
          :min-height="620"
          @change="handleSearch"
        />
      </aside>

      <section class="stats-main">
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
                placeholder="姓名 / 工号"
                clearable
                @keyup.enter.native="handleSearch"
              />
            </el-form-item>
            <el-form-item label="设备名称">
              <el-input
                v-model.trim="searchForm.deviceKeyword"
                placeholder="支持设备名模糊筛选"
                clearable
                @keyup.enter.native="handleSearch"
              />
            </el-form-item>
            <el-form-item label="工资区间">
              <div class="salary-range">
                <el-input-number
                  v-model="searchForm.salaryMin"
                  :min="0"
                  :precision="2"
                  controls-position="right"
                  placeholder="最低"
                />
                <span>至</span>
                <el-input-number
                  v-model="searchForm.salaryMax"
                  :min="0"
                  :precision="2"
                  controls-position="right"
                  placeholder="最高"
                />
              </div>
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

        <el-row :gutter="20" class="stat-cards">
          <el-col :span="8">
            <div class="stat-card blue">
              <div class="stat-icon"><i class="el-icon-money"></i></div>
              <div class="stat-info">
                <div class="stat-value">{{ effectiveSummary.totalSalary.toFixed(2) }}</div>
                <div class="stat-label">{{ $t('statistics.totalSalary') }} (元)</div>
              </div>
            </div>
          </el-col>
          <el-col :span="8">
            <div class="stat-card green">
              <div class="stat-icon"><i class="el-icon-s-goods"></i></div>
              <div class="stat-info">
                <div class="stat-value">{{ effectiveSummary.totalPieces }}</div>
                <div class="stat-label">{{ $t('statistics.totalPieces') }} (件)</div>
              </div>
            </div>
          </el-col>
          <el-col :span="8">
            <div class="stat-card orange">
              <div class="stat-icon"><i class="el-icon-s-marketing"></i></div>
              <div class="stat-info">
                <div class="stat-value">{{ effectiveSummary.averageSalary.toFixed(2) }}</div>
                <div class="stat-label">{{ $t('statistics.averageSalary') }} (元/人)</div>
              </div>
            </div>
          </el-col>
        </el-row>

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

          <el-table
            :data="pagedTableData"
            border
            v-loading="loading"
            show-summary
            :max-height="tableMaxHeight"
            empty-text="暂无数据"
          >
            <el-table-column prop="employeeName" label="员工姓名" width="120" />
            <el-table-column prop="employeeCode" label="员工工号" width="110" />
            <el-table-column prop="deviceName" label="设备名称" width="130" />
            <el-table-column prop="workDays" label="工作天数" width="100" align="right">
              <template slot-scope="scope">
                {{ scope.row.workDays || scope.row.workDayCount || '-' }}
              </template>
            </el-table-column>
            <el-table-column prop="patternName" label="花型名称" min-width="150">
              <template slot-scope="scope">
                {{ scope.row.patternName || '-' }}
              </template>
            </el-table-column>
            <el-table-column prop="orderNo" label="订单号" width="130">
              <template slot-scope="scope">
                {{ scope.row.orderNo || '-' }}
              </template>
            </el-table-column>
            <el-table-column prop="totalPieces" label="加工件数" width="100" align="right" />
            <el-table-column prop="unitPrice" label="单价(元)" width="100" align="right" />
            <el-table-column prop="salary" label="工资(元)" width="120" align="right">
              <template slot-scope="scope">
                <span class="text-primary">{{ Number(scope.row.salary || 0).toFixed(2) }}</span>
              </template>
            </el-table-column>
            <el-table-column prop="bonus" label="奖金(元)" width="100" align="right" />
            <el-table-column prop="totalAmount" label="合计(元)" width="120" align="right">
              <template slot-scope="scope">
                <span class="text-success">{{ Number(scope.row.totalAmount || 0).toFixed(2) }}</span>
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
      </section>
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
        <el-table-column prop="orderNo" label="订单号" width="120">
          <template slot-scope="scope">
            {{ scope.row.orderNo || '-' }}
          </template>
        </el-table-column>
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
import DeviceTreePanel from '@/components/DeviceTreePanel.vue'

const getDefaultRange = () => {
  const end = new Date()
  const start = new Date()
  start.setDate(end.getDate() - 6)
  const format = (date) => {
    const year = date.getFullYear()
    const month = String(date.getMonth() + 1).padStart(2, '0')
    const day = String(date.getDate()).padStart(2, '0')
    return `${year}-${month}-${day}`
  }
  return [format(start), format(end)]
}

const defaultDeviceFilter = () => ({
  label: '',
  nodeType: '',
  groupId: '',
  deviceId: '',
  deviceIds: []
})

export default {
  name: 'SalaryStats',
  components: {
    DeviceTreePanel
  },
  data() {
    return {
      loading: false,
      searchForm: {
        dateRange: getDefaultRange(),
        employeeId: '',
        employeeKeyword: '',
        deviceKeyword: '',
        salaryMin: null,
        salaryMax: null,
        deviceFilter: defaultDeviceFilter()
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
      detailDialog: {
        visible: false,
        loading: false,
        title: '工资明细',
        rows: []
      }
    }
  },
  computed: {
    tableMaxHeight() {
      return 'calc(100vh - 410px)'
    },
    pagedTableData() {
      const start = (this.pagination.page - 1) * this.pagination.pageSize
      return this.tableData.slice(start, start + this.pagination.pageSize)
    },
    hasLocalFilter() {
      return !!(this.searchForm.deviceKeyword || this.searchForm.salaryMin !== null || this.searchForm.salaryMax !== null)
    },
    effectiveSummary() {
      if (!this.hasLocalFilter) {
        return {
          totalSalary: Number(this.summaryData.totalSalary || 0),
          totalPieces: Number(this.summaryData.totalPieces || 0),
          averageSalary: Number(this.summaryData.averageSalary || 0)
        }
      }
      const totalSalary = this.tableData.reduce((sum, row) => sum + Number(row.salary || row.totalAmount || 0), 0)
      const totalPieces = this.tableData.reduce((sum, row) => sum + Number(row.totalPieces || 0), 0)
      const employeeIds = new Set(this.tableData.map(row => row.employeeId || row.employeeCode || row.employeeName))
      return {
        totalSalary,
        totalPieces,
        averageSalary: employeeIds.size ? totalSalary / employeeIds.size : 0
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
        const res = await getEmployeeList({ page: 1, pageSize: 1000 })
        if (res.code === 0) {
          this.employeeList = Array.isArray(res.data) ? res.data : (res.data?.list || [])
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
          deviceKeyword: this.searchForm.deviceKeyword,
          salaryMin: this.searchForm.salaryMin,
          salaryMax: this.searchForm.salaryMax,
          page: 1,
          pageSize: 2000
        })
        if (res.code === 0) {
          const rawList = res.data.list || []
          this.summaryData = res.data.summary || { totalSalary: 0, totalPieces: 0, averageSalary: 0 }
          this.tableData = this.applyLocalFilters(rawList)
          this.pagination.total = this.tableData.length
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
    applyLocalFilters(list) {
      return list.filter(row => {
        const deviceKeyword = String(this.searchForm.deviceKeyword || '').trim().toLowerCase()
        const salaryMin = this.searchForm.salaryMin
        const salaryMax = this.searchForm.salaryMax
        const amount = Number(row.salary || row.totalAmount || 0)
        const matchedDevice = !deviceKeyword || String(row.deviceName || '').toLowerCase().includes(deviceKeyword)
        const matchedMin = salaryMin === null || salaryMin === undefined || amount >= Number(salaryMin)
        const matchedMax = salaryMax === null || salaryMax === undefined || amount <= Number(salaryMax)
        return matchedDevice && matchedMin && matchedMax
      })
    },
    handleSearch() {
      this.pagination.page = 1
      this.fetchData()
    },
    handleReset() {
      this.searchForm = {
        dateRange: getDefaultRange(),
        employeeId: '',
        employeeKeyword: '',
        deviceKeyword: '',
        salaryMin: null,
        salaryMax: null,
        deviceFilter: defaultDeviceFilter()
      }
      this.pagination.page = 1
      this.fetchData()
    },
    handleSizeChange(size) {
      this.pagination.pageSize = size
    },
    handlePageChange(page) {
      this.pagination.page = page
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
          deviceKeyword: this.searchForm.deviceKeyword,
          salaryMin: this.searchForm.salaryMin,
          salaryMax: this.searchForm.salaryMax,
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
        grid: { left: '4%', right: '4%', bottom: '3%', top: 16, containLabel: true },
        xAxis: { type: 'value', axisLabel: { color: '#6a7f9d' } },
        yAxis: {
          type: 'category',
          axisLabel: { color: '#6a7f9d' },
          data: rankData.map(item => item.name)
        },
        series: [{
          type: 'bar',
          data: rankData.map(item => item.value),
          barWidth: 18,
          itemStyle: {
            color: new echarts.graphic.LinearGradient(0, 0, 1, 0, [
              { offset: 0, color: '#2f6df6' },
              { offset: 1, color: '#2fb46e' }
            ]),
            borderRadius: [0, 12, 12, 0]
          }
        }]
      }, true)
    },
    initSalaryTrendChart() {
      const chart = this.getOrCreateChart('salaryTrend', this.$refs.salaryTrendChart)
      const trendData = this.chartData.salaryTrend || []
      chart.setOption({
        tooltip: { trigger: 'axis' },
        grid: { left: '4%', right: '4%', bottom: '3%', top: 16, containLabel: true },
        xAxis: {
          type: 'category',
          data: trendData.map(item => item.date),
          axisLabel: { color: '#6a7f9d' },
          axisLine: { lineStyle: { color: '#dbe4f0' } }
        },
        yAxis: {
          type: 'value',
          axisLabel: { color: '#6a7f9d' },
          splitLine: { lineStyle: { color: '#edf2f8' } }
        },
        series: [{
          type: 'line',
          smooth: true,
          data: trendData.map(item => item.value),
          symbol: 'circle',
          symbolSize: 8,
          lineStyle: { width: 3, color: '#2f6df6' },
          itemStyle: { color: '#2f6df6' },
          areaStyle: {
            color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
              { offset: 0, color: 'rgba(47, 109, 246, 0.22)' },
              { offset: 1, color: 'rgba(47, 109, 246, 0.05)' }
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
.salary-range {
  display: flex;
  align-items: center;
  gap: 8px;

  span {
    color: #7b8da6;
    font-size: 12px;
  }
}
</style>
