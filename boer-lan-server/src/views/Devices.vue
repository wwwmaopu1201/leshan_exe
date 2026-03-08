<template>
  <div>
    <div class="page-title">设备管理</div>
    <el-card>
      <el-table :data="devices">
        <el-table-column prop="name" label="设备名称" />
        <el-table-column prop="code" label="设备编号" />
        <el-table-column prop="ip" label="IP地址" />
        <el-table-column prop="status" label="状态">
          <template slot-scope="{ row }">
            <el-tag :type="getStatusType(row.status)">
              {{ row.status }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="150">
          <template slot-scope="{ row }">
            <el-button size="small" @click="pingDevice(row.ip)" icon="el-icon-refresh">Ping</el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script>
export default {
  name: 'Devices',
  data() {
    return {
      devices: []
    }
  },
  mounted() {
    this.loadDevices()
  },
  methods: {
    async loadDevices() {
      try {
        const res = await this.$axios.get('/device/list')
        if (res.code === 0) {
          this.devices = res.data
        }
      } catch (error) {
        console.error('加载设备列表失败', error)
      }
    },
    getStatusType(status) {
      const map = {
        'online': 'success',
        'offline': 'info',
        'working': 'success',
        'idle': 'warning',
        'alarm': 'danger'
      }
      return map[status] || 'info'
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
</style>
