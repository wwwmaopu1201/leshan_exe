<template>
  <div class="page-shell">
    <div class="page-header">
      <div class="page-title-block">
        <h2>辅助工具</h2>
        <p>用于网络诊断、端口占用检查、防火墙处理以及服务端配置维护。</p>
      </div>
    </div>

    <div class="tool-layout">
      <div class="tool-main">
        <div class="surface-card">
          <div class="section-title">
            <div>
              <h3>网络诊断工具</h3>
              <p>快速查看本机 IP、主机名、ARP 缓存以及目标设备连通性。</p>
            </div>
          </div>
          <div class="tool-button-grid">
            <el-button icon="el-icon-view" @click="showNetworkInfo">查看本机 IP 信息</el-button>
            <el-button icon="el-icon-refresh" @click="showPingDialog">Ping 设备</el-button>
            <el-button :loading="commandLoading" icon="el-icon-share" @click="showArpCache">查看 ARP 缓存</el-button>
            <el-button :loading="commandLoading" icon="el-icon-monitor" @click="showHostname">查看主机名</el-button>
          </div>
        </div>

        <div class="surface-card">
          <div class="section-title">
            <div>
              <h3>端口占用诊断</h3>
              <p>用于排查服务端口是否被其他程序占用，也可查看整机端口监听概况。</p>
            </div>
          </div>

          <div class="tool-port-row">
            <el-input-number
              v-model="portToCheck"
              :min="1"
              :max="65535"
              controls-position="right"
              placeholder="输入端口号"
            />
            <div class="action-group">
              <el-button type="primary" :loading="commandLoading" @click="checkPortUsage">
                查看端口占用
              </el-button>
              <el-button :loading="commandLoading" @click="showNetstatOverview">
                查看全部端口
              </el-button>
              <el-button :loading="commandLoading" @click="checkServerPortUsage">
                查看当前服务端口进程
              </el-button>
            </div>
          </div>

          <div class="soft-note">
            <i class="el-icon-warning-outline"></i>
            <span>端口修改后需要重启服务器程序生效；Windows 下会自动补充 tasklist 进程信息。</span>
          </div>
        </div>

        <div class="surface-card">
          <div class="section-title">
            <div>
              <h3>防火墙快捷操作</h3>
              <p>常用于首次部署和局域网无法接入时的快速检查。</p>
            </div>
          </div>
          <div class="tool-button-grid">
            <el-button :loading="commandLoading" icon="el-icon-setting" @click="openFirewallConfig">
              打开防火墙配置
            </el-button>
            <el-button :loading="commandLoading" icon="el-icon-view" @click="showFirewallStatus">
              查看防火墙状态
            </el-button>
            <el-button type="success" plain :loading="commandLoading" @click="setFirewallState(true)">
              开启防火墙
            </el-button>
            <el-button type="danger" plain :loading="commandLoading" @click="setFirewallState(false)">
              关闭防火墙
            </el-button>
          </div>
        </div>
      </div>

      <div class="tool-side">
        <div class="surface-card">
          <div class="section-title">
            <div>
              <h3>服务器配置</h3>
              <p>统一维护管理端口、共享目录和调试输出开关。</p>
            </div>
          </div>

          <el-form label-width="110px" class="tool-form">
            <el-form-item label="服务器端口">
              <el-input-number
                v-model="settings.serverPort"
                :min="1"
                :max="65535"
                controls-position="right"
              />
            </el-form-item>
            <el-form-item label="共享文件夹目录">
              <el-input
                v-model.trim="settings.sharedFolder"
                placeholder="可为空，建议填写绝对路径"
                clearable
              />
            </el-form-item>
            <el-form-item label="调试输出">
              <el-switch
                v-model="settings.debugOutputEnabled"
                active-text="开启"
                inactive-text="关闭"
              />
            </el-form-item>
          </el-form>

          <div class="action-group">
            <el-button type="primary" :loading="settingsLoading" @click="saveSettings">保存配置</el-button>
            <el-button type="warning" plain @click="clearDebugLogs">清空调试日志</el-button>
          </div>
        </div>

        <div class="surface-card">
          <div class="section-title">
            <div>
              <h3>运行环境信息</h3>
              <p>显示当前服务端程序的端口、工作目录和系统环境。</p>
            </div>
          </div>

          <div class="info-grid">
            <div class="info-item">
              <span class="info-item__label">系统环境</span>
              <strong class="info-item__value">{{ serverInfo.os || '-' }} / {{ serverInfo.arch || '-' }}</strong>
            </div>
            <div class="info-item">
              <span class="info-item__label">管理端口</span>
              <strong class="info-item__value">{{ serverInfo.port || '-' }}</strong>
            </div>
            <div class="info-item">
              <span class="info-item__label">设备 TCP 端口</span>
              <strong class="info-item__value">{{ serverInfo.tcpPort || '-' }}</strong>
            </div>
            <div class="info-item">
              <span class="info-item__label">共享目录</span>
              <strong class="info-item__value">{{ settings.sharedFolder || '-' }}</strong>
            </div>
            <div class="info-item full-width">
              <span class="info-item__label">工作目录</span>
              <span class="info-item__value">{{ serverInfo.workDir || '-' }}</span>
              <el-button type="text" size="mini" @click="copyText(serverInfo.workDir)">复制</el-button>
            </div>
            <div class="info-item full-width">
              <span class="info-item__label">数据目录</span>
              <span class="info-item__value">{{ serverInfo.dataDir || '-' }}</span>
              <el-button type="text" size="mini" @click="copyText(serverInfo.dataDir)">复制</el-button>
            </div>
          </div>
        </div>
      </div>
    </div>

    <el-dialog
      :title="outputDialog.title"
      :visible.sync="outputDialog.visible"
      width="820px"
      append-to-body
    >
      <div class="mono-output">{{ outputDialog.content }}</div>
    </el-dialog>
  </div>
</template>

<script>
export default {
  name: 'Tools',
  data() {
    return {
      settingsLoading: false,
      commandLoading: false,
      portToCheck: 0,
      serverInfo: {
        workDir: '',
        dataDir: '',
        os: '',
        arch: '',
        port: 0,
        tcpPort: 0
      },
      settings: {
        serverPort: 8088,
        sharedFolder: '',
        debugOutputEnabled: true
      },
      outputDialog: {
        visible: false,
        title: '',
        content: ''
      },
      latestCommandOutput: ''
    }
  },
  mounted() {
    this.initPage()
  },
  methods: {
    async initPage() {
      await Promise.all([this.loadServerInfo(), this.loadSettings()])
    },
    async loadServerInfo() {
      try {
        const res = await this.$axios.get('/system/info')
        if (res.code === 0) {
          this.serverInfo = {
            ...this.serverInfo,
            ...res.data
          }
          if (!this.settings.serverPort) {
            this.settings.serverPort = Number(res.data?.port || 8088)
          }
        }
      } catch (error) {
        console.error('加载服务器信息失败', error)
      }
    },
    async loadSettings() {
      try {
        const res = await this.$axios.get('/system/config')
        if (res.code !== 0 || !Array.isArray(res.data)) {
          return
        }

        const map = res.data.reduce((acc, item) => {
          if (item?.key) acc[item.key] = item.value
          return acc
        }, {})

        const serverPort = Number(map.server_port || this.serverInfo.port || 8088)
        this.settings.serverPort = Number.isFinite(serverPort) && serverPort > 0 ? serverPort : 8088
        this.settings.sharedFolder = map.shared_folder || ''
        this.settings.debugOutputEnabled = this.parseBoolConfig(map.debug_output_enabled, true)
      } catch (error) {
        console.error('加载服务器配置失败', error)
      }
    },
    parseBoolConfig(raw, fallback = false) {
      if (raw === null || raw === undefined || raw === '') return fallback
      const normalized = String(raw).trim().toLowerCase()
      if (['1', 'true', 'yes', 'on'].includes(normalized)) return true
      if (['0', 'false', 'no', 'off'].includes(normalized)) return false
      return fallback
    },
    async saveSettings() {
      const port = Number(this.settings.serverPort || 0)
      if (!port || port < 1 || port > 65535) {
        this.$message.warning('请输入有效端口（1-65535）')
        return
      }

      this.settingsLoading = true
      try {
        await Promise.all([
          this.$axios.post('/system/config', {
            key: 'server_port',
            value: String(port),
            desc: '服务器端口（重启后生效）'
          }),
          this.$axios.post('/system/config', {
            key: 'shared_folder',
            value: this.settings.sharedFolder || '',
            desc: '共享文件夹目录'
          }),
          this.$axios.post('/system/config', {
            key: 'debug_output_enabled',
            value: String(!!this.settings.debugOutputEnabled),
            desc: '调试输出开关'
          })
        ])
        this.$message.success('配置已保存，端口修改需重启服务器后生效')
      } catch (error) {
        console.error('保存服务器配置失败', error)
      } finally {
        this.settingsLoading = false
      }
    },
    async showNetworkInfo() {
      try {
        const res = await this.$axios.get('/system/network')
        if (res.code === 0) {
          const info = res.data.map(item =>
            `${item.name}: ${item.addresses.join(', ')}`
          ).join('\n')
          this.$alert(info, '网络信息', {
            confirmButtonText: '确定'
          })
        }
      } catch (error) {
        console.error('获取网络信息失败', error)
      }
    },
    async executeCommand(command, args = [], title = '命令输出') {
      this.commandLoading = true
      try {
        const res = await this.$axios.post('/system/command', { command, args })
        const output = String(res?.data?.output || '')
        const errText = String(res?.data?.error || '')
        const codeLabel = res?.code === 0 ? '[执行成功]' : '[执行完成，返回异常信息]'
        const content = [codeLabel, output, errText ? `\n[错误信息]\n${errText}` : '']
          .filter(Boolean)
          .join('\n')
        this.latestCommandOutput = content
        this.openOutputDialog(title, content || '命令无输出')
      } catch (error) {
        console.error('执行命令失败', error)
      } finally {
        this.commandLoading = false
      }
    },
    showArpCache() {
      this.executeCommand('arp', ['-a'], 'ARP 缓存')
    },
    showHostname() {
      this.executeCommand('hostname', [], '主机名')
    },
    showNetstatOverview() {
      const args = this.isWindowsPlatform() ? ['-ano'] : ['-an']
      this.executeCommand('netstat', args, '端口占用总览')
    },
    openFirewallConfig() {
      if (!this.isWindowsPlatform()) {
        this.$message.warning('当前平台暂不支持该快捷操作')
        return
      }
      this.executeCommand('control', ['firewall.cpl'], '打开防火墙配置')
    },
    showFirewallStatus() {
      if (!this.isWindowsPlatform()) {
        this.$message.warning('当前平台暂不支持该快捷操作')
        return
      }
      this.executeCommand('netsh', ['advfirewall', 'show', 'allprofiles'], '防火墙状态')
    },
    async setFirewallState(enabled) {
      if (!this.isWindowsPlatform()) {
        this.$message.warning('当前平台暂不支持该快捷操作')
        return
      }
      const actionText = enabled ? '开启' : '关闭'
      try {
        await this.$confirm(`确定要${actionText}系统防火墙吗？`, '提示', {
          confirmButtonText: '确定',
          cancelButtonText: '取消',
          type: 'warning'
        })
      } catch (error) {
        if (error !== 'cancel') {
          console.error(`${actionText}防火墙确认失败`, error)
        }
        return
      }
      const state = enabled ? 'on' : 'off'
      await this.executeCommand(
        'netsh',
        ['advfirewall', 'set', 'allprofiles', 'state', state],
        `${actionText}防火墙`
      )
    },
    async checkPortUsage() {
      const port = Number(this.portToCheck || 0)
      if (!port || port < 1 || port > 65535) {
        this.$message.warning('请输入有效端口号')
        return
      }

      this.commandLoading = true
      try {
        const args = this.isWindowsPlatform() ? ['-ano'] : ['-an']
        const res = await this.$axios.post('/system/command', { command: 'netstat', args })
        const output = String(res?.data?.output || '')
        const filtered = this.filterPortLines(output, port)
        const errText = String(res?.data?.error || '')
        let processLines = []
        if (this.isWindowsPlatform() && filtered.length > 0) {
          const pids = this.extractWindowsPids(filtered)
          processLines = await this.fetchWindowsProcessLines(pids)
        }

        const header = filtered.length
          ? `端口 ${port} 占用记录（共 ${filtered.length} 行）`
          : `未匹配到端口 ${port} 占用记录`
        const content = [
          header,
          '',
          filtered.length ? filtered.join('\n') : output || '命令无输出',
          processLines.length ? `\n[进程信息]\n${processLines.join('\n')}` : '',
          errText ? `\n[错误信息]\n${errText}` : ''
        ].join('\n')
        this.latestCommandOutput = content
        this.openOutputDialog(`端口 ${port} 诊断结果`, content)
      } catch (error) {
        console.error('端口占用诊断失败', error)
      } finally {
        this.commandLoading = false
      }
    },
    checkServerPortUsage() {
      const serverPort = Number(this.serverInfo.port || this.settings.serverPort || 0)
      if (!serverPort || serverPort < 1 || serverPort > 65535) {
        this.$message.warning('当前服务端口不可用，请先检查服务配置')
        return
      }
      this.portToCheck = serverPort
      this.checkPortUsage()
    },
    filterPortLines(output, port) {
      const lines = String(output || '').split(/\r?\n/)
      const pattern = new RegExp(`[:.]${port}(\\s|$)`)
      return lines.filter(line => pattern.test(line))
    },
    extractWindowsPids(lines = []) {
      const set = new Set()
      lines.forEach(line => {
        const parts = String(line || '').trim().split(/\s+/)
        const pid = parts[parts.length - 1]
        if (/^\d+$/.test(pid)) {
          set.add(pid)
        }
      })
      return Array.from(set)
    },
    async fetchWindowsProcessLines(pids = []) {
      if (!Array.isArray(pids) || pids.length === 0) {
        return []
      }
      const lines = []
      for (const pid of pids) {
        try {
          const res = await this.$axios.post('/system/command', {
            command: 'tasklist',
            args: ['/FI', `PID eq ${pid}`]
          })
          const output = String(res?.data?.output || '').trim()
          if (!output) {
            lines.push(`PID ${pid}: 无输出`)
            continue
          }
          const outputLines = output.split(/\r?\n/).map(item => item.trim()).filter(Boolean)
          const matchedLine = outputLines.find(item =>
            item.includes(pid) &&
            !item.includes('映像名称') &&
            !item.includes('Image Name') &&
            !item.startsWith('=')
          )
          lines.push(`PID ${pid}: ${matchedLine || outputLines[outputLines.length - 1]}`)
        } catch (error) {
          lines.push(`PID ${pid}: 查询失败`)
        }
      }
      return lines
    },
    isWindowsPlatform() {
      return String(this.serverInfo.os || '').toLowerCase().includes('windows')
    },
    async pingDevice(ip) {
      try {
        const res = await this.$axios.post('/system/ping', null, {
          params: { ip }
        })
        if (res.code === 0) {
          this.$alert(`<pre>${res.data.output}</pre>`, 'Ping结果', {
            dangerouslyUseHTMLString: true,
            confirmButtonText: '确定'
          })
        }
      } catch (error) {
        console.error('Ping失败', error)
      }
    },
    showPingDialog() {
      this.$prompt('请输入要 Ping 的 IP 地址', 'Ping 设备', {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        inputPattern: /^(\d{1,3}\.){3}\d{1,3}$/,
        inputErrorMessage: 'IP 地址格式不正确'
      }).then(({ value }) => {
        this.pingDevice(value)
      }).catch(() => {})
    },
    openOutputDialog(title, content) {
      this.outputDialog.title = title
      this.outputDialog.content = content
      this.outputDialog.visible = true
    },
    async clearDebugLogs() {
      try {
        await this.$confirm('确定要清空服务器调试日志吗？', '提示', {
          type: 'warning'
        })
        await this.$axios.delete('/system/logs')
        this.$message.success('调试日志已清空')
      } catch (error) {
        if (error !== 'cancel') {
          console.error('清空调试日志失败', error)
        }
      }
    },
    copyText(text) {
      const value = String(text || '').trim()
      if (!value) return

      if (navigator.clipboard?.writeText) {
        navigator.clipboard.writeText(value)
          .then(() => this.$message.success('已复制'))
          .catch(() => this.fallbackCopy(value))
        return
      }
      this.fallbackCopy(value)
    },
    fallbackCopy(value) {
      const textarea = document.createElement('textarea')
      textarea.value = value
      document.body.appendChild(textarea)
      textarea.select()
      try {
        document.execCommand('copy')
        this.$message.success('已复制')
      } catch {
        this.$message.warning('复制失败，请手动复制')
      } finally {
        document.body.removeChild(textarea)
      }
    }
  }
}
</script>

<style scoped>
.tool-layout {
  display: grid;
  grid-template-columns: minmax(0, 1.18fr) minmax(320px, 0.82fr);
  gap: 18px;
}

.tool-main,
.tool-side {
  display: grid;
  gap: 18px;
}

.tool-button-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
  gap: 12px;
}

.tool-port-row {
  display: grid;
  gap: 14px;
}

.tool-form ::v-deep .el-form-item {
  margin-bottom: 18px;
}

.full-width {
  grid-column: 1 / -1;
}

@media (max-width: 1180px) {
  .tool-layout {
    grid-template-columns: 1fr;
  }
}
</style>
