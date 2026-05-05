<template>
  <div class="login-shell">
    <div class="login-background" :style="backgroundStyle">
      <span v-for="n in 7" :key="`scan-${n}`" class="scan-line" :style="{ left: `${10 + n * 12}%` }"></span>
      <span v-for="n in 10" :key="`spark-${n}`" class="light-dot" :style="sparkStyle(n)"></span>
    </div>

    <div class="login-topbar">
      <div class="brand-mark">
        <span class="brand-mark__bohr">Bohr</span>
        <span>{{ $t('login.title') }}</span>
      </div>
      <el-dropdown class="lang-dropdown" trigger="click" @command="changeLang">
        <div class="lang-chip">
          <i class="el-icon-world"></i>
          <span>{{ currentLanguageLabel }}</span>
          <i class="el-icon-arrow-down"></i>
        </div>
        <el-dropdown-menu slot="dropdown">
          <el-dropdown-item command="zh-CN">简体中文</el-dropdown-item>
          <el-dropdown-item command="en-US">English</el-dropdown-item>
        </el-dropdown-menu>
      </el-dropdown>
    </div>

    <div class="login-stage">
      <section class="login-visual" aria-hidden="true">
        <div class="scene-card scene-card--a">
          <div class="scene-card__bar"></div>
        </div>
        <div class="scene-card scene-card--b">
          <div class="scene-card__wave"></div>
        </div>
        <div class="scene-card scene-card--c"></div>

        <div class="cube-stack">
          <span class="cube cube--1"></span>
          <span class="cube cube--2"></span>
          <span class="cube cube--3"></span>
          <span class="cube cube--4"></span>
        </div>

        <div class="network-base"></div>
        <span class="node node--1"></span>
        <span class="node node--2"></span>
        <span class="node node--3"></span>
        <span class="node node--4"></span>
        <span class="node node--5"></span>
        <span class="node-link node-link--1"></span>
        <span class="node-link node-link--2"></span>
        <span class="node-link node-link--3"></span>

        <div class="screen-shell">
          <div class="screen-frame">
            <div class="screen-display">
              <span class="display-line display-line--1"></span>
              <span class="display-line display-line--2"></span>
              <span class="display-circle"></span>
              <span class="display-ring display-ring--1"></span>
              <span class="display-ring display-ring--2"></span>
            </div>
          </div>
          <div class="screen-stand"></div>
        </div>

        <div class="operator operator--left">
          <span class="operator-head"></span>
          <span class="operator-body"></span>
          <span class="operator-arm"></span>
          <span class="operator-leg operator-leg--left"></span>
          <span class="operator-leg operator-leg--right"></span>
        </div>

        <div class="operator operator--right">
          <span class="operator-head"></span>
          <span class="operator-body"></span>
          <span class="operator-arm"></span>
          <span class="operator-leg operator-leg--left"></span>
          <span class="operator-leg operator-leg--right"></span>
        </div>

        <div class="mouse-shape"></div>
      </section>

      <section class="login-card">
        <h1 class="login-title">{{ $t('login.title') }}</h1>

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
              :placeholder="$t('login.serverIpPlaceholder')"
              prefix-icon="el-icon-s-promotion"
            />
          </el-form-item>

          <el-form-item prop="serverPort">
            <el-input
              v-model="loginForm.serverPort"
              :placeholder="$t('login.portPlaceholder')"
              prefix-icon="el-icon-connection"
              maxlength="5"
            />
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

          <el-button
            type="primary"
            :loading="loading"
            class="login-submit"
            @click="handleLogin"
          >
            {{ loading ? $t('login.connecting') : $t('login.login') }}
          </el-button>

          <div class="login-actions">
            <el-checkbox v-model="loginForm.remember">
              {{ $t('login.rememberPassword') }}
            </el-checkbox>
          </div>
        </el-form>
      </section>
    </div>

    <div class="login-footer">
      <div>{{ $t('login.companyName') }}</div>
      <div>{{ $t('login.companyNameEn') }}</div>
    </div>
  </div>
</template>

<script>
import { mapActions } from 'vuex'
import { login } from '@/api/auth'
import { getDefaultAccessiblePath } from '@/utils/permission'
import loginBackground from '@/assets/images/login-background-client.png'

const DEFAULT_SERVER_PORT = '8088'

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

    const validateServerPort = (rule, value, callback) => {
      const port = String(value || '').trim()
      if (!port) {
        callback(new Error(this.$t('login.portRequired')))
        return
      }
      if (!/^\d+$/.test(port)) {
        callback(new Error(this.$t('login.portInvalid')))
        return
      }
      const portNumber = Number(port)
      if (!Number.isInteger(portNumber) || portNumber < 1 || portNumber > 65535) {
        callback(new Error(this.$t('login.portInvalid')))
        return
      }
      callback()
    }

    const validateUsername = (rule, value, callback) => {
      if (!String(value || '').trim()) {
        callback(new Error(this.$t('login.usernameRequired')))
        return
      }
      callback()
    }

    const validatePassword = (rule, value, callback) => {
      if (!String(value || '').trim()) {
        callback(new Error(this.$t('login.passwordRequired')))
        return
      }
      callback()
    }

    return {
      loginForm: {
        serverIp: localStorage.getItem('serverIp') || '',
        serverPort: localStorage.getItem('serverPort') || DEFAULT_SERVER_PORT,
        username: localStorage.getItem('rememberedUsername') || '',
        password: localStorage.getItem('rememberedPassword') || '',
        remember: !!localStorage.getItem('rememberedUsername') && !!localStorage.getItem('rememberedPassword')
      },
      loginRules: {
        serverIp: [{ validator: validateServerIp, trigger: 'blur' }],
        serverPort: [{ validator: validateServerPort, trigger: 'blur' }],
        username: [{ validator: validateUsername, trigger: 'blur' }],
        password: [{ validator: validatePassword, trigger: 'blur' }]
      },
      loading: false,
      currentLang: localStorage.getItem('language') || 'zh-CN'
    }
  },
  computed: {
    backgroundStyle() {
      return {
        '--login-bg-image': `url(${loginBackground})`
      }
    },
    currentLanguageLabel() {
      return this.currentLang === 'en-US' ? 'English' : '简体中文'
    }
  },
  methods: {
    ...mapActions(['login', 'updateServerConfig']),
    sparkStyle(index) {
      const styles = [
        { left: '6%', top: '76%' },
        { left: '17%', top: '56%' },
        { left: '24%', top: '18%' },
        { left: '34%', top: '72%' },
        { left: '48%', top: '12%' },
        { left: '59%', top: '67%' },
        { left: '71%', top: '34%' },
        { left: '83%', top: '79%' },
        { left: '89%', top: '48%' },
        { left: '94%', top: '23%' }
      ]
      return styles[index - 1] || {}
    },
    async handleLogin() {
      try {
        await this.$refs.loginForm.validate()
      } catch (error) {
        return
      }

      this.loading = true
      this.loginForm.serverIp = String(this.loginForm.serverIp || '').trim()
      this.loginForm.serverPort = String(this.loginForm.serverPort || '').trim()
      this.loginForm.username = String(this.loginForm.username || '').trim()

      this.updateServerConfig({
        ip: this.loginForm.serverIp,
        port: this.loginForm.serverPort
      })

      try {
        const res = await login({
          username: this.loginForm.username,
          password: this.loginForm.password
        })

        if (res.data.user && res.data.user.disabled) {
          this.$message.error(this.$t('login.accountDisabled'))
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
        this.$router.push(getDefaultAccessiblePath())
      } catch (error) {
        const message = error.userMessage || error.response?.data?.message || this.$t('login.loginFailed')
        this.$message.error(message)
      } finally {
        this.loading = false
      }
    },
    changeLang(lang) {
      this.currentLang = lang
      this.$i18n.locale = lang
      localStorage.setItem('language', lang)
      this.$store.commit('SET_LANGUAGE', lang)
    }
  }
}
</script>

<style lang="scss" scoped>
.login-shell {
  position: relative;
  min-height: 100vh;
  overflow: hidden;
  background: #d9e8f7;
}

.login-background {
  position: absolute;
  inset: 0;
  background-image:
    linear-gradient(135deg, rgba(255, 255, 255, 0.08), transparent 22%),
    linear-gradient(180deg, rgba(11, 37, 79, 0.14), rgba(11, 37, 79, 0.32)),
    var(--login-bg-image);
  background-position: center;
  background-repeat: no-repeat;
  background-size: auto, auto, cover;
}

.scan-line {
  position: absolute;
  top: 0;
  width: 2px;
  height: 84px;
  background: linear-gradient(180deg, rgba(255, 255, 255, 0.55), transparent);
  opacity: 0.45;
}

.scan-line::before {
  content: '';
  position: absolute;
  left: -10px;
  top: 10px;
  width: 24px;
  height: 60px;
  background-image: radial-gradient(circle, rgba(255, 255, 255, 0.72) 0 1px, transparent 1px);
  background-size: 6px 9px;
  opacity: 0.3;
}

.light-dot {
  position: absolute;
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.82);
  box-shadow: 0 0 18px rgba(255, 255, 255, 0.9);
}

.login-topbar {
  position: relative;
  z-index: 2;
  height: 60px;
  padding: 0 12px 0 10px;
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.brand-mark {
  display: inline-flex;
  align-items: baseline;
  gap: 4px;
  font-size: 12px;
  color: #4a4a4a;
}

.brand-mark__bohr {
  color: #d74242;
  font-weight: 700;
  font-size: 18px;
}

.lang-chip {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  color: rgba(255, 255, 255, 0.92);
  font-size: 12px;
  cursor: pointer;
}

.lang-dropdown ::v-deep .el-dropdown-selfdefine:focus {
  outline: none;
}

.login-stage {
  position: relative;
  z-index: 2;
  min-height: calc(100vh - 98px);
  padding: 18px 84px 0;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 48px;
}

.login-visual {
  position: relative;
  flex: 1;
  min-height: 520px;
}

.scene-card {
  position: absolute;
  border-radius: 18px;
  background: linear-gradient(180deg, rgba(105, 144, 255, 0.9), rgba(77, 241, 243, 0.72));
  box-shadow: 0 28px 50px rgba(45, 111, 206, 0.18);
}

.scene-card--a {
  left: 118px;
  top: 112px;
  width: 94px;
  height: 138px;
}

.scene-card--b {
  left: 194px;
  top: 136px;
  width: 128px;
  height: 98px;
  transform: skew(-14deg);
  opacity: 0.82;
}

.scene-card--c {
  left: 72px;
  top: 236px;
  width: 118px;
  height: 70px;
  opacity: 0.68;
}

.scene-card__bar,
.scene-card__wave {
  position: absolute;
  inset: 16px;
}

.scene-card__bar::before,
.scene-card__bar::after {
  content: '';
  position: absolute;
  bottom: 0;
  width: 18px;
  border-radius: 10px 10px 0 0;
  background: rgba(255, 255, 255, 0.2);
}

.scene-card__bar::before {
  left: 8px;
  height: 42px;
}

.scene-card__bar::after {
  left: 38px;
  height: 72px;
}

.scene-card__wave::before {
  content: '';
  position: absolute;
  left: 8px;
  right: 8px;
  top: 26px;
  height: 24px;
  border-top: 3px solid rgba(130, 255, 239, 0.95);
  border-radius: 18px;
  transform: skew(14deg);
}

.cube-stack {
  position: absolute;
  left: 86px;
  top: 140px;
  width: 118px;
  height: 156px;
}

.cube {
  position: absolute;
  width: 38px;
  border-radius: 8px 8px 0 0;
  background: linear-gradient(180deg, rgba(108, 118, 252, 0.9), rgba(91, 229, 255, 0.95));
}

.cube--1 {
  left: 0;
  bottom: 0;
  height: 42px;
  background: linear-gradient(180deg, #fc7a75, #f85d74);
}

.cube--2 {
  left: 24px;
  bottom: 0;
  height: 70px;
}

.cube--3 {
  left: 48px;
  bottom: 0;
  height: 106px;
}

.cube--4 {
  left: 72px;
  bottom: 0;
  height: 138px;
}

.network-base {
  position: absolute;
  left: 42px;
  bottom: 26px;
  width: 392px;
  height: 164px;
  border-radius: 18px;
  background:
    linear-gradient(180deg, rgba(32, 112, 214, 0.34), rgba(25, 141, 214, 0.48)),
    repeating-linear-gradient(0deg, rgba(112, 214, 255, 0.24), rgba(112, 214, 255, 0.24) 1px, transparent 1px, transparent 23px),
    repeating-linear-gradient(90deg, rgba(112, 214, 255, 0.24), rgba(112, 214, 255, 0.24) 1px, transparent 1px, transparent 23px);
  transform: perspective(380px) rotateX(64deg);
  box-shadow: 0 24px 48px rgba(31, 109, 202, 0.2);
}

.node,
.node::before {
  position: absolute;
  border-radius: 50%;
}

.node {
  width: 14px;
  height: 14px;
  background: #69fff4;
  box-shadow: 0 0 0 6px rgba(102, 255, 243, 0.18);
}

.node::before {
  content: '';
  inset: -8px;
  border: 2px solid rgba(105, 255, 244, 0.3);
}

.node--1 { left: 38px; bottom: 84px; }
.node--2 { left: 72px; bottom: 40px; }
.node--3 { left: 146px; bottom: 88px; }
.node--4 { left: 242px; bottom: 42px; }
.node--5 { left: 322px; bottom: 104px; }

.node-link {
  position: absolute;
  height: 3px;
  border-radius: 999px;
  background: linear-gradient(90deg, rgba(78, 255, 242, 0.12), rgba(78, 255, 242, 0.92), rgba(78, 255, 242, 0.12));
}

.node-link--1 {
  left: 48px;
  bottom: 89px;
  width: 106px;
  transform: rotate(-10deg);
}

.node-link--2 {
  left: 148px;
  bottom: 77px;
  width: 106px;
  transform: rotate(14deg);
}

.node-link--3 {
  left: 248px;
  bottom: 74px;
  width: 88px;
  transform: rotate(-20deg);
}

.screen-shell {
  position: absolute;
  left: 198px;
  top: 110px;
  width: 340px;
  height: 262px;
}

.screen-frame {
  position: absolute;
  inset: 0;
  padding: 16px;
  border-radius: 10px;
  background: linear-gradient(145deg, #3e456b, #576286 48%, #343d5b);
  box-shadow: 0 28px 44px rgba(37, 75, 148, 0.28);
}

.screen-display {
  position: relative;
  width: 100%;
  height: 100%;
  overflow: hidden;
  border-radius: 4px;
  background:
    radial-gradient(circle at 65% 44%, rgba(140, 104, 255, 0.7), transparent 18%),
    radial-gradient(circle at 42% 30%, rgba(61, 255, 240, 0.26), transparent 34%),
    linear-gradient(180deg, #42ebf4 0%, #2fc6ee 56%, #18b0e7 100%);
}

.display-line {
  position: absolute;
  left: 24px;
  height: 5px;
  border-radius: 999px;
  background: rgba(255, 255, 255, 0.54);
}

.display-line--1 {
  top: 24px;
  width: 80px;
}

.display-line--2 {
  top: 38px;
  width: 52px;
}

.display-circle,
.display-ring {
  position: absolute;
  border-radius: 50%;
}

.display-circle {
  left: 144px;
  top: 74px;
  width: 46px;
  height: 46px;
  background: rgba(255, 255, 255, 0.12);
}

.display-ring {
  left: 126px;
  top: 56px;
  border: 3px solid rgba(168, 95, 255, 0.68);
}

.display-ring--1 {
  width: 82px;
  height: 82px;
}

.display-ring--2 {
  left: 138px;
  top: 68px;
  width: 58px;
  height: 58px;
  border-color: rgba(83, 238, 255, 0.8);
}

.screen-stand {
  position: absolute;
  left: 90px;
  right: 90px;
  bottom: -32px;
  height: 42px;
  border-radius: 0 0 18px 18px;
  background: linear-gradient(180deg, rgba(255, 255, 255, 0.95), rgba(220, 232, 244, 0.92));
  box-shadow: 0 16px 28px rgba(84, 131, 206, 0.22);
}

.screen-stand::before {
  content: '';
  position: absolute;
  left: 102px;
  right: 102px;
  bottom: -18px;
  height: 18px;
  border-radius: 0 0 999px 999px;
  background: rgba(225, 236, 246, 0.96);
}

.operator {
  position: absolute;
  width: 48px;
  height: 116px;
}

.operator--left {
  left: 246px;
  top: 264px;
}

.operator--right {
  left: 312px;
  top: 252px;
}

.operator-head {
  position: absolute;
  left: 14px;
  top: 0;
  width: 18px;
  height: 18px;
  border-radius: 50%;
  background: #ffd3c5;
}

.operator-body {
  position: absolute;
  left: 10px;
  top: 18px;
  width: 24px;
  height: 42px;
  border-radius: 12px 12px 8px 8px;
  background: linear-gradient(180deg, #ff8572, #ff625d);
}

.operator--right .operator-body {
  background: linear-gradient(180deg, #56ddff, #3ec5ff);
}

.operator-arm {
  position: absolute;
  left: 28px;
  top: 30px;
  width: 22px;
  height: 4px;
  border-radius: 999px;
  background: #ffd3c5;
  transform: rotate(-32deg);
  transform-origin: left center;
}

.operator--right .operator-arm {
  left: -2px;
  transform: rotate(-118deg);
}

.operator-leg {
  position: absolute;
  top: 56px;
  width: 4px;
  height: 46px;
  border-radius: 999px;
  background: #45558a;
}

.operator-leg--left {
  left: 16px;
  transform: rotate(10deg);
}

.operator-leg--right {
  left: 28px;
  transform: rotate(-8deg);
}

.operator--right .operator-leg--left {
  transform: rotate(18deg);
}

.operator--right .operator-leg--right {
  transform: rotate(-20deg);
}

.mouse-shape {
  position: absolute;
  right: 102px;
  bottom: 102px;
  width: 82px;
  height: 38px;
  border-radius: 999px;
  background: linear-gradient(180deg, rgba(255, 255, 255, 0.96), rgba(221, 235, 248, 0.94));
  box-shadow: 0 12px 22px rgba(95, 141, 209, 0.18);
  transform: rotate(-12deg);
}

.login-card {
  width: 100%;
  max-width: 320px;
  padding: 22px 34px 28px;
  border-radius: 4px;
  background: rgba(255, 255, 255, 0.97);
  box-shadow: 0 14px 34px rgba(68, 120, 194, 0.18);
}

.login-title {
  margin: 8px 0 22px;
  text-align: center;
  font-size: 18px;
  line-height: 1.4;
  color: #4c8bd8;
  font-weight: 700;
  letter-spacing: 0.08em;
}

.login-form ::v-deep .el-form-item {
  margin-bottom: 16px;
}

.login-form ::v-deep .el-input__inner {
  height: 34px;
  line-height: 34px;
  border-radius: 0;
  border-color: #cfd8e3;
  background: rgba(255, 255, 255, 0.92);
  font-size: 12px;
  color: #6d7785;
}

.login-form ::v-deep .el-input__prefix {
  left: 8px;
  color: #a0acb9;
}

.login-form ::v-deep .el-input__icon {
  line-height: 34px;
}

.login-form ::v-deep .el-input__inner::-webkit-input-placeholder {
  color: #b6bec9;
}

.login-form ::v-deep .el-form-item.is-error .el-input__inner {
  border-color: #f56c6c;
}

.login-actions {
  display: flex;
  justify-content: flex-end;
  margin-top: -2px;
  margin-bottom: 0;
}

.login-actions ::v-deep .el-checkbox {
  color: #7d8ea2;
  font-size: 12px;
}

.login-actions ::v-deep .el-checkbox__label {
  font-size: 12px;
  padding-left: 4px;
}

.login-submit {
  width: 100%;
  height: 36px;
  margin-bottom: 10px;
  border-radius: 0;
  font-size: 13px;
  letter-spacing: 0.2em;
  background: #4a9cf7;
  border-color: #4a9cf7;
}

.login-submit:hover,
.login-submit:focus {
  background: #63a9f8;
  border-color: #63a9f8;
}

.login-footer {
  position: relative;
  z-index: 2;
  padding-bottom: 8px;
  text-align: center;
  color: #4f5968;
  font-size: 12px;
  line-height: 1.6;
}

@media (max-width: 1180px) {
  .login-stage {
    padding: 12px 40px 0;
    gap: 24px;
  }

  .login-visual {
    min-height: 460px;
    transform: scale(0.88);
    transform-origin: center;
  }
}

@media (max-width: 980px) {
  .login-stage {
    min-height: calc(100vh - 110px);
    padding: 12px 24px 0;
    flex-direction: column;
    justify-content: center;
  }

  .login-visual {
    width: 100%;
    min-height: 360px;
    transform: scale(0.72);
  }
}

@media (max-width: 640px) {
  .login-topbar {
    padding: 0 12px;
  }

  .brand-mark {
    font-size: 11px;
  }

  .brand-mark__bohr {
    font-size: 16px;
  }

  .lang-chip {
    font-size: 11px;
  }

  .login-stage {
    padding: 8px 12px 0;
  }

  .login-visual {
    display: none;
  }

  .login-card {
    max-width: 100%;
    padding: 18px 18px 22px;
  }
}
</style>
