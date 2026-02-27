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
        <el-table-column prop="deviceName" label="设备名称" width="120" />
        <el-table-column prop="totalPieces" label="加工件数" width="100" align="right" />
        <el-table-column prop="totalStitches" label="针数" width="120" align="right" />
        <el-table-column prop="threadLength" label="用线量(m)" width="120" align="right" />
        <el-table-column prop="runningTime" label="运行时长(h)" width="120" align="right" />
        <el-table-column prop="efficiency" label="效率" width="100" align="center">
          <template slot-scope="scope">
            <span :class="getEfficiencyClass(scope.row.efficiency)">{{ scope.row.efficiency }}%</span>
          </template>
        </el-table-column>
        <el-table-column prop="patternCount" label="花型数量" width="100" align="right" />
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
import { getProcessOverview } from '@/api/statistics'

export default {
  name: 'ProcessOverview',
  data() {
    return {
      loading: false,
      searchForm: {
        dateRange: [],
        deviceId: ''
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
        const res = await getProcessOverview({
          startDate: this.searchForm.dateRange?.[0],
          endDate: this.searchForm.dateRange?.[1],
          deviceId: this.searchForm.deviceId,
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
      const chart = echarts.init(this.$refs.productionChart)
      this.charts.production = chart
      chart.setOption({
        tooltip: { trigger: 'axis' },
        legend: { data: ['产量', '效率'] },
        grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
        xAxis: {
          type: 'category',
          data: ['01-14', '01-15', '01-16', '01-17', '01-18', '01-19', '01-20']
        },
        yAxis: [
          { type: 'value', name: '产量(件)' },
          { type: 'value', name: '效率(%)', max: 100 }
        ],
        series: [
          {
            name: '产量',
            type: 'bar',
            data: [4200, 4500, 4100, 4800, 4600, 5100, 4900],
            itemStyle: { color: '#409EFF', borderRadius: [4, 4, 0, 0] }
          },
          {
            name: '效率',
            type: 'line',
            yAxisIndex: 1,
            data: [75, 78, 72, 82, 79, 85, 80],
            smooth: true,
            lineStyle: { color: '#67C23A', width: 3 },
            itemStyle: { color: '#67C23A' }
          }
        ]
      })
    },
    initDevicePieChart() {
      const chart = echarts.init(this.$refs.devicePieChart)
      this.charts.devicePie = chart
      chart.setOption({
        tooltip: { trigger: 'item', formatter: '{b}: {c} ({d}%)' },
        legend: { orient: 'vertical', right: 10, top: 'center' },
        series: [{
          type: 'pie',
          radius: ['40%', '70%'],
          center: ['40%', '50%'],
          data: [
            { name: 'A-001', value: 12580 },
            { name: 'A-002', value: 11200 },
            { name: 'B-001', value: 10500 },
            { name: 'B-002', value: 9800 }
          ],
          color: ['#409EFF', '#67C23A', '#E6A23C', '#F56C6C']
        }]
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
