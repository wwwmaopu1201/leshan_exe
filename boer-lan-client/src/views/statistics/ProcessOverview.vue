<template>
  <div class="stats-layout">
    <div class="stats-side">
      <device-tree-panel v-model="searchForm.deviceFilter" />
    </div>
    <div class="stats-main page-container">
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
      <el-row :gutter="20" class="stat-row">
        <el-col :span="6">
          <div class="stat-card">
            <div class="stat-icon blue"><i class="el-icon-s-goods"></i></div>
            <div class="stat-info">
              <div class="stat-value">{{ overview.totalPieces.toLocaleString() }}</div>
              <div class="stat-label">加工总件数</div>
            </div>
          </div>
        </el-col>
        <el-col :span="6">
          <div class="stat-card">
            <div class="stat-icon green"><i class="el-icon-sort"></i></div>
            <div class="stat-info">
              <div class="stat-value">{{ overview.totalThread.toLocaleString() }}</div>
              <div class="stat-label">总用线量(m)</div>
            </div>
          </div>
        </el-col>
        <el-col :span="6">
          <div class="stat-card">
            <div class="stat-icon orange"><i class="el-icon-time"></i></div>
            <div class="stat-info">
              <div class="stat-value">{{ overview.totalHours }}</div>
              <div class="stat-label">总运行时长(h)</div>
            </div>
          </div>
        </el-col>
        <el-col :span="6">
          <div class="stat-card">
            <div class="stat-icon purple"><i class="el-icon-data-line"></i></div>
            <div class="stat-info">
              <div class="stat-value">{{ overview.avgEfficiency }}%</div>
              <div class="stat-label">平均效率</div>
            </div>
          </div>
        </el-col>
      </el-row>

      <!-- 图表 -->
      <el-row :gutter="20" class="chart-row">
        <el-col :span="16">
          <div class="chart-card">
            <div class="chart-title">日产量趋势</div>
            <div ref="productionChart" class="chart-container"></div>
          </div>
        </el-col>
        <el-col :span="8">
          <div class="chart-card">
            <div class="chart-title">设备产量分布</div>
            <div ref="devicePieChart" class="chart-container"></div>
          </div>
        </el-col>
      </el-row>

      <!-- 设备加工明细 -->
      <div class="card">
        <div class="card-header flex-between">
          <span>设备加工明细</span>
          <el-button type="primary" size="small" icon="el-icon-download" @click="handleExport">
            {{ $t('statistics.exportExcel') }}
          </el-button>
        </div>
        <el-table :data="tableData" border v-loading="loading">
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
    </div>
  </div>
</template>

<script>
import * as echarts from 'echarts'
import { getProcessOverview, exportStatistics } from '@/api/statistics'
import DeviceTreePanel from '@/components/DeviceTreePanel.vue'

const getTodayRange = () => {
  const now = new Date()
  const year = now.getFullYear()
  const month = String(now.getMonth() + 1).padStart(2, '0')
  const day = String(now.getDate()).padStart(2, '0')
  const today = `${year}-${month}-${day}`
  return [today, today]
}

export default {
  name: 'ProcessOverview',
  components: {
    DeviceTreePanel
  },
  data() {
    return {
      loading: false,
      searchForm: {
        dateRange: getTodayRange(),
        deviceFilter: {
          label: '',
          nodeType: '',
          deviceId: '',
          deviceIds: []
        }
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
          page: this.pagination.page,
          pageSize: this.pagination.pageSize
        })
        if (res.code === 0) {
          this.overview = res.data.overview || { totalPieces: 0, totalThread: 0, totalHours: 0, avgEfficiency: 0 }
          this.tableData = res.data.list || []
          this.pagination.total = res.data.total || 0
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
    handleSearch() {
      this.pagination.page = 1
      this.fetchData()
    },
    handleReset() {
      this.searchForm = {
        dateRange: getTodayRange(),
        deviceFilter: {
          label: '',
          nodeType: '',
          deviceId: '',
          deviceIds: []
        }
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
    getEfficiencyClass(value) {
      if (value >= 80) return 'text-success'
      if (value >= 60) return 'text-warning'
      return 'text-danger'
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
        legend: { data: ['产量', '效率'] },
        grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
        xAxis: {
          type: 'category',
          data: trendData.map(item => item.date)
        },
        yAxis: [
          { type: 'value', name: '产量(件)' },
          { type: 'value', name: '效率(%)', max: 100 }
        ],
        series: [
          {
            name: '产量',
            type: 'bar',
            data: trendData.map(item => item.pieces ?? item.value ?? 0),
            itemStyle: { color: '#409EFF', borderRadius: [4, 4, 0, 0] }
          },
          {
            name: '效率',
            type: 'line',
            yAxisIndex: 1,
            data: trendData.map(item => item.efficiency ?? 0),
            smooth: true,
            lineStyle: { color: '#67C23A', width: 3 },
            itemStyle: { color: '#67C23A' }
          }
        ]
      }, true)
    },
    initDevicePieChart() {
      const chart = this.getOrCreateChart('devicePie', this.$refs.devicePieChart)
      const pieData = this.chartData.deviceDistribution || []
      chart.setOption({
        tooltip: { trigger: 'item', formatter: '{b}: {c} ({d}%)' },
        legend: { orient: 'vertical', right: 10, top: 'center' },
        series: [{
          type: 'pie',
          radius: ['40%', '70%'],
          center: ['40%', '50%'],
          data: pieData,
          color: ['#409EFF', '#67C23A', '#E6A23C', '#F56C6C']
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
.stats-layout {
  display: flex;
  gap: 16px;
}

.stats-side {
  width: 260px;
  flex-shrink: 0;
}

.stats-main {
  flex: 1;
  min-width: 0;
}

@media (max-width: 1200px) {
  .stats-layout {
    flex-direction: column;
  }

  .stats-side {
    width: 100%;
  }
}

.stat-row {
  margin-bottom: 20px;
}

.stat-card {
  display: flex;
  align-items: center;
  padding: 20px;
  background: #fff;
  border-radius: 8px;

  .stat-icon {
    width: 50px;
    height: 50px;
    border-radius: 10px;
    display: flex;
    align-items: center;
    justify-content: center;
    margin-right: 15px;

    i { font-size: 24px; color: #fff; }

    &.blue { background: linear-gradient(135deg, #409EFF, #2d8cf0); }
    &.green { background: linear-gradient(135deg, #67C23A, #5daf34); }
    &.orange { background: linear-gradient(135deg, #E6A23C, #d69330); }
    &.purple { background: linear-gradient(135deg, #9b59b6, #8e44ad); }
  }

  .stat-info {
    .stat-value {
      font-size: 24px;
      font-weight: bold;
      color: #303133;
    }
    .stat-label {
      font-size: 14px;
      color: #909399;
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
    height: 300px;
  }
}

.text-success { color: #67C23A; font-weight: bold; }
.text-warning { color: #E6A23C; font-weight: bold; }
.text-danger { color: #F56C6C; font-weight: bold; }
</style>
