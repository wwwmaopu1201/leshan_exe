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
        <el-form-item :label="$t('statistics.alarmType')">
          <el-select v-model="searchForm.alarmType" clearable placeholder="全部类型">
            <el-option label="全部类型" value="" />
            <el-option label="断线报警" value="1" />
            <el-option label="张力报警" value="2" />
            <el-option label="电机报警" value="3" />
            <el-option label="传感器报警" value="4" />
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
        <el-table-column prop="deviceName" label="设备名称" width="120" />
        <el-table-column prop="alarmType" label="报警类型" width="120">
          <template slot-scope="scope">
            <el-tag type="danger" size="small">{{ scope.row.alarmType }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="alarmCode" label="报警代码" width="100" />
        <el-table-column prop="description" label="报警描述" min-width="200" />
        <el-table-column prop="duration" label="持续时长" width="100" />
        <el-table-column prop="status" label="处理状态" width="100" align="center">
          <template slot-scope="scope">
            <el-tag :type="scope.row.status === '已处理' ? 'success' : 'warning'" size="small">
              {{ scope.row.status }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="startTime" label="开始时间" width="160" />
        <el-table-column prop="endTime" label="结束时间" width="160" />
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
import { getAlarmStats } from '@/api/statistics'

export default {
  name: 'AlarmStats',
  data() {
    return {
      loading: false,
      searchForm: {
        dateRange: [],
        deviceId: '',
        alarmType: ''
      },
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
          deviceId: this.searchForm.deviceId,
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
      this.searchForm = { dateRange: [], deviceId: '', alarmType: '' }
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
    initCharts() {
      this.initAlarmTypePieChart()
      this.initAlarmTrendChart()
    },
    initAlarmTypePieChart() {
      const chart = echarts.init(this.$refs.alarmTypePieChart)
      this.charts.alarmTypePie = chart
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
      })
    },
    initAlarmTrendChart() {
      const chart = echarts.init(this.$refs.alarmTrendChart)
      this.charts.alarmTrend = chart
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
