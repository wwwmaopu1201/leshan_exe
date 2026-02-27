<template>
  <div class="home-page">
    <!-- 设备状态统计卡片 -->
    <el-row :gutter="20" class="stat-cards">
      <el-col :span="6">
        <div class="stat-card">
          <div class="stat-icon">
            <i class="el-icon-monitor"></i>
          </div>
          <div class="stat-content">
            <div class="stat-value">{{ stats.totalDevices }}</div>
            <div class="stat-label">{{ $t('home.totalDevices') }}</div>
          </div>
        </div>
      </el-col>
      <el-col :span="6">
        <div class="stat-card success">
          <div class="stat-icon">
            <i class="el-icon-circle-check"></i>
          </div>
          <div class="stat-content">
            <div class="stat-value">{{ stats.onlineDevices }}</div>
            <div class="stat-label">{{ $t('home.onlineDevices') }}</div>
          </div>
        </div>
      </el-col>
      <el-col :span="6">
        <div class="stat-card warning">
          <div class="stat-icon">
            <i class="el-icon-remove-outline"></i>
          </div>
          <div class="stat-content">
            <div class="stat-value">{{ stats.offlineDevices }}</div>
            <div class="stat-label">{{ $t('home.offlineDevices') }}</div>
          </div>
        </div>
      </el-col>
      <el-col :span="6">
        <div class="stat-card danger">
          <div class="stat-icon">
            <i class="el-icon-warning"></i>
          </div>
          <div class="stat-content">
            <div class="stat-value">{{ stats.alarmDevices }}</div>
            <div class="stat-label">{{ $t('home.alarmDevices') }}</div>
          </div>
        </div>
      </el-col>
    </el-row>

    <!-- 图表区域 -->
    <el-row :gutter="20" class="chart-row">
      <el-col :span="16">
        <div class="chart-card">
          <div class="chart-title">{{ $t('home.weeklyEfficiency') }}</div>
          <div ref="efficiencyChart" class="chart-container"></div>
        </div>
      </el-col>
      <el-col :span="8">
        <div class="chart-card">
          <div class="chart-title">{{ $t('home.patternUsage') }}</div>
          <div ref="patternChart" class="chart-container"></div>
        </div>
      </el-col>
    </el-row>

    <el-row :gutter="20" class="chart-row">
      <el-col :span="8">
        <div class="chart-card">
          <div class="chart-title">{{ $t('home.modelRatio') }}</div>
          <div ref="modelChart" class="chart-container"></div>
        </div>
      </el-col>
      <el-col :span="8">
        <div class="chart-card">
          <div class="chart-title">{{ $t('home.topProduction') }}</div>
          <div ref="topChart" class="chart-container"></div>
        </div>
      </el-col>
      <el-col :span="8">
        <div class="chart-card">
          <div class="chart-title">{{ $t('home.productionStats') }}</div>
          <div ref="productionChart" class="chart-container"></div>
        </div>
      </el-col>
    </el-row>
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
        offlineDevices: 0,
        alarmDevices: 0,
        weeklyEfficiency: [],
        patternUsage: [],
        modelRatio: [],
        topProduction: [],
        productionByHour: []
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
      try {
        const res = await getHomeStats()
        if (res.code === 0) {
          this.stats = {
            totalDevices: res.data.totalDevices || 0,
            onlineDevices: res.data.onlineDevices || 0,
            offlineDevices: res.data.offlineDevices || 0,
            alarmDevices: res.data.alarmDevices || 0,
            weeklyEfficiency: res.data.weeklyEfficiency || [],
            patternUsage: res.data.patternUsage || [],
            modelRatio: res.data.modelRatio || [],
            topProduction: res.data.topProduction || [],
            productionByHour: res.data.productionByHour || []
          }
          this.$nextTick(() => {
            this.initCharts()
          })
        }
      } catch (error) {
        console.error('Failed to fetch home stats:', error)
      }
    },
    initCharts() {
      this.initEfficiencyChart()
      this.initPatternChart()
      this.initModelChart()
      this.initTopChart()
      this.initProductionChart()
    },
    initEfficiencyChart() {
      if (!this.$refs.efficiencyChart) return
      const chart = echarts.init(this.$refs.efficiencyChart)
      this.charts.efficiency = chart
      chart.setOption({
        tooltip: {
          trigger: 'axis',
          formatter: '{b}: {c}%'
        },
        grid: {
          left: '3%',
          right: '4%',
          bottom: '3%',
          containLabel: true
        },
        xAxis: {
          type: 'category',
          data: this.stats.weeklyEfficiency.map(d => d.date),
          axisLine: { lineStyle: { color: '#ddd' } },
          axisLabel: { color: '#666' }
        },
        yAxis: {
          type: 'value',
          max: 100,
          axisLabel: { formatter: '{value}%', color: '#666' },
          splitLine: { lineStyle: { color: '#eee' } }
        },
        series: [{
          data: this.stats.weeklyEfficiency.map(d => d.value),
          type: 'line',
          smooth: true,
          symbol: 'circle',
          symbolSize: 8,
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
    initPatternChart() {
      if (!this.$refs.patternChart) return
      const chart = echarts.init(this.$refs.patternChart)
      this.charts.pattern = chart
      chart.setOption({
        tooltip: {
          trigger: 'item',
          formatter: '{b}: {c} ({d}%)'
        },
        legend: {
          orient: 'vertical',
          right: 10,
          top: 'center'
        },
        series: [{
          type: 'pie',
          radius: ['40%', '70%'],
          center: ['40%', '50%'],
          avoidLabelOverlap: false,
          label: { show: false },
          emphasis: {
            label: { show: true, fontSize: 14, fontWeight: 'bold' }
          },
          labelLine: { show: false },
          data: this.stats.patternUsage.map(d => ({
            name: d.name,
            value: d.value
          })),
          color: ['#409EFF', '#67C23A', '#E6A23C', '#909399']
        }]
      })
    },
    initModelChart() {
      if (!this.$refs.modelChart) return
      const chart = echarts.init(this.$refs.modelChart)
      this.charts.model = chart
      chart.setOption({
        tooltip: {
          trigger: 'item',
          formatter: '{b}: {c} ({d}%)'
        },
        legend: {
          orient: 'vertical',
          right: 10,
          top: 'center'
        },
        series: [{
          type: 'pie',
          radius: ['50%', '70%'],
          center: ['40%', '50%'],
          label: { show: false },
          data: this.stats.modelRatio.map(d => ({
            name: d.name,
            value: d.value
          })),
          color: ['#409EFF', '#67C23A', '#E6A23C']
        }]
      })
    },
    initTopChart() {
      if (!this.$refs.topChart) return
      const chart = echarts.init(this.$refs.topChart)
      this.charts.top = chart
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
          type: 'value',
          axisLabel: { color: '#666' },
          splitLine: { lineStyle: { color: '#eee' } }
        },
        yAxis: {
          type: 'category',
          data: this.stats.topProduction.map(d => d.name).reverse(),
          axisLine: { lineStyle: { color: '#ddd' } },
          axisLabel: { color: '#666' }
        },
        series: [{
          type: 'bar',
          data: this.stats.topProduction.map(d => d.value).reverse(),
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
    initProductionChart() {
      if (!this.$refs.productionChart) return
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
          data: this.stats.productionByHour.map(d => d.hour),
          axisLine: { lineStyle: { color: '#ddd' } },
          axisLabel: { color: '#666', rotate: 45 }
        },
        yAxis: {
          type: 'value',
          axisLabel: { color: '#666' },
          splitLine: { lineStyle: { color: '#eee' } }
        },
        series: [{
          type: 'bar',
          data: this.stats.productionByHour.map(d => d.value),
          barWidth: 20,
          itemStyle: {
            color: '#409EFF',
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
.home-page {
  padding: 20px;
  background-color: #f5f7fa;
  min-height: 100%;
}

.stat-cards {
  margin-bottom: 20px;
}

.stat-card {
  display: flex;
  align-items: center;
  padding: 20px;
  background: linear-gradient(135deg, #409EFF 0%, #2d8cf0 100%);
  border-radius: 8px;
  color: #fff;
  box-shadow: 0 4px 12px rgba(64, 158, 255, 0.3);

  &.success {
    background: linear-gradient(135deg, #67C23A 0%, #5daf34 100%);
    box-shadow: 0 4px 12px rgba(103, 194, 58, 0.3);
  }

  &.warning {
    background: linear-gradient(135deg, #E6A23C 0%, #d69330 100%);
    box-shadow: 0 4px 12px rgba(230, 162, 60, 0.3);
  }

  &.danger {
    background: linear-gradient(135deg, #F56C6C 0%, #e85656 100%);
    box-shadow: 0 4px 12px rgba(245, 108, 108, 0.3);
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

    i {
      font-size: 28px;
    }
  }

  .stat-content {
    .stat-value {
      font-size: 32px;
      font-weight: bold;
      line-height: 1.2;
    }

    .stat-label {
      font-size: 14px;
      opacity: 0.9;
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
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.1);

  .chart-title {
    font-size: 16px;
    font-weight: 600;
    color: #303133;
    margin-bottom: 15px;
    padding-bottom: 10px;
    border-bottom: 1px solid #eee;
  }

  .chart-container {
    width: 100%;
    height: 280px;
  }
}
</style>
