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
          <el-form-item :label="$t('statistics.alarmType')">
            <el-select v-model="searchForm.alarmType" clearable placeholder="全部类型">
              <el-option label="全部类型" value="" />
              <el-option
                v-for="type in alarmTypeOptions"
                :key="type"
                :label="type"
                :value="type"
              />
            </el-select>
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
          <div class="stat-card danger">
            <div class="stat-icon"><i class="el-icon-warning"></i></div>
            <div class="stat-info">
              <div class="stat-value">{{ summary.totalAlarms }}</div>
              <div class="stat-label">{{ $t('statistics.alarmCount') }}</div>
            </div>
          </div>
        </el-col>
        <el-col :span="6">
          <div class="stat-card warning">
            <div class="stat-icon"><i class="el-icon-time"></i></div>
            <div class="stat-info">
              <div class="stat-value">{{ summary.totalDuration }}</div>
              <div class="stat-label">总报警时长(min)</div>
            </div>
          </div>
        </el-col>
        <el-col :span="6">
          <div class="stat-card blue">
            <div class="stat-icon"><i class="el-icon-monitor"></i></div>
            <div class="stat-info">
              <div class="stat-value">{{ summary.affectedDevices }}</div>
              <div class="stat-label">涉及设备数</div>
            </div>
          </div>
        </el-col>
        <el-col :span="6">
          <div class="stat-card green">
            <div class="stat-icon"><i class="el-icon-circle-check"></i></div>
            <div class="stat-info">
              <div class="stat-value">{{ summary.resolvedRate }}%</div>
              <div class="stat-label">已处理率</div>
            </div>
          </div>
        </el-col>
      </el-row>

      <!-- 图表 -->
      <el-row :gutter="20" class="chart-row">
        <el-col :span="12">
          <div class="chart-card">
            <div class="chart-title">报警类型分布</div>
            <div ref="alarmTypePieChart" class="chart-container"></div>
          </div>
        </el-col>
        <el-col :span="12">
          <div class="chart-card">
            <div class="chart-title">日报警趋势</div>
            <div ref="alarmTrendChart" class="chart-container"></div>
          </div>
        </el-col>
      </el-row>

      <!-- 报警记录表格 -->
      <div class="card">
        <div class="card-header flex-between">
          <span>报警记录明细</span>
          <el-button type="primary" size="small" icon="el-icon-download" @click="handleExport">
            {{ $t('statistics.exportExcel') }}
          </el-button>
        </div>
        <el-table :data="tableData" border v-loading="loading">
          <el-table-column type="index" label="序号" width="60" align="center" />
          <el-table-column prop="deviceName" label="设备名称" min-width="120" />
          <el-table-column prop="employeeCode" label="员工工号" width="100" />
          <el-table-column prop="employeeName" label="员工姓名" width="110" />
          <el-table-column prop="alarmTime" label="报警时间" width="160" />
          <el-table-column prop="alarmInfo" label="报警信息" min-width="200" />
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
import { getAlarmStats, exportStatistics } from '@/api/statistics'
import DeviceTreePanel from '@/components/DeviceTreePanel.vue'

export default {
  name: 'AlarmStats',
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
        },
        alarmType: ''
      },
      alarmTypeOptions: ['断线报警', '张力报警', '电机报警', '传感器报警'],
      summary: {
        totalAlarms: 0,
        totalDuration: 0,
        affectedDevices: 0,
        resolvedRate: 0
      },
      tableData: [],
      chartData: {
        alarmTypePie: [],
        alarmTrend: []
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
        const res = await getAlarmStats({
          startDate: this.searchForm.dateRange?.[0],
          endDate: this.searchForm.dateRange?.[1],
          deviceId: this.searchForm.deviceFilter.deviceId,
          deviceIds: this.searchForm.deviceFilter.deviceIds.join(','),
          alarmType: this.searchForm.alarmType,
          page: this.pagination.page,
          pageSize: this.pagination.pageSize
        })
        if (res.code === 0) {
          this.summary = res.data.summary || { totalAlarms: 0, totalDuration: 0, affectedDevices: 0, resolvedRate: 0 }
          this.tableData = res.data.list || []
          this.pagination.total = res.data.total || 0
          this.chartData = {
            alarmTypePie: res.data.alarmTypePie || [],
            alarmTrend: res.data.alarmTrend || []
          }
          this.$nextTick(() => {
            this.initCharts()
          })
        }
      } catch (error) {
        console.error('Failed to fetch alarm stats:', error)
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
        },
        alarmType: ''
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
        const response = await exportStatistics('alarm', {
          startDate: this.searchForm.dateRange?.[0],
          endDate: this.searchForm.dateRange?.[1],
          deviceId: this.searchForm.deviceFilter.deviceId,
          deviceIds: this.searchForm.deviceFilter.deviceIds.join(','),
          alarmType: this.searchForm.alarmType
        })
        this.downloadBlob(response, `alarm_stats_${Date.now()}.csv`)
        this.$message.success('导出成功')
      } catch (error) {
        console.error('Failed to export alarm stats:', error)
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
      this.initAlarmTypePieChart()
      this.initAlarmTrendChart()
    },
    initAlarmTypePieChart() {
      const chart = this.getOrCreateChart('alarmTypePie', this.$refs.alarmTypePieChart)
      const pieData = this.chartData.alarmTypePie.length > 0
        ? this.chartData.alarmTypePie
        : [
            { name: '断线报警', value: 0 },
            { name: '张力报警', value: 0 },
            { name: '电机报警', value: 0 },
            { name: '传感器报警', value: 0 }
          ]
      chart.setOption({
        tooltip: { trigger: 'item', formatter: '{b}: {c}次 ({d}%)' },
        legend: { orient: 'vertical', right: 10, top: 'center' },
        series: [{
          type: 'pie',
          radius: ['40%', '70%'],
          center: ['40%', '50%'],
          data: pieData,
          color: ['#F56C6C', '#E6A23C', '#409EFF', '#909399']
        }]
      }, true)
    },
    initAlarmTrendChart() {
      const chart = this.getOrCreateChart('alarmTrend', this.$refs.alarmTrendChart)
      const trendData = this.chartData.alarmTrend
      const dates = trendData.map(item => item.date) || []
      const counts = trendData.map(item => item.count) || []
      const durations = trendData.map(item => item.avgDuration) || []
      chart.setOption({
        tooltip: { trigger: 'axis' },
        legend: { data: ['报警次数', '平均时长'] },
        grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
        xAxis: {
          type: 'category',
          data: dates
        },
        yAxis: [
          { type: 'value', name: '次数' },
          { type: 'value', name: '时长(min)' }
        ],
        series: [
          {
            name: '报警次数',
            type: 'bar',
            data: counts,
            itemStyle: { color: '#F56C6C', borderRadius: [4, 4, 0, 0] }
          },
          {
            name: '平均时长',
            type: 'line',
            yAxisIndex: 1,
            data: durations,
            smooth: true,
            lineStyle: { color: '#E6A23C', width: 3 },
            itemStyle: { color: '#E6A23C' }
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
  border-radius: 8px;
  color: #fff;

  &.danger { background: linear-gradient(135deg, #F56C6C, #e85656); }
  &.warning { background: linear-gradient(135deg, #E6A23C, #d69330); }
  &.blue { background: linear-gradient(135deg, #409EFF, #2d8cf0); }
  &.green { background: linear-gradient(135deg, #67C23A, #5daf34); }

  .stat-icon {
    width: 50px;
    height: 50px;
    background: rgba(255, 255, 255, 0.2);
    border-radius: 50%;
    display: flex;
    align-items: center;
    justify-content: center;
    margin-right: 15px;

    i { font-size: 24px; }
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
    height: 300px;
  }
}
</style>
