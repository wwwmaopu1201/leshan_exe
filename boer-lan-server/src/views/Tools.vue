<template>
  <div>
    <div class="page-title">辅助工具</div>

    <el-row :gutter="20">
      <el-col :span="14">
        <el-card class="tool-card">
          <div slot="header">
            <i class="el-icon-connection"></i>
            <span>网络诊断工具</span>
          </div>
          <div class="button-group">
            <el-button @click="showNetworkInfo" icon="el-icon-view">查看本机IP信息</el-button>
            <el-button @click="showPingDialog" icon="el-icon-refresh">Ping设备</el-button>
            <el-button :loading="commandLoading" @click="showArpCache" icon="el-icon-share">查看ARP缓存</el-button>
            <el-button :loading="commandLoading" @click="showHostname" icon="el-icon-monitor">查看主机名</el-button>
          </div>
        </el-card>

        <el-card class="tool-card">
          <div slot="header">
            <i class="el-icon-cpu"></i>
            <span>端口占用诊断</span>
          </div>
          <div class="port-row">
            <el-input-number
              v-model="portToCheck"
              :min="1"
              :max="65535"
              controls-position="right"
              placeholder="输入端口号"
            />
            <el-button type="primary" :loading="commandLoading" @click="checkPortUsage">
              查看端口占用
            </el-button>
            <el-button :loading="commandLoading" @click="showNetstatOverview">
              查看全部端口
            </el-button>
          </div>
          <div class="hint-text">
            端口修改后需重启服务器程序生效；端口占用信息来自系统 `netstat` 输出。
          </div>
        </el-card>

        <el-card class="tool-card">
          <div slot="header">
            <i class="el-icon-lock"></i>
            <span>防火墙快捷操作</span>
          </div>
          <div class="button-group">
            <el-button :loading="commandLoading" @click="openFirewallConfig" icon="el-icon-setting">
              打开防火墙配置
            </el-button>
            <el-button :loading="commandLoading" @click="showFirewallStatus" icon="el-icon-view">
              查看防火墙状态
            </el-button>
            <el-button type="success" plain :loading="commandLoading" @click="setFirewallState(true)">
              开启防火墙
            </el-button>
            <el-button type="danger" plain :loading="commandLoading" @click="setFirewallState(false)">
              关闭防火墙
            </el-button>
          </div>
          <div class="hint-text">
            当前通过系统命令执行防火墙检查与开关，适用于 Windows（`netsh advfirewall`）。
          </div>
        </el-card>
      </el-col>

      <el-col :span="10">
        <el-card class="tool-card">
          <div slot="header">
            <i class="el-icon-setting"></i>
            <span>服务器配置</span>
          </div>
          <el-form label-width="120px">
            <el-form-item label="服务器端口">
              <el-input-number
                v-model="settings.serverPort"
                :min="1"
                :max="65535"
                controls-position="right"
                style="width: 100%;"
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
          <div class="button-group">
            <el-button type="primary" :loading="settingsLoading" @click="saveSettings">保存配置</el-button>
            <el-button type="warning" plain @click="clearDebugLogs">清空调试日志</el-button>
          </div>
        </el-card>

        <el-card class="tool-card">
          <div slot="header">
            <i class="el-icon-document"></i>
            <span>运行环境信息</span>
          </div>
          <div class="meta-row">
            <span>系统</span>
            <strong>{{ serverInfo.os || '-' }} / {{ serverInfo.arch || '-' }}</strong>
          </div>
          <div class="meta-row">
            <span>当前端口</span>
            <strong>{{ serverInfo.port || '-' }}</strong>
          </div>
          <div class="meta-row">
            <span>工作目录</span>
            <el-tooltip :content="serverInfo.workDir || '-'" placement="top">
              <span class="path-text">{{ serverInfo.workDir || '-' }}</span>
            </el-tooltip>
            <el-button type="text" size="mini" @click="copyText(serverInfo.workDir)">复制</el-button>
          </div>
          <div class="meta-row">
            <span>数据目录</span>
            <el-tooltip :content="serverInfo.dataDir || '-'" placement="top">
              <span class="path-text">{{ serverInfo.dataDir || '-' }}</span>
            </el-tooltip>
            <el-button type="text" size="mini" @click="copyText(serverInfo.dataDir)">复制</el-button>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <el-dialog
      :title="outputDialog.title"
      :visible.sync="outputDialog.visible"
      width="780px"
      append-to-body
    >
      <div class="output-box">{{ outputDialog.content }}</div>
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
        port: 0
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
          this.serverInfo = res.data
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

        const header = filtered.length
          ? `端口 ${port} 占用记录（共 ${filtered.length} 行）`
          : `未匹配到端口 ${port} 占用记录`
        const content = [
          header,
          '',
          filtered.length ? filtered.join('\n') : output || '命令无输出',
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
    filterPortLines(output, port) {
      const lines = String(output || '').split(/\r?\n/)
      const pattern = new RegExp(`[:.]${port}(\\s|$)`)
      return lines.filter(line => pattern.test(line))
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
          this.$alert(
            `<pre>${res.data.output}</pre>`,
            'Ping结果',
            {
              dangerouslyUseHTMLString: true,
              confirmButtonText: '确定'
            }
          )
        }
      } catch (error) {
        console.error('Ping失败', error)
      }
    },
    showPingDialog() {
      this.$prompt('请输入要Ping的IP地址', 'Ping设备', {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        inputPattern: /^(\d{1,3}\.){3}\d{1,3}$/,
        inputErrorMessage: 'IP地址格式不正确'
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
.page-title {
  font-size: 20px;
  font-weight: bold;
  margin-bottom: 20px;
}

.tool-card {
  margin-bottom: 20px;
}

.button-group {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
}

.port-row {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 10px;
}

.hint-text {
  color: #909399;
  font-size: 12px;
}

.meta-row {
  display: flex;
  align-items: center;
  margin-bottom: 10px;
  gap: 8px;
}

.meta-row > span:first-child {
  min-width: 76px;
  color: #909399;
}

.path-text {
  flex: 1;
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}

.output-box {
  max-height: 460px;
  overflow: auto;
  white-space: pre-wrap;
  word-break: break-all;
  background: #0f172a;
  color: #e2e8f0;
  border-radius: 6px;
  padding: 12px;
  font-family: Menlo, Monaco, Consolas, monospace;
  font-size: 12px;
}
</style>
