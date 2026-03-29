<template>
  <div class="page-container">
    <div class="password-layout card">
      <div class="password-form-wrap">
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
      } else if (!value || value.length < 6 || value.length > 32) {
        callback(new Error('密码长度需在6-32位'))
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
          this.$message.success('密码修改成功，请重新登录')
          this.handleReset()
          setTimeout(() => {
            this.$store.dispatch('logout')
            this.$router.replace('/login')
          }, 500)
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
.password-layout {
  min-height: 520px;
}

.password-form-wrap {
  max-width: 520px;
}

@media (max-width: 980px) {
  .password-layout {
    min-height: auto;
  }

  .password-form-wrap {
    max-width: 100%;
  }
}
</style>
