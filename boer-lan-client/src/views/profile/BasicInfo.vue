<template>
  <div class="page-container">
    <div class="profile-card">
      <div class="profile-header">
        <div class="avatar-section">
          <el-avatar :size="100" icon="el-icon-user-solid" />
          <el-button size="small" class="mt-10">更换头像</el-button>
        </div>
        <div class="info-section">
          <h2>{{ userInfo.nickname }}</h2>
          <p class="role">{{ userInfo.role === 'admin' ? '管理员' : '普通用户' }}</p>
          <p class="join-time">加入时间: {{ userInfo.createTime }}</p>
        </div>
      </div>

      <el-divider />

      <div class="profile-form">
        <el-form ref="formRef" :model="form" :rules="rules" label-width="100px">
          <el-form-item label="用户名">
            <el-input v-model="form.username" disabled />
          </el-form-item>
          <el-form-item :label="$t('profile.nickname')" prop="nickname">
            <el-input v-model="form.nickname" />
          </el-form-item>
          <el-form-item :label="$t('profile.email')" prop="email">
            <el-input v-model="form.email" />
          </el-form-item>
          <el-form-item :label="$t('profile.phone')" prop="phone">
            <el-input v-model="form.phone" />
          </el-form-item>
          <el-form-item label="所属部门">
            <el-input v-model="form.department" disabled />
          </el-form-item>
          <el-form-item>
            <el-button type="primary" @click="handleSave">
              {{ $t('profile.updateInfo') }}
            </el-button>
          </el-form-item>
        </el-form>
      </div>
    </div>

    <div class="profile-card mt-20">
      <h3>登录记录</h3>
      <el-table :data="loginLogs" border size="small">
        <el-table-column prop="time" label="登录时间" width="180" />
        <el-table-column prop="ip" label="IP地址" width="150" />
        <el-table-column prop="device" label="设备" />
        <el-table-column prop="status" label="状态" width="100">
          <template slot-scope="scope">
            <el-tag :type="scope.row.status === '成功' ? 'success' : 'danger'" size="small">
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
        email: 'admin@boer.com',
        phone: '13800138000',
        department: '技术部'
      },
      rules: {
        nickname: [{ required: true, message: '请输入昵称', trigger: 'blur' }],
        email: [
          { required: true, message: '请输入邮箱', trigger: 'blur' },
          { type: 'email', message: '请输入正确的邮箱格式', trigger: 'blur' }
        ],
        phone: [
          { required: true, message: '请输入手机号', trigger: 'blur' },
          { pattern: /^1[3-9]\d{9}$/, message: '请输入正确的手机号', trigger: 'blur' }
        ]
      },
      userInfo: {
        nickname: '管理员',
        role: 'admin',
        createTime: '2024-01-01 10:00:00'
      },
      loginLogs: [],
      loading: false
    }
  },
  computed: {
    ...mapState(['user'])
  },
  mounted() {
    if (this.user) {
      this.form.username = this.user.username || 'admin'
      this.form.nickname = this.user.nickname || '管理员'
      this.form.email = this.user.email || ''
      this.form.phone = this.user.phone || ''
      this.form.department = this.user.department || ''
      this.userInfo.nickname = this.user.nickname || '管理员'
      this.userInfo.role = this.user.role || 'admin'
      this.userInfo.createTime = this.user.createTime || ''
    }
    this.fetchLoginLogs()
  },
  methods: {
    async fetchLoginLogs() {
      try {
        const res = await getLoginLogs()
        if (res.code === 0) {
          this.loginLogs = res.data || []
        }
      } catch (error) {
        console.error('Failed to fetch login logs:', error)
      }
    },
    async handleSave() {
      try {
        await this.$refs.formRef.validate()
        this.loading = true
        const res = await updateProfile({
          nickname: this.form.nickname,
          email: this.form.email,
          phone: this.form.phone
        })
        if (res.code === 0) {
          this.$message.success('信息更新成功')
          // 更新 Vuex 中的用户信息
          this.$store.commit('SET_USER', {
            ...this.user,
            nickname: this.form.nickname,
            email: this.form.email,
            phone: this.form.phone
          })
          this.userInfo.nickname = this.form.nickname
        } else {
          this.$message.error(res.message || '信息更新失败')
        }
      } catch (error) {
        console.error('Update profile failed:', error)
        this.$message.error('信息更新失败')
      } finally {
        this.loading = false
      }
    }
  }
}
</script>

<style lang="scss" scoped>
.profile-card {
  background: #fff;
  border-radius: 8px;
  padding: 30px;

  h3 {
    margin-bottom: 20px;
    color: #303133;
  }
}

.profile-header {
  display: flex;
  align-items: center;

  .avatar-section {
    display: flex;
    flex-direction: column;
    align-items: center;
    margin-right: 40px;
  }

  .info-section {
    h2 {
      font-size: 24px;
      margin: 0 0 10px;
      color: #303133;
    }

    .role {
      color: #409EFF;
      font-size: 14px;
      margin: 0 0 5px;
    }

    .join-time {
      color: #909399;
      font-size: 13px;
      margin: 0;
    }
  }
}

.profile-form {
  max-width: 500px;
}

.mt-10 {
  margin-top: 10px;
}

.mt-20 {
  margin-top: 20px;
}
</style>
