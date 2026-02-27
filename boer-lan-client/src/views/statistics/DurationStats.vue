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
        <el-form-item :label="$t('statistics.device')">
          <el-select v-model="searchForm.deviceId" clearable placeholder="全部设备">
            <el-option label="全部设备" value="" />
            <el-option label="A-001" value="1" />
            <el-option label="A-002" value="2" />
            <el-option label="B-001" value="3" />
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
        <el-table-column prop="deviceName" label="设备名称" width="120" />
        <el-table-column prop="totalTime" label="总时长(h)" width="100" align="right" />
        <el-table-column prop="runningTime" label="运行时长(h)" width="120" align="right">
          <template slot-scope="scope">
            <span class="text-success">{{ scope.row.runningTime }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="idleTime" label="空闲时长(h)" width="120" align="right">
          <template slot-scope="scope">
            <span class="text-warning">{{ scope.row.idleTime }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="alarmTime" label="报警时长(h)" width="120" align="right">
          <template slot-scope="scope">
            <span class="text-danger">{{ scope.row.alarmTime }}</span>
          </template>
        </el-table-column>
        <el-table-column prop="utilizationRate" label="利用率" width="150">
          <template slot-scope="scope">
            <el-progress
              :percentage="scope.row.utilizationRate"
              :color="getProgressColor(scope.row.utilizationRate)"
            />
          </template>
        </el-table-column>
        <el-table-column prop="date" label="日期" width="120" />
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
</template>

<script>
import * as echarts from 'echarts'
import { getDurationStats } from '@/api/statistics'

export default {
  name: 'DurationStats',
  data() {
    return {
      loading: false,
      searchForm: {
        dateRange: [],
        deviceId: ''
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
        total: 4
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
          deviceId: this.searchForm.deviceId,
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
      this.searchForm = { dateRange: [], deviceId: '' }
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
      const chart = echarts.init(this.$refs.durationPieChart)
      this.charts.durationPie = chart
      chart.setOption({
        tooltip: { trigger: 'item', formatter: '{b}: {c}h ({d}%)' },
        legend: { orient: 'vertical', right: 10, top: 'center' },
        series: [{
          type: 'pie',
          radius: ['40%', '70%'],
          center: ['40%', '50%'],
          data: [
            { name: '运行时长', value: this.summary.runningTime },
            { name: '空闲时长', value: this.summary.idleTime },
            { name: '报警时长', value: this.summary.alarmTime }
          ],
          color: ['#67C23A', '#E6A23C', '#F56C6C']
        }]
      })
    },
    initDurationTrendChart() {
      const chart = echarts.init(this.$refs.durationTrendChart)
      this.charts.durationTrend = chart
      chart.setOption({
        tooltip: { trigger: 'axis' },
        legend: { data: ['运行', '空闲', '报警'] },
        grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
        xAxis: {
          type: 'category',
          data: ['01-14', '01-15', '01-16', '01-17', '01-18', '01-19', '01-20']
        },
        yAxis: { type: 'value', name: '时长(h)' },
        series: [
          {
            name: '运行',
            type: 'bar',
            stack: 'total',
            data: [180, 175, 185, 190, 178, 182, 180],
            itemStyle: { color: '#67C23A' }
          },
          {
            name: '空闲',
            type: 'bar',
            stack: 'total',
            data: [45, 50, 40, 35, 47, 43, 45],
            itemStyle: { color: '#E6A23C' }
          },
          {
            name: '报警',
            type: 'bar',
            stack: 'total',
            data: [15, 15, 15, 15, 15, 15, 15],
            itemStyle: { color: '#F56C6C' }
          }
        ]
      })
    },
    handleResize() {
      Object.values(this.charts).forEach(chart => chart && chart.resize())
    }
  }
}
</script>

<style lang="scss" scoped>
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
