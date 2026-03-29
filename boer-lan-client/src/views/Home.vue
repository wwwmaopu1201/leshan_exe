<template>
  <div class="page-container home-page">
    <div class="hero-card card">
      <div class="hero-actions">
        <div class="hero-pill danger">
          <span>报警设备</span>
          <strong>{{ stats.alarmDevices }}</strong>
        </div>
        <div class="hero-filter">
          <span>产量统计范围</span>
          <el-radio-group v-model="productionRange" size="mini" @change="handleProductionRangeChange">
            <el-radio-button label="week">近一周</el-radio-button>
            <el-radio-button label="month">近一月</el-radio-button>
            <el-radio-button label="custom">自定义</el-radio-button>
          </el-radio-group>
        </div>
      </div>
    </div>

    <div v-if="productionRange === 'custom'" class="search-bar compact-bar">
      <el-form :inline="true">
        <el-form-item label="自定义时间">
          <el-date-picker
            v-model="customRange"
            type="daterange"
            value-format="yyyy-MM-dd"
            range-separator="至"
            start-placeholder="开始日期"
            end-placeholder="结束日期"
            @change="refreshCharts"
          />
        </el-form-item>
      </el-form>
    </div>

    <div v-if="rangeNotice" class="range-notice">
      <i class="el-icon-info"></i>
      <span>{{ rangeNotice }}</span>
    </div>

    <div class="status-grid">
      <div class="status-card total">
        <span class="status-label">{{ $t('home.totalDevices') }}</span>
        <strong class="status-value">{{ stats.totalDevices }}</strong>
      </div>
      <div class="status-card online">
        <span class="status-label">{{ $t('home.onlineDevices') }}</span>
        <strong class="status-value">{{ stats.onlineDevices }}</strong>
      </div>
      <div class="status-card working">
        <span class="status-label">{{ $t('home.workingDevices') }}</span>
        <strong class="status-value">{{ stats.workingDevices }}</strong>
      </div>
      <div class="status-card offline">
        <span class="status-label">{{ $t('home.offlineDevices') }}</span>
        <strong class="status-value">{{ stats.offlineDevices }}</strong>
      </div>
      <div class="status-card alarm">
        <span class="status-label">{{ $t('home.alarmDevices') }}</span>
        <strong class="status-value">{{ stats.alarmDevices }}</strong>
      </div>
    </div>

    <div class="home-grid">
      <div class="chart-card chart-wide">
        <div class="chart-title">{{ $t('home.weeklyEfficiency') }}</div>
        <div class="chart-subtitle">按近 7 日设备平均运行效率统计</div>
        <div ref="efficiencyChart" class="chart-container"></div>
      </div>

      <div class="chart-card">
        <div class="chart-title">{{ $t('home.currentStatus') }}</div>
        <div class="chart-subtitle">按在线、运行、离线和报警状态分布</div>
        <div ref="currentStatusChart" class="chart-container compact"></div>
      </div>

      <div class="chart-card">
        <div class="chart-title">{{ $t('home.patternUsage') }}</div>
        <div class="chart-subtitle">圆环粗细与机型占比保持一致</div>
        <div ref="patternChart" class="chart-container compact"></div>
      </div>

      <div class="chart-card">
        <div class="chart-title">{{ $t('home.modelRatio') }}</div>
        <div class="chart-subtitle">图例统一放在左侧，便于快速阅读</div>
        <div ref="modelChart" class="chart-container compact"></div>
      </div>

      <div class="chart-card">
        <div class="chart-title">{{ $t('home.topProduction') }}</div>
        <div class="chart-subtitle">按近 7 天前三设备产量占比展示</div>
        <div ref="topChart" class="chart-container compact"></div>
      </div>

      <div class="chart-card">
        <div class="chart-title">{{ productionChartTitle }}</div>
        <div class="chart-subtitle">{{ productionChartSubtitle }}</div>
        <div ref="productionChart" class="chart-container compact"></div>
      </div>
    </div>
  </div>
</template>

<script>
import * as echarts from 'echarts'
import { getHomeStats } from '@/api/statistics'

export default {
  name: 'Home',
  data() {
    return {
      stats: {
        totalDevices: 0,
        onlineDevices: 0,
        workingDevices: 0,
        offlineDevices: 0,
        alarmDevices: 0,
        weeklyEfficiency: [],
        patternUsage: [],
        modelRatio: [],
        topProduction: [],
        runningStatusByHour: [],
        productionByDay: []
      },
      refreshTimer: null,
      charts: {},
      productionRange: 'week',
      customRange: []
    }
  },
  computed: {
    productionChartTitle() {
      const map = {
        week: '产量统计（近一周）',
        month: '产量统计（近一月）',
        custom: '产量统计（自定义）'
      }
      return map[this.productionRange]
    },
    productionChartSubtitle() {
      if (this.productionRange === 'custom' && this.customRange?.length === 2) {
        return `${this.customRange[0]} 至 ${this.customRange[1]}`
      }
      return '当前基于服务端返回数据绘制'
    },
    rangeNotice() {
      if (this.productionRange === 'week') {
        return ''
      }
      return '当前接口仍以近 7 日数据为基础，月度和自定义范围待服务端补充更长周期统计后可完全生效。'
    }
  },
  mounted() {
    this.fetchData()
    this.startAutoRefresh()
    window.addEventListener('resize', this.handleResize)
  },
  beforeDestroy() {
    this.stopAutoRefresh()
    window.removeEventListener('resize', this.handleResize)
    Object.values(this.charts).forEach(chart => chart && chart.dispose())
  },
  methods: {
    async fetchData() {
      try {
        const res = await getHomeStats()
        if (res.code === 0) {
          this.stats = {
            totalDevices: res.data.totalDevices || 0,
            onlineDevices: res.data.onlineDevices || 0,
            workingDevices: res.data.workingDevices || 0,
            offlineDevices: res.data.offlineDevices || 0,
            alarmDevices: res.data.alarmDevices || 0,
            weeklyEfficiency: res.data.weeklyEfficiency || [],
            patternUsage: res.data.patternUsage || [],
            modelRatio: res.data.modelRatio || [],
            topProduction: res.data.topProduction || [],
            runningStatusByHour: res.data.runningStatusByHour || [],
            productionByDay: res.data.productionByDay || res.data.productionByHour || []
          }
          this.refreshCharts()
        }
      } catch (error) {
        console.error('Failed to fetch home stats:', error)
      }
    },
    startAutoRefresh() {
      this.stopAutoRefresh()
      this.refreshTimer = setInterval(() => {
        this.fetchData()
      }, 60 * 1000)
    },
    stopAutoRefresh() {
      if (this.refreshTimer) {
        clearInterval(this.refreshTimer)
        this.refreshTimer = null
      }
    },
    handleProductionRangeChange() {
      if (this.productionRange !== 'custom') {
        this.customRange = []
      }
      this.refreshCharts()
    },
    refreshCharts() {
      this.$nextTick(() => {
        this.initEfficiencyChart()
        this.initCurrentStatusChart()
        this.initPatternChart()
        this.initModelChart()
        this.initTopChart()
        this.initProductionChart()
      })
    },
    getOrCreateChart(key, refName) {
      if (this.charts[key]) {
        return this.charts[key]
      }
      if (!this.$refs[refName]) {
        return null
      }
      const chart = echarts.init(this.$refs[refName])
      this.charts[key] = chart
      return chart
    },
    getDonutSeries(data, colors) {
      return [{
        type: 'pie',
        radius: ['48%', '68%'],
        center: ['67%', '54%'],
        label: { show: false },
        labelLine: { show: false },
        data,
        color: colors
      }]
    },
    initEfficiencyChart() {
      const chart = this.getOrCreateChart('efficiency', 'efficiencyChart')
      if (!chart) return

      const seriesData = this.stats.weeklyEfficiency || []
      chart.setOption({
        tooltip: {
          trigger: 'axis',
          formatter: '{b}: {c}%'
        },
        grid: {
          left: '4%',
          right: '4%',
          top: 20,
          bottom: 24,
          containLabel: true
        },
        xAxis: {
          type: 'category',
          data: seriesData.map(item => item.date),
          axisLine: { lineStyle: { color: '#dbe4f0' } },
          axisLabel: { color: '#69809f' }
        },
        yAxis: {
          type: 'value',
          max: 100,
          axisLabel: { formatter: '{value}%', color: '#69809f' },
          splitLine: { lineStyle: { color: '#edf2f8' } }
        },
        series: [{
          type: 'line',
          smooth: true,
          symbol: 'circle',
          symbolSize: 9,
          data: seriesData.map(item => item.value),
          lineStyle: { width: 3, color: '#2f6df6' },
          itemStyle: { color: '#2f6df6' },
          areaStyle: {
            color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
              { offset: 0, color: 'rgba(47, 109, 246, 0.26)' },
              { offset: 1, color: 'rgba(47, 109, 246, 0.04)' }
            ])
          }
        }]
      }, true)
    },
    initCurrentStatusChart() {
      const chart = this.getOrCreateChart('currentStatus', 'currentStatusChart')
      if (!chart) return

      const data = [
        { name: '在线', value: this.stats.onlineDevices || 0 },
        { name: '运行中', value: this.stats.workingDevices || 0 },
        { name: '离线', value: this.stats.offlineDevices || 0 },
        { name: '报警', value: this.stats.alarmDevices || 0 }
      ]

      chart.setOption({
        tooltip: { trigger: 'item', formatter: '{b}: {c} 台 ({d}%)' },
        legend: {
          orient: 'vertical',
          left: 8,
          top: 'middle',
          itemWidth: 10,
          itemHeight: 10,
          textStyle: { color: '#5f7392' }
        },
        series: this.getDonutSeries(data, ['#2fb46e', '#2f6df6', '#8a98ad', '#ef5a5a'])
      }, true)
    },
    initPatternChart() {
      const chart = this.getOrCreateChart('pattern', 'patternChart')
      if (!chart) return

      const data = (this.stats.patternUsage || []).map(item => ({
        name: item.name,
        value: item.value
      }))

      chart.setOption({
        tooltip: { trigger: 'item', formatter: '{b}: {c} ({d}%)' },
        legend: {
          orient: 'vertical',
          left: 8,
          top: 'middle',
          itemWidth: 10,
          itemHeight: 10,
          textStyle: { color: '#5f7392' }
        },
        series: this.getDonutSeries(data, ['#2f6df6', '#4aa7ff', '#28b5c8', '#2fb46e', '#f0b037', '#ef5a5a'])
      }, true)
    },
    initModelChart() {
      const chart = this.getOrCreateChart('model', 'modelChart')
      if (!chart) return

      const data = (this.stats.modelRatio || []).map(item => ({
        name: item.name,
        value: item.value
      }))

      chart.setOption({
        tooltip: { trigger: 'item', formatter: '{b}: {c} ({d}%)' },
        legend: {
          orient: 'vertical',
          left: 8,
          top: 'middle',
          itemWidth: 10,
          itemHeight: 10,
          textStyle: { color: '#5f7392' }
        },
        series: this.getDonutSeries(data, ['#173f97', '#2f6df6', '#4aa7ff', '#28b5c8', '#2fb46e'])
      }, true)
    },
    initTopChart() {
      const chart = this.getOrCreateChart('top', 'topChart')
      if (!chart) return

      const data = (this.stats.topProduction || []).map(item => ({
        name: item.name,
        value: item.value
      }))

      chart.setOption({
        tooltip: { trigger: 'item', formatter: '{b}: {c} 件 ({d}%)' },
        legend: {
          bottom: 0,
          textStyle: { color: '#5f7392' }
        },
        series: [{
          type: 'pie',
          radius: ['26%', '72%'],
          roseType: 'radius',
          center: ['50%', '46%'],
          data,
          label: { color: '#587090' },
          color: ['#173f97', '#2f6df6', '#4aa7ff']
        }]
      }, true)
    },
    getProductionSeries() {
      const source = (this.stats.productionByDay || []).map((item, index) => ({
        label: item.date || item.hour || `第${index + 1}天`,
        rawDate: item.fullDate || item.date || '',
        value: Number(item.value ?? item.pieces ?? item.count ?? 0)
      }))

      if (this.productionRange === 'custom' && this.customRange?.length === 2) {
        const [start, end] = this.customRange
        const filtered = source.filter(item => {
          if (!item.rawDate || item.rawDate.length !== 10) {
            return false
          }
          return item.rawDate >= start && item.rawDate <= end
        })
        return filtered.length ? filtered : source
      }

      return source
    },
    initProductionChart() {
      const chart = this.getOrCreateChart('production', 'productionChart')
      if (!chart) return

      const seriesData = this.getProductionSeries()
      chart.setOption({
        tooltip: { trigger: 'axis' },
        grid: {
          left: '4%',
          right: '4%',
          top: 20,
          bottom: 24,
          containLabel: true
        },
        xAxis: {
          type: 'category',
          data: seriesData.map(item => item.label),
          axisLine: { lineStyle: { color: '#dbe4f0' } },
          axisLabel: { color: '#69809f' }
        },
        yAxis: {
          type: 'value',
          axisLabel: { color: '#69809f' },
          splitLine: { lineStyle: { color: '#edf2f8' } }
        },
        series: [{
          type: 'bar',
          barWidth: 18,
          data: seriesData.map(item => item.value),
          itemStyle: {
            borderRadius: [10, 10, 0, 0],
            color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
              { offset: 0, color: '#4aa7ff' },
              { offset: 1, color: '#2f6df6' }
            ])
          }
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
.hero-card {
  display: flex;
  align-items: center;
  justify-content: flex-end;
  gap: 20px;
  margin-bottom: 18px;
}

.hero-actions {
  display: flex;
  align-items: center;
  gap: 14px;
  flex-wrap: wrap;
}

.hero-pill {
  min-width: 120px;
  padding: 12px 16px;
  border-radius: 18px;
  background: #f5f8fd;

  span {
    display: block;
    color: #7e8ea6;
    font-size: 12px;
    margin-bottom: 6px;
  }

  strong {
    font-size: 24px;
    color: #243654;
  }

  &.danger strong {
    color: #ef5a5a;
  }
}

.hero-filter {
  display: flex;
  flex-direction: column;
  gap: 8px;
  color: #667b99;
  font-size: 12px;
}

.compact-bar {
  padding-top: 12px;
  padding-bottom: 12px;
}

.range-notice {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 18px;
  padding: 12px 14px;
  border-radius: 16px;
  background: #edf4ff;
  color: #2f6df6;
}

.status-grid {
  display: grid;
  grid-template-columns: repeat(5, minmax(0, 1fr));
  gap: 14px;
  margin-bottom: 18px;
}

.status-card {
  min-height: 112px;
  padding: 20px;
  border-radius: 22px;
  color: #fff;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  box-shadow: 0 18px 30px rgba(47, 109, 246, 0.16);

  &.total { background: linear-gradient(135deg, #173f97, #2f6df6); }
  &.online { background: linear-gradient(135deg, #2fb46e, #1f935e); }
  &.working { background: linear-gradient(135deg, #3476ff, #1d4ecc); }
  &.offline { background: linear-gradient(135deg, #95a4ba, #71839e); }
  &.alarm { background: linear-gradient(135deg, #ef5a5a, #d94156); }
}

.status-label {
  font-size: 13px;
  opacity: 0.86;
}

.status-value {
  font-size: 30px;
  line-height: 1;
}

.home-grid {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 18px;
}

.chart-wide {
  grid-column: span 2;
}

.compact {
  height: 290px;
}

@media (max-width: 1200px) {
  .status-grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }

  .home-grid {
    grid-template-columns: 1fr 1fr;
  }

  .chart-wide {
    grid-column: span 2;
  }
}

@media (max-width: 768px) {
  .hero-card {
    flex-direction: column;
    align-items: flex-start;
  }

  .status-grid,
  .home-grid {
    grid-template-columns: 1fr;
  }

  .chart-wide {
    grid-column: span 1;
  }
}
</style>
