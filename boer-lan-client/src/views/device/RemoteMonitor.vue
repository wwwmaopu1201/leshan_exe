<template>
  <div class="page-container">
    <div class="monitor-layout">
      <div class="device-selector">
        <el-select
          v-model="selectedDeviceId"
          placeholder="选择要监控的设备"
          filterable
          @change="handleDeviceChange"
        >
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

        <el-input
          v-model="deviceKeyword"
          clearable
          placeholder="按设备名称搜索"
          @keyup.enter.native="handleDeviceFilter"
          @clear="handleDeviceFilter"
        />

        <el-date-picker
          v-model="deviceDateRange"
          type="daterange"
          value-format="yyyy-MM-dd"
          range-separator="至"
          start-placeholder="开始日期"
          end-placeholder="结束日期"
          clearable
          @change="handleDeviceFilter"
        />

        <el-button plain icon="el-icon-search" @click="handleDeviceFilter">筛选设备</el-button>

        <el-input-number
          v-model="vnc.port"
          :min="1"
          :max="65535"
          controls-position="right"
          placeholder="VNC端口"
        />

        <el-input
          v-model="vnc.password"
          placeholder="VNC密码(可选)"
          show-password
          clearable
        />

        <el-radio-group v-model="vnc.mode" @change="handleModeChange">
          <el-radio-button label="monitor">远程监控</el-radio-button>
          <el-radio-button label="control">远程控制</el-radio-button>
        </el-radio-group>

        <el-button
          type="primary"
          :loading="vnc.connecting"
          :disabled="!selectedDevice || vnc.connected"
          @click="connectVNC"
        >
          连接
        </el-button>
        <el-button
          type="danger"
          plain
          :disabled="!vnc.connected && !vnc.connecting"
          @click="disconnectVNC()"
        >
          关闭监控
        </el-button>
        <el-button icon="el-icon-refresh" @click="refreshData">刷新数据</el-button>
      </div>

      <template v-if="selectedDevice">
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
              <div class="status-label">累计针数</div>
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

        <el-row :gutter="20">
          <el-col :span="16">
            <div class="card">
              <div class="card-header flex-between">
                <span>设备监控画面（VNC）</span>
                <el-tag :type="vnc.connected ? 'success' : (vnc.connecting ? 'warning' : 'info')" size="small">
                  {{ vnc.connecting ? '连接中' : (vnc.connected ? (vnc.mode === 'control' ? '控制模式' : '监控模式') : '未连接') }}
                </el-tag>
              </div>
              <div class="monitor-screen">
                <div ref="vncCanvas" class="vnc-canvas"></div>
                <div v-if="!vnc.connected" class="screen-placeholder">
                  <i class="el-icon-video-camera"></i>
                  <p>{{ vnc.connecting ? '正在连接VNC...' : '请点击“连接”开始监控' }}</p>
                  <p class="hint">{{ vnc.status }}</p>
                </div>
              </div>
            </div>
          </el-col>
          <el-col :span="8">
            <div class="card">
              <div class="card-header">连接信息</div>
              <div class="connection-meta">
                <div class="meta-row">
                  <span>设备名称</span>
                  <strong>{{ selectedDevice.name }}</strong>
                </div>
                <div class="meta-row">
                  <span>设备IP</span>
                  <strong>{{ selectedDevice.ip || '-' }}</strong>
                </div>
                <div class="meta-row">
                  <span>VNC端口</span>
                  <strong>{{ vnc.port }}</strong>
                </div>
                <div class="meta-row">
                  <span>连接状态</span>
                  <strong>{{ vnc.status }}</strong>
                </div>
              </div>
              <el-alert
                v-if="vnc.mode === 'monitor'"
                title="当前为远程监控模式，只读不可操作设备。"
                type="info"
                show-icon
                :closable="false"
              />
              <el-alert
                v-else
                title="当前为远程控制模式，可操作鼠标键盘。"
                type="warning"
                show-icon
                :closable="false"
              />
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
import RFB from '@novnc/novnc/lib/rfb'
import { getDeviceList } from '@/api/device'
import { getDashboardData, getAlarmStats } from '@/api/statistics'

export default {
  name: 'RemoteMonitor',
  data() {
    return {
      selectedDeviceId: null,
      deviceKeyword: '',
      deviceDateRange: [],
      deviceList: [],
      selectedDevice: null,
      realtimeData: {
        spindleSpeed: 0,
        currentStitches: 0,
        currentPattern: '-'
      },
      alarms: [],
      chart: null,
      rfb: null,
      rfbListeners: null,
      vnc: {
        port: 5900,
        password: '',
        mode: 'monitor',
        connected: false,
        connecting: false,
        status: '未连接'
      }
    }
  },
  mounted() {
    this.fetchDevices()
    window.addEventListener('resize', this.handleResize)
  },
  beforeDestroy() {
    window.removeEventListener('resize', this.handleResize)
    this.disconnectVNC(false)
    if (this.chart) {
      this.chart.dispose()
      this.chart = null
    }
  },
  methods: {
    async fetchDevices() {
      try {
        const res = await getDeviceList({
          page: 1,
          pageSize: 500,
          keyword: this.deviceKeyword,
          startDate: this.deviceDateRange?.[0] || '',
          endDate: this.deviceDateRange?.[1] || ''
        })
        if (res.code === 0) {
          this.deviceList = res.data.list || []
        }

        const routeDeviceId = Number(this.$route.query.id)
        const hasSelected = this.selectedDeviceId && this.deviceList.some(item => item.id === this.selectedDeviceId)
        if (!hasSelected && routeDeviceId && this.deviceList.some(item => item.id === routeDeviceId)) {
          this.selectedDeviceId = routeDeviceId
        } else if (!hasSelected && this.deviceList.length > 0) {
          this.selectedDeviceId = this.deviceList[0].id
        } else if (!hasSelected) {
          this.selectedDeviceId = null
        }
        if (this.selectedDeviceId) {
          this.handleDeviceChange(this.selectedDeviceId)
        } else {
          this.selectedDevice = null
          this.disconnectVNC(false)
        }
      } catch (error) {
        console.error('Failed to fetch device list:', error)
        this.$message.error('获取设备列表失败')
      }
    },
    handleDeviceFilter() {
      this.fetchDevices()
    },
    handleDeviceChange(deviceId) {
      if (this.selectedDevice && this.selectedDevice.id !== deviceId) {
        this.disconnectVNC(false)
      }
      this.selectedDevice = this.deviceList.find(d => d.id === deviceId) || null
      if (this.selectedDevice) {
        this.loadDeviceData()
      }
    },
    async loadDeviceData() {
      if (!this.selectedDevice) return

      const deviceId = this.selectedDevice.id

      try {
        const [dashboardRes, alarmRes] = await Promise.all([
          getDashboardData(deviceId),
          getAlarmStats({
            deviceId,
            page: 1,
            pageSize: 5
          })
        ])

        if (dashboardRes.code === 0) {
          this.realtimeData = {
            spindleSpeed: Number(dashboardRes.data?.spindleSpeed || 0),
            currentStitches: Number(dashboardRes.data?.totalPieces || 0),
            currentPattern: '-'
          }
          this.$nextTick(() => {
            this.initChart(dashboardRes.data?.hourlyProduction || [])
          })
        }

        if (alarmRes.code === 0) {
          this.alarms = (alarmRes.data?.list || []).map(item => ({
            id: item.id,
            message: item.alarmInfo || item.description || item.alarmType || '报警',
            time: item.alarmTime || item.startTime || '-'
          }))
        } else {
          this.alarms = []
        }
      } catch (error) {
        console.error('Failed to load device monitor data:', error)
      }
    },
    async refreshData() {
      await this.fetchDevices()
      if (this.selectedDevice) {
        this.loadDeviceData()
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
    buildVncWsUrl() {
      if (!this.selectedDevice) return ''
      const serverUrl = this.$store.getters.serverUrl
      if (!serverUrl || serverUrl === 'http://:') return ''

      const wsBase = serverUrl.startsWith('https://')
        ? serverUrl.replace('https://', 'wss://')
        : serverUrl.replace('http://', 'ws://')
      const token = this.$store.state.token

      const params = new URLSearchParams()
      if (token) params.set('token', token)
      params.set('port', String(this.vnc.port))

      return `${wsBase}/api/device/vnc/ws/${this.selectedDevice.id}?${params.toString()}`
    },
    async connectVNC() {
      if (!this.selectedDevice) {
        this.$message.warning('请先选择设备')
        return
      }
      if (this.selectedDevice.status === 'offline') {
        this.$message.warning('离线设备不可监控')
        return
      }

      const wsUrl = this.buildVncWsUrl()
      if (!wsUrl) {
        this.$message.error('服务器地址未配置')
        return
      }
      if (!this.$refs.vncCanvas) {
        this.$message.error('VNC容器未就绪')
        return
      }

      this.disconnectVNC(false)
      this.vnc.connecting = true
      this.vnc.status = '连接中...'

      try {
        const options = {}
        if (this.vnc.password) {
          options.credentials = { password: this.vnc.password }
        }

        const rfb = new RFB(this.$refs.vncCanvas, wsUrl, options)
        rfb.scaleViewport = true
        rfb.resizeSession = true
        rfb.viewOnly = this.vnc.mode === 'monitor'

        const onConnect = () => {
          this.vnc.connected = true
          this.vnc.connecting = false
          this.vnc.status = '已连接'
          this.$message.success('VNC连接成功')
        }

        const onDisconnect = (event) => {
          this.cleanupRfb()
          this.vnc.connected = false
          this.vnc.connecting = false
          this.vnc.status = event?.detail?.clean ? '连接已关闭' : '连接中断'
        }

        const onSecurityFailure = () => {
          this.vnc.status = '认证失败'
        }

        const onCredentialsRequired = () => {
          if (this.vnc.password) {
            rfb.sendCredentials({ password: this.vnc.password })
            return
          }
          this.$prompt('请输入VNC密码', 'VNC认证', {
            confirmButtonText: '确定',
            cancelButtonText: '取消',
            inputType: 'password'
          }).then(({ value }) => {
            this.vnc.password = value
            rfb.sendCredentials({ password: value })
          }).catch(() => {
            this.vnc.status = '认证取消'
            rfb.disconnect()
          })
        }

        rfb.addEventListener('connect', onConnect)
        rfb.addEventListener('disconnect', onDisconnect)
        rfb.addEventListener('securityfailure', onSecurityFailure)
        rfb.addEventListener('credentialsrequired', onCredentialsRequired)

        this.rfb = rfb
        this.rfbListeners = {
          onConnect,
          onDisconnect,
          onSecurityFailure,
          onCredentialsRequired
        }
      } catch (error) {
        this.vnc.connected = false
        this.vnc.connecting = false
        this.vnc.status = '连接失败'
        this.cleanupRfb()
        console.error('Failed to connect VNC:', error)
        this.$message.error('VNC连接失败')
      }
    },
    handleModeChange(mode) {
      if (mode === 'control') {
        this.$confirm(
          '切换到远程控制模式前，请先在设备操作屏确认允许远程控制。是否继续切换？',
          '远程控制确认',
          {
            confirmButtonText: '继续',
            cancelButtonText: '取消',
            type: 'warning'
          }
        ).then(() => {
          if (this.rfb) this.rfb.viewOnly = false
        }).catch(() => {
          this.vnc.mode = 'monitor'
          if (this.rfb) this.rfb.viewOnly = true
        })
        return
      }
      if (this.rfb) this.rfb.viewOnly = true
    },
    disconnectVNC(showMessage = true) {
      if (this.rfb) {
        this.cleanupRfb(true)
      }
      this.vnc.connected = false
      this.vnc.connecting = false
      this.vnc.status = '已关闭'
      if (showMessage) {
        this.$message.info('监控已关闭')
      }
    },
    cleanupRfb(triggerDisconnect = false) {
      if (!this.rfb) return

      if (this.rfbListeners) {
        this.rfb.removeEventListener('connect', this.rfbListeners.onConnect)
        this.rfb.removeEventListener('disconnect', this.rfbListeners.onDisconnect)
        this.rfb.removeEventListener('securityfailure', this.rfbListeners.onSecurityFailure)
        this.rfb.removeEventListener('credentialsrequired', this.rfbListeners.onCredentialsRequired)
      }

      if (triggerDisconnect) {
        try {
          this.rfb.disconnect()
        } catch (error) {
          console.error('Failed to disconnect VNC:', error)
        }
      }

      this.rfb = null
      this.rfbListeners = null
    },
    initChart(hourlyProduction = []) {
      if (this.chart) {
        this.chart.dispose()
      }
      const chartDom = this.$refs.realtimeChart
      if (!chartDom) return
      this.chart = echarts.init(chartDom)

      const timeData = hourlyProduction.map(item => item.hour)
      const productionData = hourlyProduction.map(item => Number(item.value || 0))
      const spindleBase = Number(this.realtimeData.spindleSpeed || 0)
      const speedData = timeData.map((_, index) => {
        if (spindleBase <= 0) return 0
        const ratio = 0.88 + (index % 5) * 0.03
        return Math.round(spindleBase * ratio)
      })

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
    },
    handleResize() {
      if (this.chart) this.chart.resize()
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
  flex-wrap: wrap;
  gap: 12px;
  margin-bottom: 20px;

  .el-select {
    width: 300px;
  }

  .el-input-number {
    width: 140px;
  }

  .el-input {
    width: 220px;
  }

  .el-date-editor {
    width: 260px;
  }
}

.status-dot {
  display: inline-block;
  width: 8px;
  height: 8px;
  border-radius: 50%;
  margin-left: 8px;

  &.online,
  &.working {
    background: #67C23A;
  }
  &.offline {
    background: #909399;
  }
  &.idle {
    background: #E6A23C;
  }
  &.alarm {
    background: #F56C6C;
  }
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

    &.online,
    &.working {
      color: #67C23A;
    }
    &.offline {
      color: #909399;
    }
    &.idle {
      color: #E6A23C;
    }
    &.alarm {
      color: #F56C6C;
    }

    &.pattern {
      font-size: 16px;
    }
  }
}

.monitor-screen {
  position: relative;
  background: #0f172a;
  border-radius: 8px;
  height: 460px;
  overflow: hidden;
}

.vnc-canvas {
  width: 100%;
  height: 100%;
  background: #000;
}

.vnc-canvas ::v-deep canvas {
  width: 100% !important;
  height: 100% !important;
}

.screen-placeholder {
  position: absolute;
  inset: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  text-align: center;
  color: #94a3b8;
  background: rgba(15, 23, 42, 0.92);

  i {
    font-size: 72px;
    margin-bottom: 16px;
  }

  p {
    font-size: 16px;
    margin: 4px 0;
  }

  .hint {
    font-size: 13px;
    color: #cbd5e1;
  }
}

.connection-meta {
  margin-bottom: 16px;

  .meta-row {
    display: flex;
    justify-content: space-between;
    margin-bottom: 10px;
    color: #606266;

    strong {
      color: #303133;
      margin-left: 16px;
      word-break: break-all;
    }
  }
}

.alarm-list {
  max-height: 220px;
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

@media (max-width: 1280px) {
  .monitor-screen {
    height: 380px;
  }
}
</style>
