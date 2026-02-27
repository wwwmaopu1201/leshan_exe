<template>
  <div class="page-container">
    <div class="monitor-layout">
      <!-- 设备选择 -->
      <div class="device-selector">
        <el-select v-model="selectedDeviceId" placeholder="选择要监控的设备" @change="handleDeviceChange">
          <el-option
            v-for="device in deviceList"
            :key="device.id"
            :label="device.name"
            :value="device.id"
          >
            <span>{{ device.name }}</span>
            <span :class="['status-dot', device.status]"></span>
          </el-option>
        </el-select>
        <el-button type="primary" icon="el-icon-refresh" @click="refreshData">刷新数据</el-button>
      </div>

      <template v-if="selectedDevice">
        <!-- 实时状态 -->
        <el-row :gutter="20" class="status-row">
          <el-col :span="6">
            <div class="status-card">
              <div class="status-label">运行状态</div>
              <div :class="['status-value', selectedDevice.status]">
                {{ getStatusText(selectedDevice.status) }}
              </div>
            </div>
          </el-col>
          <el-col :span="6">
            <div class="status-card">
              <div class="status-label">主轴转速</div>
              <div class="status-value">{{ realtimeData.spindleSpeed }} <small>RPM</small></div>
            </div>
          </el-col>
          <el-col :span="6">
            <div class="status-card">
              <div class="status-label">当前针数</div>
              <div class="status-value">{{ realtimeData.currentStitches }}</div>
            </div>
          </el-col>
          <el-col :span="6">
            <div class="status-card">
              <div class="status-label">当前花型</div>
              <div class="status-value pattern">{{ realtimeData.currentPattern }}</div>
            </div>
          </el-col>
        </el-row>

        <!-- 监控画面和控制 -->
        <el-row :gutter="20">
          <el-col :span="16">
            <div class="card">
              <div class="card-header">设备监控画面</div>
              <div class="monitor-screen">
                <div class="screen-placeholder">
                  <i class="el-icon-video-camera"></i>
                  <p>实时监控画面</p>
                  <p class="hint">连接设备后显示</p>
                </div>
              </div>
            </div>
          </el-col>
          <el-col :span="8">
            <div class="card">
              <div class="card-header">设备控制</div>
              <div class="control-panel">
                <el-button type="success" icon="el-icon-video-play" :disabled="selectedDevice.status === 'working'" block>
                  启动
                </el-button>
                <el-button type="warning" icon="el-icon-video-pause" :disabled="selectedDevice.status !== 'working'" block>
                  暂停
                </el-button>
                <el-button type="danger" icon="el-icon-switch-button" block>
                  停止
                </el-button>
                <el-divider />
                <el-button icon="el-icon-position" block>
                  回原点
                </el-button>
                <el-button icon="el-icon-scissors" block>
                  剪线
                </el-button>
                <el-button icon="el-icon-refresh-left" block>
                  复位
                </el-button>
              </div>
            </div>

            <div class="card mt-20">
              <div class="card-header">报警信息</div>
              <div class="alarm-list">
                <div v-if="alarms.length === 0" class="no-alarm">
                  <i class="el-icon-circle-check"></i>
                  <span>无报警</span>
                </div>
                <div v-for="alarm in alarms" :key="alarm.id" class="alarm-item">
                  <i class="el-icon-warning"></i>
                  <div class="alarm-content">
                    <div class="alarm-msg">{{ alarm.message }}</div>
                    <div class="alarm-time">{{ alarm.time }}</div>
                  </div>
                </div>
              </div>
            </div>
          </el-col>
        </el-row>

        <!-- 实时数据图表 -->
        <div class="card mt-20">
          <div class="card-header">实时数据趋势</div>
          <div ref="realtimeChart" class="chart-container"></div>
        </div>
      </template>

      <template v-else>
        <div class="empty-state">
          <i class="el-icon-monitor"></i>
          <p>请选择要监控的设备</p>
        </div>
      </template>
    </div>
  </div>
</template>

<script>
import * as echarts from 'echarts'

export default {
  name: 'RemoteMonitor',
  data() {
    return {
      selectedDeviceId: null,
      deviceList: [
        { id: 1, name: '缝纫机A-001', status: 'working' },
        { id: 2, name: '缝纫机A-002', status: 'online' },
        { id: 3, name: '缝纫机B-001', status: 'idle' },
        { id: 4, name: '缝纫机B-002', status: 'alarm' }
      ],
      selectedDevice: null,
      realtimeData: {
        spindleSpeed: 0,
        currentStitches: 0,
        currentPattern: '-'
      },
      alarms: [],
      chart: null,
      chartData: {
        time: [],
        speed: [],
        production: []
      }
    }
  },
  mounted() {
    const deviceId = this.$route.query.id
    if (deviceId) {
      this.selectedDeviceId = parseInt(deviceId)
      this.handleDeviceChange(this.selectedDeviceId)
    }
  },
  beforeDestroy() {
    if (this.chart) {
      this.chart.dispose()
    }
  },
  methods: {
    handleDeviceChange(deviceId) {
      this.selectedDevice = this.deviceList.find(d => d.id === deviceId)
      if (this.selectedDevice) {
        this.loadDeviceData()
      }
    },
    loadDeviceData() {
      // Mock realtime data
      this.realtimeData = {
        spindleSpeed: 3500,
        currentStitches: 12580,
        currentPattern: 'Pattern-001.dst'
      }

      // Mock alarms for alarm status device
      if (this.selectedDevice.status === 'alarm') {
        this.alarms = [
          { id: 1, message: '断线报警', time: '10:30:25' },
          { id: 2, message: '张力异常', time: '10:28:15' }
        ]
      } else {
        this.alarms = []
      }

      // Init chart
      this.$nextTick(() => {
        this.initChart()
      })
    },
    refreshData() {
      if (this.selectedDevice) {
        this.loadDeviceData()
        this.$message.success('数据已刷新')
      }
    },
    getStatusText(status) {
      const map = {
        online: '在线',
        offline: '离线',
        working: '运行中',
        idle: '空闲',
        alarm: '报警'
      }
      return map[status] || status
    },
    initChart() {
      if (this.chart) {
        this.chart.dispose()
      }

      const chartDom = this.$refs.realtimeChart
      if (!chartDom) return

      this.chart = echarts.init(chartDom)

      // Generate mock time series data
      const now = new Date()
      const timeData = []
      const speedData = []
      const productionData = []

      for (let i = 30; i >= 0; i--) {
        const time = new Date(now - i * 60000)
        timeData.push(time.toLocaleTimeString())
        speedData.push(3000 + Math.random() * 1000)
        productionData.push(100 + Math.floor(Math.random() * 50))
      }

      this.chart.setOption({
        tooltip: {
          trigger: 'axis'
        },
        legend: {
          data: ['主轴转速', '产量']
        },
        grid: {
          left: '3%',
          right: '4%',
          bottom: '3%',
          containLabel: true
        },
        xAxis: {
          type: 'category',
          boundaryGap: false,
          data: timeData
        },
        yAxis: [
          {
            type: 'value',
            name: '转速(RPM)',
            position: 'left'
          },
          {
            type: 'value',
            name: '产量(件)',
            position: 'right'
          }
        ],
        series: [
          {
            name: '主轴转速',
            type: 'line',
            smooth: true,
            data: speedData,
            lineStyle: { color: '#409EFF' },
            itemStyle: { color: '#409EFF' }
          },
          {
            name: '产量',
            type: 'line',
            smooth: true,
            yAxisIndex: 1,
            data: productionData,
            lineStyle: { color: '#67C23A' },
            itemStyle: { color: '#67C23A' }
          }
        ]
      })
    }
  }
}
</script>

<style lang="scss" scoped>
.monitor-layout {
  min-height: calc(100vh - 120px);
}

.device-selector {
  display: flex;
  gap: 15px;
  margin-bottom: 20px;

  .el-select {
    width: 300px;
  }
}

.status-dot {
  display: inline-block;
  width: 8px;
  height: 8px;
  border-radius: 50%;
  margin-left: 8px;

  &.online, &.working { background: #67C23A; }
  &.offline { background: #909399; }
  &.idle { background: #E6A23C; }
  &.alarm { background: #F56C6C; }
}

.status-row {
  margin-bottom: 20px;
}

.status-card {
  background: #fff;
  border-radius: 8px;
  padding: 20px;
  text-align: center;

  .status-label {
    color: #909399;
    font-size: 14px;
    margin-bottom: 10px;
  }

  .status-value {
    font-size: 28px;
    font-weight: bold;
    color: #303133;

    small {
      font-size: 14px;
      font-weight: normal;
      color: #909399;
    }

    &.online, &.working { color: #67C23A; }
    &.offline { color: #909399; }
    &.idle { color: #E6A23C; }
    &.alarm { color: #F56C6C; }

    &.pattern {
      font-size: 16px;
    }
  }
}

.monitor-screen {
  background: #1a1a2e;
  border-radius: 8px;
  height: 400px;
  display: flex;
  align-items: center;
  justify-content: center;

  .screen-placeholder {
    text-align: center;
    color: #666;

    i {
      font-size: 80px;
      margin-bottom: 20px;
    }

    p {
      font-size: 18px;
      margin: 5px 0;
    }

    .hint {
      font-size: 14px;
      color: #999;
    }
  }
}

.control-panel {
  .el-button {
    width: 100%;
    margin: 10px 0;
  }
}

.alarm-list {
  max-height: 200px;
  overflow-y: auto;

  .no-alarm {
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 30px;
    color: #67C23A;

    i {
      font-size: 24px;
      margin-right: 10px;
    }
  }

  .alarm-item {
    display: flex;
    align-items: flex-start;
    padding: 10px;
    border-bottom: 1px solid #eee;

    i {
      color: #F56C6C;
      font-size: 18px;
      margin-right: 10px;
      margin-top: 2px;
    }

    .alarm-content {
      flex: 1;

      .alarm-msg {
        color: #303133;
        margin-bottom: 4px;
      }

      .alarm-time {
        color: #909399;
        font-size: 12px;
      }
    }
  }
}

.chart-container {
  height: 300px;
}

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 400px;
  color: #909399;

  i {
    font-size: 80px;
    margin-bottom: 20px;
  }
}
</style>
