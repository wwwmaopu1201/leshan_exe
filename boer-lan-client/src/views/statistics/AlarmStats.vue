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
            <el-card shadow="never" class="stat-card danger" :body-style="{ padding: '0' }">
              <div class="stat-card__body">
                <div class="stat-icon"><i class="el-icon-warning"></i></div>
                <div class="stat-info stat-card__content">
                <div class="stat-value">{{ summary.totalAlarms }}</div>
                <div class="stat-label">{{ $t('statistics.alarmCount') }}</div>
                </div>
              </div>
            </el-card>
          </el-col>
          <el-col :span="6">
            <el-card shadow="never" class="stat-card orange" :body-style="{ padding: '0' }">
              <div class="stat-card__body">
                <div class="stat-icon"><i class="el-icon-time"></i></div>
                <div class="stat-info stat-card__content">
                <div class="stat-value">{{ formatDurationFromMinutes(summary.totalDuration) }}</div>
                <div class="stat-label">总报警时长</div>
                </div>
              </div>
            </el-card>
          </el-col>
          <el-col :span="6">
            <el-card shadow="never" class="stat-card blue" :body-style="{ padding: '0' }">
              <div class="stat-card__body">
                <div class="stat-icon"><i class="el-icon-monitor"></i></div>
                <div class="stat-info stat-card__content">
                <div class="stat-value">{{ summary.affectedDevices }}</div>
                <div class="stat-label">涉及设备数</div>
                </div>
              </div>
            </el-card>
          </el-col>
          <el-col :span="6">
            <el-card shadow="never" class="stat-card green" :body-style="{ padding: '0' }">
              <div class="stat-card__body">
                <div class="stat-icon"><i class="el-icon-circle-check"></i></div>
                <div class="stat-info stat-card__content">
                <div class="stat-value">{{ summary.resolvedRate }}%</div>
                <div class="stat-label">已处理率</div>
                </div>
              </div>
            </el-card>
          </el-col>
        </el-row>

        <el-row :gutter="20" class="chart-row">
          <el-col :span="10">
            <el-card shadow="never" class="chart-card">
              <div slot="header" class="chart-card__header">
                <div class="chart-title">报警类型分布</div>
                <div class="chart-subtitle">查看当前范围内的报警组成</div>
              </div>
              <div ref="alarmTypePieChart" class="chart-container"></div>
            </el-card>
          </el-col>
          <el-col :span="14">
            <el-card shadow="never" class="chart-card">
              <div slot="header" class="chart-card__header">
                <div class="chart-title">日报警趋势</div>
                <div class="chart-subtitle">追踪报警次数与平均时长</div>
              </div>
              <div ref="alarmTrendChart" class="chart-container"></div>
            </el-card>
          </el-col>
        </el-row>

        <el-card ref="detailTableCard" shadow="never" class="card page-table-card">
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
            :height="tableHeight"
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
        </el-card>
      </section>
    </div>
  </div>
</template>

<script>
import * as echarts from 'echarts'
import { getAlarmStats, exportStatistics } from '@/api/statistics'
import DeviceTreePanel from '@/components/DeviceTreePanel.vue'
import { saveResponseWithDialog } from '@/utils/file-export'
import { formatDurationFromMinutes } from '@/utils'

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
      alarmTypeOptions: [],
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
      charts: {},
      tableHeight: 320
    }
  },
  computed: {
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
          this.updateAlarmTypeOptions(this.chartData.alarmTypePie, rawList)
          this.$nextTick(() => {
            this.initCharts()
            this.syncTableHeight()
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
    updateAlarmTypeOptions(pieData = [], list = []) {
      const options = new Set(this.searchForm.alarmType ? [this.searchForm.alarmType] : [])
      ;(pieData || []).forEach(item => {
        const name = String(item?.name || '').trim()
        if (name && name !== '未分类') options.add(name)
      })
      ;(list || []).forEach(item => {
        const type = String(item?.alarmType || item?.alarmInfo || '').trim()
        if (type && type !== '报警') options.add(type)
      })
      this.alarmTypeOptions = Array.from(options)
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
      this.syncTableHeight()
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
        const { saved } = await saveResponseWithDialog(response, `alarm_stats_${Date.now()}.csv`, {
          mimeType: 'text/csv;charset=utf-8;',
          description: 'CSV 文件',
          extensions: ['csv']
        })
        if (saved === null) {
          return
        }
        this.$message.success(saved ? '导出文件已保存' : '导出成功')
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
    formatDurationFromMinutes(minutes) {
      return formatDurationFromMinutes(minutes)
    },
    truncateLegendName(name, maxLength = 8) {
      const text = String(name || '')
      if ([...text].length <= maxLength) return text
      return `${[...text].slice(0, maxLength).join('')}...`
    },
    formatPieLabelName(name) {
      return [...String(name || '')].slice(0, 3).join('')
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
            { name: '暂无报警', value: 0 }
          ]
      chart.setOption({
        tooltip: { trigger: 'item', formatter: '{b}: {c}次 ({d}%)' },
        legend: {
          orient: 'vertical',
          left: 0,
          top: 'middle',
          width: 96,
          itemGap: 10,
          tooltip: {
            show: true,
            formatter: params => params.name
          },
          textStyle: {
            color: '#6a7f9d',
            width: 72,
            overflow: 'truncate'
          },
          formatter: name => this.truncateLegendName(name)
        },
        series: [{
          type: 'pie',
          radius: ['46%', '68%'],
          center: ['72%', '50%'],
          label: {
            formatter: params => this.formatPieLabelName(params.name)
          },
          data: pieData,
          color: ['#ef5a5a', '#f0b037', '#2f6df6', '#8a98ad']
        }]
      }, true)
    },
    initAlarmTrendChart() {
      const chart = this.getOrCreateChart('alarmTrend', this.$refs.alarmTrendChart)
      const trendData = this.chartData.alarmTrend || []
      const dates = trendData.map(item => item.date)
      const counts = trendData.map(item => Math.trunc(Number(item.count) || 0))
      const durations = trendData.map(item => item.avgDuration)
      chart.setOption({
        tooltip: {
          trigger: 'axis',
          formatter: params => {
            const title = params?.[0]?.axisValue || ''
            const rows = (params || []).map(item => {
              const value = item.seriesName === '平均时长'
                ? this.formatDurationFromMinutes(item.value)
                : item.value
              return `${item.marker}${item.seriesName}: ${value}`
            })
            return [title, ...rows].join('<br/>')
          }
        },
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
          {
            type: 'value',
            name: '次数',
            minInterval: 1,
            axisTick: { show: false },
            axisLine: { show: false },
            axisLabel: {
              color: '#6a7f9d',
              formatter: value => `${Math.trunc(Number(value) || 0)}`
            },
            splitLine: { lineStyle: { color: '#e9eef5', type: 'dashed' } }
          },
          {
            type: 'value',
            name: '时长',
            axisTick: { show: false },
            axisLine: { show: false },
            axisLabel: {
              color: '#6a7f9d',
              formatter: value => this.formatDurationFromMinutes(value)
            },
            splitLine: { show: false }
          }
        ],
        series: [
          {
            name: '报警次数',
            type: 'bar',
            barWidth: 34,
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
    syncTableHeight() {
      this.$nextTick(() => {
        const card = this.$refs.detailTableCard && this.$refs.detailTableCard.$el
        if (!card) return
        const body = card.querySelector('.el-card__body')
        const header = card.querySelector('.card-header')
        const pagination = card.querySelector('.el-pagination')
        if (!body || !header || !pagination) return
        const nextHeight = body.clientHeight - header.offsetHeight - pagination.offsetHeight - 16
        this.tableHeight = Math.max(240, nextHeight)
      })
    },
    handleResize() {
      Object.values(this.charts).forEach(chart => chart && chart.resize())
      this.syncTableHeight()
    }
  }
}
</script>
