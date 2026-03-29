<template>
  <div class="page-container">
    <div class="dashboard-layout">
      <aside class="dashboard-side">
        <device-tree-panel
          v-model="treeScope"
          title="设备范围"
          :min-height="620"
          @change="handleTreeScopeChange"
          @refresh="fetchDeviceTree"
        />
      </aside>

      <section class="dashboard-main">
        <div class="scope-card card">
          <div class="scope-meta">
            <div class="scope-badge" :class="selectedScope.nodeType || 'all'">
              {{ scopeBadgeText }}
            </div>
            <div class="meta-chip">
              <span>当前范围</span>
              <strong>{{ selectedScope.label }}</strong>
            </div>
            <div class="meta-chip">
              <span>设备数量</span>
              <strong>{{ selectedScope.deviceCount || 0 }} 台</strong>
            </div>
            <div v-if="selectedScope.nodeType === 'device'" class="meta-chip">
              <span>设备型号</span>
              <strong>{{ selectedScope.model || '-' }}</strong>
            </div>
            <div v-if="selectedScope.nodeType === 'device'" class="meta-chip">
              <span>设备 IP</span>
              <strong>{{ selectedScope.ip || '-' }}</strong>
            </div>
            <el-button icon="el-icon-refresh" circle @click="reloadCurrentScope" />
          </div>
        </div>

        <el-row :gutter="20" class="stat-row">
          <el-col :span="6">
            <div class="stat-card blue">
              <div class="stat-icon"><i class="el-icon-s-goods"></i></div>
              <div class="stat-info">
                <div class="stat-value">{{ dashboardData.totalPieces }}</div>
                <div class="stat-extra">今日 {{ dashboardData.todayPieces }} 件</div>
                <div class="stat-label">{{ $t('dashboard.totalPieces') }}</div>
              </div>
            </div>
          </el-col>
          <el-col :span="6">
            <div class="stat-card green">
              <div class="stat-icon"><i class="el-icon-sort"></i></div>
              <div class="stat-info">
                <div class="stat-value">{{ dashboardData.totalThreadLength }}<small>m</small></div>
                <div class="stat-extra">{{ threadExtraText }}</div>
                <div class="stat-label">{{ $t('dashboard.threadLength') }}</div>
              </div>
            </div>
          </el-col>
          <el-col :span="6">
            <div class="stat-card orange">
              <div class="stat-icon"><i class="el-icon-time"></i></div>
              <div class="stat-info">
                <div class="stat-value">{{ dashboardData.runningTime }}<small>h</small></div>
                <div class="stat-extra">{{ runtimeExtraText }}</div>
                <div class="stat-label">{{ $t('dashboard.runningTime') }}</div>
              </div>
            </div>
          </el-col>
          <el-col :span="6">
            <div class="stat-card">
              <div class="stat-icon"><i class="el-icon-data-line"></i></div>
              <div class="stat-info">
                <div class="stat-value">{{ dashboardData.utilizationRate }}<small>%</small></div>
                <div class="stat-extra">当前范围综合使用率</div>
                <div class="stat-label">{{ $t('dashboard.utilizationRate') }}</div>
              </div>
            </div>
          </el-col>
        </el-row>

        <div class="dashboard-grid">
          <div class="chart-card gauge-card">
            <div class="chart-title">{{ $t('dashboard.spindleSpeed') }}</div>
            <div class="chart-subtitle">当前设备或所选范围的主轴转速</div>
            <div ref="gaugeChart" class="chart-container gauge"></div>
          </div>

          <div class="chart-card">
            <div class="chart-title">加工总件数（近7天）</div>
            <div class="chart-subtitle">用于观察每日产量波动</div>
            <div ref="pieces7dChart" class="chart-container"></div>
          </div>

          <div class="chart-card">
            <div class="chart-title">加工产量统计</div>
            <div class="chart-subtitle">展示接口返回的时序产量数据</div>
            <div ref="productionChart" class="chart-container"></div>
          </div>

          <div class="chart-card chart-wide">
            <div class="chart-title">运行 / 加工时长（近7天）</div>
            <div class="chart-subtitle">运行时长与加工时长对照查看</div>
            <div ref="runtimeChart" class="chart-container"></div>
          </div>

          <div class="chart-card chart-wide">
            <div class="chart-title">设备使用率（近7天）</div>
            <div class="chart-subtitle">按日查看使用率变化趋势</div>
            <div ref="utilizationChart" class="chart-container"></div>
          </div>
        </div>
      </section>
    </div>
  </div>
</template>

<script>
import * as echarts from 'echarts'
import { getDeviceTree } from '@/api/device'
import { getDashboardData } from '@/api/statistics'
import DeviceTreePanel from '@/components/DeviceTreePanel.vue'

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
        hourlyProduction: [],
        pieces7d: [],
        runningProcessingTrend: [],
        utilizationTrend: []
      },
      charts: {}
    }
  },
  computed: {
    runtimeExtraText() {
      if (this.selectedScope.nodeType === 'group') {
        return `组均加工 ${this.dashboardData.processingTime}h`
      }
      if (this.selectedScope.nodeType === 'all') {
        return `厂均加工 ${this.dashboardData.processingTime}h`
      }
      return `加工 ${this.dashboardData.processingTime}h`
    },
    threadExtraText() {
      if (this.selectedScope.nodeType === 'device') {
        return `已用 ${this.dashboardData.usedThreadLength}m`
      }
      return `平均 ${this.dashboardData.avgUsedThreadLength}m · 累计 ${this.dashboardData.usedThreadLength}m`
    },
    scopeBadgeText() {
      if (this.selectedScope.nodeType === 'device') return '单台设备'
      if (this.selectedScope.nodeType === 'group') return '设备组'
      return '全厂汇总'
    },
    scopeDescription() {
      if (this.selectedScope.nodeType === 'device') {
        return `当前查看 ${this.selectedScope.label} 的实时数据看板。`
      }
      if (this.selectedScope.nodeType === 'group') {
        return `当前范围为设备组，共 ${this.selectedScope.deviceCount} 台设备。`
      }
      return '当前查看全厂设备综合数据。'
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
    async loadDashboardData(params = {}) {
      try {
        const res = await getDashboardData(params)
        if (res.code === 0) {
          this.dashboardData = {
            totalPieces: res.data.totalPieces || 0,
            todayPieces: res.data.todayPieces || 0,
            threadLength: res.data.threadLength || 0,
            totalThreadLength: res.data.totalThreadLength || res.data.threadLength || 0,
            usedThreadLength: res.data.usedThreadLength || res.data.threadLength || 0,
            avgUsedThreadLength: res.data.avgUsedThreadLength || res.data.usedThreadLength || res.data.threadLength || 0,
            spindleSpeed: res.data.spindleSpeed || 0,
            runningTime: res.data.runningTime || 0,
            processingTime: res.data.processingTime || 0,
            utilizationRate: res.data.utilizationRate || 0,
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
      this.loadDashboardData({ deviceIds: (payload.deviceIds || []).join(',') })
    },
    reloadCurrentScope() {
      if (this.treeScope.nodeType === 'device') {
        this.loadDashboardData({ deviceId: this.treeScope.deviceId })
        return
      }
      if (this.treeScope.nodeType === 'group') {
        this.loadDashboardData({ deviceIds: this.treeScope.deviceIds.join(',') })
        return
      }
      this.loadDashboardData({})
    },
    initCharts() {
      this.initGaugeChart()
      this.initPieces7dChart()
      this.initProductionChart()
      this.initRuntimeChart()
      this.initUtilizationChart()
    },
    getOrCreateChart(key, refName) {
      if (this.charts[key]) {
        return this.charts[key]
      }
      if (!this.$refs[refName]) return null
      const chart = echarts.init(this.$refs[refName])
      this.charts[key] = chart
      return chart
    },
    initGaugeChart() {
      const chart = this.getOrCreateChart('gauge', 'gaugeChart')
      if (!chart) return
      chart.setOption({
        series: [{
          type: 'gauge',
          startAngle: 210,
          endAngle: -30,
          min: 0,
          max: 5000,
          splitNumber: 5,
          itemStyle: { color: '#2f6df6' },
          progress: {
            show: true,
            width: 18
          },
          pointer: {
            show: false
          },
          axisLine: {
            lineStyle: {
              width: 18,
              color: [[1, '#e8eef7']]
            }
          },
          axisTick: { show: false },
          splitLine: { show: false },
          axisLabel: {
            distance: 26,
            color: '#8a98ad',
            fontSize: 12
          },
          anchor: { show: false },
          title: {
            offsetCenter: [0, '46%'],
            color: '#8a98ad',
            fontSize: 13
          },
          detail: {
            valueAnimation: true,
            offsetCenter: [0, '8%'],
            fontSize: 30,
            fontWeight: 'bold',
            formatter: '{value}',
            color: '#22324d'
          },
          data: [{
            value: this.dashboardData.spindleSpeed,
            name: 'RPM'
          }]
        }]
      }, true)
    },
    initProductionChart() {
      const chart = this.getOrCreateChart('production', 'productionChart')
      if (!chart) return
      chart.setOption({
        tooltip: { trigger: 'axis', axisPointer: { type: 'line' } },
        grid: { left: '4%', right: '4%', bottom: '4%', top: 20, containLabel: true },
        xAxis: {
          type: 'category',
          data: this.dashboardData.hourlyProduction.map(d => d.date || d.hour),
          axisLine: { lineStyle: { color: '#dbe4f0' } },
          axisLabel: { color: '#6a7f9d', rotate: 35 }
        },
        yAxis: {
          type: 'value',
          axisLabel: { color: '#6a7f9d' },
          splitLine: { lineStyle: { color: '#edf2f8' } }
        },
        series: [{
          type: 'line',
          smooth: true,
          symbol: 'circle',
          symbolSize: 7,
          data: this.dashboardData.hourlyProduction.map(d => d.value),
          lineStyle: { color: '#2f6df6', width: 3 },
          itemStyle: { color: '#2f6df6' },
          areaStyle: {
            color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
              { offset: 0, color: 'rgba(47, 109, 246, 0.28)' },
              { offset: 1, color: 'rgba(47, 109, 246, 0.04)' }
            ])
          }
        }]
      }, true)
    },
    initPieces7dChart() {
      const chart = this.getOrCreateChart('pieces7d', 'pieces7dChart')
      if (!chart) return
      chart.setOption({
        tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' } },
        grid: { left: '4%', right: '4%', bottom: '4%', top: 20, containLabel: true },
        xAxis: {
          type: 'category',
          data: this.dashboardData.pieces7d.map(d => d.date || d.hour),
          axisLine: { lineStyle: { color: '#dbe4f0' } },
          axisLabel: { color: '#6a7f9d' }
        },
        yAxis: {
          type: 'value',
          axisLabel: { color: '#6a7f9d' },
          splitLine: { lineStyle: { color: '#edf2f8' } }
        },
        series: [{
          type: 'bar',
          data: this.dashboardData.pieces7d.map(d => d.value),
          barWidth: 22,
          itemStyle: {
            color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
              { offset: 0, color: '#2fb46e' },
              { offset: 1, color: '#1f935e' }
            ]),
            borderRadius: [10, 10, 0, 0]
          }
        }]
      }, true)
    },
    initRuntimeChart() {
      const chart = this.getOrCreateChart('runtime', 'runtimeChart')
      if (!chart) return
      chart.setOption({
        tooltip: { trigger: 'axis' },
        legend: {
          top: 0,
          textStyle: { color: '#6a7f9d' },
          data: ['运行时长', '加工时长']
        },
        grid: { left: '4%', right: '4%', bottom: '4%', top: 40, containLabel: true },
        xAxis: {
          type: 'category',
          data: this.dashboardData.runningProcessingTrend.map(d => d.date),
          axisLine: { lineStyle: { color: '#dbe4f0' } },
          axisLabel: { color: '#6a7f9d' }
        },
        yAxis: {
          type: 'value',
          axisLabel: { color: '#6a7f9d', formatter: '{value}h' },
          splitLine: { lineStyle: { color: '#edf2f8' } }
        },
        series: [
          {
            name: '运行时长',
            type: 'line',
            smooth: true,
            symbol: 'circle',
            symbolSize: 6,
            data: this.dashboardData.runningProcessingTrend.map(d => d.runningTime),
            lineStyle: { color: '#2fb46e', width: 3 },
            itemStyle: { color: '#2fb46e' }
          },
          {
            name: '加工时长',
            type: 'line',
            smooth: true,
            symbol: 'circle',
            symbolSize: 6,
            data: this.dashboardData.runningProcessingTrend.map(d => d.processingTime),
            lineStyle: { color: '#2f6df6', width: 3 },
            itemStyle: { color: '#2f6df6' }
          }
        ]
      }, true)
    },
    initUtilizationChart() {
      const chart = this.getOrCreateChart('utilization', 'utilizationChart')
      if (!chart) return
      chart.setOption({
        tooltip: {
          trigger: 'axis',
          formatter: '{b}: {c}%'
        },
        grid: { left: '4%', right: '4%', bottom: '4%', top: 20, containLabel: true },
        xAxis: {
          type: 'category',
          data: this.dashboardData.utilizationTrend.map(d => d.date),
          axisLine: { lineStyle: { color: '#dbe4f0' } },
          axisLabel: { color: '#6a7f9d' }
        },
        yAxis: {
          type: 'value',
          max: 100,
          axisLabel: { color: '#6a7f9d', formatter: '{value}%' },
          splitLine: { lineStyle: { color: '#edf2f8' } }
        },
        series: [{
          type: 'bar',
          data: this.dashboardData.utilizationTrend.map(d => d.value),
          barWidth: 24,
          itemStyle: {
            color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
              { offset: 0, color: '#f0b037' },
              { offset: 1, color: '#cf7b11' }
            ]),
            borderRadius: [10, 10, 0, 0]
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
.dashboard-layout {
  display: flex;
  gap: 18px;
  min-height: calc(100vh - 132px);
}

.dashboard-side {
  width: 280px;
  flex-shrink: 0;
}

.dashboard-main {
  flex: 1;
  min-width: 0;
}

.scope-card {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 18px;
  margin-bottom: 18px;
}

.scope-badge {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-width: 88px;
  height: 32px;
  padding: 0 14px;
  border-radius: 999px;
  background: #eef4ff;
  color: #2f6df6;
  font-size: 12px;
  font-weight: 700;

  &.device {
    background: rgba(47, 180, 110, 0.12);
    color: #2fb46e;
  }

  &.group {
    background: rgba(47, 109, 246, 0.12);
    color: #2f6df6;
  }
}

.scope-meta {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
  min-width: 0;
}

.meta-chip {
  min-width: 120px;
  padding: 12px 14px;
  border-radius: 18px;
  background: #f7faff;

  span {
    display: block;
    color: #8a98ad;
    font-size: 12px;
    margin-bottom: 6px;
  }

  strong {
    color: #22324d;
    font-size: 15px;
  }
}

.dashboard-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 18px;
}

.gauge-card {
  grid-row: span 1;
}

.chart-wide {
  grid-column: span 2;
}

::v-deep .chart-card .gauge {
  height: 300px;
}

@media (max-width: 1200px) {
  .dashboard-layout {
    flex-direction: column;
  }

  .dashboard-side {
    width: 100%;
  }
}

@media (max-width: 768px) {
  .scope-card {
    flex-direction: column;
    align-items: flex-start;
  }

  .dashboard-grid {
    grid-template-columns: 1fr;
  }

  .chart-wide {
    grid-column: span 1;
  }
}
</style>
