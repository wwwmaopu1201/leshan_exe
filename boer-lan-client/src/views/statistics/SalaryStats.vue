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
        <el-form-item :label="$t('statistics.device')">
          <el-select v-model="searchForm.deviceId" clearable placeholder="全部设备">
            <el-option
              v-for="device in deviceList"
              :key="device.id"
              :label="device.name"
              :value="device.id"
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
        <el-button type="primary" size="small" icon="el-icon-download" @click="handleExport">
          {{ $t('statistics.exportExcel') }}
        </el-button>
      </div>
      <el-table :data="tableData" border v-loading="loading" show-summary>
        <el-table-column prop="employeeName" label="员工姓名" width="120" />
        <el-table-column prop="employeeCode" label="员工编号" width="100" />
        <el-table-column prop="deviceName" label="使用设备" width="120" />
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
        <el-table-column prop="date" label="日期" width="120" />
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
  </div>
</template>

<script>
import * as echarts from 'echarts'
import { getSalaryStats } from '@/api/statistics'
import { getEmployeeList } from '@/api/employee'
import { getDeviceList } from '@/api/device'

export default {
  name: 'SalaryStats',
  data() {
    return {
      loading: false,
      searchForm: {
        dateRange: [],
        employeeId: '',
        deviceId: ''
      },
      employeeList: [],
      deviceList: [],
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
        total: 5
      },
      charts: {}
    }
  },
  mounted() {
    this.fetchEmployees()
    this.fetchDevices()
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
    async fetchDevices() {
      try {
        const res = await getDeviceList({ pageSize: 100 })
        if (res.code === 0) {
          this.deviceList = res.data.list || []
        }
      } catch (error) {
        console.error('Failed to fetch devices:', error)
      }
    },
    async fetchData() {
      this.loading = true
      try {
        const res = await getSalaryStats({
          startDate: this.searchForm.dateRange?.[0],
          endDate: this.searchForm.dateRange?.[1],
          employeeId: this.searchForm.employeeId,
          deviceId: this.searchForm.deviceId,
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
      this.pagination.page = 1
      this.fetchData()
    },
    handleReset() {
      this.searchForm = { dateRange: [], employeeId: '', deviceId: '' }
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
      this.initSalaryRankChart()
      this.initSalaryTrendChart()
    },
    initSalaryRankChart() {
      const chart = echarts.init(this.$refs.salaryRankChart)
      this.charts.salaryRank = chart
      chart.setOption({
        tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' } },
        grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
        xAxis: { type: 'value' },
        yAxis: {
          type: 'category',
          data: ['王五', '赵六', '钱七', '李四', '张三']
        },
        series: [{
          type: 'bar',
          data: [4200, 4500, 4800, 5200, 5800],
          barWidth: 20,
          itemStyle: {
            color: new echarts.graphic.LinearGradient(0, 0, 1, 0, [
              { offset: 0, color: '#409EFF' },
              { offset: 1, color: '#67C23A' }
            ]),
            borderRadius: [0, 4, 4, 0]
          }
        }]
      })
    },
    initSalaryTrendChart() {
      const chart = echarts.init(this.$refs.salaryTrendChart)
      this.charts.salaryTrend = chart
      chart.setOption({
        tooltip: { trigger: 'axis' },
        grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
        xAxis: {
          type: 'category',
          data: ['01-14', '01-15', '01-16', '01-17', '01-18', '01-19', '01-20']
        },
        yAxis: { type: 'value' },
        series: [{
          type: 'line',
          data: [4200, 4500, 4100, 4800, 4600, 5100, 4900],
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
      })
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
