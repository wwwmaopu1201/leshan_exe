<template>
  <div class="login-shell">
    <div class="login-background"></div>

    <div class="login-panel">
      <section class="login-brand">
        <div class="login-brand__badge">SERVER</div>
        <h1>博尔局域网服务器管理软件</h1>
        <p>统一管理设备接入、账号权限、外部数据库同步和现场调试信息。</p>

        <div class="login-brand__highlights">
          <div class="highlight-card">
            <i class="el-icon-monitor"></i>
            <div>
              <strong>设备接入状态</strong>
              <span>集中查看设备上线、TCP 接入与调试日志。</span>
            </div>
          </div>
          <div class="highlight-card">
            <i class="el-icon-s-check"></i>
            <div>
              <strong>角色账号协同</strong>
              <span>针对不同工厂快速配置角色、账号和可见分组。</span>
            </div>
          </div>
          <div class="highlight-card">
            <i class="el-icon-connection"></i>
            <div>
              <strong>外部数据库同步</strong>
              <span>支持数据库连接测试、同步状态查看与手动触发。</span>
            </div>
          </div>
        </div>
      </section>

      <section class="login-card">
        <div class="login-card__header">
          <div>
            <h2>后台登录</h2>
            <p>请输入服务端管理账号和密码。</p>
          </div>
          <div class="login-card__tag">本机后台</div>
        </div>

        <el-form ref="loginFormRef" :model="loginForm" :rules="rules" class="login-form">
          <el-form-item label="账号" prop="username">
            <el-input
              v-model.trim="loginForm.username"
              placeholder="请输入账号"
              prefix-icon="el-icon-user"
              @keyup.enter.native="login"
            />
          </el-form-item>
          <el-form-item label="密码" prop="password">
            <el-input
              v-model="loginForm.password"
              type="password"
              show-password
              placeholder="请输入密码"
              prefix-icon="el-icon-lock"
              @keyup.enter.native="login"
            />
          </el-form-item>

          <div class="login-card__note">
            <i class="el-icon-info"></i>
            <span>登录后可直接进入本机服务端管理后台，不需要额外配置服务器地址。</span>
          </div>

          <el-button
            type="primary"
            class="login-submit"
            :loading="loading"
            @click="login"
          >
            登录系统
          </el-button>
        </el-form>
      </section>
    </div>
  </div>
</template>

<script>
export default {
  name: 'Login',
  data() {
    return {
      loading: false,
      loginForm: {
        username: '',
        password: ''
      },
      rules: {
        username: [{ required: true, message: '请输入账号', trigger: 'blur' }],
        password: [{ required: true, message: '请输入密码', trigger: 'blur' }]
      }
    }
  },
  methods: {
    async login() {
      const valid = await this.$refs.loginFormRef.validate().catch(() => false)
      if (!valid) {
        return
      }

      this.loading = true
      try {
        const res = await this.$axios.post('/auth/login', this.loginForm)
        if (res.code === 0) {
          localStorage.setItem('token', res.data.token)
          this.$message.success('登录成功')
          this.$router.push('/home')
        }
      } catch (error) {
        console.error('登录失败', error)
      } finally {
        this.loading = false
      }
    }
  }
}
</script>

<style lang="scss" scoped>
.login-shell {
  position: relative;
  min-height: 100%;
  overflow: hidden;
  background:
    radial-gradient(circle at 14% 18%, rgba(36, 108, 248, 0.18), transparent 30%),
    radial-gradient(circle at 88% 22%, rgba(29, 138, 102, 0.12), transparent 24%),
    linear-gradient(135deg, #f8fbff 0%, #ebf2fb 48%, #f4f8ff 100%);
}

.login-background {
  position: absolute;
  inset: 0;
  background:
    linear-gradient(120deg, rgba(15, 32, 66, 0.03) 0%, rgba(15, 32, 66, 0) 42%),
    radial-gradient(circle at bottom right, rgba(47, 109, 246, 0.08), transparent 28%);
}

.login-panel {
  position: relative;
  z-index: 1;
  min-height: 100%;
  padding: 44px;
  display: grid;
  grid-template-columns: minmax(0, 1.05fr) minmax(360px, 480px);
  gap: 24px;
}

.login-brand,
.login-card {
  border-radius: 30px;
  border: 1px solid rgba(221, 229, 240, 0.92);
  box-shadow: 0 24px 48px rgba(59, 87, 132, 0.1);
}

.login-brand {
  padding: 42px;
  color: #ffffff;
  background:
    radial-gradient(circle at top left, rgba(255, 255, 255, 0.18), transparent 30%),
    linear-gradient(150deg, #0f2042 0%, #0d4f96 58%, #0f7e96 100%);

  h1 {
    margin: 22px 0 12px;
    font-size: 38px;
    line-height: 1.24;
  }

  p {
    max-width: 560px;
    margin: 0;
    color: rgba(230, 240, 255, 0.88);
    font-size: 15px;
    line-height: 1.8;
  }
}

.login-brand__badge {
  width: fit-content;
  min-height: 30px;
  padding: 0 12px;
  border-radius: 999px;
  background: rgba(255, 255, 255, 0.14);
  display: inline-flex;
  align-items: center;
  justify-content: center;
  letter-spacing: 0.08em;
  font-size: 12px;
}

.login-brand__highlights {
  margin-top: 28px;
  display: grid;
  gap: 14px;
}

.highlight-card {
  padding: 18px 20px;
  border-radius: 22px;
  display: flex;
  align-items: flex-start;
  gap: 14px;
  background: rgba(255, 255, 255, 0.1);
  border: 1px solid rgba(255, 255, 255, 0.1);

  i {
    width: 40px;
    height: 40px;
    border-radius: 14px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    background: rgba(255, 255, 255, 0.16);
    font-size: 18px;
  }

  strong {
    display: block;
    margin-bottom: 6px;
    font-size: 15px;
  }

  span {
    color: rgba(230, 240, 255, 0.84);
    line-height: 1.7;
    font-size: 13px;
  }
}

.login-card {
  padding: 32px 30px;
  align-self: center;
  background: rgba(255, 255, 255, 0.96);
  backdrop-filter: blur(14px);
}

.login-card__header {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 24px;

  h2 {
    margin: 0;
    font-size: 28px;
    color: #22324d;
  }

  p {
    margin: 8px 0 0;
    color: #8393a9;
    line-height: 1.7;
  }
}

.login-card__tag {
  min-height: 30px;
  padding: 0 12px;
  border-radius: 999px;
  background: rgba(47, 109, 246, 0.1);
  border: 1px solid rgba(47, 109, 246, 0.16);
  color: #2f6df6;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  font-size: 12px;
}

.login-form {
  ::v-deep .el-form-item {
    margin-bottom: 20px;
  }

  ::v-deep .el-input__inner {
    height: 46px;
    border-radius: 14px;
  }
}

.login-card__note {
  min-height: 44px;
  padding: 12px 14px;
  border-radius: 16px;
  background: #f5f8ff;
  display: flex;
  align-items: flex-start;
  gap: 10px;
  color: #5f728c;
  line-height: 1.7;
  margin-bottom: 22px;

  i {
    margin-top: 2px;
    color: #2f6df6;
  }
}

.login-submit {
  width: 100%;
  min-height: 46px;
  border-radius: 16px;
  font-size: 15px;
}

@media (max-width: 1080px) {
  .login-panel {
    grid-template-columns: 1fr;
    padding: 28px;
  }
}

@media (max-width: 640px) {
  .login-panel {
    padding: 18px;
  }

  .login-brand,
  .login-card {
    padding: 24px 20px;
  }
}
</style>
