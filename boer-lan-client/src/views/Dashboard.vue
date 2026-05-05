<template>
  <div class="page-container dashboard-page">
    <div class="dashboard-layout">
      <aside class="dashboard-side">
        <div class="side-legend">
          <span class="side-legend__item">
            <i class="side-legend__dot is-idle"></i>
            空闲
          </span>
          <span class="side-legend__item">
            <i class="side-legend__dot is-alarm"></i>
            报警
          </span>
          <span class="side-legend__item">
            <i class="side-legend__dot is-offline"></i>
            关机
          </span>
        </div>

        <device-tree-panel
          v-model="treeScope"
          title="设备组信息"
          :min-height="660"
          :show-selection="false"
          :show-search="false"
          :show-refresh="false"
          @change="handleTreeScopeChange"
        />
      </aside>

      <section class="dashboard-main">
        <div class="dashboard-stat-row">
          <section
            v-for="card in statCards"
            :key="card.key"
            class="metric-card"
            :class="card.theme"
          >
            <header class="metric-card__title">{{ card.title }}</header>
            <div class="metric-card__metrics">
              <div
                v-for="metric in card.metrics"
                :key="metric.label"
                class="metric-card__metric"
              >
                <div class="metric-card__label">{{ metric.label }}</div>
                <div class="metric-card__value">
                  {{ metric.value }}
                  <small v-if="metric.unit">{{ metric.unit }}</small>
                </div>
              </div>
            </div>
          </section>
        </div>

        <div class="dashboard-chart-row">
          <el-card shadow="never" class="board-card board-card--runtime">
            <div slot="header" class="board-card__header">
              <span class="board-card__title">运行/加工时长统计</span>
              <div class="board-card__legend">
                <span class="board-card__legend-item">
                  <i class="legend-line is-blue"></i>
                  运行时长
                </span>
                <span class="board-card__legend-item">
                  <i class="legend-line is-green"></i>
                  加工时长
                </span>
              </div>
            </div>

            <div class="runtime-panel">
              <div class="runtime-panel__summary">
                <div class="runtime-panel__metric">
                  <div class="runtime-panel__label">当天运行时长</div>
                  <div class="runtime-panel__value">{{ formatDuration(dashboardData.runningTime) }}</div>
                </div>
                <div class="runtime-panel__metric">
                  <div class="runtime-panel__label">当天加工时长</div>
                  <div class="runtime-panel__value is-green">{{ formatDuration(dashboardData.processingTime) }}</div>
                </div>
              </div>
              <div ref="runtimeChart" class="runtime-panel__chart"></div>
            </div>
          </el-card>

          <el-card shadow="never" class="board-card board-card--utilization">
            <div slot="header" class="board-card__header">
              <span class="board-card__title">设备使用率</span>
            </div>

            <div class="utilization-panel">
              <div class="utilization-panel__summary">
                <div class="utilization-panel__label">当天使用率</div>
                <div class="utilization-panel__value">{{ formatPercent(dashboardData.utilizationRate) }}</div>
              </div>
              <div ref="utilizationChart" class="utilization-panel__chart"></div>
            </div>
          </el-card>
        </div>

        <el-card shadow="never" class="board-card board-card--production">
          <div slot="header" class="board-card__header">
            <span class="board-card__title">加工产量统计</span>
          </div>

          <div ref="productionChart" class="production-chart"></div>
        </el-card>
      </section>
    </div>
  </div>
</template>

<script>
import * as echarts from 'echarts'
import { getDeviceTree } from '@/api/device'
import { getDashboardData } from '@/api/statistics'
import DeviceTreePanel from '@/components/DeviceTreePanel.vue'
import { formatDurationFromHours } from '@/utils'

const defaultTreeScope = () => ({
  label: '',
  nodeType: '',
  groupId: '',
  deviceId: '',
  deviceIds: []
})

export default {
  name: 'Dashboard',
  components: {
    DeviceTreePanel
  },
  data() {
    return {
      deviceTree: [],
      treeScope: defaultTreeScope(),
      selectedScope: {
        label: '全厂设备',
        nodeType: 'all',
        status: '',
        model: '',
        ip: '',
        deviceCount: 0
      },
      dashboardData: {
        totalPieces: 0,
        todayPieces: 0,
        threadLength: 0,
        totalThreadLength: 0,
        usedThreadLength: 0,
        avgUsedThreadLength: 0,
        spindleSpeed: 0,
        runningTime: 0,
        processingTime: 0,
        utilizationRate: 0,
        todayAlarmCount: 0,
        totalAlarmCount: 0,
        onlineDeviceCount: 0,
        scopeDeviceCount: 0,
        hourlyProduction: [],
        pieces7d: [],
        runningProcessingTrend: [],
        utilizationTrend: []
      },
      charts: {}
    }
  },
  computed: {
    averageUtilizationRate() {
      const source = this.dashboardData.utilizationTrend || []
      if (source.length === 0) {
        return 0
      }
      const total = source.reduce((sum, item) => sum + Number(item.value || 0), 0)
      return total / source.length
    },
    statCards() {
      return [
        {
          key: 'production',
          title: '产量统计',
          theme: 'is-blue',
          metrics: [
            { label: '今日产量（件）', value: this.toNumber(this.dashboardData.todayPieces) },
            { label: '总产量（件）', value: this.toNumber(this.dashboardData.totalPieces) }
          ]
        },
        {
          key: 'utilization',
          title: '设备使用率',
          theme: 'is-green',
          metrics: [
            { label: '当天使用率', value: this.formatPercent(this.dashboardData.utilizationRate) },
            { label: '平均使用率', value: this.formatPercent(this.averageUtilizationRate) }
          ]
        },
        {
          key: 'alarm',
          title: '报警统计',
          theme: 'is-orange',
          metrics: [
            { label: '今日报警数', value: this.toNumber(this.dashboardData.todayAlarmCount) },
            { label: '总报警数', value: this.toNumber(this.dashboardData.totalAlarmCount) }
          ]
        },
        {
          key: 'online',
          title: '在线设备数',
          theme: 'is-cyan',
          metrics: [
            { label: '在线设备总数', value: this.toNumber(this.dashboardData.onlineDeviceCount) },
            { label: '总设备数', value: this.toNumber(this.dashboardData.scopeDeviceCount) }
          ]
        }
      ]
    }
  },
  mounted() {
    window.addEventListener('resize', this.handleResize)
    this.fetchDeviceTree()
  },
  beforeDestroy() {
    window.removeEventListener('resize', this.handleResize)
    Object.values(this.charts).forEach(chart => chart && chart.dispose())
  },
  methods: {
    toNumber(value) {
      const num = Number(value)
      return Number.isFinite(num) ? num : 0
    },
    formatPercent(value) {
      const num = this.toNumber(value)
      if (num === 0) {
        return '0%'
      }
      if (Math.abs(num) < 1) {
        return `${num.toFixed(2)}%`
      }
      return `${num.toFixed(1).replace(/\.0$/, '')}%`
    },
    formatDuration(value) {
      return formatDurationFromHours(this.toNumber(value))
    },
    async fetchDeviceTree() {
      try {
        const res = await getDeviceTree()
        if (res.code === 0) {
          this.deviceTree = this.attachNodeKeys(res.data || [])
          this.setDefaultScopeAndLoad()
        }
      } catch (error) {
        console.error('Failed to fetch device tree:', error)
      }
    },
    attachNodeKeys(nodes = []) {
      return nodes.map(node => {
        const nodeType = node.type === 'device' ? 'device' : 'group'
        const children = Array.isArray(node.children) ? this.attachNodeKeys(node.children) : []
        return {
          ...node,
          _nodeKey: `${nodeType}-${node.id}`,
          children
        }
      })
    },
    findNodeByKey(key, nodes = this.deviceTree) {
      const stack = [...nodes]
      while (stack.length > 0) {
        const current = stack.pop()
        if (!current) continue
        if (current._nodeKey === key) {
          return current
        }
        if (Array.isArray(current.children) && current.children.length > 0) {
          stack.push(...current.children)
        }
      }
      return null
    },
    countDeviceNodes(nodes = []) {
      let count = 0
      const stack = [...nodes]
      while (stack.length > 0) {
        const current = stack.pop()
        if (!current) continue
        if (current.type === 'device') {
          count += 1
          continue
        }
        if (Array.isArray(current.children) && current.children.length > 0) {
          stack.push(...current.children)
        }
      }
      return count
    },
    setDefaultScopeAndLoad() {
      const deviceCount = this.countDeviceNodes(this.deviceTree)
      this.selectedScope = {
        label: '全厂设备',
        nodeType: 'all',
        status: '',
        model: '',
        ip: '',
        deviceCount
      }
      this.treeScope = defaultTreeScope()
      this.loadDashboardData({})
    },
    async loadDashboardData(params = {}) {
      try {
        const res = await getDashboardData(params)
        if (res.code === 0) {
          this.dashboardData = {
            totalPieces: this.toNumber(res.data.totalPieces),
            todayPieces: this.toNumber(res.data.todayPieces),
            threadLength: this.toNumber(res.data.threadLength),
            totalThreadLength: this.toNumber(res.data.totalThreadLength || res.data.threadLength),
            usedThreadLength: this.toNumber(res.data.usedThreadLength || res.data.threadLength),
            avgUsedThreadLength: this.toNumber(res.data.avgUsedThreadLength || res.data.usedThreadLength || res.data.threadLength),
            spindleSpeed: this.toNumber(res.data.spindleSpeed),
            runningTime: this.toNumber(res.data.runningTime),
            processingTime: this.toNumber(res.data.processingTime),
            utilizationRate: this.toNumber(res.data.utilizationRate),
            todayAlarmCount: this.toNumber(res.data.todayAlarmCount),
            totalAlarmCount: this.toNumber(res.data.totalAlarmCount),
            onlineDeviceCount: this.toNumber(res.data.onlineDeviceCount),
            scopeDeviceCount: this.toNumber(res.data.scopeDeviceCount),
            hourlyProduction: res.data.hourlyProduction || [],
            pieces7d: (res.data.hourlyProduction || []).slice(-7),
            runningProcessingTrend: res.data.runningProcessingTrend || [],
            utilizationTrend: res.data.utilizationTrend || []
          }
        }
      } catch (error) {
        console.error('Failed to load dashboard data:', error)
        this.dashboardData = {
          totalPieces: 0,
          todayPieces: 0,
          threadLength: 0,
          totalThreadLength: 0,
          usedThreadLength: 0,
          avgUsedThreadLength: 0,
          spindleSpeed: 0,
          runningTime: 0,
          processingTime: 0,
          utilizationRate: 0,
          todayAlarmCount: 0,
          totalAlarmCount: 0,
          onlineDeviceCount: 0,
          scopeDeviceCount: 0,
          hourlyProduction: [],
          pieces7d: [],
          runningProcessingTrend: [],
          utilizationTrend: []
        }
      }

      this.$nextTick(() => {
        this.initCharts()
      })
    },
    handleTreeScopeChange(payload) {
      if (!payload?.nodeType) {
        this.setDefaultScopeAndLoad()
        return
      }

      if (payload.nodeType === 'device') {
        const node = this.findNodeByKey(`device-${payload.deviceId}`)
        this.selectedScope = {
          label: payload.label,
          nodeType: 'device',
          status: node?.status || '',
          model: node?.model || '',
          ip: node?.ip || '',
          deviceCount: 1
        }
        this.loadDashboardData({ deviceId: payload.deviceId })
        return
      }

      const node = this.findNodeByKey(`group-${payload.groupId}`)
      this.selectedScope = {
        label: payload.label || '设备组',
        nodeType: 'group',
        status: '',
        model: '',
        ip: '',
        deviceCount: payload.deviceIds?.length || this.countDeviceNodes(node?.children || [])
      }
      const deviceIds = payload.deviceIds || []
      this.loadDashboardData({ deviceIds: deviceIds.length > 0 ? deviceIds.join(',') : '0' })
    },
    initCharts() {
      this.initRuntimeChart()
      this.initUtilizationChart()
      this.initProductionChart()
    },
    getOrCreateChart(key, refName) {
      if (this.charts[key]) {
        return this.charts[key]
      }
      const el = this.$refs[refName]
      if (!el) return null
      const chart = echarts.init(el)
      this.charts[key] = chart
      return chart
    },
    initRuntimeChart() {
      const chart = this.getOrCreateChart('runtime', 'runtimeChart')
      if (!chart) return

      chart.setOption({
        animationDuration: 500,
        tooltip: {
          trigger: 'axis',
          formatter: params => {
            const title = params?.[0]?.axisValue || ''
            const rows = (params || []).map(item => {
              return `${item.marker}${item.seriesName}: ${this.formatDuration(item.value)}`
            })
            return [title, ...rows].join('<br/>')
          }
        },
        grid: { left: 16, right: 12, top: 24, bottom: 24, containLabel: true },
        xAxis: {
          type: 'category',
          data: this.dashboardData.runningProcessingTrend.map(d => d.date),
          axisTick: { show: false },
          axisLine: { lineStyle: { color: '#d8e1ec' } },
          axisLabel: { color: '#6d7b8f', fontSize: 11 }
        },
        yAxis: {
          type: 'value',
          axisTick: { show: false },
          axisLine: { show: false },
          splitLine: { lineStyle: { color: '#ecf1f6', type: 'dashed' } },
          axisLabel: {
            color: '#6d7b8f',
            fontSize: 11,
            formatter: value => this.formatDuration(value)
          }
        },
        series: [
          {
            name: '运行时长',
            type: 'line',
            smooth: true,
            symbol: 'circle',
            symbolSize: 6,
            data: this.dashboardData.runningProcessingTrend.map(d => this.toNumber(d.runningTime)),
            lineStyle: { color: '#6a9dff', width: 3 },
            itemStyle: { color: '#6a9dff' }
          },
          {
            name: '加工时长',
            type: 'line',
            smooth: true,
            symbol: 'circle',
            symbolSize: 6,
            data: this.dashboardData.runningProcessingTrend.map(d => this.toNumber(d.processingTime)),
            lineStyle: { color: '#7cc66f', width: 3 },
            itemStyle: { color: '#7cc66f' }
          }
        ]
      }, true)
    },
    initUtilizationChart() {
      const chart = this.getOrCreateChart('utilization', 'utilizationChart')
      if (!chart) return
      const values = this.dashboardData.utilizationTrend.map(d => this.toNumber(d.value))
      const maxValue = Math.max(...values, 0)
      const yAxisMax = maxValue > 0 && maxValue < 10 ? Math.max(1, Math.ceil(maxValue * 1.2)) : 100

      chart.setOption({
        animationDuration: 500,
        tooltip: {
          trigger: 'axis',
          formatter: params => {
            const item = Array.isArray(params) ? params[0] : params
            return `${item?.axisValue || ''}<br />设备使用率：${this.formatPercent(item?.data || 0)}`
          }
        },
        grid: { left: 18, right: 12, top: 16, bottom: 16, containLabel: true },
        xAxis: {
          type: 'category',
          data: this.dashboardData.utilizationTrend.map(d => d.date),
          axisTick: { show: false },
          axisLine: { lineStyle: { color: '#d8e1ec' } },
          axisLabel: { color: '#6d7b8f', fontSize: 11 }
        },
        yAxis: {
          type: 'value',
          min: 0,
          max: yAxisMax,
          axisTick: { show: false },
          axisLine: { show: false },
          splitLine: { lineStyle: { color: '#ecf1f6', type: 'dashed' } },
          axisLabel: { color: '#6d7b8f', fontSize: 11, formatter: value => this.formatPercent(value) }
        },
        series: [{
          type: 'bar',
          barWidth: 26,
          data: values,
          itemStyle: {
            color: '#3c89f7'
          }
        }]
      }, true)
    },
    initProductionChart() {
      const chart = this.getOrCreateChart('production', 'productionChart')
      if (!chart) return

      chart.setOption({
        animationDuration: 500,
        tooltip: {
          trigger: 'axis',
          formatter: params => {
            const point = Array.isArray(params) ? params[0] : params
            return `${point.axisValue}<br />${this.selectedScope.label} 当天产量 ${point.data}`
          }
        },
        grid: { left: 34, right: 18, top: 22, bottom: 22 },
        xAxis: {
          type: 'category',
          boundaryGap: false,
          data: this.dashboardData.hourlyProduction.map(d => d.date || d.hour),
          axisTick: { show: false },
          axisLine: { lineStyle: { color: '#d8e1ec' } },
          axisLabel: { color: '#6d7b8f', fontSize: 11 }
        },
        yAxis: {
          type: 'value',
          axisTick: { show: false },
          axisLine: { show: false },
          splitLine: { lineStyle: { color: '#ecf1f6', type: 'dashed' } },
          axisLabel: { color: '#6d7b8f', fontSize: 11 }
        },
        series: [{
          type: 'line',
          smooth: true,
          symbol: 'circle',
          symbolSize: 8,
          data: this.dashboardData.hourlyProduction.map(d => this.toNumber(d.value)),
          lineStyle: { color: '#14c8f7', width: 4 },
          itemStyle: {
            color: '#14c8f7',
            borderColor: '#ffffff',
            borderWidth: 2
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
.dashboard-page {
  height: 100%;
  background: #f2f5f8;
  overflow: auto;
}

.dashboard-layout {
  height: 100%;
  min-width: 0;
  padding: 10px;
  box-sizing: border-box;
  display: grid;
  grid-template-columns: 272px minmax(0, 1fr);
  gap: 10px;
  align-items: stretch;
}

.dashboard-side {
  min-width: 0;
}

.side-legend {
  height: 28px;
  padding: 0 10px;
  display: flex;
  align-items: center;
  justify-content: flex-end;
  gap: 14px;
  color: #778396;
  font-size: 12px;
}

.side-legend__item {
  display: inline-flex;
  align-items: center;
  gap: 5px;
}

.side-legend__dot {
  width: 10px;
  height: 10px;
  border-radius: 50%;
}

.side-legend__dot.is-idle {
  background: #70cb37;
}

.side-legend__dot.is-alarm {
  background: #ff3b30;
}

.side-legend__dot.is-offline {
  background: #7b7b7b;
}

.dashboard-side ::v-deep .device-tree-panel {
  height: calc(100% - 28px);
}

.dashboard-side ::v-deep .panel-shell {
  padding: 0;
  border: 1px solid #dfe4eb;
  border-radius: 2px;
  background: #fff;
}

.dashboard-side ::v-deep .panel-header {
  min-height: 38px;
  padding: 10px 12px 6px;
}

.dashboard-side ::v-deep .panel-title {
  color: #434d5b;
  font-size: 13px;
  font-weight: 700;
}

.dashboard-side ::v-deep .tree-wrapper {
  border: none;
  border-top: 1px solid #edf1f5;
}

.dashboard-side ::v-deep .tree-node-label {
  font-size: 12px;
}

.dashboard-main {
  min-width: 0;
  min-height: 0;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.dashboard-stat-row {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 10px;
}

.metric-card {
  min-height: 98px;
  padding: 16px 18px;
  border-radius: 6px;
  color: #fff;
  box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.08);
}

.metric-card.is-blue {
  background: linear-gradient(180deg, #29a9f7, #1f98ef);
}

.metric-card.is-green {
  background: linear-gradient(180deg, #3fdc00, #31c900);
}

.metric-card.is-orange {
  background: linear-gradient(180deg, #ffab18, #ff9800);
}

.metric-card.is-cyan {
  background: linear-gradient(180deg, #21cdd3, #18bec4);
}

.metric-card__title {
  margin-bottom: 14px;
  font-size: 13px;
  font-weight: 700;
}

.metric-card__metrics {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 14px;
}

.metric-card__label {
  margin-bottom: 8px;
  font-size: 11px;
  opacity: 0.88;
}

.metric-card__value {
  font-size: 18px;
  font-weight: 700;
  line-height: 1;
}

.metric-card__value small {
  margin-left: 4px;
  font-size: 11px;
  font-weight: 600;
}

.dashboard-chart-row {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
}

.board-card {
  border: 1px solid #dfe4eb;
  border-radius: 2px;
  box-shadow: none;

  ::v-deep .el-card__header {
    padding: 12px 14px 0;
    border-bottom: none;
  }

  ::v-deep .el-card__body {
    padding: 10px 14px 14px;
  }
}

.board-card--production {
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
}

.board-card__header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 12px;
}

.board-card__title {
  color: #404a58;
  font-size: 13px;
  font-weight: 700;
}

.board-card__legend {
  display: inline-flex;
  align-items: center;
  gap: 12px;
}

.board-card__legend-item {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  color: #6d7b8f;
  font-size: 12px;
}

.legend-line {
  width: 18px;
  height: 3px;
  border-radius: 999px;
}

.legend-line.is-blue {
  background: #6a9dff;
}

.legend-line.is-green {
  background: #7cc66f;
}

.runtime-panel,
.utilization-panel {
  display: grid;
  grid-template-columns: 126px minmax(0, 1fr);
  gap: 10px;
  align-items: stretch;
}

.runtime-panel {
  grid-template-columns: 178px minmax(0, 1fr);
}

.runtime-panel__summary {
  min-height: 188px;
  padding: 0 0 8px 6px;
  display: flex;
  flex-direction: column;
  justify-content: center;
  gap: 24px;
}

.runtime-panel__metric {
  display: grid;
  gap: 8px;
}

.utilization-panel__summary {
  padding: 54px 0 0 6px;
}

.runtime-panel__label,
.utilization-panel__label {
  color: #576474;
  font-size: 12px;
  font-weight: 700;
}

.runtime-panel__value,
.utilization-panel__value {
  margin-top: 8px;
  color: #56a3ff;
  font-size: 30px;
  font-weight: 700;
  line-height: 1;
}

.runtime-panel__value.is-green {
  color: #6dc56e;
}

.runtime-panel__value {
  font-size: 20px;
  line-height: 1.15;
  white-space: nowrap;
}

.runtime-panel__chart,
.utilization-panel__chart {
  height: 188px;
}

.board-card--production ::v-deep .el-card__body {
  flex: 1;
  min-height: 0;
  padding: 6px 14px 14px;
  display: flex;
}

.production-chart {
  flex: 1;
  min-height: 350px;
}

@media (max-width: 1440px) {
  .dashboard-layout {
    grid-template-columns: 258px minmax(0, 1fr);
  }
}

@media (max-width: 0px) {
  .dashboard-layout {
    grid-template-columns: 1fr;
  }

  .dashboard-side ::v-deep .device-tree-panel {
    height: auto;
    max-height: 320px;
  }
}

@media (max-width: 0px) {
  .dashboard-layout {
    padding: 6px;
    gap: 8px;
  }

  .dashboard-main,
  .dashboard-stat-row,
  .dashboard-chart-row {
    gap: 8px;
  }

  .dashboard-stat-row {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }

  .dashboard-chart-row {
    grid-template-columns: 1fr;
  }

  .metric-card {
    min-height: 86px;
    padding: 12px;
  }

  .runtime-panel,
  .utilization-panel {
    grid-template-columns: 1fr;
  }

  .runtime-panel__summary,
  .utilization-panel__summary {
    min-height: auto;
    padding: 0;
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 12px;
  }

  .runtime-panel__value,
  .utilization-panel__value {
    font-size: 24px;
  }

  .runtime-panel__chart,
  .utilization-panel__chart {
    height: 220px;
  }

  .production-chart {
    height: 280px;
  }
}

@media (max-width: 0px) {
  .dashboard-stat-row,
  .metric-card__metrics,
  .runtime-panel__summary,
  .utilization-panel__summary {
    grid-template-columns: 1fr;
  }

  .side-legend,
  .board-card__legend {
    flex-wrap: wrap;
    justify-content: flex-start;
    height: auto;
  }
}
</style>
