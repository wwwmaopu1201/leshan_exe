<template>
  <div class="page-container password-page">
    <div class="password-shell">
      <div class="password-head">
        <h2 class="password-head__title">个人中心</h2>
        <div class="password-head__tabs">
          <button type="button" class="password-tab" @click="goBasicInfo">
            基础信息
          </button>
          <button type="button" class="password-tab is-active">
            修改密码
          </button>
        </div>
      </div>

      <div class="password-panel">
        <div class="password-avatar">
          <div class="password-avatar__image">
            <el-avatar class="password-avatar-el" :size="88" :src="avatarSrc" fit="cover" />
          </div>
          <label for="change-password-avatar-input" class="password-avatar__badge">
            <i class="el-icon-camera"></i>
          </label>
          <input
            id="change-password-avatar-input"
            ref="avatarInput"
            type="file"
            accept=".png,.jpg,.jpeg,.webp"
            style="display: none;"
            @change="handleAvatarChange"
          >
        </div>

        <div class="password-form-wrap">
          <el-form
            ref="formRef"
            :model="form"
            :rules="rules"
            label-width="84px"
            class="password-form"
          >
            <el-form-item :label="$t('profile.oldPassword')" prop="oldPassword">
              <el-input
                v-model="form.oldPassword"
                type="password"
                show-password
                placeholder="请输入旧密码"
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
                placeholder="请确认新密码"
              />
            </el-form-item>

            <el-form-item class="password-form__actions">
              <el-button type="primary" @click="handleSubmit">
                保存
              </el-button>
              <el-button @click="handleClose">
                关闭
              </el-button>
            </el-form-item>
          </el-form>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import defaultAvatar from '@/assets/images/default-avatar.svg'
import { changePassword, uploadAvatar } from '@/api/auth'

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
      avatarUploading: false,
      form: {
        oldPassword: '',
        newPassword: '',
        confirmPassword: ''
      },
      rules: {
        oldPassword: [
          { required: true, message: '请输入旧密码', trigger: 'blur' }
        ],
        newPassword: [
          { required: true, message: '请输入新密码', trigger: 'blur' },
          { validator: validateNewPassword, trigger: 'blur' }
        ],
        confirmPassword: [
          { required: true, message: '请确认新密码', trigger: 'blur' },
          { validator: validateConfirm, trigger: 'blur' }
        ]
      }
    }
  },
  computed: {
    avatarSrc() {
      const avatar = String(this.$store.state.user?.avatar || '').trim()
      if (!avatar) {
        return defaultAvatar
      }
      if (avatar.startsWith('http://') || avatar.startsWith('https://') || avatar.startsWith('data:')) {
        return avatar
      }
      const ip = String(this.$store.state.serverConfig?.ip || '').trim()
      const port = String(this.$store.state.serverConfig?.port || '').trim()
      if (!ip || !port) {
        return defaultAvatar
      }
      return `http://${ip}:${port}${avatar}?v=${encodeURIComponent(avatar)}`
    }
  },
  methods: {
    goBasicInfo() {
      this.$router.push('/profile/info')
    },
    async handleAvatarChange(event) {
      const file = event?.target?.files?.[0]
      if (!file) {
        return
      }
      if (!/^image\/(png|jpeg|webp)$/.test(file.type)) {
        this.$message.warning('头像仅支持 png/jpg/jpeg/webp 格式')
        return
      }
      if (file.size > 2 * 1024 * 1024) {
        this.$message.warning('头像文件不能超过 2MB')
        return
      }

      const formData = new FormData()
      formData.append('file', file)

      try {
        this.avatarUploading = true
        const res = await uploadAvatar(formData)
        if (res.code === 0) {
          const avatar = res.data?.avatar || ''
          this.$store.commit('SET_USER', {
            ...this.$store.state.user,
            avatar
          })
          this.$message.success('头像更新成功')
        } else {
          this.$message.error(res.message || '头像更新失败')
        }
      } catch (error) {
        console.error('Upload avatar failed:', error)
        this.$message.error('头像更新失败')
      } finally {
        this.avatarUploading = false
        if (this.$refs.avatarInput) {
          this.$refs.avatarInput.value = ''
        }
      }
    },
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
      this.$refs.formRef?.resetFields()
    },
    handleClose() {
      this.goBasicInfo()
    }
  }
}
</script>

<style lang="scss" scoped>
.password-page {
  background: #ffffff;
}

.password-shell {
  min-height: 100%;
  padding: 12px 16px 20px;
}

.password-head {
  margin-bottom: 26px;
}

.password-head__title {
  position: relative;
  margin: 0 0 16px;
  padding-left: 8px;
  color: #404854;
  font-size: 14px;
  font-weight: 700;
  line-height: 1;
}

.password-head__title::before {
  content: '';
  position: absolute;
  left: 0;
  top: 0;
  width: 2px;
  height: 14px;
  border-radius: 999px;
  background: #2ca7ff;
}

.password-head__tabs {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 12px;
}

.password-tab {
  padding: 0;
  border: none;
  background: transparent;
  color: #9099a5;
  font-size: 12px;
  cursor: pointer;
}

.password-tab.is-active {
  min-width: 74px;
  height: 24px;
  padding: 0 14px;
  border-radius: 999px;
  background: #eef4ff;
  color: #5a8bff;
  font-weight: 600;
}

.password-panel {
  width: 350px;
}

.password-avatar {
  position: relative;
  width: 92px;
  margin: 0 0 34px 24px;
}

.password-avatar__image {
  width: 88px;
  height: 88px;
  border-radius: 50%;
  overflow: hidden;
  box-shadow: 0 6px 16px rgba(56, 162, 255, 0.18);
}

.password-avatar__image ::v-deep .el-avatar {
  width: 88px;
  height: 88px;
  background: #eef3f8;
}

.password-avatar-el ::v-deep img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  object-position: center;
  display: block;
}

.password-avatar__badge {
  position: absolute;
  right: -2px;
  bottom: -2px;
  width: 22px;
  height: 22px;
  border-radius: 50%;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  background: #f2f7ff;
  border: 1px solid #dbe8ff;
  color: #6ca8ff;
  font-size: 12px;
  cursor: pointer;
}

.password-form-wrap {
  width: 320px;
}

.password-form ::v-deep .el-form-item {
  margin-bottom: 14px;
}

.password-form ::v-deep .el-form-item__label {
  color: #626d7d;
  font-size: 12px;
  font-weight: 600;
}

.password-form ::v-deep .el-input__inner {
  height: 30px;
  line-height: 30px;
  border: 1px solid #eef2f7;
  background: #f5f7fa;
  color: #606b7b;
  font-size: 12px;
}

.password-form ::v-deep .el-input__inner::placeholder {
  color: #b1bac8;
}

.password-form__actions ::v-deep .el-form-item__content {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-top: 22px;
}

.password-form__actions ::v-deep .el-button {
  min-width: 56px;
  height: 28px;
  padding: 0 16px;
  font-size: 12px;
}

@media (max-width: 768px) {
  .password-shell {
    padding: 8px;
  }

  .password-panel,
  .password-form-wrap {
    width: 100%;
  }

  .password-head {
    margin-bottom: 18px;
  }

  .password-avatar {
    margin-left: 0;
  }

  .password-form__actions ::v-deep .el-form-item__content {
    flex-wrap: wrap;
    margin-left: 0 !important;
  }
}
</style>
