<template>
  <div class="page-container">
    <div class="profile-layout">
      <div class="card profile-overview">
        <div class="profile-overview-head">
          <el-avatar :size="88" icon="el-icon-user-solid" />
          <div class="profile-overview-meta">
            <h3>{{ userInfo.nickname || form.nickname || '-' }}</h3>
            <div class="profile-chip-row">
              <span class="profile-chip account">{{ form.username || '-' }}</span>
              <span class="profile-chip role">{{ formatRoleLabel(userInfo.role || user?.role || '') }}</span>
            </div>
          </div>
        </div>

        <div class="profile-summary-grid">
          <div class="summary-item">
            <span class="summary-label">{{ $t('profile.accountName') }}</span>
            <strong>{{ form.nickname || '-' }}</strong>
          </div>
          <div class="summary-item">
            <span class="summary-label">{{ $t('profile.phone') }}</span>
            <strong>{{ form.phone || '-' }}</strong>
          </div>
          <div class="summary-item">
            <span class="summary-label">{{ $t('profile.createTime') }}</span>
            <strong>{{ form.createTime || '-' }}</strong>
          </div>
          <div class="summary-item">
            <span class="summary-label">{{ $t('profile.roleName') }}</span>
            <strong>{{ form.roleName || '-' }}</strong>
          </div>
        </div>

        <div class="profile-note">
          <i class="el-icon-info"></i>
          <span>此页面仅维护当前账号基本信息；密码修改和语言切换已分别收口到标题栏与修改密码页面。</span>
        </div>
      </div>

      <div class="card profile-editor">
        <div class="section-title">
          <div>
            <h3>基本资料</h3>
            <p>修改昵称和手机号后会立即同步到当前登录账号。</p>
          </div>
        </div>

        <el-form ref="formRef" :model="form" :rules="rules" label-width="96px" class="profile-form">
          <div class="form-grid">
            <el-form-item :label="$t('profile.account')">
              <el-input v-model="form.username" disabled />
            </el-form-item>
            <el-form-item :label="$t('profile.accountName')" prop="nickname">
              <el-input v-model.trim="form.nickname" />
            </el-form-item>
            <el-form-item :label="$t('profile.phone')" prop="phone">
              <el-input v-model.trim="form.phone" />
            </el-form-item>
            <el-form-item :label="$t('profile.roleName')">
              <el-input v-model="form.roleName" disabled />
            </el-form-item>
            <el-form-item :label="$t('profile.createTime')" class="full-row">
              <el-input v-model="form.createTime" disabled />
            </el-form-item>
          </div>

          <el-form-item class="form-actions">
            <el-button type="primary" :loading="submitting" @click="handleSave">
              {{ $t('profile.updateInfo') }}
            </el-button>
            <el-button @click="resetForm">
              {{ $t('common.reset') }}
            </el-button>
          </el-form-item>
        </el-form>
      </div>
    </div>

    <div class="card profile-log-card">
      <div class="section-title">
        <div>
          <h3>登录记录</h3>
          <p>用于核对账号近期的登录时间、终端信息和登录结果。</p>
        </div>
      </div>

      <el-table v-loading="logLoading" :data="loginLogs" border empty-text="暂无登录记录">
        <el-table-column prop="time" label="登录时间" width="180" />
        <el-table-column prop="ip" label="IP地址" width="150" />
        <el-table-column prop="device" label="设备" min-width="200" />
        <el-table-column prop="status" label="状态" width="110" align="center">
          <template slot-scope="scope">
            <el-tag :type="scope.row.status === '成功' ? 'success' : 'danger'" size="small" effect="plain">
              {{ scope.row.status }}
            </el-tag>
          </template>
        </el-table-column>
      </el-table>
    </div>
  </div>
</template>

<script>
import { mapState } from 'vuex'
import { updateProfile, getLoginLogs } from '@/api/auth'

export default {
  name: 'BasicInfo',
  data() {
    return {
      form: {
        username: 'admin',
        nickname: '管理员',
        phone: '',
        roleName: '',
        createTime: ''
      },
      rules: {
        nickname: [{ required: true, message: '请输入账号姓名', trigger: 'blur' }],
        phone: [
          { required: true, message: '请输入手机号', trigger: 'blur' },
          { pattern: /^1[3-9]\d{9}$/, message: '请输入正确的手机号', trigger: 'blur' }
        ]
      },
      userInfo: {
        nickname: '管理员',
        role: '',
        createTime: ''
      },
      loginLogs: [],
      submitting: false,
      logLoading: false
    }
  },
  computed: {
    ...mapState(['user'])
  },
  mounted() {
    this.syncUserInfo()
    this.fetchLoginLogs()
  },
  methods: {
    syncUserInfo() {
      if (!this.user) {
        return
      }
      this.form.username = this.user.username || 'admin'
      this.form.nickname = this.user.nickname || '管理员'
      this.form.phone = this.user.phone || ''
      this.form.roleName = this.formatRoleLabel(this.user.role || '')
      this.form.createTime = this.user.createTime || ''
      this.userInfo.nickname = this.user.nickname || '管理员'
      this.userInfo.role = this.user.role || ''
      this.userInfo.createTime = this.user.createTime || ''
    },
    formatRoleLabel(roleName) {
      if (!roleName) return '-'
      if (roleName === 'admin') return '管理员'
      if (roleName === 'user') return '普通账号'
      return roleName
    },
    async fetchLoginLogs() {
      this.logLoading = true
      try {
        const res = await getLoginLogs()
        if (res.code === 0) {
          this.loginLogs = Array.isArray(res.data) ? res.data : []
        }
      } catch (error) {
        console.error('Failed to fetch login logs:', error)
      } finally {
        this.logLoading = false
      }
    },
    resetForm() {
      this.syncUserInfo()
      this.$nextTick(() => {
        this.$refs.formRef?.clearValidate()
      })
    },
    async handleSave() {
      const valid = await this.$refs.formRef.validate().catch(() => false)
      if (!valid) {
        return
      }

      try {
        this.submitting = true
        const res = await updateProfile({
          nickname: this.form.nickname,
          phone: this.form.phone
        })
        if (res.code === 0) {
          this.$message.success('信息更新成功')
          this.$store.commit('SET_USER', {
            ...this.user,
            nickname: this.form.nickname,
            phone: this.form.phone
          })
          this.userInfo.nickname = this.form.nickname
          this.syncUserInfo()
        } else {
          this.$message.error(res.message || '信息更新失败')
        }
      } catch (error) {
        console.error('Update profile failed:', error)
        this.$message.error('信息更新失败')
      } finally {
        this.submitting = false
      }
    }
  }
}
</script>

<style lang="scss" scoped>
.profile-layout {
  display: grid;
  grid-template-columns: minmax(280px, 360px) minmax(0, 1fr);
  gap: 18px;
}

.profile-overview {
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  gap: 22px;
  min-height: 100%;
}

.profile-overview-head {
  display: flex;
  align-items: center;
  gap: 18px;

  h3 {
    margin-bottom: 10px;
    font-size: 24px;
    color: #22324d;
  }
}

.profile-overview-meta {
  min-width: 0;
}

.profile-chip-row {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

.profile-chip {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-height: 30px;
  padding: 0 12px;
  border-radius: 999px;
  font-size: 12px;
  border: 1px solid transparent;

  &.account {
    background: rgba(47, 109, 246, 0.1);
    border-color: rgba(47, 109, 246, 0.16);
    color: #2f6df6;
  }

  &.role {
    background: rgba(23, 63, 151, 0.08);
    border-color: rgba(23, 63, 151, 0.12);
    color: #173f97;
  }
}

.profile-summary-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 12px;
}

.summary-item {
  padding: 16px 16px 14px;
  border-radius: 18px;
  background: linear-gradient(180deg, #ffffff 0%, #f7faff 100%);
  border: 1px solid rgba(219, 228, 240, 0.85);

  strong {
    display: block;
    margin-top: 8px;
    color: #23324b;
    line-height: 1.6;
    word-break: break-all;
  }
}

.summary-label {
  color: #8090a8;
  font-size: 12px;
}

.profile-note {
  display: flex;
  align-items: flex-start;
  gap: 10px;
  padding: 14px 16px;
  border-radius: 18px;
  background: #f5f8ff;
  color: #61748d;
  line-height: 1.7;

  i {
    margin-top: 2px;
    color: #2f6df6;
  }
}

.profile-editor {
  min-height: 420px;
}

.profile-form {
  margin-top: 10px;
}

.form-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 0 18px;
}

.full-row {
  grid-column: 1 / -1;
}

.form-actions {
  margin-top: 10px;
  margin-bottom: 0;
}

.profile-log-card {
  margin-top: 18px;
}

@media (max-width: 1080px) {
  .profile-layout {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 720px) {
  .profile-overview-head {
    align-items: flex-start;
    flex-direction: column;
  }

  .profile-summary-grid,
  .form-grid {
    grid-template-columns: 1fr;
  }

  .full-row {
    grid-column: auto;
  }
}
</style>
