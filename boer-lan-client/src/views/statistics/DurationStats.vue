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
            <div class="stat-icon blue"><i class="el-icon-time"></i></div>
            <div class="stat-info">
              <div class="stat-value">{{ summary.totalTime }}</div>
              <div class="stat-label">总时长(h)</div>
            </div>
          </div>
        </el-col>
        <el-col :span="6">
          <div class="stat-card">
            <div class="stat-icon green"><i class="el-icon-video-play"></i></div>
            <div class="stat-info">
              <div class="stat-value">{{ summary.runningTime }}</div>
              <div class="stat-label">{{ $t('statistics.processingTime') }}(h)</div>
            </div>
          </div>
        </el-col>
        <el-col :span="6">
          <div class="stat-card">
            <div class="stat-icon orange"><i class="el-icon-video-pause"></i></div>
            <div class="stat-info">
              <div class="stat-value">{{ summary.idleTime }}</div>
              <div class="stat-label">{{ $t('statistics.idleTime') }}(h)</div>
            </div>
          </div>
        </el-col>
        <el-col :span="6">
          <div class="stat-card">
            <div class="stat-icon red"><i class="el-icon-warning"></i></div>
            <div class="stat-info">
              <div class="stat-value">{{ summary.alarmTime }}</div>
              <div class="stat-label">{{ $t('statistics.alarmTime') }}(h)</div>
            </div>
          </div>
        </el-col>
      </el-row>

      <!-- 图表 -->
      <el-row :gutter="20" class="chart-row">
        <el-col :span="12">
          <div class="chart-card">
            <div class="chart-title">时长分布</div>
            <div ref="durationPieChart" class="chart-container"></div>
          </div>
        </el-col>
        <el-col :span="12">
          <div class="chart-card">
            <div class="chart-title">日运行时长趋势</div>
            <div ref="durationTrendChart" class="chart-container"></div>
          </div>
        </el-col>
      </el-row>

      <!-- 数据表格 -->
      <div class="card">
        <div class="card-header flex-between">
          <span>设备时长明细</span>
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
          <el-table-column prop="startTime" label="开始时间" width="160" />
          <el-table-column prop="endTime" label="结束时间" width="160" />
          <el-table-column prop="sewDuration" label="缝纫时长(h)" width="110" align="right">
            <template slot-scope="scope">
              <span class="text-success">{{ scope.row.sewDuration }}</span>
            </template>
          </el-table-column>
          <el-table-column prop="avgSewDuration" label="平均缝纫时长(min/次)" width="170" align="right">
            <template slot-scope="scope">
              <span class="text-warning">{{ scope.row.avgSewDuration }}</span>
            </template>
          </el-table-column>
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
import { getDurationStats, exportStatistics } from '@/api/statistics'
import DeviceTreePanel from '@/components/DeviceTreePanel.vue'

export default {
  name: 'DurationStats',
  components: {
    DeviceTreePanel
  },
  data() {
    return {
      loading: false,
      searchForm: {
        dateRange: [],
        deviceFilter: {
          label: '',
          nodeType: '',
          deviceId: '',
          deviceIds: []
        }
      },
      summary: {
        totalTime: 0,
        runningTime: 0,
        idleTime: 0,
        alarmTime: 0
      },
      tableData: [],
      chartData: {
        durationPie: [],
        durationTrend: []
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
        const res = await getDurationStats({
          startDate: this.searchForm.dateRange?.[0],
          endDate: this.searchForm.dateRange?.[1],
          deviceId: this.searchForm.deviceFilter.deviceId,
          deviceIds: this.searchForm.deviceFilter.deviceIds.join(','),
          page: this.pagination.page,
          pageSize: this.pagination.pageSize
        })
        if (res.code === 0) {
          this.summary = res.data.summary || { totalTime: 0, runningTime: 0, idleTime: 0, alarmTime: 0 }
          this.tableData = res.data.list || []
          this.pagination.total = res.data.total || 0
          this.chartData = {
            durationPie: res.data.durationPie || [],
            durationTrend: res.data.durationTrend || []
          }
          this.$nextTick(() => {
            this.initCharts()
          })
        }
      } catch (error) {
        console.error('Failed to fetch duration stats:', error)
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
        dateRange: [],
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
        const response = await exportStatistics('duration', {
          startDate: this.searchForm.dateRange?.[0],
          endDate: this.searchForm.dateRange?.[1],
          deviceId: this.searchForm.deviceFilter.deviceId,
          deviceIds: this.searchForm.deviceFilter.deviceIds.join(',')
        })
        this.downloadBlob(response, `duration_stats_${Date.now()}.csv`)
        this.$message.success('导出成功')
      } catch (error) {
        console.error('Failed to export duration stats:', error)
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
    getProgressColor(value) {
      if (value >= 80) return '#67C23A'
      if (value >= 60) return '#E6A23C'
      return '#F56C6C'
    },
    initCharts() {
      this.initDurationPieChart()
      this.initDurationTrendChart()
    },
    initDurationPieChart() {
      const chart = this.getOrCreateChart('durationPie', this.$refs.durationPieChart)
      const pieData = this.chartData.durationPie?.length > 0
        ? this.chartData.durationPie
        : [
            { name: '运行时长', value: this.summary.runningTime },
            { name: '空闲时长', value: this.summary.idleTime },
            { name: '报警时长', value: this.summary.alarmTime }
          ]
      chart.setOption({
        tooltip: { trigger: 'item', formatter: '{b}: {c}h ({d}%)' },
        legend: { orient: 'vertical', right: 10, top: 'center' },
        series: [{
          type: 'pie',
          radius: ['40%', '70%'],
          center: ['40%', '50%'],
          data: pieData,
          color: ['#67C23A', '#E6A23C', '#F56C6C']
        }]
      }, true)
    },
    initDurationTrendChart() {
      const chart = this.getOrCreateChart('durationTrend', this.$refs.durationTrendChart)
      const trendData = this.chartData.durationTrend || []
      chart.setOption({
        tooltip: { trigger: 'axis' },
        legend: { data: ['运行', '空闲', '报警'] },
        grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
        xAxis: {
          type: 'category',
          data: trendData.map(item => item.date)
        },
        yAxis: { type: 'value', name: '时长(h)' },
        series: [
          {
            name: '运行',
            type: 'bar',
            stack: 'total',
            data: trendData.map(item => item.runningTime ?? 0),
            itemStyle: { color: '#67C23A' }
          },
          {
            name: '空闲',
            type: 'bar',
            stack: 'total',
            data: trendData.map(item => item.idleTime ?? 0),
            itemStyle: { color: '#E6A23C' }
          },
          {
            name: '报警',
            type: 'bar',
            stack: 'total',
            data: trendData.map(item => item.alarmTime ?? 0),
            itemStyle: { color: '#F56C6C' }
          }
        ]
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
    &.red { background: linear-gradient(135deg, #F56C6C, #e85656); }
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

.text-success { color: #67C23A; }
.text-warning { color: #E6A23C; }
.text-danger { color: #F56C6C; }
</style>
