<template>
  <div class="page-container home-page">
    <div class="home-dashboard">
      <div class="home-top-row">
        <el-card shadow="never" class="home-card status-overview-card">
          <div slot="header" class="home-card__header">
            <span class="home-card__title">设备状态统计</span>
          </div>
          <div class="status-overview-grid">
            <div
              v-for="item in statusSummaryItems"
              :key="item.key"
              class="status-overview-item"
            >
              <div class="status-overview-item__label">{{ item.label }}</div>
              <div class="status-overview-item__content">
                <div class="status-overview-item__icon" :class="item.color">
                  <i :class="item.icon"></i>
                </div>
                <div class="status-overview-item__value">{{ item.value }}</div>
              </div>
            </div>
          </div>
        </el-card>

        <el-card shadow="never" class="home-card efficiency-card">
          <div slot="header" class="home-card__header">
            <span class="home-card__title">近7日设备使用效率</span>
          </div>
          <div class="efficiency-card__body">
            <div class="efficiency-card__meta">
              <div class="efficiency-card__meta-label">当天设备使用效率</div>
              <div class="efficiency-card__meta-value">{{ latestEfficiency }}%</div>
              <div class="efficiency-card__meta-delta" :class="efficiencyTrendClass">
                较前天
                <span>{{ Math.abs(efficiencyDelta) }}%</span>
                <i :class="efficiencyTrendIcon"></i>
              </div>
            </div>
            <div ref="efficiencyChart" class="efficiency-card__chart"></div>
          </div>
        </el-card>
      </div>

      <div class="home-middle-row">
        <el-card shadow="never" class="home-card donut-card pattern-card">
          <div slot="header" class="home-card__header">
            <span class="home-card__title">花型使用占比统计</span>
          </div>
          <div class="donut-card__body">
            <div class="donut-card__legend">
              <div
                v-for="item in patternUsageLegend"
                :key="item.name"
                class="donut-card__legend-item"
              >
                <span class="legend-dot" :style="{ backgroundColor: item.color }"></span>
                <span class="legend-name">{{ item.name }}</span>
              </div>
            </div>
            <div class="donut-card__chart-wrap">
              <div class="donut-card__center-label donut-card__center-label--pattern">
                花型使用<br>占比统计
              </div>
              <div ref="patternChart" class="donut-card__chart"></div>
            </div>
          </div>
        </el-card>

        <el-card shadow="never" class="home-card donut-card model-card">
          <div slot="header" class="home-card__header">
            <span class="home-card__title">设备机型占比统计</span>
          </div>
          <div class="donut-card__body">
            <div class="donut-card__legend">
              <div class="donut-card__legend-summary">
                <div class="donut-card__legend-summary-label">设备总数</div>
                <div class="donut-card__legend-summary-value">{{ stats.totalDevices }}</div>
              </div>
              <div
                v-for="item in modelRatioLegend"
                :key="item.name"
                class="donut-card__legend-item"
              >
                <span class="legend-dot" :style="{ backgroundColor: item.color }"></span>
                <span class="legend-name">{{ item.name }}</span>
              </div>
            </div>
            <div class="donut-card__chart-wrap">
              <div class="donut-card__center-label donut-card__center-label--model">
                机型占比
              </div>
              <div ref="modelChart" class="donut-card__chart"></div>
            </div>
          </div>
        </el-card>

        <el-card shadow="never" class="home-card donut-card top-production-card">
          <div slot="header" class="home-card__header">
            <span class="home-card__title">前三设备生产量占比统计</span>
          </div>
          <div class="donut-card__body">
            <div class="donut-card__legend">
              <div class="donut-card__legend-summary">
                <div class="donut-card__legend-summary-label">当天前三设备生产量</div>
                <div class="donut-card__legend-summary-value">{{ topProductionTotal }}件</div>
              </div>
              <div
                v-for="item in topProductionLegend"
                :key="item.name"
                class="donut-card__legend-item"
              >
                <span class="legend-dot" :style="{ backgroundColor: item.color }"></span>
                <span class="legend-name">{{ item.name }}</span>
              </div>
            </div>
            <div class="donut-card__chart-wrap">
              <div class="donut-card__center-label donut-card__center-label--top">
                前三设备<br>生产占比
              </div>
              <div ref="topChart" class="donut-card__chart"></div>
            </div>
          </div>
        </el-card>
      </div>

      <div class="home-bottom-row">
        <el-card shadow="never" class="home-card trend-card running-card">
          <div slot="header" class="home-card__header split">
            <span class="home-card__title">当前设备运行状态</span>
            <div class="trend-legend">
              <span class="trend-legend__item">
                <i class="legend-dot green"></i>
                开机数
              </span>
              <span class="trend-legend__item">
                <i class="legend-dot orange"></i>
                关机数
              </span>
            </div>
          </div>
          <div ref="runningChart" class="trend-card__chart"></div>
        </el-card>

        <el-card shadow="never" class="home-card trend-card production-card">
          <div slot="header" class="home-card__header split">
            <span class="home-card__title">产量统计</span>
            <div class="production-toolbar">
              <el-button-group>
                <el-button
                  size="mini"
                  :type="productionRange === 'week' ? 'primary' : 'default'"
                  @click="setProductionRange('week')"
                >
                  近7日
                </el-button>
                <el-button
                  size="mini"
                  :type="productionRange === 'month' ? 'primary' : 'default'"
                  @click="setProductionRange('month')"
                >
                  近1月
                </el-button>
              </el-button-group>
              <el-date-picker
                v-model="selectedProductionDate"
                type="date"
                size="mini"
                value-format="yyyy-MM-dd"
                placeholder="选择日期"
              />
            </div>
          </div>
          <div ref="productionChart" class="trend-card__chart"></div>
        </el-card>
      </div>
    </div>
  </div>
</template>

<script>
import * as echarts from 'echarts'
import { getHomeStats } from '@/api/statistics'

const PATTERN_COLORS = ['#37C2D1', '#3B82F6', '#EF4444', '#8B5CF6', '#2DD4BF', '#1D4ED8', '#F59E0B', '#22C55E']
const MODEL_COLORS = ['#1D9BF0', '#1D4ED8', '#7C3AED', '#16C5F3', '#76E05A']
const TOP_COLORS = ['#FF9800', '#18BFF2', '#62E05A']

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
      selectedProductionDate: this.formatDate(new Date())
    }
  },
  computed: {
    statusSummaryItems() {
      return [
        { key: 'totalDevices', label: '设备总数', value: this.stats.totalDevices, icon: 'el-icon-wallet', color: 'gold' },
        { key: 'onlineDevices', label: '设备在线数', value: this.stats.onlineDevices, icon: 'el-icon-monitor', color: 'blue' },
        { key: 'workingDevices', label: '设备纵机数', value: this.stats.workingDevices, icon: 'el-icon-video-play', color: 'green' },
        { key: 'offlineDevices', label: '设备关机数', value: this.stats.offlineDevices, icon: 'el-icon-switch-button', color: 'orange' }
      ]
    },
    latestEfficiency() {
      const list = this.stats.weeklyEfficiency || []
      const latest = list[list.length - 1]
      return Number(latest?.value || 0)
    },
    efficiencyDelta() {
      const list = this.stats.weeklyEfficiency || []
      if (list.length < 2) {
        return 0
      }
      return Number(list[list.length - 1]?.value || 0) - Number(list[list.length - 2]?.value || 0)
    },
    efficiencyTrendClass() {
      if (this.efficiencyDelta > 0) return 'up'
      if (this.efficiencyDelta < 0) return 'down'
      return 'flat'
    },
    efficiencyTrendIcon() {
      if (this.efficiencyDelta > 0) return 'el-icon-top'
      if (this.efficiencyDelta < 0) return 'el-icon-bottom'
      return 'el-icon-minus'
    },
    patternUsageLegend() {
      return this.mapLegend(this.stats.patternUsage, PATTERN_COLORS, 8)
    },
    modelRatioLegend() {
      return this.mapLegend(this.stats.modelRatio, MODEL_COLORS, 5)
    },
    topProductionLegend() {
      return this.mapLegend(this.stats.topProduction, TOP_COLORS, 3)
    },
    topProductionTotal() {
      return this.topProductionLegend.reduce((sum, item) => sum + Number(item.value || 0), 0)
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
    formatDate(date) {
      const year = date.getFullYear()
      const month = String(date.getMonth() + 1).padStart(2, '0')
      const day = String(date.getDate()).padStart(2, '0')
      return `${year}-${month}-${day}`
    },
    mapLegend(source = [], colors = [], limit = 8) {
      return (source || []).slice(0, limit).map((item, index) => ({
        name: item.name || `未命名${index + 1}`,
        value: Number(item.value || 0),
        color: colors[index % colors.length]
      }))
    },
    async fetchData() {
      try {
        const res = await getHomeStats()
        if (res.code === 0) {
          this.stats = {
            totalDevices: Number(res.data.totalDevices || 0),
            onlineDevices: Number(res.data.onlineDevices || 0),
            workingDevices: Number(res.data.workingDevices || 0),
            offlineDevices: Number(res.data.offlineDevices || 0),
            alarmDevices: Number(res.data.alarmDevices || 0),
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
      if (!this.refreshTimer) {
        return
      }
      clearInterval(this.refreshTimer)
      this.refreshTimer = null
    },
    setProductionRange(range) {
      if (this.productionRange === range) {
        return
      }
      this.productionRange = range
      this.initProductionChart()
    },
    refreshCharts() {
      this.$nextTick(() => {
        this.initEfficiencyChart()
        this.initPatternChart()
        this.initModelChart()
        this.initTopChart()
        this.initRunningChart()
        this.initProductionChart()
      })
    },
    getOrCreateChart(key, refName) {
      if (this.charts[key]) {
        return this.charts[key]
      }
      const el = this.$refs[refName]
      if (!el) {
        return null
      }
      const chart = echarts.init(el)
      this.charts[key] = chart
      return chart
    },
    buildDonutOption(data, colors, options = {}) {
      const center = options.center || ['58%', '53%']
      const radius = options.radius || ['52%', '72%']

      return {
        tooltip: { trigger: 'item', formatter: '{b}: {c} ({d}%)' },
        color: colors,
        series: [{
          type: 'pie',
          radius,
          center,
          avoidLabelOverlap: true,
          label: { show: false },
          labelLine: { show: false },
          itemStyle: {
            borderColor: '#fff',
            borderWidth: 2
          },
          data
        }]
      }
    },
    initEfficiencyChart() {
      const chart = this.getOrCreateChart('efficiency', 'efficiencyChart')
      if (!chart) return

      const seriesData = this.stats.weeklyEfficiency || []
      chart.setOption({
        animationDuration: 500,
        tooltip: { trigger: 'axis', formatter: '{b}: {c}%' },
        grid: { left: 2, right: 8, top: 8, bottom: 4, containLabel: true },
        xAxis: {
          type: 'category',
          boundaryGap: false,
          data: seriesData.map(item => item.date),
          axisTick: { show: false },
          axisLine: { lineStyle: { color: '#D8E1EF' } },
          axisLabel: { color: '#697A96', fontSize: 11, margin: 10 }
        },
        yAxis: {
          type: 'value',
          min: 0,
          max: 100,
          axisLabel: { formatter: '{value}%', color: '#697A96', fontSize: 11, margin: 10 },
          splitLine: { lineStyle: { color: '#EDF2F8', type: 'dashed' } }
        },
        series: [{
          data: seriesData.map(item => Number(item.value || 0)),
          type: 'line',
          smooth: true,
          symbol: 'circle',
          symbolSize: 6,
          lineStyle: { color: '#76A7F7', width: 3 },
          itemStyle: { color: '#76A7F7' },
          areaStyle: {
            color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
              { offset: 0, color: 'rgba(118, 167, 247, 0.32)' },
              { offset: 1, color: 'rgba(118, 167, 247, 0.06)' }
            ])
          }
        }]
      }, true)
    },
    initPatternChart() {
      const chart = this.getOrCreateChart('pattern', 'patternChart')
      if (!chart) return

      chart.setOption(
        this.buildDonutOption(
          this.patternUsageLegend,
          PATTERN_COLORS,
          {
            center: ['58%', '54%'],
            radius: ['50%', '72%']
          }
        ),
        true
      )
    },
    initModelChart() {
      const chart = this.getOrCreateChart('model', 'modelChart')
      if (!chart) return

      chart.setOption(
        this.buildDonutOption(
          this.modelRatioLegend,
          MODEL_COLORS,
          {
            center: ['58%', '54%'],
            radius: ['52%', '72%']
          }
        ),
        true
      )
    },
    initTopChart() {
      const chart = this.getOrCreateChart('top', 'topChart')
      if (!chart) return

      chart.setOption(
        this.buildDonutOption(
          this.topProductionLegend,
          TOP_COLORS,
          {
            center: ['58%', '54%'],
            radius: ['52%', '72%']
          }
        ),
        true
      )
    },
    initRunningChart() {
      const chart = this.getOrCreateChart('running', 'runningChart')
      if (!chart) return

      const source = this.stats.runningStatusByHour || []
      chart.setOption({
        animationDuration: 500,
        tooltip: { trigger: 'axis' },
        grid: { left: 34, right: 18, top: 18, bottom: 22 },
        xAxis: {
          type: 'category',
          boundaryGap: false,
          data: source.map(item => item.hour),
          axisTick: { show: false },
          axisLine: { lineStyle: { color: '#D9E1EE' } },
          axisLabel: { color: '#7B8798', fontSize: 11 }
        },
        yAxis: {
          type: 'value',
          axisLabel: { color: '#7B8798', fontSize: 11 },
          splitLine: { lineStyle: { color: '#E7EDF6', type: 'dashed' } }
        },
        series: [
          {
            name: '开机数',
            type: 'line',
            smooth: true,
            symbol: 'circle',
            symbolSize: 7,
            data: source.map(item => Number(item.online || 0)),
            lineStyle: { color: '#20C55E', width: 3 },
            itemStyle: { color: '#20C55E' }
          },
          {
            name: '关机数',
            type: 'line',
            smooth: true,
            symbol: 'circle',
            symbolSize: 7,
            data: source.map(item => Number(item.offline || 0)),
            lineStyle: { color: '#FF9800', width: 3 },
            itemStyle: { color: '#FF9800' }
          }
        ]
      }, true)
    },
    getProductionChartData() {
      const source = (this.stats.productionByDay || []).map((item, index) => ({
        label: item.date || `第${index + 1}天`,
        value: Number(item.value ?? item.pieces ?? item.count ?? 0)
      }))
      if (this.productionRange === 'month') {
        return source.slice(-7)
      }
      return source
    },
    initProductionChart() {
      const chart = this.getOrCreateChart('production', 'productionChart')
      if (!chart) return

      const source = this.getProductionChartData()
      chart.setOption({
        animationDuration: 500,
        tooltip: { trigger: 'axis' },
        grid: { left: 40, right: 12, top: 16, bottom: 22 },
        xAxis: {
          type: 'category',
          data: source.map(item => item.label),
          axisTick: { show: false },
          axisLine: { lineStyle: { color: '#D9E1EE' } },
          axisLabel: { color: '#7B8798', fontSize: 11 }
        },
        yAxis: {
          type: 'value',
          axisLabel: { color: '#7B8798', fontSize: 11 },
          splitLine: { lineStyle: { color: '#E7EDF6', type: 'dashed' } }
        },
        series: [{
          type: 'bar',
          barWidth: 30,
          data: source.map(item => item.value),
          label: {
            show: true,
            position: 'top',
            color: '#24A8F4',
            fontSize: 11,
            fontWeight: 600
          },
          itemStyle: {
            color: '#18A9F5',
            borderRadius: [2, 2, 0, 0]
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
.home-page {
  padding: 12px;
  background: #edf0f3;
  overflow: auto;
}

.home-dashboard {
  display: flex;
  flex-direction: column;
  gap: 14px;
  min-width: 1120px;
}

.home-top-row,
.home-middle-row,
.home-bottom-row {
  display: grid;
  gap: 14px;
}

.home-top-row {
  grid-template-columns: 1.42fr 1fr;
}

.home-middle-row {
  grid-template-columns: repeat(3, minmax(0, 1fr));
}

.home-bottom-row {
  grid-template-columns: 1fr 1.25fr;
}

.home-card {
  border: 1px solid #dfe5ef;
  border-radius: 2px;
  box-shadow: none;
  background: #fff;

  ::v-deep .el-card__header {
    padding: 12px 14px 0;
    border-bottom: none;
  }

  ::v-deep .el-card__body {
    padding: 10px 14px 14px;
  }
}

.home-card__header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  min-height: 22px;

  &.split {
    gap: 12px;
  }
}

.home-card__title {
  position: relative;
  padding-left: 8px;
  color: #3a4556;
  font-size: 13px;
  font-weight: 700;
  line-height: 1;

  &::before {
    content: '';
    position: absolute;
    left: 0;
    top: 0;
    width: 2px;
    height: 13px;
    border-radius: 1px;
    background: #4aa6ff;
  }
}

.status-overview-grid {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 8px;
  padding: 10px 0 2px;
}

.status-overview-item__label {
  margin-bottom: 20px;
  color: #5c6778;
  font-size: 12px;
  font-weight: 600;
  text-align: center;
}

.status-overview-item__content {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 12px;
}

.status-overview-item__icon {
  width: 31px;
  height: 31px;
  border-radius: 5px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  color: #fff;
  font-size: 17px;

  &.gold {
    background: #c8a477;
  }

  &.blue {
    background: #2167ff;
  }

  &.green {
    background: #16c04d;
  }

  &.orange {
    background: #ff9800;
  }
}

.status-overview-item__value {
  color: #2f3642;
  font-size: 22px;
  font-weight: 700;
  line-height: 1;
}

.efficiency-card__body {
  display: grid;
  grid-template-columns: 152px 1fr;
  gap: 14px;
  align-items: start;
  min-height: 176px;
}

.efficiency-card__meta {
  padding: 18px 0 0 2px;
}

.efficiency-card__meta-label {
  margin-bottom: 14px;
  color: #5e6b7d;
  font-size: 12px;
  font-weight: 600;
}

.efficiency-card__meta-value {
  margin-bottom: 22px;
  color: #2e3642;
  font-size: 20px;
  font-weight: 700;
  line-height: 1;
}

.efficiency-card__meta-delta {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  color: #8d99a9;
  font-size: 12px;

  span {
    font-weight: 700;
  }

  &.up {
    color: #1dbf73;
  }

  &.down {
    color: #25c7c9;
  }

  &.flat {
    color: #8d99a9;
  }
}

.efficiency-card__chart {
  height: 176px;
}

.donut-card__body {
  display: grid;
  grid-template-columns: 114px minmax(0, 1fr);
  gap: 6px;
  align-items: stretch;
  min-height: 248px;
}

.donut-card__legend {
  padding: 10px 2px 8px 0;
  display: flex;
  flex-direction: column;
  gap: 9px;

  &.compact {
    margin-top: 14px;
  }
}

.donut-card__legend-summary {
  margin-bottom: 8px;
}

.donut-card__legend-summary-label {
  margin-bottom: 8px;
  color: #5e6b7d;
  font-size: 12px;
  font-weight: 600;
  line-height: 1.3;
}

.donut-card__legend-summary-value {
  color: #313845;
  font-size: 20px;
  font-weight: 700;
  line-height: 1.1;
}

.donut-card__legend-item {
  display: flex;
  align-items: center;
  gap: 7px;
  min-width: 0;
  line-height: 1.1;
}

.legend-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  flex-shrink: 0;

  &.green {
    background: #20c55e;
  }

  &.orange {
    background: #ff9800;
  }
}

.legend-name {
  color: #7c8798;
  font-size: 12px;
  line-height: 1.2;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.donut-card__chart {
  min-width: 0;
  height: 236px;
}

.donut-card__chart-wrap {
  position: relative;
  min-width: 0;
}

.donut-card__center-label {
  position: absolute;
  top: 54%;
  z-index: 1;
  width: 104px;
  color: #2f3a4d;
  font-size: 16px;
  font-weight: 700;
  line-height: 1.35;
  text-align: center;
  pointer-events: none;
  transform: translate(-50%, -50%);
}

.donut-card__center-label--pattern {
  left: 58%;
}

.donut-card__center-label--model,
.donut-card__center-label--top {
  left: 58%;
}

.trend-card__chart {
  height: 278px;
}

.trend-legend {
  display: flex;
  align-items: center;
  gap: 16px;
}

.trend-legend__item {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  color: #4c5563;
  font-size: 12px;
  font-weight: 600;
}

.production-toolbar {
  display: flex;
  align-items: center;
  gap: 8px;
}

.production-toolbar ::v-deep .el-button {
  min-width: 50px;
  height: 26px;
  padding: 0 12px;
  font-size: 12px;
}

.production-toolbar ::v-deep .el-input__inner {
  width: 118px;
  height: 26px;
  line-height: 26px;
  padding: 0 10px;
  font-size: 12px;
}

.pattern-card .donut-card__chart {
  margin-left: 0;
}

.model-card .donut-card__chart,
.top-production-card .donut-card__chart {
  margin-left: 0;
}

@media (max-width: 1280px) {
  .home-page {
    padding: 10px;
  }

  .home-dashboard {
    min-width: 1040px;
  }
}
</style>
