<template>
  <div>
    <div class="page-title">连接数据库</div>

    <el-row :gutter="20" style="margin-bottom: 20px;">
      <el-col :span="12">
        <el-card>
          <div slot="header"><span>服务器基础信息</span></div>
          <p><strong>服务器端口:</strong> {{ serverInfo.port }}</p>
          <p><strong>服务器IP:</strong> {{ (serverInfo.ips || []).join(', ') || '-' }}</p>
          <p><strong>工作目录:</strong> {{ serverInfo.workDir || '-' }}</p>
        </el-card>
      </el-col>
      <el-col :span="12">
        <el-card>
          <div slot="header"><span>连接说明</span></div>
          <p>1. 连接外部数据库为可选功能，不连接时系统仍可使用本地数据。</p>
          <p>2. 当前支持 MySQL 完整连接测试；MSSQL 提供网络连通性检测。</p>
          <p>3. 可配置同步间隔（分钟）用于后续数据同步策略。</p>
          <p>4. 最后更新时间：{{ formatTimestamp(form.updatedAt) }}</p>
          <p>5. 最近同步时间：{{ formatTimestamp(syncStatus.lastSyncAt) }}</p>
          <p>6. 下次同步时间：{{ formatTimestamp(syncStatus.nextSyncAt) }}</p>
          <p>7. 同步状态：{{ syncStatusText(syncStatus.status) }}</p>
        </el-card>
      </el-col>
    </el-row>

    <el-card>
      <div slot="header" style="display: flex; justify-content: space-between; align-items: center;">
        <span>外部数据库连接配置</span>
        <div>
          <el-button icon="el-icon-refresh" @click="loadConfig">重新加载</el-button>
          <el-button type="warning" :loading="testing" @click="testConnection">测试连接</el-button>
          <el-button type="primary" :loading="saving" @click="saveConfig">保存配置</el-button>
        </div>
      </div>

      <el-form ref="dbFormRef" :model="form" :rules="rules" label-width="120px" style="max-width: 820px;">
        <el-form-item label="启用外部连接">
          <el-switch
            v-model="form.enabled"
            active-text="启用"
            inactive-text="关闭"
          />
        </el-form-item>

        <el-form-item label="数据库类型" prop="dbType">
          <el-select v-model="form.dbType" style="width: 220px;" @change="handleDBTypeChange">
            <el-option label="MySQL" value="mysql" />
            <el-option label="MSSQL" value="mssql" />
          </el-select>
        </el-form-item>

        <el-form-item label="服务器地址" prop="host">
          <el-input v-model="form.host" placeholder="例如 127.0.0.1" />
        </el-form-item>

        <el-form-item label="端口" prop="port">
          <el-input-number v-model="form.port" :min="1" :max="65535" />
        </el-form-item>

        <el-form-item label="登录名" prop="username">
          <el-input v-model="form.username" />
        </el-form-item>

        <el-form-item label="密码" prop="password">
          <el-input v-model="form.password" show-password type="password" />
        </el-form-item>

        <el-form-item label="数据库名" prop="database">
          <el-input v-model="form.database" />
        </el-form-item>

        <el-form-item label="字符集" prop="charset" v-if="form.dbType === 'mysql'">
          <el-input v-model="form.charset" placeholder="utf8mb4" />
        </el-form-item>

        <el-form-item label="同步间隔(分钟)" prop="syncIntervalMinutes">
          <el-input-number v-model="form.syncIntervalMinutes" :min="5" :max="720" />
        </el-form-item>
      </el-form>
    </el-card>
  </div>
</template>

<script>
export default {
  name: 'Database',
  data() {
    return {
      saving: false,
      testing: false,
      serverInfo: {
        ips: [],
        port: 8088,
        workDir: ''
      },
      syncStatus: {
        status: 'disabled',
        lastSyncAt: 0,
        nextSyncAt: 0
      },
      syncTimer: null,
      form: {
        dbType: 'mysql',
        host: '127.0.0.1',
        port: 3306,
        username: '',
        password: '',
        database: '',
        charset: 'utf8mb4',
        syncIntervalMinutes: 30,
        enabled: false,
        updatedAt: 0
      },
      rules: {
        dbType: [{ required: true, message: '请选择数据库类型', trigger: 'change' }],
        host: [{ required: true, message: '请输入服务器地址', trigger: 'blur' }],
        port: [{ required: true, message: '请输入端口', trigger: 'change' }],
        username: [{ required: true, message: '请输入登录名', trigger: 'blur' }],
        database: [{ required: true, message: '请输入数据库名', trigger: 'blur' }],
        syncIntervalMinutes: [{ required: true, message: '请输入同步间隔', trigger: 'change' }]
      }
    }
  },
  mounted() {
    this.loadServerInfo()
    this.loadConfig()
    this.loadSyncStatus()
    this.syncTimer = setInterval(() => {
      this.loadSyncStatus()
    }, 15000)
  },
  beforeDestroy() {
    if (this.syncTimer) {
      clearInterval(this.syncTimer)
      this.syncTimer = null
    }
  },
  methods: {
    formatTimestamp(ts) {
      if (!ts) return '-'
      const date = new Date(ts * 1000)
      if (Number.isNaN(date.getTime())) return '-'
      return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, '0')}-${String(date.getDate()).padStart(2, '0')} ${String(date.getHours()).padStart(2, '0')}:${String(date.getMinutes()).padStart(2, '0')}:${String(date.getSeconds()).padStart(2, '0')}`
    },
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
    async loadConfig() {
      try {
        const res = await this.$axios.get('/system/database/config')
        if (res.code === 0 && res.data) {
          this.form = {
            ...this.form,
            ...res.data
          }
        }
      } catch (error) {
        console.error('加载数据库配置失败', error)
      }
    },
    async loadSyncStatus() {
      try {
        const res = await this.$axios.get('/system/database/sync-status')
        if (res.code === 0 && res.data) {
          this.syncStatus = {
            ...this.syncStatus,
            ...res.data
          }
        }
      } catch (error) {
        console.error('加载同步状态失败', error)
      }
    },
    syncStatusText(status) {
      const map = {
        disabled: '未启用',
        waiting_first_sync: '等待首次同步',
        scheduled: '已调度',
        due: '待执行',
        mssql_not_supported: '当前版本未启用MSSQL自动同步'
      }
      return map[status] || status || '-'
    },
    handleDBTypeChange(value) {
      if (value === 'mssql' && this.form.port === 3306) {
        this.form.port = 1433
      }
      if (value === 'mysql' && this.form.port === 1433) {
        this.form.port = 3306
      }
    },
    async testConnection() {
      try {
        await this.$refs.dbFormRef.validate()
        this.testing = true
        const res = await this.$axios.post('/system/database/test', this.form)
        if (res.code === 0) {
          this.$message.success(res.message || '连接测试成功')
        } else {
          this.$message.error(res.message || '连接测试失败')
        }
      } catch (error) {
        console.error('测试连接失败', error)
      } finally {
        this.testing = false
      }
    },
    async saveConfig() {
      try {
        await this.$refs.dbFormRef.validate()
        this.saving = true
        const res = await this.$axios.post('/system/database/config', this.form)
        if (res.code === 0) {
          this.form.updatedAt = res.data?.updatedAt || this.form.updatedAt
          this.loadSyncStatus()
          this.$message.success('保存成功')
        } else {
          this.$message.error(res.message || '保存失败')
        }
      } catch (error) {
        console.error('保存数据库配置失败', error)
      } finally {
        this.saving = false
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

.el-card p {
  margin: 8px 0;
}
</style>
