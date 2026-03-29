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
            <el-form-item label="设备名称">
              <el-input
                v-model.trim="searchForm.deviceKeyword"
                placeholder="按设备名称搜索明细"
                clearable
                @keyup.enter.native="handleSearch"
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

        <el-row :gutter="20" class="stat-row">
          <el-col :span="6">
            <div class="stat-card blue">
              <div class="stat-icon"><i class="el-icon-s-goods"></i></div>
              <div class="stat-info">
                <div class="stat-value">{{ overview.totalPieces.toLocaleString() }}</div>
                <div class="stat-label">加工总件数</div>
              </div>
            </div>
          </el-col>
          <el-col :span="6">
            <div class="stat-card green">
              <div class="stat-icon"><i class="el-icon-sort"></i></div>
              <div class="stat-info">
                <div class="stat-value">{{ overview.totalThread.toLocaleString() }}</div>
                <div class="stat-label">总用线量(m)</div>
              </div>
            </div>
          </el-col>
          <el-col :span="6">
            <div class="stat-card orange">
              <div class="stat-icon"><i class="el-icon-time"></i></div>
              <div class="stat-info">
                <div class="stat-value">{{ overview.totalHours }}</div>
                <div class="stat-label">总运行时长(h)</div>
              </div>
            </div>
          </el-col>
          <el-col :span="6">
            <div class="stat-card">
              <div class="stat-icon"><i class="el-icon-data-analysis"></i></div>
              <div class="stat-info">
                <div class="stat-value">{{ overview.avgEfficiency }}%</div>
                <div class="stat-label">平均效率</div>
              </div>
            </div>
          </el-col>
        </el-row>

        <el-row :gutter="20" class="chart-row">
          <el-col :span="15">
            <div class="chart-card">
              <div class="chart-title">日产量趋势</div>
              <div class="chart-subtitle">按日查看产量与效率变化</div>
              <div ref="productionChart" class="chart-container"></div>
            </div>
          </el-col>
          <el-col :span="9">
            <div class="chart-card">
              <div class="chart-title">设备产量分布</div>
              <div class="chart-subtitle">按设备查看当前范围内产量占比</div>
              <div ref="devicePieChart" class="chart-container"></div>
            </div>
          </el-col>
        </el-row>

        <div class="card">
          <div class="card-header flex-between">
            <span>设备加工明细</span>
            <el-button type="primary" size="small" icon="el-icon-download" @click="handleExport">
              {{ $t('statistics.exportExcel') }}
            </el-button>
          </div>
          <el-table
            :data="pagedTableData"
            border
            v-loading="loading"
            :max-height="tableMaxHeight"
            empty-text="暂无数据"
          >
            <el-table-column type="index" label="序号" width="60" align="center" />
            <el-table-column prop="deviceName" label="设备名称" min-width="120" />
            <el-table-column prop="employeeCode" label="员工工号" width="100" />
            <el-table-column prop="employeeName" label="员工姓名" width="110" />
            <el-table-column prop="date" label="日期" width="110" />
            <el-table-column prop="patternName" label="花型名称" min-width="150" />
            <el-table-column prop="patternStitches" label="花型针数" width="100" align="right" />
            <el-table-column prop="sewSpeed" label="缝纫速度(针/分钟)" width="140" align="right" />
            <el-table-column prop="startTime" label="开始时间" width="160" />
            <el-table-column prop="processCount" label="加工次数" width="90" align="right" />
            <el-table-column prop="avgProcessDuration" label="平均加工时长(min/次)" width="160" align="right" />
            <el-table-column prop="patternSewCount" label="花型缝纫次数" width="120" align="right" />
            <el-table-column prop="alarmInfo" label="报警信息" min-width="140" />
            <el-table-column prop="alarmTime" label="报警时间" width="160" />
            <el-table-column prop="cumulativeUpTime" label="累计开机时长(h)" width="130" align="right" />
          </el-table>

          <el-pagination
            :current-page="pagination.page"
            :page-size="pagination.pageSize"
            :total="pagination.total"
            layout="total, sizes, prev, pager, next, jumper"
            @size-change="handleSizeChange"
            @current-change="handlePageChange"
          />
        </div>
      </section>
    </div>
  </div>
</template>

<script>
import * as echarts from 'echarts'
import { getProcessOverview, exportStatistics } from '@/api/statistics'
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
  name: 'ProcessOverview',
  components: {
    DeviceTreePanel
  },
  data() {
    return {
      loading: false,
      searchForm: {
        dateRange: getDefaultRange(),
        deviceKeyword: '',
        deviceFilter: defaultDeviceFilter()
      },
      overview: {
        totalPieces: 0,
        totalThread: 0,
        totalHours: 0,
        avgEfficiency: 0
      },
      tableData: [],
      chartData: {
        productionTrend: [],
        deviceDistribution: []
      },
      pagination: {
        page: 1,
        pageSize: 10,
        total: 0
      },
      charts: {}
    }
  },
  computed: {
    tableMaxHeight() {
      return 'calc(100vh - 390px)'
    },
    pagedTableData() {
      const start = (this.pagination.page - 1) * this.pagination.pageSize
      return this.tableData.slice(start, start + this.pagination.pageSize)
    }
  },
  mounted() {
    this.fetchData()
    window.addEventListener('resize', this.handleResize)
  },
  beforeDestroy() {
    window.removeEventListener('resize', this.handleResize)
    Object.values(this.charts).forEach(chart => chart && chart.dispose())
  },
  methods: {
    async fetchData() {
      this.loading = true
      try {
        const res = await getProcessOverview({
          startDate: this.searchForm.dateRange?.[0],
          endDate: this.searchForm.dateRange?.[1],
          deviceId: this.searchForm.deviceFilter.deviceId,
          deviceIds: this.searchForm.deviceFilter.deviceIds.join(','),
          page: 1,
          pageSize: 2000
        })
        if (res.code === 0) {
          this.overview = res.data.overview || { totalPieces: 0, totalThread: 0, totalHours: 0, avgEfficiency: 0 }
          const rawList = res.data.list || []
          this.tableData = this.applyLocalFilters(rawList)
          this.pagination.total = this.tableData.length
          this.chartData = {
            productionTrend: res.data.productionTrend || [],
            deviceDistribution: res.data.deviceDistribution || []
          }
          this.$nextTick(() => {
            this.initCharts()
          })
        }
      } catch (error) {
        console.error('Failed to fetch process overview:', error)
      } finally {
        this.loading = false
      }
    },
    applyLocalFilters(list) {
      const keyword = String(this.searchForm.deviceKeyword || '').trim().toLowerCase()
      if (!keyword) return list
      return list.filter(item => String(item.deviceName || '').toLowerCase().includes(keyword))
    },
    handleSearch() {
      this.pagination.page = 1
      this.fetchData()
    },
    handleReset() {
      this.searchForm = {
        dateRange: getDefaultRange(),
        deviceKeyword: '',
        deviceFilter: defaultDeviceFilter()
      }
      this.handleSearch()
    },
    handleSizeChange(size) {
      this.pagination.pageSize = size
    },
    handlePageChange(page) {
      this.pagination.page = page
    },
    async handleExport() {
      try {
        const response = await exportStatistics('process', {
          startDate: this.searchForm.dateRange?.[0],
          endDate: this.searchForm.dateRange?.[1],
          deviceId: this.searchForm.deviceFilter.deviceId,
          deviceIds: this.searchForm.deviceFilter.deviceIds.join(',')
        })
        this.downloadBlob(response, `process_overview_${Date.now()}.csv`)
        this.$message.success('导出成功')
      } catch (error) {
        console.error('Failed to export process overview:', error)
      }
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
      this.initProductionChart()
      this.initDevicePieChart()
    },
    initProductionChart() {
      const chart = this.getOrCreateChart('production', this.$refs.productionChart)
      const trendData = this.chartData.productionTrend || []
      chart.setOption({
        tooltip: { trigger: 'axis' },
        legend: {
          top: 0,
          data: ['产量', '效率'],
          textStyle: { color: '#6a7f9d' }
        },
        grid: { left: '4%', right: '4%', bottom: '4%', top: 40, containLabel: true },
        xAxis: {
          type: 'category',
          axisLabel: { color: '#6a7f9d' },
          axisLine: { lineStyle: { color: '#dbe4f0' } },
          data: trendData.map(item => item.date)
        },
        yAxis: [
          { type: 'value', name: '产量(件)', axisLabel: { color: '#6a7f9d' }, splitLine: { lineStyle: { color: '#edf2f8' } } },
          { type: 'value', name: '效率(%)', max: 100, axisLabel: { color: '#6a7f9d' } }
        ],
        series: [
          {
            name: '产量',
            type: 'bar',
            data: trendData.map(item => item.pieces ?? item.value ?? 0),
            itemStyle: { color: '#2f6df6', borderRadius: [10, 10, 0, 0] }
          },
          {
            name: '效率',
            type: 'line',
            yAxisIndex: 1,
            smooth: true,
            data: trendData.map(item => item.efficiency ?? 0),
            lineStyle: { color: '#2fb46e', width: 3 },
            itemStyle: { color: '#2fb46e' }
          }
        ]
      }, true)
    },
    initDevicePieChart() {
      const chart = this.getOrCreateChart('devicePie', this.$refs.devicePieChart)
      const pieData = this.chartData.deviceDistribution || []
      chart.setOption({
        tooltip: { trigger: 'item', formatter: '{b}: {c} ({d}%)' },
        legend: {
          orient: 'vertical',
          left: 0,
          top: 'middle',
          textStyle: { color: '#6a7f9d' }
        },
        series: [{
          type: 'pie',
          radius: ['46%', '68%'],
          center: ['68%', '50%'],
          data: pieData,
          color: ['#2f6df6', '#4aa7ff', '#2fb46e', '#f0b037', '#ef5a5a']
        }]
      }, true)
    },
    handleResize() {
      Object.values(this.charts).forEach(chart => chart && chart.resize())
    }
  }
}
</script>
