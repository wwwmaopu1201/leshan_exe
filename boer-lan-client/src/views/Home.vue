<template>
  <div class="page-container home-page">
    <div class="home-layout">
      <section class="home-top-grid">
        <div class="home-stack">
          <el-card shadow="never" class="panel-card panel-card--status">
            <div slot="header" class="panel-header">
              <span class="panel-title">设备状态统计</span>
            </div>

            <div class="status-grid">
              <div
                v-for="item in statusCards"
                :key="item.key"
                class="status-item"
              >
                <div class="status-item__icon" :class="item.color">
                  <i :class="item.icon"></i>
                </div>
                <div class="status-item__label">{{ item.label }}</div>
                <div class="status-item__value">
                  {{ item.value }}
                  <span class="status-item__unit">{{ item.unit }}</span>
                </div>
              </div>
            </div>
          </el-card>

          <el-card shadow="never" class="panel-card panel-card--usage">
            <div slot="header" class="panel-header panel-header--split">
              <span class="panel-title">设备使用率统计</span>
              <div class="usage-tabs">
                <button
                  v-for="tab in usageTabs"
                  :key="tab.key"
                  type="button"
                  class="usage-tabs__button"
                  :class="{ 'is-active': activeUsageTab === tab.key }"
                  @click="activeUsageTab = tab.key"
                >
                  {{ tab.label }}
                </button>
              </div>
            </div>

            <el-table
              :data="visibleUsageRows"
              height="236"
              size="mini"
              stripe
              class="usage-table"
            >
              <el-table-column prop="rank" label="序号" width="58" align="center" />
              <el-table-column prop="name" :label="usageNameLabel" min-width="134" show-overflow-tooltip />
              <el-table-column prop="runningTimeLabel" label="运行时长" width="126" align="center" />
              <el-table-column prop="idleTimeLabel" label="待机时长" width="126" align="center" />
              <el-table-column prop="efficiencyLabel" label="使用率" width="104" align="center" />
            </el-table>
          </el-card>
        </div>

        <el-card shadow="never" class="panel-card panel-card--pattern">
          <div slot="header" class="panel-header">
            <span class="panel-title">实时花型款号数据</span>
          </div>

          <div class="pattern-list">
            <div class="pattern-list__head">
              <span class="pattern-list__head-rank">序号</span>
              <span class="pattern-list__head-name">花型名称</span>
              <span class="pattern-list__head-value">次数</span>
            </div>

            <template v-if="patternRows.length">
              <div
                v-for="(item, index) in patternRows"
                :key="`${item.name}-${index}`"
                class="pattern-row"
                :class="{ 'is-highlight': index === 0 }"
              >
                <span class="pattern-row__rank">{{ index + 1 }}</span>
                <span class="pattern-row__name">{{ item.name }}</span>
                <span class="pattern-row__value">{{ item.value }}</span>
              </div>
            </template>
            <div v-else class="pattern-empty">
              暂无实时花型数据
            </div>
          </div>
        </el-card>

        <el-card shadow="never" class="panel-card panel-card--ranking">
          <div slot="header" class="panel-header">
            <span class="panel-title">设备效率排名</span>
          </div>

          <div class="ranking-grid">
            <div class="ranking-column">
              <div class="ranking-column__title">
                效率排名
                <span>（由高到低）</span>
              </div>
              <div class="ranking-list">
                <div
                  v-for="item in highEfficiencyRows"
                  :key="`high-${item.name}`"
                  class="ranking-item"
                >
                  <div class="ranking-item__header">
                    <span class="ranking-item__name">{{ item.name }}</span>
                    <strong class="ranking-item__value">{{ item.efficiencyLabel }}</strong>
                  </div>
                  <div class="ranking-bar">
                    <span class="ranking-bar__fill is-green" :style="{ width: item.efficiencyBarWidth }"></span>
                  </div>
                </div>
              </div>
            </div>

            <div class="ranking-column">
              <div class="ranking-column__title">
                低效率排名
                <span>（由低到高）</span>
              </div>
              <div class="ranking-list">
                <div
                  v-for="item in lowEfficiencyRows"
                  :key="`low-${item.name}`"
                  class="ranking-item"
                >
                  <div class="ranking-item__header">
                    <span class="ranking-item__name">{{ item.name }}</span>
                    <strong class="ranking-item__value">{{ item.efficiencyLabel }}</strong>
                  </div>
                  <div class="ranking-bar">
                    <span class="ranking-bar__fill is-red" :style="{ width: item.efficiencyBarWidth }"></span>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </el-card>
      </section>

      <section class="home-bottom-grid">
        <el-card shadow="never" class="panel-card panel-card--chart">
          <div slot="header" class="panel-header panel-header--split">
            <span class="panel-title">当前设备运行状态</span>
            <div class="chart-legend">
              <span class="chart-legend__item">
                <i class="chart-legend__dot is-green"></i>
                开机数
              </span>
              <span class="chart-legend__item">
                <i class="chart-legend__dot is-orange"></i>
                关机数
              </span>
            </div>
          </div>

          <div ref="runningChart" class="chart-surface chart-surface--running"></div>
        </el-card>

        <el-card shadow="never" class="panel-card panel-card--chart panel-card--production">
          <div slot="header" class="panel-header panel-header--split">
            <span class="panel-title">产量统计</span>
            <div class="production-toolbar">
              <div class="production-toolbar__tabs">
                <button
                  type="button"
                  class="production-toolbar__tab"
                  :class="{ 'is-active': productionRange === '7d' }"
                  @click="applyQuickRange('7d')"
                >
                  近7日
                </button>
                <button
                  type="button"
                  class="production-toolbar__tab"
                  :class="{ 'is-active': productionRange === '30d' }"
                  @click="applyQuickRange('30d')"
                >
                  近1月
                </button>
              </div>

              <el-date-picker
                v-model="productionStartDate"
                type="date"
                size="mini"
                value-format="yyyy-MM-dd"
                placeholder="开始时间"
                @change="handleProductionDateChange"
              />
              <span class="production-toolbar__sep">至</span>
              <el-date-picker
                v-model="productionEndDate"
                type="date"
                size="mini"
                value-format="yyyy-MM-dd"
                placeholder="结束时间"
                @change="handleProductionDateChange"
              />
            </div>
          </div>

          <div ref="productionChart" class="chart-surface chart-surface--production"></div>
        </el-card>
      </section>
    </div>
  </div>
</template>

<script>
import * as echarts from 'echarts'
import { getDeviceGroups, getDeviceList } from '@/api/device'
import { getHomeStats, getProcessOverview } from '@/api/statistics'
import { formatDurationFromHours } from '@/utils'

const USAGE_TABS = [
  { key: 'device', label: '设备' },
  { key: 'group', label: '设备组' },
  { key: 'line', label: '生产线' }
]

export default {
  name: 'Home',
  data() {
    return {
      homeStats: {
        totalDevices: 0,
        onlineDevices: 0,
        workingDevices: 0,
        offlineDevices: 0,
        alarmDevices: 0,
        patternUsage: [],
        deviceUsage: [],
        runningStatusByHour: [],
        productionByDay: []
      },
      deviceCatalog: [],
      groupCatalog: [],
      productionTrend: [],
      charts: {},
      refreshTimer: null,
      activeUsageTab: 'device',
      productionRange: '7d',
      productionStartDate: '',
      productionEndDate: ''
    }
  },
  computed: {
    usageTabs() {
      return USAGE_TABS
    },
    usageNameLabel() {
      if (this.activeUsageTab === 'group') return '设备组名称'
      if (this.activeUsageTab === 'line') return '生产线名称'
      return '设备名称'
    },
    groupMetaMap() {
      return new Map(this.flattenGroups(this.groupCatalog).map(item => [Number(item.id), item]))
    },
    deviceMetaMap() {
      const byId = new Map()
      const byName = new Map()
      ;(this.deviceCatalog || []).forEach(item => {
        byId.set(String(item.id), item)
        byName.set(String(item.name || '').trim(), item)
      })
      return { byId, byName }
    },
    statusCards() {
      return [
        {
          key: 'total',
          label: '设备总数',
          value: this.toNumber(this.homeStats.totalDevices),
          unit: '台',
          icon: 'el-icon-s-platform',
          color: 'is-sand'
        },
        {
          key: 'online',
          label: '设备在线数',
          value: this.toNumber(this.homeStats.onlineDevices),
          unit: '台',
          icon: 'el-icon-monitor',
          color: 'is-green'
        },
        {
          key: 'offline',
          label: '设备关机数',
          value: this.toNumber(this.homeStats.offlineDevices),
          unit: '台',
          icon: 'el-icon-switch-button',
          color: 'is-orange'
        },
        {
          key: 'onlineRate',
          label: '设备在线率',
          value: this.formatPlainPercent(this.safePercent(this.homeStats.onlineDevices, this.homeStats.totalDevices)),
          unit: '%',
          icon: 'el-icon-data-analysis',
          color: 'is-blue'
        }
      ]
    },
    patternRows() {
      return (this.homeStats.patternUsage || []).slice(0, 10).map(item => ({
        name: item.name || '未命名花型',
        value: this.toNumber(item.value)
      }))
    },
    deviceUsageRows() {
      const rows = (this.homeStats.deviceUsage || [])
        .map(item => {
          const meta = this.resolveDeviceMeta(item)
          return {
            name: item.name || meta.name || `设备${item.deviceId || ''}`,
            groupId: this.toNumber(meta.groupId),
            group: meta.groupName || meta.group || '未分组',
            lineId: this.toNumber(meta.lineId),
            line: meta.line || '未分线',
            runningTime: this.toNumber(item.runningTime),
            idleTime: this.toNumber(item.idleTime),
            efficiency: this.toNumber(item.efficiency)
          }
        })
        .sort((a, b) => b.efficiency - a.efficiency)

      return this.decorateUsageRows(rows)
    },
    visibleUsageRows() {
      if (this.activeUsageTab === 'device') {
        return this.deviceUsageRows
      }

      const grouped = new Map()
      this.deviceUsageRows.forEach(item => {
        const isGroupMode = this.activeUsageTab === 'group'
        const key = isGroupMode
          ? `group:${item.groupId || item.group}`
          : `line:${item.lineId || item.line}`
        if (!grouped.has(key)) {
          grouped.set(key, {
            name: isGroupMode ? item.group : item.line,
            runningTime: 0,
            idleTime: 0,
            efficiencySum: 0,
            deviceCount: 0
          })
        }
        const target = grouped.get(key)
        target.runningTime += item.runningTime
        target.idleTime += item.idleTime
        target.efficiencySum += item.efficiency
        target.deviceCount += 1
      })

      return this.decorateUsageRows(
        Array.from(grouped.values()).map(item => ({
          ...item,
          efficiency: item.deviceCount > 0 ? item.efficiencySum / item.deviceCount : 0
        }))
      )
    },
    highEfficiencyRows() {
      return this.deviceUsageRows.slice(0, 10)
    },
    lowEfficiencyRows() {
      return [...this.deviceUsageRows]
        .sort((a, b) => a.efficiency - b.efficiency)
        .slice(0, 10)
        .map((item, index) => this.decorateUsageRow(item, index))
    },
    productionChartRows() {
      const source = (this.productionTrend && this.productionTrend.length > 0)
        ? this.productionTrend
        : (this.homeStats.productionByDay || [])

      return source.map((item, index) => ({
        label: this.formatAxisDate(item.date, index),
        value: this.toNumber(item.pieces ?? item.value)
      }))
    }
  },
  mounted() {
    this.applyQuickRange('7d', false)
    this.fetchPageData()
    this.startAutoRefresh()
    window.addEventListener('resize', this.handleResize)
  },
  beforeDestroy() {
    this.stopAutoRefresh()
    window.removeEventListener('resize', this.handleResize)
    Object.values(this.charts).forEach(chart => chart && chart.dispose())
  },
  methods: {
    toNumber(value) {
      const num = Number(value)
      return Number.isFinite(num) ? num : 0
    },
    safePercent(value, total) {
      const numerator = this.toNumber(value)
      const denominator = this.toNumber(total)
      if (denominator <= 0) {
        return 0
      }
      return (numerator / denominator) * 100
    },
    formatPlainPercent(value) {
      return this.toNumber(value).toFixed(0)
    },
    formatPercent(value) {
      return `${this.toNumber(value).toFixed(2)}%`
    },
    formatDuration(hours) {
      return formatDurationFromHours(this.toNumber(hours))
    },
    formatAxisDate(date, index) {
      const raw = String(date || '').trim()
      if (!raw) {
        return `第${index + 1}项`
      }
      if (raw.includes('-') && raw.length >= 5) {
        const parts = raw.split('-')
        return `${parts[parts.length - 2]}-${parts[parts.length - 1]}`
      }
      return raw
    },
    getToday() {
      return this.formatDate(new Date())
    },
    formatDate(date) {
      const year = date.getFullYear()
      const month = String(date.getMonth() + 1).padStart(2, '0')
      const day = String(date.getDate()).padStart(2, '0')
      return `${year}-${month}-${day}`
    },
    offsetDate(days) {
      const date = new Date()
      date.setDate(date.getDate() + days)
      return this.formatDate(date)
    },
    normalizeGroupParentId(item) {
      return this.toNumber(item.parentId ?? item.parent_id ?? item.parentID)
    },
    flattenGroups(groups) {
      const result = []
      const walk = (items = [], parentId = 0) => {
        items.forEach(item => {
          if (!item) {
            return
          }
          const normalized = {
            ...item,
            id: this.toNumber(item.id),
            parentId: this.normalizeGroupParentId(item) || parentId
          }
          result.push(normalized)
          if (Array.isArray(item.children) && item.children.length) {
            walk(item.children, normalized.id)
          }
        })
      }
      walk(groups)
      return result
    },
    resolveDeviceMeta(item) {
      const idKey = String(item.deviceId || '')
      const nameKey = String(item.name || '').trim()
      const meta = this.deviceMetaMap.byId.get(idKey) || this.deviceMetaMap.byName.get(nameKey) || {}
      const groupId = this.toNumber(item.groupId ?? meta.groupId)
      const lineMeta = this.resolveLineMeta(groupId)
      const itemLineId = this.toNumber(item.lineId)
      const itemLineName = String(item.lineName || '').trim()
      return {
        ...meta,
        groupId,
        group: this.resolveGroupLabel(groupId, item.groupName || meta.group),
        groupName: this.resolveGroupName(groupId, item.groupName || meta.group),
        line: itemLineName || lineMeta.name,
        lineId: itemLineId || lineMeta.id
      }
    },
    getGroupChain(groupId) {
      const chain = []
      let currentId = this.toNumber(groupId)
      const visited = new Set()
      while (currentId > 0 && !visited.has(currentId)) {
        const current = this.groupMetaMap.get(currentId)
        if (!current) {
          break
        }
        chain.push(current)
        visited.add(currentId)
        currentId = this.normalizeGroupParentId(current)
      }
      return chain.reverse()
    },
    getVisibleGroupChain(groupId) {
      const chain = this.getGroupChain(groupId)
      if (chain.length > 1 && !chain[0]?.parentId) {
        return chain.slice(1)
      }
      return chain
    },
    resolveGroupLabel(groupId, fallbackName = '') {
      const chain = this.getVisibleGroupChain(groupId)
      if (!chain.length) {
        return String(fallbackName || '').trim() || '未分组'
      }
      return chain.map(item => item.name).join(' / ')
    },
    resolveGroupName(groupId, fallbackName = '') {
      const chain = this.getVisibleGroupChain(groupId)
      if (!chain.length) {
        return String(fallbackName || '').trim() || '未分组'
      }
      return chain[chain.length - 1]?.name || String(fallbackName || '').trim() || '未分组'
    },
    resolveLineMeta(groupId) {
      const chain = this.getVisibleGroupChain(groupId)
      if (!chain.length) {
        return { id: 0, name: '未分线' }
      }
      return {
        id: this.toNumber(chain[0].id),
        name: chain[0].name || '未分线'
      }
    },
    decorateUsageRow(item, index) {
      const efficiency = this.toNumber(item.efficiency)
      return {
        ...item,
        rank: index + 1,
        runningTimeLabel: this.formatDuration(item.runningTime),
        idleTimeLabel: this.formatDuration(item.idleTime),
        efficiencyLabel: this.formatPercent(efficiency),
        efficiencyBarWidth: `${Math.max(6, Math.min(100, efficiency))}%`
      }
    },
    decorateUsageRows(rows, limit = 0) {
      const sorted = [...rows]
        .sort((a, b) => b.efficiency - a.efficiency || b.runningTime - a.runningTime)
      const source = limit > 0 ? sorted.slice(0, limit) : sorted
      return source.map((item, index) => this.decorateUsageRow(item, index))
    },
    async fetchPageData() {
      try {
        await Promise.all([
          this.fetchHomeStats(),
          this.fetchDeviceCatalog(),
          this.fetchGroupCatalog(),
          this.fetchProductionTrend()
        ])
        this.refreshCharts()
      } catch (error) {
        console.error('Failed to load home page data:', error)
      }
    },
    async fetchHomeStats() {
      const res = await getHomeStats()
      if (res.code !== 0 || !res.data) {
        return
      }

      this.homeStats = {
        totalDevices: this.toNumber(res.data.totalDevices),
        onlineDevices: this.toNumber(res.data.onlineDevices),
        workingDevices: this.toNumber(res.data.workingDevices),
        offlineDevices: this.toNumber(res.data.offlineDevices),
        alarmDevices: this.toNumber(res.data.alarmDevices),
        patternUsage: res.data.patternUsage || [],
        deviceUsage: res.data.deviceUsage || [],
        runningStatusByHour: res.data.runningStatusByHour || [],
        productionByDay: res.data.productionByDay || []
      }
    },
    async fetchDeviceCatalog() {
      try {
        const res = await getDeviceList({
          page: 1,
          pageSize: 2000
        })
        if (res.code !== 0 || !res.data) {
          return
        }
        this.deviceCatalog = res.data.list || []
      } catch (error) {
        this.deviceCatalog = []
      }
    },
    async fetchGroupCatalog() {
      try {
        const res = await getDeviceGroups()
        if (res.code !== 0 || !Array.isArray(res.data)) {
          return
        }
        this.groupCatalog = res.data || []
      } catch (error) {
        this.groupCatalog = []
      }
    },
    async fetchProductionTrend() {
      if (!this.productionStartDate || !this.productionEndDate) {
        return
      }

      const res = await getProcessOverview({
        startDate: this.productionStartDate,
        endDate: this.productionEndDate,
        page: 1,
        pageSize: 10
      })

      if (res.code !== 0 || !res.data) {
        return
      }

      this.productionTrend = res.data.productionTrend || []
    },
    startAutoRefresh() {
      this.stopAutoRefresh()
      this.refreshTimer = window.setInterval(() => {
        this.fetchPageData()
      }, 60 * 1000)
    },
    stopAutoRefresh() {
      if (!this.refreshTimer) {
        return
      }
      clearInterval(this.refreshTimer)
      this.refreshTimer = null
    },
    applyQuickRange(range, shouldFetch = true) {
      this.productionRange = range
      this.productionEndDate = this.getToday()
      this.productionStartDate = range === '30d'
        ? this.offsetDate(-29)
        : this.offsetDate(-6)

      if (shouldFetch) {
        this.fetchProductionTrend().then(() => {
          this.initProductionChart()
        })
      }
    },
    handleProductionDateChange() {
      if (!this.productionStartDate || !this.productionEndDate) {
        return
      }
      if (this.productionStartDate > this.productionEndDate) {
        this.$message.warning('开始时间不能晚于结束时间')
        return
      }
      this.productionRange = 'custom'
      this.fetchProductionTrend().then(() => {
        this.initProductionChart()
      })
    },
    refreshCharts() {
      this.$nextTick(() => {
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
    initRunningChart() {
      const chart = this.getOrCreateChart('running', 'runningChart')
      if (!chart) return

      const source = this.homeStats.runningStatusByHour || []
      chart.setOption({
        animationDuration: 500,
        tooltip: { trigger: 'axis' },
        grid: { left: 30, right: 20, top: 16, bottom: 18 },
        xAxis: {
          type: 'category',
          boundaryGap: false,
          data: source.map(item => item.hour),
          axisTick: { show: false },
          axisLine: { lineStyle: { color: '#cfd7e4' } },
          axisLabel: { color: '#6f7b8c', fontSize: 11 }
        },
        yAxis: {
          type: 'value',
          axisLabel: { color: '#6f7b8c', fontSize: 11 },
          splitLine: { lineStyle: { color: '#e9eef5', type: 'dashed' } }
        },
        series: [
          {
            name: '开机数',
            type: 'line',
            smooth: true,
            symbol: 'circle',
            symbolSize: 6,
            data: source.map(item => this.toNumber(item.online)),
            lineStyle: { width: 3, color: '#18c656' },
            itemStyle: { color: '#18c656' }
          },
          {
            name: '关机数',
            type: 'line',
            smooth: true,
            symbol: 'circle',
            symbolSize: 6,
            data: source.map(item => this.toNumber(item.offline)),
            lineStyle: { width: 3, color: '#ffa300' },
            itemStyle: { color: '#ffa300' }
          }
        ]
      }, true)
    },
    initProductionChart() {
      const chart = this.getOrCreateChart('production', 'productionChart')
      if (!chart) return

      const source = this.productionChartRows
      chart.setOption({
        animationDuration: 500,
        tooltip: { trigger: 'axis' },
        grid: { left: 36, right: 18, top: 16, bottom: 20 },
        xAxis: {
          type: 'category',
          data: source.map(item => item.label),
          axisTick: { show: false },
          axisLine: { lineStyle: { color: '#cfd7e4' } },
          axisLabel: { color: '#6f7b8c', fontSize: 11 }
        },
        yAxis: {
          type: 'value',
          axisLabel: { color: '#6f7b8c', fontSize: 11 },
          splitLine: { lineStyle: { color: '#e9eef5', type: 'dashed' } }
        },
        series: [{
          type: 'bar',
          barWidth: 34,
          data: source.map(item => item.value),
          label: {
            show: true,
            position: 'top',
            color: '#22a8ef',
            fontSize: 11,
            fontWeight: 700
          },
          itemStyle: {
            color: '#18a9f5',
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
  background: #f1f3f6;
  overflow: auto;
}

.home-layout {
  min-width: 0;
  padding: 10px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.home-top-grid {
  display: grid;
  grid-template-columns: 1.42fr 0.92fr 1.18fr;
  gap: 10px;
  align-items: stretch;
}

.home-stack {
  display: grid;
  grid-template-rows: auto 1fr;
  gap: 10px;
}

.home-bottom-grid {
  display: grid;
  grid-template-columns: 1fr 1.48fr;
  gap: 10px;
}

.panel-card {
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

.panel-header {
  display: flex;
  align-items: center;
  gap: 8px;
  min-height: 22px;
}

.panel-header--split {
  justify-content: space-between;
  gap: 12px;
}

.panel-title {
  position: relative;
  padding-left: 8px;
  color: #394554;
  font-size: 13px;
  font-weight: 700;
  line-height: 1;
}

.panel-title::before {
  content: '';
  position: absolute;
  left: 0;
  top: 0;
  width: 2px;
  height: 14px;
  border-radius: 1px;
  background: #1bb4ff;
}

.panel-card--status {
  min-height: 122px;
}

.status-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(112px, 1fr));
  gap: 12px;
  padding-top: 6px;
}

.status-item {
  text-align: center;
}

.status-item__icon {
  width: 42px;
  height: 42px;
  margin: 0 auto 10px;
  border-radius: 8px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  color: #fff;
  font-size: 20px;
}

.status-item__icon.is-sand {
  background: #c9ae8d;
}

.status-item__icon.is-green {
  background: #1dc64f;
}

.status-item__icon.is-orange {
  background: #ffb000;
}

.status-item__icon.is-blue {
  background: #29a1ff;
}

.status-item__label {
  margin-bottom: 8px;
  color: #6a7483;
  font-size: 12px;
  font-weight: 600;
}

.status-item__value {
  color: #22a8ef;
  font-size: 30px;
  font-weight: 700;
  line-height: 1;
}

.status-item__unit {
  margin-left: 4px;
  font-size: 14px;
}

.panel-card--usage {
  min-height: 294px;
}

.usage-tabs {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.usage-tabs__button {
  min-width: 54px;
  height: 26px;
  padding: 0 12px;
  border: 1px solid #d9e0e8;
  background: #fff;
  color: #5b6778;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
}

.usage-tabs__button.is-active {
  border-color: #1f9eff;
  background: #1f9eff;
  color: #fff;
}

.usage-table {
  width: 100%;
}

.usage-table ::v-deep th {
  background: #f5f7fb;
  color: #657285;
  font-weight: 700;
}

.usage-table ::v-deep .cell {
  font-size: 12px;
}

.panel-card--pattern,
.panel-card--ranking {
  min-height: 426px;
}

.pattern-list {
  padding-top: 4px;
}

.pattern-list__head,
.pattern-row {
  display: grid;
  grid-template-columns: 54px 1fr 62px;
  align-items: center;
}

.pattern-list__head {
  height: 34px;
  padding: 0 10px;
  background: #f4f6f9;
  color: #667285;
  font-size: 12px;
  font-weight: 700;
}

.pattern-row {
  min-height: 34px;
  padding: 0 10px;
  border-bottom: 1px dashed #e6ebf2;
  color: #687486;
  font-size: 12px;
}

.pattern-row.is-highlight {
  background: linear-gradient(90deg, rgba(164, 255, 171, 0.85), rgba(123, 242, 141, 0.7));
}

.pattern-row__rank,
.pattern-row__value {
  text-align: center;
}

.pattern-row__name {
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.pattern-empty {
  min-height: 340px;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #8a95a6;
  font-size: 13px;
}

.ranking-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 16px;
}

.ranking-column__title {
  margin-bottom: 12px;
  color: #627085;
  font-size: 12px;
  font-weight: 700;
  text-align: center;
  line-height: 1.5;
}

.ranking-column__title span {
  color: #66a1de;
}

.ranking-list {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.ranking-item__header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  margin-bottom: 6px;
  color: #697587;
  font-size: 12px;
}

.ranking-item__name {
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.ranking-item__value {
  flex-shrink: 0;
  color: #7a8697;
  font-size: 12px;
}

.ranking-bar {
  height: 8px;
  border-radius: 999px;
  background: #eceff4;
  overflow: hidden;
}

.ranking-bar__fill {
  display: block;
  height: 100%;
  border-radius: inherit;
}

.ranking-bar__fill.is-green {
  background: linear-gradient(90deg, #6ee06d, #18c656);
}

.ranking-bar__fill.is-red {
  background: linear-gradient(90deg, #ff7272, #ff2c38);
}

.panel-card--chart {
  min-height: 326px;
}

.chart-legend {
  display: inline-flex;
  align-items: center;
  gap: 18px;
}

.chart-legend__item {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  color: #586476;
  font-size: 12px;
  font-weight: 600;
}

.chart-legend__dot {
  width: 10px;
  height: 10px;
  border-radius: 50%;
}

.chart-legend__dot.is-green {
  background: #18c656;
}

.chart-legend__dot.is-orange {
  background: #ffa300;
}

.production-toolbar {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  flex: 0 0 auto;
  flex-wrap: nowrap;
  white-space: nowrap;
}

.production-toolbar__tabs {
  display: inline-flex;
  align-items: center;
  flex: 0 0 auto;
  gap: 6px;
}

.production-toolbar__tab {
  min-width: 58px;
  height: 26px;
  padding: 0 12px;
  border: 1px solid #d9e0e8;
  background: #fff;
  color: #5b6778;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
}

.production-toolbar__tab.is-active {
  border-color: #1f9eff;
  background: #1f9eff;
  color: #fff;
}

.production-toolbar__sep {
  flex: 0 0 auto;
  color: #7b8798;
  font-size: 12px;
}

.production-toolbar ::v-deep .el-date-editor.el-input {
  flex: 0 0 112px;
  width: 112px;
}

.production-toolbar ::v-deep .el-input__inner {
  width: 112px;
  height: 26px;
  line-height: 26px;
  padding: 0 10px;
  font-size: 12px;
}

.chart-surface {
  width: 100%;
}

.chart-surface--running {
  height: 252px;
}

.chart-surface--production {
  height: 252px;
}

@media (max-width: 1440px) {
  .home-top-grid {
    grid-template-columns: 1.32fr 0.9fr 1.08fr;
  }
}

@media (max-width: 0px) {
  .home-top-grid {
    grid-template-columns: minmax(0, 1fr) minmax(280px, 0.82fr);
  }

  .home-stack {
    grid-column: 1 / -1;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    grid-template-rows: auto;
  }

  .home-bottom-grid {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 0px) {
  .home-layout {
    padding: 6px;
    gap: 8px;
  }

  .home-top-grid,
  .home-stack,
  .home-bottom-grid,
  .ranking-grid {
    grid-template-columns: 1fr;
    gap: 8px;
  }

  .home-stack {
    grid-column: auto;
  }

  .panel-card--usage,
  .panel-card--pattern,
  .panel-card--ranking,
  .panel-card--chart {
    min-height: auto;
  }

  .status-grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }

  .status-item__value {
    font-size: 24px;
  }

  .pattern-empty {
    min-height: 180px;
  }

  .chart-surface--running,
  .chart-surface--production {
    height: 220px;
  }
}

@media (max-width: 0px) {
  .panel-header--split {
    align-items: flex-start;
  }

  .usage-tabs,
  .production-toolbar__tabs {
    width: 100%;
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }

  .usage-tabs__button,
  .production-toolbar__tab {
    width: 100%;
  }

  .production-toolbar ::v-deep .el-input,
  .production-toolbar ::v-deep .el-input__inner {
    width: 100%;
  }

  .chart-legend {
    flex-wrap: wrap;
    gap: 8px;
  }
}
</style>
