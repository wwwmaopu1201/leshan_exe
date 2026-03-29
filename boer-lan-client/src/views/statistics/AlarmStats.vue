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
            <div class="stat-card danger">
              <div class="stat-icon"><i class="el-icon-warning"></i></div>
              <div class="stat-info">
                <div class="stat-value">{{ summary.totalAlarms }}</div>
                <div class="stat-label">{{ $t('statistics.alarmCount') }}</div>
              </div>
            </div>
          </el-col>
          <el-col :span="6">
            <div class="stat-card orange">
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

        <el-row :gutter="20" class="chart-row">
          <el-col :span="10">
            <div class="chart-card">
              <div class="chart-title">报警类型分布</div>
              <div class="chart-subtitle">查看当前范围内的报警组成</div>
              <div ref="alarmTypePieChart" class="chart-container"></div>
            </div>
          </el-col>
          <el-col :span="14">
            <div class="chart-card">
              <div class="chart-title">日报警趋势</div>
              <div class="chart-subtitle">追踪报警次数与平均时长</div>
              <div ref="alarmTrendChart" class="chart-container"></div>
            </div>
          </el-col>
        </el-row>

        <div class="card">
          <div class="card-header flex-between">
            <span>报警记录明细</span>
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
      </section>
    </div>
  </div>
</template>

<script>
import * as echarts from 'echarts'
import { getAlarmStats, exportStatistics } from '@/api/statistics'
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
  name: 'AlarmStats',
  components: {
    DeviceTreePanel
  },
  data() {
    return {
      loading: false,
      searchForm: {
        dateRange: getDefaultRange(),
        deviceKeyword: '',
        deviceFilter: defaultDeviceFilter(),
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
        const res = await getAlarmStats({
          startDate: this.searchForm.dateRange?.[0],
          endDate: this.searchForm.dateRange?.[1],
          deviceId: this.searchForm.deviceFilter.deviceId,
          deviceIds: this.searchForm.deviceFilter.deviceIds.join(','),
          alarmType: this.searchForm.alarmType,
          page: 1,
          pageSize: 2000
        })
        if (res.code === 0) {
          this.summary = res.data.summary || { totalAlarms: 0, totalDuration: 0, affectedDevices: 0, resolvedRate: 0 }
          const rawList = res.data.list || []
          this.tableData = this.applyLocalFilters(rawList)
          this.pagination.total = this.tableData.length
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
        deviceFilter: defaultDeviceFilter(),
        alarmType: ''
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
          color: ['#ef5a5a', '#f0b037', '#2f6df6', '#8a98ad']
        }]
      }, true)
    },
    initAlarmTrendChart() {
      const chart = this.getOrCreateChart('alarmTrend', this.$refs.alarmTrendChart)
      const trendData = this.chartData.alarmTrend || []
      const dates = trendData.map(item => item.date)
      const counts = trendData.map(item => item.count)
      const durations = trendData.map(item => item.avgDuration)
      chart.setOption({
        tooltip: { trigger: 'axis' },
        legend: {
          top: 0,
          data: ['报警次数', '平均时长'],
          textStyle: { color: '#6a7f9d' }
        },
        grid: { left: '4%', right: '4%', bottom: '4%', top: 40, containLabel: true },
        xAxis: {
          type: 'category',
          data: dates,
          axisLabel: { color: '#6a7f9d' },
          axisLine: { lineStyle: { color: '#dbe4f0' } }
        },
        yAxis: [
          { type: 'value', name: '次数', axisLabel: { color: '#6a7f9d' }, splitLine: { lineStyle: { color: '#edf2f8' } } },
          { type: 'value', name: '时长(min)', axisLabel: { color: '#6a7f9d' } }
        ],
        series: [
          {
            name: '报警次数',
            type: 'bar',
            data: counts,
            itemStyle: { color: '#ef5a5a', borderRadius: [10, 10, 0, 0] }
          },
          {
            name: '平均时长',
            type: 'line',
            yAxisIndex: 1,
            smooth: true,
            data: durations,
            lineStyle: { color: '#f0b037', width: 3 },
            itemStyle: { color: '#f0b037' }
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
