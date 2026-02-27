<template>
  <div class="dashboard-page">
    <div class="dashboard-layout">
      <!-- 左侧设备树 -->
      <div class="device-tree-panel">
        <div class="panel-header">
          <span>设备列表</span>
          <el-input
            v-model="treeFilter"
            size="small"
            placeholder="搜索设备"
            prefix-icon="el-icon-search"
            clearable
          />
        </div>
        <div class="tree-container">
          <el-tree
            ref="deviceTree"
            :data="deviceTree"
            :props="treeProps"
            :filter-node-method="filterNode"
            node-key="id"
            highlight-current
            default-expand-all
            @node-click="handleNodeClick"
          >
            <span class="tree-node" slot-scope="{ node, data }">
              <i :class="getNodeIcon(data)"></i>
              <span>{{ node.label }}</span>
              <span v-if="data.type === 'device'" :class="['status-dot', data.status]"></span>
            </span>
          </el-tree>
        </div>
      </div>

      <!-- 右侧数据看板 -->
      <div class="dashboard-content">
        <template v-if="selectedDevice">
          <!-- 设备信息头部 -->
          <div class="device-header">
            <div class="device-info">
              <h2>{{ selectedDevice.label }}</h2>
              <span :class="['status-tag', selectedDevice.status]">
                {{ getStatusText(selectedDevice.status) }}
              </span>
            </div>
            <div class="device-meta">
              <span>型号: {{ selectedDevice.model }}</span>
              <span>IP: 192.168.1.101</span>
            </div>
          </div>

          <!-- 统计卡片 -->
          <el-row :gutter="20" class="stat-row">
            <el-col :span="6">
              <div class="stat-card">
                <div class="stat-icon blue">
                  <i class="el-icon-s-goods"></i>
                </div>
                <div class="stat-info">
                  <div class="stat-value">{{ dashboardData.totalPieces }}</div>
                  <div class="stat-label">{{ $t('dashboard.totalPieces') }}</div>
                </div>
              </div>
            </el-col>
            <el-col :span="6">
              <div class="stat-card">
                <div class="stat-icon green">
                  <i class="el-icon-sort"></i>
                </div>
                <div class="stat-info">
                  <div class="stat-value">{{ dashboardData.threadLength }}<small>m</small></div>
                  <div class="stat-label">{{ $t('dashboard.threadLength') }}</div>
                </div>
              </div>
            </el-col>
            <el-col :span="6">
              <div class="stat-card">
                <div class="stat-icon orange">
                  <i class="el-icon-time"></i>
                </div>
                <div class="stat-info">
                  <div class="stat-value">{{ dashboardData.runningTime }}<small>h</small></div>
                  <div class="stat-label">{{ $t('dashboard.runningTime') }}</div>
                </div>
              </div>
            </el-col>
            <el-col :span="6">
              <div class="stat-card">
                <div class="stat-icon purple">
                  <i class="el-icon-data-line"></i>
                </div>
                <div class="stat-info">
                  <div class="stat-value">{{ dashboardData.utilizationRate }}<small>%</small></div>
                  <div class="stat-label">{{ $t('dashboard.utilizationRate') }}</div>
                </div>
              </div>
            </el-col>
          </el-row>

          <!-- 图表区域 -->
          <el-row :gutter="20" class="chart-row">
            <el-col :span="8">
              <div class="chart-card">
                <div class="chart-title">{{ $t('dashboard.spindleSpeed') }}</div>
                <div ref="gaugeChart" class="chart-container gauge"></div>
              </div>
            </el-col>
            <el-col :span="16">
              <div class="chart-card">
                <div class="chart-title">{{ $t('dashboard.productionStats') }}</div>
                <div ref="productionChart" class="chart-container"></div>
              </div>
            </el-col>
          </el-row>
        </template>

        <template v-else>
          <div class="empty-state">
            <i class="el-icon-monitor"></i>
            <p>{{ $t('dashboard.selectDevice') }}</p>
          </div>
        </template>
      </div>
    </div>
  </div>
</template>

<script>
import * as echarts from 'echarts'
import { getDeviceTree } from '@/api/device'
import { getDashboardData } from '@/api/statistics'

export default {
  name: 'Dashboard',
  data() {
    return {
      deviceTree: [],
      treeProps: {
        children: 'children',
        label: 'label'
      },
      treeFilter: '',
      selectedDevice: null,
      dashboardData: {
        totalPieces: 0,
        threadLength: 0,
        spindleSpeed: 0,
        runningTime: 0,
        processingTime: 0,
        utilizationRate: 0,
        hourlyProduction: []
      },
      charts: {}
    }
  },
  watch: {
    treeFilter(val) {
      this.$refs.deviceTree.filter(val)
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
          this.deviceTree = res.data || []
        }
      } catch (error) {
        console.error('Failed to fetch device tree:', error)
      }
    },
    async loadDashboardData(deviceId) {
      try {
        const res = await getDashboardData(deviceId)
        if (res.code === 0) {
          this.dashboardData = {
            totalPieces: res.data.totalPieces || 0,
            threadLength: res.data.threadLength || 0,
            spindleSpeed: res.data.spindleSpeed || 0,
            runningTime: res.data.runningTime || 0,
            processingTime: res.data.processingTime || 0,
            utilizationRate: res.data.utilizationRate || 0,
            hourlyProduction: res.data.hourlyProduction || []
          }
        }
      } catch (error) {
        console.error('Failed to load dashboard data:', error)
        this.dashboardData = {
          totalPieces: 0,
          threadLength: 0,
          spindleSpeed: 0,
          runningTime: 0,
          processingTime: 0,
          utilizationRate: 0,
          hourlyProduction: []
        }
      }

      this.$nextTick(() => {
        this.initCharts()
      })
    },
    filterNode(value, data) {
      if (!value) return true
      return data.label.toLowerCase().includes(value.toLowerCase())
    },
    handleNodeClick(data) {
      if (data.type === 'device') {
        this.selectedDevice = data
        this.loadDashboardData(data.id)
      }
    },
    getNodeIcon(data) {
      if (data.type === 'device') {
        return 'el-icon-monitor'
      }
      return data.children && data.children.length ? 'el-icon-folder-opened' : 'el-icon-folder'
    },
    getStatusText(status) {
      const statusMap = {
        online: '在线',
        offline: '离线',
        working: '工作中',
        idle: '空闲',
        alarm: '报警'
      }
      return statusMap[status] || status
    },
    initCharts() {
      this.initGaugeChart()
      this.initProductionChart()
    },
    initGaugeChart() {
      if (!this.$refs.gaugeChart) return
      if (this.charts.gauge) {
        this.charts.gauge.dispose()
      }
      const chart = echarts.init(this.$refs.gaugeChart)
      this.charts.gauge = chart
      chart.setOption({
        series: [{
          type: 'gauge',
          startAngle: 200,
          endAngle: -20,
          min: 0,
          max: 5000,
          splitNumber: 5,
          itemStyle: {
            color: '#409EFF'
          },
          progress: {
            show: true,
            width: 20
          },
          pointer: {
            show: false
          },
          axisLine: {
            lineStyle: {
              width: 20,
              color: [[1, '#e6e6e6']]
            }
          },
          axisTick: {
            show: false
          },
          splitLine: {
            show: false
          },
          axisLabel: {
            distance: 30,
            color: '#999',
            fontSize: 12
          },
          anchor: {
            show: false
          },
          title: {
            show: false
          },
          detail: {
            valueAnimation: true,
            width: '60%',
            lineHeight: 40,
            borderRadius: 8,
            offsetCenter: [0, '10%'],
            fontSize: 28,
            fontWeight: 'bold',
            formatter: '{value}',
            color: '#303133'
          },
          data: [{
            value: this.dashboardData.spindleSpeed,
            name: 'RPM'
          }]
        }]
      })
    },
    initProductionChart() {
      if (!this.$refs.productionChart) return
      if (this.charts.production) {
        this.charts.production.dispose()
      }
      const chart = echarts.init(this.$refs.productionChart)
      this.charts.production = chart
      chart.setOption({
        tooltip: {
          trigger: 'axis',
          axisPointer: { type: 'shadow' }
        },
        grid: {
          left: '3%',
          right: '4%',
          bottom: '3%',
          containLabel: true
        },
        xAxis: {
          type: 'category',
          data: this.dashboardData.hourlyProduction.map(d => d.hour),
          axisLine: { lineStyle: { color: '#ddd' } },
          axisLabel: { color: '#666' }
        },
        yAxis: {
          type: 'value',
          axisLabel: { color: '#666' },
          splitLine: { lineStyle: { color: '#eee' } }
        },
        series: [{
          type: 'bar',
          data: this.dashboardData.hourlyProduction.map(d => d.value),
          barWidth: 30,
          itemStyle: {
            color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
              { offset: 0, color: '#409EFF' },
              { offset: 1, color: '#67C23A' }
            ]),
            borderRadius: [4, 4, 0, 0]
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
.dashboard-page {
  height: 100%;
  background-color: #f5f7fa;
}

.dashboard-layout {
  display: flex;
  height: 100%;
}

.device-tree-panel {
  width: 280px;
  background: #fff;
  border-right: 1px solid #e6e6e6;
  display: flex;
  flex-direction: column;

  .panel-header {
    padding: 15px;
    border-bottom: 1px solid #e6e6e6;

    > span {
      display: block;
      font-size: 16px;
      font-weight: 600;
      margin-bottom: 10px;
      color: #303133;
    }
  }

  .tree-container {
    flex: 1;
    overflow: auto;
    padding: 10px;
  }
}

.tree-node {
  display: flex;
  align-items: center;
  font-size: 14px;

  i {
    margin-right: 5px;
    color: #909399;
  }

  .status-dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    margin-left: 8px;

    &.online, &.working { background: #67C23A; }
    &.offline { background: #909399; }
    &.idle { background: #E6A23C; }
    &.alarm { background: #F56C6C; }
  }
}

.dashboard-content {
  flex: 1;
  padding: 20px;
  overflow: auto;
}

.device-header {
  background: #fff;
  padding: 20px;
  border-radius: 8px;
  margin-bottom: 20px;
  display: flex;
  justify-content: space-between;
  align-items: center;

  .device-info {
    display: flex;
    align-items: center;

    h2 {
      font-size: 20px;
      margin: 0;
      margin-right: 15px;
      color: #303133;
    }

    .status-tag {
      padding: 4px 12px;
      border-radius: 4px;
      font-size: 12px;

      &.online, &.working {
        background: rgba(103, 194, 58, 0.1);
        color: #67C23A;
      }
      &.offline {
        background: rgba(144, 147, 153, 0.1);
        color: #909399;
      }
      &.idle {
        background: rgba(230, 162, 60, 0.1);
        color: #E6A23C;
      }
      &.alarm {
        background: rgba(245, 108, 108, 0.1);
        color: #F56C6C;
      }
    }
  }

  .device-meta {
    color: #909399;
    font-size: 14px;

    span {
      margin-left: 20px;
    }
  }
}

.stat-row {
  margin-bottom: 20px;
}

.stat-card {
  background: #fff;
  border-radius: 8px;
  padding: 20px;
  display: flex;
  align-items: center;

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

      small {
        font-size: 14px;
        font-weight: normal;
        color: #909399;
        margin-left: 2px;
      }
    }

    .stat-label {
      font-size: 14px;
      color: #909399;
      margin-top: 5px;
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
    color: #303133;
    margin-bottom: 15px;
  }

  .chart-container {
    width: 100%;
    height: 300px;

    &.gauge {
      height: 250px;
    }
  }
}

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 100%;
  color: #909399;

  i {
    font-size: 80px;
    margin-bottom: 20px;
  }

  p {
    font-size: 16px;
  }
}
</style>
