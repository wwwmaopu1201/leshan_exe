<template>
  <div class="page-shell">
    <div class="page-header">
      <div class="page-title-block">
        <h2>连接数据库</h2>
        <p>配置外部数据库连接、同步周期和手动同步动作，不连接时系统仍可使用本地数据。</p>
      </div>
    </div>

    <div class="summary-grid">
      <div class="summary-card primary">
        <div class="summary-card__icon">
          <i class="el-icon-office-building"></i>
        </div>
        <div class="summary-card__label">服务器地址</div>
        <div class="summary-card__value summary-card__value--small">{{ serverIpText }}</div>
        <div class="summary-card__hint">管理端口 {{ serverInfo.port || '-' }}，当前本机工作目录可在下方查看。</div>
      </div>
      <div class="summary-card success">
        <div class="summary-card__icon">
          <i class="el-icon-connection"></i>
        </div>
        <div class="summary-card__label">同步状态</div>
        <div class="summary-card__value summary-card__value--small">{{ syncStatusText(syncStatus.status) }}</div>
        <div class="summary-card__hint">最近同步：{{ formatTimestamp(syncStatus.lastSyncAt) }}</div>
      </div>
      <div class="summary-card info">
        <div class="summary-card__icon">
          <i class="el-icon-timer"></i>
        </div>
        <div class="summary-card__label">下次同步</div>
        <div class="summary-card__value summary-card__value--small">{{ formatTimestamp(syncStatus.nextSyncAt) }}</div>
        <div class="summary-card__hint">配置保存成功后会自动按周期调度。</div>
      </div>
    </div>

    <div class="database-layout">
      <div class="surface-card">
        <div class="section-title">
          <div>
            <h3>连接说明</h3>
            <p>当前支持 MySQL、MSSQL 连接测试，可单独配置同步间隔并手动触发同步。</p>
          </div>
        </div>

        <div class="info-grid">
          <div class="info-item">
            <span class="info-item__label">是否启用</span>
            <strong class="info-item__value">{{ form.enabled ? '已启用' : '未启用' }}</strong>
          </div>
          <div class="info-item">
            <span class="info-item__label">数据库类型</span>
            <strong class="info-item__value">{{ String(form.dbType || '-').toUpperCase() }}</strong>
          </div>
          <div class="info-item">
            <span class="info-item__label">最近更新时间</span>
            <span class="info-item__value">{{ formatTimestamp(form.updatedAt) }}</span>
          </div>
          <div class="info-item">
            <span class="info-item__label">同步间隔</span>
            <span class="info-item__value">{{ form.syncIntervalMinutes || '-' }} 分钟</span>
          </div>
          <div class="info-item full-width">
            <span class="info-item__label">工作目录</span>
            <span class="info-item__value">{{ serverInfo.workDir || '-' }}</span>
          </div>
        </div>
      </div>

      <div class="surface-card">
        <div class="section-title">
          <div>
            <h3>外部数据库连接配置</h3>
            <p>配置完成后可先测试连接，再保存并根据需要立即同步一次。</p>
          </div>
          <div class="action-group">
            <el-button icon="el-icon-refresh" @click="loadConfig">重新加载</el-button>
            <el-button type="warning" :loading="testing" @click="testConnection">测试连接</el-button>
            <el-button type="success" :loading="syncing" @click="syncNow">立即同步</el-button>
            <el-button type="primary" :loading="saving" @click="saveConfig">保存配置</el-button>
          </div>
        </div>

        <el-form ref="dbFormRef" :model="form" :rules="rules" label-width="110px" class="database-form">
          <div class="database-form-grid">
            <el-form-item label="启用外部连接" class="full-row">
              <el-switch
                v-model="form.enabled"
                active-text="启用"
                inactive-text="关闭"
              />
            </el-form-item>

            <el-form-item label="数据库类型" prop="dbType">
              <el-select v-model="form.dbType" @change="handleDBTypeChange">
                <el-option label="MySQL" value="mysql" />
                <el-option label="MSSQL" value="mssql" />
              </el-select>
            </el-form-item>

            <el-form-item label="服务器地址" prop="host">
              <el-input v-model.trim="form.host" placeholder="例如 127.0.0.1" />
            </el-form-item>

            <el-form-item label="端口" prop="port">
              <el-input-number v-model="form.port" :min="1" :max="65535" />
            </el-form-item>

            <el-form-item label="登录名" prop="username">
              <el-input v-model.trim="form.username" />
            </el-form-item>

            <el-form-item label="密码" prop="password">
              <el-input v-model="form.password" show-password type="password" />
            </el-form-item>

            <el-form-item label="数据库名" prop="database">
              <el-input v-model.trim="form.database" />
            </el-form-item>

            <el-form-item v-if="form.dbType === 'mysql'" label="字符集" prop="charset">
              <el-input v-model.trim="form.charset" placeholder="utf8mb4" />
            </el-form-item>

            <el-form-item label="同步间隔(分钟)" prop="syncIntervalMinutes">
              <el-input-number v-model="form.syncIntervalMinutes" :min="5" :max="720" />
            </el-form-item>
          </div>
        </el-form>

        <div class="soft-note">
          <i class="el-icon-info"></i>
          <span>同步时间显示为空时，说明当前尚未执行过同步或外部数据库连接尚未启用。</span>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
export default {
  name: 'Database',
  data() {
    return {
      saving: false,
      testing: false,
      syncing: false,
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
  computed: {
    serverIpText() {
      const ips = Array.isArray(this.serverInfo.ips) ? this.serverInfo.ips.filter(Boolean) : []
      return ips.length ? ips.join(', ') : '-'
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
          this.serverInfo = {
            ...this.serverInfo,
            ...res.data
          }
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
        due: '待执行'
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
      const valid = await this.$refs.dbFormRef.validate().catch(() => false)
      if (!valid) {
        return
      }

      try {
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
      const valid = await this.$refs.dbFormRef.validate().catch(() => false)
      if (!valid) {
        return
      }

      try {
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
    },
    async syncNow() {
      try {
        this.syncing = true
        const res = await this.$axios.post('/system/database/sync-now')
        if (res.code === 0) {
          this.$message.success(res.message || '同步成功')
          this.loadSyncStatus()
        } else {
          this.$message.error(res.message || '同步失败')
        }
      } catch (error) {
        console.error('手动同步失败', error)
      } finally {
        this.syncing = false
      }
    }
  }
}
</script>

<style scoped>
.database-layout {
  display: grid;
  grid-template-columns: minmax(320px, 0.82fr) minmax(0, 1.18fr);
  gap: 18px;
}

.database-form-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 0 18px;
}

.summary-card__value--small {
  font-size: 24px;
  line-height: 1.3;
  word-break: break-word;
}

.full-row {
  grid-column: 1 / -1;
}

@media (max-width: 1180px) {
  .database-layout {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 720px) {
  .database-form-grid {
    grid-template-columns: 1fr;
  }
}
</style>
