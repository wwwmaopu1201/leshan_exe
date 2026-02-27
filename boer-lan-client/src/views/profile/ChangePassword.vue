<template>
  <div class="page-container">
    <div class="password-card">
      <h3>{{ $t('menu.changePassword') }}</h3>
      <el-form ref="formRef" :model="form" :rules="rules" label-width="100px">
        <el-form-item :label="$t('profile.oldPassword')" prop="oldPassword">
          <el-input
            v-model="form.oldPassword"
            type="password"
            show-password
            placeholder="请输入原密码"
          />
        </el-form-item>
        <el-form-item :label="$t('profile.newPassword')" prop="newPassword">
          <el-input
            v-model="form.newPassword"
            type="password"
            show-password
            placeholder="请输入新密码"
          />
        </el-form-item>
        <el-form-item :label="$t('profile.confirmPassword')" prop="confirmPassword">
          <el-input
            v-model="form.confirmPassword"
            type="password"
            show-password
            placeholder="请再次输入新密码"
          />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" @click="handleSubmit">
            {{ $t('common.confirm') }}
          </el-button>
          <el-button @click="handleReset">
            {{ $t('common.reset') }}
          </el-button>
        </el-form-item>
      </el-form>

      <div class="password-tips">
        <h4>密码要求:</h4>
        <ul>
          <li>密码长度至少8位</li>
          <li>必须包含大小写字母和数字</li>
          <li>可以包含特殊字符(!@#$%^&*)</li>
          <li>新密码不能与原密码相同</li>
        </ul>
      </div>
    </div>
  </div>
</template>

<script>
import { changePassword } from '@/api/auth'

export default {
  name: 'ChangePassword',
  data() {
    const validateConfirm = (rule, value, callback) => {
      if (value !== this.form.newPassword) {
        callback(new Error(this.$t('profile.passwordMismatch')))
      } else {
        callback()
      }
    }

    const validateNewPassword = (rule, value, callback) => {
      if (value === this.form.oldPassword) {
        callback(new Error('新密码不能与原密码相同'))
      } else if (!/^(?=.*[a-z])(?=.*[A-Z])(?=.*\d).{8,}$/.test(value)) {
        callback(new Error('密码必须包含大小写字母和数字，且长度至少8位'))
      } else {
        callback()
      }
    }

    return {
      form: {
        oldPassword: '',
        newPassword: '',
        confirmPassword: ''
      },
      rules: {
        oldPassword: [
          { required: true, message: '请输入原密码', trigger: 'blur' }
        ],
        newPassword: [
          { required: true, message: '请输入新密码', trigger: 'blur' },
          { validator: validateNewPassword, trigger: 'blur' }
        ],
        confirmPassword: [
          { required: true, message: '请再次输入新密码', trigger: 'blur' },
          { validator: validateConfirm, trigger: 'blur' }
        ]
      }
    }
  },
  methods: {
    async handleSubmit() {
      try {
        await this.$refs.formRef.validate()
        const res = await changePassword({
          oldPassword: this.form.oldPassword,
          newPassword: this.form.newPassword
        })
        if (res.code === 0) {
          this.$message.success(this.$t('profile.passwordUpdated'))
          this.handleReset()
        } else {
          this.$message.error(res.message || '密码修改失败')
        }
      } catch (error) {
        console.error('Change password failed:', error)
        this.$message.error('密码修改失败，请检查原密码是否正确')
      }
    },
    handleReset() {
      this.$refs.formRef.resetFields()
    }
  }
}
</script>

<style lang="scss" scoped>
.password-card {
  background: #fff;
  border-radius: 8px;
  padding: 30px;
  max-width: 500px;

  h3 {
    margin-bottom: 30px;
    color: #303133;
  }
}

.password-tips {
  margin-top: 30px;
  padding: 20px;
  background: #f5f7fa;
  border-radius: 8px;

  h4 {
    margin: 0 0 10px;
    color: #606266;
    font-size: 14px;
  }

  ul {
    margin: 0;
    padding-left: 20px;
    color: #909399;
    font-size: 13px;

    li {
      margin-bottom: 5px;
    }
  }
}
</style>
