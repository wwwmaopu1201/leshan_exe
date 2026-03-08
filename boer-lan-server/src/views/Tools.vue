<template>
  <div>
    <div class="page-title">辅助工具</div>

    <el-card style="margin-bottom: 20px;">
      <div slot="header">
        <i class="el-icon-connection"></i>
        <span>网络诊断工具</span>
      </div>
      <el-button @click="showNetworkInfo" icon="el-icon-view">查看本机IP信息</el-button>
      <el-button @click="showPingDialog" icon="el-icon-refresh">Ping设备</el-button>
    </el-card>

    <el-card>
      <div slot="header">
        <i class="el-icon-document"></i>
        <span>服务器配置</span>
      </div>
      <p><strong>工作目录:</strong> {{ serverInfo.workDir }}</p>
      <p><strong>数据目录:</strong> {{ serverInfo.dataDir }}</p>
      <p><strong>系统:</strong> {{ serverInfo.os }} / {{ serverInfo.arch }}</p>
    </el-card>
  </div>
</template>

<script>
export default {
  name: 'Tools',
  data() {
    return {
      serverInfo: {
        workDir: '',
        dataDir: '',
        os: '',
        arch: ''
      }
    }
  },
  mounted() {
    this.loadServerInfo()
  },
  methods: {
    async loadServerInfo() {
      try {
        const res = await this.$axios.get('/system/info')
        if (res.code === 0) {
          this.serverInfo = res.data
        }
      } catch (error) {
        console.error('加载服务器信息失败', error)
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

.el-card p {
  margin: 10px 0;
}
</style>
