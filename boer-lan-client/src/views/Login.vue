<template>
  <div class="login-container">
    <div class="login-bg"></div>
    <div class="login-box">
      <div class="login-header">
        <img src="@/assets/images/logo.png" alt="Logo" class="logo" />
        <h1 class="title">{{ $t('login.title') }}</h1>
        <p class="subtitle">{{ $t('login.subtitle') }}</p>
      </div>

      <el-form
        ref="loginForm"
        :model="loginForm"
        :rules="loginRules"
        class="login-form"
        @submit.native.prevent="handleLogin"
      >
        <el-form-item prop="serverIp">
          <el-input
            v-model="loginForm.serverIp"
            :placeholder="$t('login.serverIp')"
            prefix-icon="el-icon-link"
          >
            <template slot="append">
              <el-input
                v-model="loginForm.port"
                :placeholder="$t('login.port')"
                style="width: 80px"
              />
            </template>
          </el-input>
        </el-form-item>

        <el-form-item prop="username">
          <el-input
            v-model="loginForm.username"
            :placeholder="$t('login.username')"
            prefix-icon="el-icon-user"
          />
        </el-form-item>

        <el-form-item prop="password">
          <el-input
            v-model="loginForm.password"
            type="password"
            :placeholder="$t('login.password')"
            prefix-icon="el-icon-lock"
            show-password
            @keyup.enter.native="handleLogin"
          />
        </el-form-item>

        <el-form-item>
          <el-checkbox v-model="loginForm.remember">
            {{ $t('login.rememberPassword') }}
          </el-checkbox>
        </el-form-item>

        <el-form-item>
          <el-button
            type="primary"
            :loading="loading"
            class="login-btn"
            @click="handleLogin"
          >
            {{ loading ? $t('login.connecting') : $t('login.login') }}
          </el-button>
        </el-form-item>
      </el-form>

      <div class="login-footer">
        <el-select v-model="currentLang" size="small" @change="changeLang">
          <el-option label="中文" value="zh-CN" />
          <el-option label="English" value="en-US" />
        </el-select>
      </div>
    </div>
  </div>
</template>

<script>
import { mapActions } from 'vuex'
import { login } from '@/api/auth'

export default {
  name: 'Login',
  data() {
    return {
      loginForm: {
        serverIp: localStorage.getItem('serverIp') || '127.0.0.1',
        port: localStorage.getItem('serverPort') || '8088',
        username: localStorage.getItem('rememberedUsername') || 'admin',
        password: localStorage.getItem('rememberedPassword') || 'admin123',
        remember: !!localStorage.getItem('rememberedUsername')
      },
      loginRules: {
        serverIp: [
          { required: false }
        ],
        username: [
          { required: true, message: '请输入账号', trigger: 'blur' }
        ],
        password: [
          { required: true, message: '请输入密码', trigger: 'blur' }
        ]
      },
      loading: false,
      currentLang: localStorage.getItem('language') || 'zh-CN'
    }
  },
  methods: {
    ...mapActions(['login', 'updateServerConfig']),
    async handleLogin() {
      try {
        await this.$refs.loginForm.validate()
      } catch (e) {
        return
      }

      this.loading = true

      // 保存服务器配置
      this.updateServerConfig({
        ip: this.loginForm.serverIp,
        port: this.loginForm.port
      })

      try {
        const res = await login({
          username: this.loginForm.username,
          password: this.loginForm.password
        })

        // 记住密码
        if (this.loginForm.remember) {
          localStorage.setItem('rememberedUsername', this.loginForm.username)
          localStorage.setItem('rememberedPassword', this.loginForm.password)
        } else {
          localStorage.removeItem('rememberedUsername')
          localStorage.removeItem('rememberedPassword')
        }

        // 登录成功
        this.login({
          token: res.data.token,
          user: res.data.user
        })

        this.$message.success(this.$t('login.loginSuccess'))
        this.$router.push('/home')
      } catch (error) {
        this.$message.error(error.message || this.$t('login.loginFailed'))
      } finally {
        this.loading = false
      }
    },
    changeLang(lang) {
      this.$i18n.locale = lang
      localStorage.setItem('language', lang)
      this.$store.commit('SET_LANGUAGE', lang)
    }
  }
}
</script>

<style lang="scss" scoped>
.login-container {
  width: 100%;
  height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  position: relative;
  background: linear-gradient(135deg, #1e3c72 0%, #2a5298 50%, #3b9dfc 100%);
}

.login-bg {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background: radial-gradient(ellipse at top left, rgba(255,255,255,0.15) 0%, transparent 50%),
              radial-gradient(ellipse at bottom right, rgba(255,255,255,0.1) 0%, transparent 50%);
  pointer-events: none;
}

.login-box {
  width: 420px;
  padding: 40px;
  background: rgba(255, 255, 255, 0.95);
  border-radius: 12px;
  box-shadow: 0 20px 60px rgba(0, 0, 0, 0.3);
  position: relative;
  z-index: 1;
}

.login-header {
  text-align: center;
  margin-bottom: 40px;

  .logo {
    width: 80px;
    height: 80px;
    margin-bottom: 20px;
  }

  .title {
    font-size: 24px;
    font-weight: bold;
    color: #1e3c72;
    margin-bottom: 8px;
  }

  .subtitle {
    font-size: 14px;
    color: #909399;
  }
}

.login-form {
  .el-form-item {
    margin-bottom: 24px;
  }

  .el-input {
    ::v-deep .el-input__inner {
      height: 44px;
      line-height: 44px;
    }

    ::v-deep .el-input-group__append {
      padding: 0;

      .el-input__inner {
        border: none;
        text-align: center;
      }
    }
  }

  .login-btn {
    width: 100%;
    height: 44px;
    font-size: 16px;
  }
}

.login-footer {
  text-align: center;
  margin-top: 20px;

  .el-select {
    width: 120px;
  }
}
</style>
