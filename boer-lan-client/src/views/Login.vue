<template>
  <div class="login-container">
    <div class="login-bg">
      <div class="bg-orb orb-a"></div>
      <div class="bg-orb orb-b"></div>
      <div class="bg-grid"></div>
    </div>

    <div class="login-shell">
      <section class="login-showcase">
        <div class="brand-chip">
          <img src="@/assets/images/logo.png" alt="Logo" class="brand-logo" />
          <div class="brand-copy">
            <span class="brand-name">BOER LAN</span>
            <span class="brand-caption">工业绣花设备联网管理</span>
          </div>
        </div>

        <div class="showcase-copy">
          <h1>{{ $t('login.title') }}</h1>
          <p>{{ $t('login.subtitle') }}</p>
        </div>

        <div class="feature-list">
          <div class="feature-card">
            <i class="el-icon-monitor"></i>
            <div>
              <strong>设备一屏总览</strong>
              <span>状态、生产、报警数据统一呈现</span>
            </div>
          </div>
          <div class="feature-card">
            <i class="el-icon-folder-opened"></i>
            <div>
              <strong>花型统一管理</strong>
              <span>上传、回传、下发进度清晰可查</span>
            </div>
          </div>
          <div class="feature-card">
            <i class="el-icon-data-analysis"></i>
            <div>
              <strong>统计闭环追踪</strong>
              <span>工资、效率、时长和报警联动分析</span>
            </div>
          </div>
        </div>
      </section>

      <section class="login-panel">
        <div class="panel-toolbar">
          <div class="toolbar-title">客户端登录</div>
          <el-select v-model="currentLang" size="mini" class="lang-select" @change="changeLang">
            <el-option label="中文" value="zh-CN" />
            <el-option label="English" value="en-US" />
          </el-select>
        </div>

        <div class="panel-head">
          <img src="@/assets/images/logo.png" alt="Logo" class="panel-logo" />
          <h2>连接服务器</h2>
          <p>请输入服务端地址和账号信息</p>
        </div>

        <el-form
          ref="loginForm"
          :model="loginForm"
          :rules="loginRules"
          class="login-form"
          @submit.native.prevent="handleLogin"
        >
          <div class="server-row">
            <el-form-item prop="serverIp" class="server-field ip-field">
              <label class="field-label">{{ $t('login.serverIp') }}</label>
              <el-input
                v-model="loginForm.serverIp"
                :placeholder="$t('login.serverIp')"
                prefix-icon="el-icon-link"
              />
            </el-form-item>

            <el-form-item prop="port" class="server-field port-field">
              <label class="field-label">{{ $t('login.port') }}</label>
              <el-input
                v-model="loginForm.port"
                :placeholder="$t('login.port')"
                prefix-icon="el-icon-connection"
              />
            </el-form-item>
          </div>

          <el-form-item prop="username">
            <label class="field-label">{{ $t('login.username') }}</label>
            <el-input
              v-model="loginForm.username"
              :placeholder="$t('login.username')"
              prefix-icon="el-icon-user"
            />
          </el-form-item>

          <el-form-item prop="password">
            <label class="field-label">{{ $t('login.password') }}</label>
            <el-input
              v-model="loginForm.password"
              type="password"
              :placeholder="$t('login.password')"
              prefix-icon="el-icon-lock"
              show-password
              @keyup.enter.native="handleLogin"
            />
          </el-form-item>

          <div class="form-actions">
            <el-checkbox v-model="loginForm.remember">
              {{ $t('login.rememberPassword') }}
            </el-checkbox>
            <span class="port-tip">默认管理端口 {{ loginForm.port || '8088' }}</span>
          </div>

          <el-button
            type="primary"
            :loading="loading"
            class="login-btn"
            @click="handleLogin"
          >
            {{ loading ? $t('login.connecting') : $t('login.login') }}
          </el-button>
        </el-form>
      </section>
    </div>
  </div>
</template>

<script>
import { mapActions } from 'vuex'
import { login } from '@/api/auth'

export default {
  name: 'Login',
  data() {
    const validateServerIp = (rule, value, callback) => {
      const ip = String(value || '').trim()
      if (!ip) {
        callback(new Error(this.$t('login.serverIpRequired')))
        return
      }
      callback()
    }

    const validatePort = (rule, value, callback) => {
      const port = String(value || '').trim()
      const portNum = Number(port)
      if (!port || Number.isNaN(portNum) || !Number.isInteger(portNum) || portNum < 1 || portNum > 65535) {
        callback(new Error('端口号需为1-65535的整数'))
        return
      }
      callback()
    }

    return {
      loginForm: {
        serverIp: localStorage.getItem('serverIp') || '',
        port: localStorage.getItem('serverPort') || '8088',
        username: localStorage.getItem('rememberedUsername') || '',
        password: localStorage.getItem('rememberedPassword') || '',
        remember: !!localStorage.getItem('rememberedUsername') && !!localStorage.getItem('rememberedPassword')
      },
      loginRules: {
        serverIp: [{ validator: validateServerIp, trigger: 'blur' }],
        port: [{ validator: validatePort, trigger: 'blur' }],
        username: [{ required: true, message: '请输入账号', trigger: 'blur' }],
        password: [{ required: true, message: '请输入密码', trigger: 'blur' }]
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
      } catch (error) {
        return
      }

      this.loading = true
      this.loginForm.serverIp = String(this.loginForm.serverIp || '').trim()
      this.loginForm.port = String(this.loginForm.port || '').trim()
      this.loginForm.username = String(this.loginForm.username || '').trim()

      this.updateServerConfig({
        ip: this.loginForm.serverIp,
        port: this.loginForm.port
      })

      try {
        const res = await login({
          username: this.loginForm.username,
          password: this.loginForm.password
        })

        if (res.data.user && res.data.user.disabled) {
          this.$message.error('您的账号已被禁用，请联系管理员')
          this.loading = false
          return
        }

        if (this.loginForm.remember) {
          localStorage.setItem('rememberedUsername', this.loginForm.username)
          localStorage.setItem('rememberedPassword', this.loginForm.password)
        } else {
          localStorage.removeItem('rememberedUsername')
          localStorage.removeItem('rememberedPassword')
        }

        this.login({
          token: res.data.token,
          user: res.data.user
        })

        this.$message.success(this.$t('login.loginSuccess'))
        this.$router.push('/home')
      } catch (error) {
        const message = error.userMessage || error.response?.data?.message || this.$t('login.loginFailed')
        this.$message.error(message)
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
  position: relative;
  width: 100%;
  min-height: 100vh;
  overflow: hidden;
  background: linear-gradient(135deg, #07162f 0%, #0f2750 52%, #0a5caa 100%);
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 32px;
}

.login-bg {
  position: absolute;
  inset: 0;
  overflow: hidden;
}

.bg-orb {
  position: absolute;
  border-radius: 50%;
  filter: blur(10px);
  opacity: 0.7;
}

.orb-a {
  width: 420px;
  height: 420px;
  left: -100px;
  top: -80px;
  background: radial-gradient(circle, rgba(78, 198, 255, 0.42) 0%, rgba(78, 198, 255, 0) 68%);
}

.orb-b {
  width: 520px;
  height: 520px;
  right: -180px;
  bottom: -140px;
  background: radial-gradient(circle, rgba(84, 122, 255, 0.36) 0%, rgba(84, 122, 255, 0) 70%);
}

.bg-grid {
  position: absolute;
  inset: 0;
  background-image:
    linear-gradient(rgba(255, 255, 255, 0.06) 1px, transparent 1px),
    linear-gradient(90deg, rgba(255, 255, 255, 0.06) 1px, transparent 1px);
  background-size: 48px 48px;
  mask-image: linear-gradient(180deg, rgba(0, 0, 0, 0.4), transparent 90%);
}

.login-shell {
  position: relative;
  z-index: 1;
  width: min(1120px, 100%);
  min-height: 680px;
  display: grid;
  grid-template-columns: 1.08fr 0.92fr;
  border-radius: 32px;
  overflow: hidden;
  background: rgba(255, 255, 255, 0.08);
  border: 1px solid rgba(255, 255, 255, 0.15);
  box-shadow: 0 30px 70px rgba(2, 12, 31, 0.35);
  backdrop-filter: blur(16px);
}

.login-showcase {
  padding: 48px 46px;
  background:
    linear-gradient(180deg, rgba(8, 25, 53, 0.82) 0%, rgba(11, 48, 93, 0.58) 100%),
    radial-gradient(circle at top left, rgba(84, 189, 255, 0.18), transparent 34%);
  color: #ffffff;
  display: flex;
  flex-direction: column;
}

.brand-chip {
  display: inline-flex;
  align-items: center;
  gap: 12px;
  padding: 10px 14px;
  width: fit-content;
  border-radius: 20px;
  background: rgba(255, 255, 255, 0.08);
  margin-bottom: 56px;
}

.brand-logo {
  width: 42px;
  height: 42px;
  border-radius: 12px;
}

.brand-copy {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.brand-name {
  font-size: 13px;
  letter-spacing: 0.14em;
  font-weight: 700;
}

.brand-caption {
  font-size: 12px;
  color: rgba(227, 237, 255, 0.78);
}

.showcase-copy {
  max-width: 420px;
  margin-bottom: 48px;

  h1 {
    font-size: 42px;
    line-height: 1.18;
    margin-bottom: 16px;
  }

  p {
    font-size: 16px;
    line-height: 1.85;
    color: rgba(226, 236, 255, 0.82);
  }
}

.feature-list {
  display: grid;
  gap: 16px;
  margin-top: auto;
}

.feature-card {
  display: flex;
  align-items: flex-start;
  gap: 14px;
  padding: 18px 20px;
  border-radius: 22px;
  background: rgba(255, 255, 255, 0.08);

  i {
    width: 42px;
    height: 42px;
    border-radius: 14px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    background: rgba(87, 183, 255, 0.16);
    color: #90dbff;
    font-size: 20px;
  }

  strong {
    display: block;
    margin-bottom: 6px;
    font-size: 15px;
  }

  span {
    display: block;
    color: rgba(229, 238, 255, 0.74);
    line-height: 1.6;
    font-size: 13px;
  }
}

.login-panel {
  background: rgba(255, 255, 255, 0.96);
  padding: 28px 42px 34px;
  display: flex;
  flex-direction: column;
}

.panel-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 42px;
}

.toolbar-title {
  color: #7f91aa;
  font-size: 13px;
  letter-spacing: 0.08em;
}

.lang-select {
  width: 110px;
}

.panel-head {
  margin-bottom: 28px;

  .panel-logo {
    width: 60px;
    height: 60px;
    border-radius: 18px;
    box-shadow: 0 14px 26px rgba(40, 102, 214, 0.18);
    margin-bottom: 18px;
  }

  h2 {
    font-size: 28px;
    color: #1d3662;
    margin-bottom: 10px;
  }

  p {
    color: #7d8ca4;
    font-size: 14px;
  }
}

.login-form {
  display: flex;
  flex-direction: column;
  gap: 18px;
  flex: 1;

  .el-form-item {
    margin-bottom: 0;
  }
}

.server-row {
  display: grid;
  grid-template-columns: minmax(0, 1fr) 172px;
  gap: 14px;
}

.server-field {
  margin-bottom: 0;
}

.field-label {
  display: block;
  margin-bottom: 10px;
  color: #50627d;
  font-size: 13px;
  font-weight: 600;
}

.form-actions {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  color: #7d8ca4;
  font-size: 12px;
}

.port-tip {
  white-space: nowrap;
}

.login-btn {
  width: 100%;
  height: 50px;
  margin-top: auto;
  border-radius: 16px;
  font-size: 15px;
  font-weight: 700;
}

::v-deep .el-input__inner {
  height: 48px;
  line-height: 48px;
  border-radius: 15px;
  border-color: #d7e0ee;
  background: #f9fbff;
}

::v-deep .el-input__prefix {
  display: flex;
  align-items: center;
}

@media (max-width: 980px) {
  .login-container {
    padding: 16px;
  }

  .login-shell {
    grid-template-columns: 1fr;
    min-height: auto;
  }

  .login-showcase {
    padding: 34px 28px;
  }

  .login-panel {
    padding: 24px 24px 28px;
  }
}

@media (max-width: 640px) {
  .server-row {
    grid-template-columns: 1fr;
  }

  .showcase-copy h1 {
    font-size: 30px;
  }

  .form-actions {
    flex-direction: column;
    align-items: flex-start;
  }
}
</style>
