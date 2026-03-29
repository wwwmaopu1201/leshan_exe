<template>
  <div class="page-container remote-page">
    <div class="monitor-layout">
      <aside class="device-tree-panel panel-shell">
        <div class="panel-header">
          <div>
            <div class="panel-title">设备树</div>
          </div>
          <el-button type="text" size="mini" @click="resetTreeFilter">重置</el-button>
        </div>

        <el-input
          v-model="treeKeyword"
          size="small"
          clearable
          placeholder="搜索树节点"
          prefix-icon="el-icon-search"
        />

        <el-input
          v-model="deviceKeyword"
          class="mt-10"
          clearable
          placeholder="按设备名称搜索"
          @keyup.enter.native="handleDeviceFilter"
          @clear="handleDeviceFilter"
        />

        <el-date-picker
          v-model="deviceDateRange"
          class="mt-10"
          type="daterange"
          value-format="yyyy-MM-dd"
          range-separator="至"
          start-placeholder="开始日期"
          end-placeholder="结束日期"
          clearable
          @change="handleDeviceFilter"
        />

        <el-button class="mt-10" plain icon="el-icon-search" @click="handleDeviceFilter">筛选设备</el-button>

        <div class="tree-wrapper mt-10">
          <el-tree
            ref="deviceTree"
            :data="deviceTree"
            :props="treeProps"
            node-key="_nodeKey"
            default-expand-all
            highlight-current
            :filter-node-method="filterTreeNode"
            @node-click="handleTreeNodeClick"
          >
            <span slot-scope="{ node, data }" class="tree-node">
              <i :class="getNodeIcon(data)"></i>
              <span>{{ node.label }}</span>
              <span v-if="data.type === 'device'" :class="['status-dot', data.status]"></span>
            </span>
          </el-tree>
        </div>
      </aside>

      <section class="monitor-content">
        <div class="hero-card card">
          <div class="hero-meta">
            <div class="hero-badge" :class="selectedDevice?.status || 'offline'">
              {{ selectedDevice ? getStatusText(selectedDevice.status) : '未选择设备' }}
            </div>
            <strong class="hero-device-name">{{ selectedDevice ? selectedDevice.name : '请选择设备' }}</strong>
            <span v-if="selectedDevice" class="hero-device-ip">设备 IP：{{ selectedDevice.ip || '-' }}</span>
          </div>

          <div class="hero-actions">
            <div class="toolbar-field">
              <span>VNC 端口</span>
              <el-input-number
                v-model="vnc.port"
                :min="1"
                :max="65535"
                controls-position="right"
                placeholder="VNC端口"
              />
            </div>
            <div class="toolbar-field password-field">
              <span>VNC 密码</span>
              <el-input
                v-model="vnc.password"
                placeholder="VNC密码(可选)"
                show-password
                clearable
              />
            </div>
            <div class="toolbar-field">
              <span>连接模式</span>
              <el-radio-group v-model="vnc.mode" @change="handleModeChange">
                <el-radio-button label="monitor">远程监控</el-radio-button>
                <el-radio-button label="control">远程控制</el-radio-button>
              </el-radio-group>
            </div>
            <div class="hero-buttons">
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
          </div>
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

          <div class="monitor-grid">
            <div class="card monitor-screen-card">
              <div class="card-header flex-between">
                <span>设备监控画面（VNC）</span>
                <el-tag :type="getConnectionTagType()" size="small">
                  {{ vnc.connecting ? '连接中' : (vnc.connected ? (vnc.mode === 'control' ? '控制模式' : '监控模式') : '未连接') }}
                </el-tag>
              </div>
              <div class="monitor-screen">
                <div ref="vncCanvas" class="vnc-canvas"></div>
                <div v-if="!vnc.connected" class="screen-placeholder">
                  <i class="el-icon-video-camera"></i>
                  <p>{{ vnc.connecting ? '正在连接 VNC...' : '请点击“连接”开始监控' }}</p>
                  <p class="hint">{{ vnc.status }}</p>
                </div>
              </div>
            </div>

            <div class="side-stack">
              <div class="card">
                <div class="card-header">连接信息</div>
                <div class="connection-meta">
                  <div class="meta-row">
                    <span>设备名称</span>
                    <strong>{{ selectedDevice.name }}</strong>
                  </div>
                  <div class="meta-row">
                    <span>设备 IP</span>
                    <strong>{{ selectedDevice.ip || '-' }}</strong>
                  </div>
                  <div class="meta-row">
                    <span>VNC 端口</span>
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

              <div class="card">
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
            </div>
          </div>

          <div class="card mt-20">
            <div class="card-header">实时数据趋势</div>
            <div ref="realtimeChart" class="chart-container"></div>
          </div>
        </template>

        <template v-else>
          <div class="empty-state panel-shell">
            <i class="el-icon-monitor"></i>
            <p>请选择要监控的设备</p>
          </div>
        </template>
      </section>
    </div>

    <el-dialog
      title="远程控制确认"
      :visible.sync="controlConfirm.visible"
      width="460px"
      @closed="resetControlConfirmState"
    >
      <div class="control-confirm-tip">
        切换远程控制前，请先在设备操作屏上同意远程控制，并填写设备端显示的确认口令。
      </div>
      <el-form label-width="90px">
        <el-form-item label="确认口令" required>
          <el-input
            v-model.trim="controlConfirm.code"
            placeholder="请输入设备端确认口令"
          />
        </el-form-item>
        <el-form-item>
          <el-checkbox v-model="controlConfirm.acknowledged">
            我已在设备端完成远程控制授权
          </el-checkbox>
        </el-form-item>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="cancelControlMode">取消</el-button>
        <el-button type="primary" @click="confirmControlMode">确认切换</el-button>
      </span>
    </el-dialog>
  </div>
</template>

<script>
import * as echarts from 'echarts'
import RFB from '@novnc/novnc/lib/rfb'
import { getDeviceList, getDeviceTree, confirmRemoteControl } from '@/api/device'
import { getDashboardData, getAlarmStats } from '@/api/statistics'

export default {
  name: 'RemoteMonitor',
  data() {
    return {
      treeKeyword: '',
      selectedDeviceId: null,
      deviceKeyword: '',
      deviceDateRange: [],
      deviceList: [],
      fullDeviceTree: [],
      deviceTree: [],
      treeProps: {
        children: 'children',
        label: 'label'
      },
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
      },
      controlConfirm: {
        visible: false,
        acknowledged: false,
        code: ''
      },
      controlSession: {
        token: '',
        expiresAt: 0
      }
    }
  },
  watch: {
    treeKeyword(val) {
      this.$refs.deviceTree?.filter(val)
    }
  },
  mounted() {
    this.fetchDeviceTree()
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
    attachTreeNodeKeys(nodes) {
      return (nodes || []).map(node => {
        const nodeType = node.type === 'device' ? 'device' : 'group'
        const children = this.attachTreeNodeKeys(node.children || [])
        return {
          ...node,
          _nodeKey: `${nodeType}-${node.id}`,
          children
        }
      })
    },
    filterTreeNode(value, data) {
      if (!value) return true
      return String(data.label || '').toLowerCase().includes(value.toLowerCase())
    },
    getNodeIcon(data) {
      if (data?.type === 'device') return 'el-icon-monitor'
      return data?.children?.length ? 'el-icon-folder-opened' : 'el-icon-folder'
    },
    collectDeviceIds(node) {
      if (!node) return []
      if (node.type === 'device') return [Number(node.id)]

      const ids = []
      const stack = [...(node.children || [])]
      while (stack.length > 0) {
        const current = stack.pop()
        if (!current) continue
        if (current.type === 'device') {
          ids.push(Number(current.id))
        } else if (Array.isArray(current.children) && current.children.length > 0) {
          stack.push(...current.children)
        }
      }
      return ids
    },
    pruneTreeByDeviceIds(nodes, allowedDeviceIDs) {
      return (nodes || []).reduce((result, node) => {
        if (node.type === 'device') {
          if (allowedDeviceIDs.has(Number(node.id))) {
            result.push({ ...node, children: [] })
          }
          return result
        }
        const children = this.pruneTreeByDeviceIds(node.children || [], allowedDeviceIDs)
        if (children.length > 0) {
          result.push({ ...node, children })
        }
        return result
      }, [])
    },
    applyDeviceTreeFilter() {
      if (!Array.isArray(this.fullDeviceTree) || this.fullDeviceTree.length === 0) {
        this.deviceTree = []
        return
      }
      const allowed = new Set((this.deviceList || []).map(item => Number(item.id)))
      this.deviceTree = this.pruneTreeByDeviceIds(this.fullDeviceTree, allowed)
      this.$nextTick(() => {
        this.$refs.deviceTree?.filter(this.treeKeyword)
        if (this.selectedDeviceId) {
          this.$refs.deviceTree?.setCurrentKey(`device-${this.selectedDeviceId}`)
        }
      })
    },
    async fetchDeviceTree() {
      try {
        const res = await getDeviceTree()
        if (res.code === 0) {
          this.fullDeviceTree = this.attachTreeNodeKeys(res.data || [])
          this.applyDeviceTreeFilter()
        }
      } catch (error) {
        console.error('Failed to fetch device tree:', error)
      }
    },
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
          this.applyDeviceTreeFilter()
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
    resetTreeFilter() {
      this.treeKeyword = ''
      this.deviceKeyword = ''
      this.deviceDateRange = []
      this.fetchDevices()
    },
    handleTreeNodeClick(node) {
      if (!node) return
      if (node.type === 'device') {
        this.selectedDeviceId = Number(node.id)
        this.handleDeviceChange(this.selectedDeviceId)
        return
      }
      const deviceIds = this.collectDeviceIds(node)
      if (!deviceIds.length) {
        this.selectedDeviceId = null
        this.selectedDevice = null
        this.disconnectVNC(false)
        return
      }
      const preferredId = deviceIds.includes(Number(this.selectedDeviceId))
        ? Number(this.selectedDeviceId)
        : Number(deviceIds[0])
      this.selectedDeviceId = preferredId
      this.handleDeviceChange(preferredId)
    },
    handleDeviceChange(deviceId) {
      if (this.selectedDevice && this.selectedDevice.id !== deviceId) {
        this.disconnectVNC(false)
      }
      this.cancelControlMode()
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
      await Promise.all([this.fetchDeviceTree(), this.fetchDevices()])
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
    getConnectionTagType() {
      if (this.vnc.connected) {
        return this.vnc.mode === 'control' ? 'warning' : 'success'
      }
      if (this.vnc.connecting) {
        return 'warning'
      }
      return 'info'
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
      params.set('mode', this.vnc.mode)
      if (this.vnc.mode === 'control' && this.controlSession.token) {
        params.set('controlToken', this.controlSession.token)
      }

      return `${wsBase}/api/device/vnc/ws/${this.selectedDevice.id}?${params.toString()}`
    },
    async connectVNC() {
      if (!this.selectedDevice) {
        this.$message.warning('请先选择设备')
        return
      }
      if (this.vnc.mode === 'control' && !this.controlSession.token) {
        this.$message.warning('控制授权已失效，请重新确认后再连接')
        this.cancelControlMode()
        return
      }
      if (
        this.vnc.mode === 'control' &&
        this.controlSession.expiresAt > 0 &&
        Math.floor(Date.now() / 1000) >= this.controlSession.expiresAt
      ) {
        this.$message.warning('控制授权已过期，请重新确认后再连接')
        this.cancelControlMode()
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

      this.disconnectVNC(false, true)
      this.vnc.connecting = true
      this.vnc.status = '连接中...'
      if (this.vnc.mode === 'control') {
        // 控制令牌为一次性授权，发起连接后立即清空，避免重复复用。
        this.resetControlSession()
      }

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
          this.vnc.status = this.vnc.mode === 'control' ? '已连接（控制模式）' : '已连接'
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
        if (!this.selectedDevice) {
          this.$message.warning('请先选择设备')
          this.vnc.mode = 'monitor'
          return
        }
        this.controlConfirm.visible = true
        this.controlConfirm.acknowledged = false
        this.controlConfirm.code = ''
        this.vnc.mode = 'monitor'
        if (this.rfb) this.rfb.viewOnly = true
        return
      }
      this.resetControlSession()
      if (this.rfb) this.rfb.viewOnly = true
    },
    async confirmControlMode() {
      if (!this.controlConfirm.code) {
        this.$message.warning('请输入设备端确认口令')
        return
      }
      if (!this.controlConfirm.acknowledged) {
        this.$message.warning('请先确认已在设备端授权')
        return
      }

      try {
        const res = await confirmRemoteControl(this.selectedDevice.id, {
          code: this.controlConfirm.code,
          acknowledged: this.controlConfirm.acknowledged
        })
        if (res.code !== 0) {
          this.$message.error(res.message || '设备端授权校验失败')
          return
        }

        this.controlSession.token = res.data?.controlToken || ''
        this.controlSession.expiresAt = Number(res.data?.expiresAt || 0)
        this.controlConfirm.visible = false
        this.vnc.mode = 'control'

        if (this.vnc.connected || this.vnc.connecting) {
          await this.connectVNC()
        } else if (this.rfb) {
          this.rfb.viewOnly = false
        }
        if (this.vnc.connected) {
          this.vnc.status = '已连接（控制模式）'
        }
        this.$message.success('已切换到远程控制模式')
      } catch (error) {
        console.error('Confirm control mode failed:', error)
        this.$message.error('远程控制授权失败')
      }
    },
    cancelControlMode(clearControlSession = true) {
      this.controlConfirm.visible = false
      this.vnc.mode = 'monitor'
      if (this.rfb) {
        this.rfb.viewOnly = true
      }
      if (clearControlSession) {
        this.resetControlSession()
      }
    },
    resetControlConfirmState() {
      this.controlConfirm.visible = false
      this.controlConfirm.code = ''
      this.controlConfirm.acknowledged = false
    },
    resetControlSession() {
      this.controlSession.token = ''
      this.controlSession.expiresAt = 0
    },
    disconnectVNC(showMessage = true, preserveMode = false) {
      if (!preserveMode) {
        this.cancelControlMode()
      } else {
        this.controlConfirm.visible = false
      }
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

      const timeData = hourlyProduction.map(item => item.hour || item.date || '-')
      const productionData = hourlyProduction.map(item => Number(item.value || 0))
      const spindleBase = Number(this.realtimeData.spindleSpeed || 0)
      const speedData = timeData.map(() => {
        if (spindleBase <= 0) return 0
        return Math.round(spindleBase)
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
  display: flex;
  gap: 16px;
  align-items: flex-start;
  min-height: calc(100vh - 132px);
}

.control-confirm-tip {
  margin-bottom: 12px;
  color: #606266;
  line-height: 1.6;
}

.device-tree-panel {
  width: 320px;
  flex: 0 0 320px;
  padding: 18px;
}

.panel-header {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  margin-bottom: 8px;
}

.panel-title {
  font-size: 16px;
  font-weight: 700;
  color: #243654;
}

.panel-subtitle {
  margin-top: 4px;
  font-size: 12px;
  color: #8494ab;
}

.tree-wrapper {
  max-height: calc(100vh - 280px);
  min-height: 360px;
  overflow: auto;
  border: 1px solid #e6edf7;
  border-radius: 18px;
  padding: 8px;
  background: #fbfdff;
}

.tree-node {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.monitor-content {
  flex: 1;
  min-width: 0;
}

.hero-card {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 18px;
  margin-bottom: 20px;
}

.hero-meta {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
  min-width: 0;
}

.hero-badge {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-width: 88px;
  height: 32px;
  padding: 0 14px;
  border-radius: 999px;
  background: rgba(138, 152, 173, 0.14);
  color: #8a98ad;
  font-size: 12px;
  font-weight: 700;

  &.online,
  &.idle {
    background: rgba(47, 180, 110, 0.12);
    color: #2fb46e;
  }

  &.working {
    background: rgba(47, 109, 246, 0.12);
    color: #2f6df6;
  }

  &.alarm {
    background: rgba(239, 90, 90, 0.12);
    color: #ef5a5a;
  }
}

.hero-device-name {
  color: #22324d;
  font-size: 18px;
  line-height: 1.2;
}

.hero-device-ip {
  color: #7b8da6;
  font-size: 13px;
}

.hero-actions {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
  align-items: flex-end;
}

.toolbar-field {
  display: flex;
  flex-direction: column;
  gap: 8px;

  span {
    color: #7b8da6;
    font-size: 12px;
    font-weight: 600;
  }

  .el-input-number {
    width: 150px;
  }

  .el-input {
    width: 220px;
  }
}

.password-field {
  min-width: 220px;
}

.hero-buttons {
  display: flex;
  gap: 10px;
  flex-wrap: wrap;
}

.status-dot {
  display: inline-block;
  width: 8px;
  height: 8px;
  border-radius: 50%;
  margin-left: 6px;

  &.online,
  &.idle {
    background: #67C23A;
  }
  &.offline {
    background: #909399;
  }
  &.working,
  &.alarm {
    background: #F56C6C;
  }
}

.status-row {
  margin-bottom: 20px;
}

.status-card {
  background: #fff;
  border-radius: 22px;
  padding: 20px;
  text-align: center;
  border: 1px solid rgba(221, 229, 240, 0.92);
  box-shadow: 0 18px 30px rgba(59, 87, 132, 0.08);

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
    &.idle {
      color: #67C23A;
    }
    &.offline {
      color: #909399;
    }
    &.working,
    &.alarm {
      color: #F56C6C;
    }

    &.pattern {
      font-size: 16px;
    }
  }
}

.monitor-grid {
  display: grid;
  grid-template-columns: minmax(0, 1.45fr) minmax(320px, 0.75fr);
  gap: 18px;
}

.side-stack {
  display: grid;
  gap: 18px;
}

.monitor-screen-card {
  min-height: 100%;
}

.monitor-screen {
  position: relative;
  background: #0f172a;
  border-radius: 18px;
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
    gap: 16px;
    margin-bottom: 10px;
    color: #606266;

    strong {
      color: #303133;
      word-break: break-all;
      text-align: right;
    }
  }
}

.alarm-list {
  max-height: 268px;
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
    padding: 12px 0;
    border-bottom: 1px solid #edf2f8;

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
  min-height: 400px;

  i {
    font-size: 80px;
    margin-bottom: 20px;
  }
}

@media (max-width: 1280px) {
  .monitor-layout {
    flex-direction: column;
  }

  .device-tree-panel {
    width: 100%;
    flex: none;
  }

  .monitor-grid {
    grid-template-columns: 1fr;
  }

  .tree-wrapper {
    max-height: 260px;
    min-height: 220px;
  }

  .monitor-screen {
    height: 380px;
  }
}

@media (max-width: 768px) {
  .hero-actions,
  .hero-buttons {
    width: 100%;
  }

  .toolbar-field,
  .toolbar-field .el-input,
  .toolbar-field .el-input-number {
    width: 100%;
  }
}
</style>
