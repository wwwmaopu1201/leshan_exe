import Vue from 'vue'
import ElementUI from 'element-ui'
import ElementLocale from 'element-ui/lib/locale'
import zhLocale from 'element-ui/lib/locale/lang/zh-CN'
import enLocale from 'element-ui/lib/locale/lang/en'
import 'element-ui/lib/theme-chalk/index.css'
import './assets/styles/global.scss'
import App from './App.vue'
import router from './router'
import request, { getRequestBaseURL, initRequestBaseURL } from './utils/request'
import { invoke } from '@tauri-apps/api/core'
import { getCurrentWindow } from '@tauri-apps/api/window'
import { installI18nNormalizer, translateApiMessage } from './utils/i18n-normalizer'

function getServerLanguage() {
  return localStorage.getItem('server_login_language') || 'zh-CN'
}

function applyElementLanguage() {
  ElementLocale.use(getServerLanguage() === 'en-US' ? enLocale : zhLocale)
}

applyElementLanguage()
Vue.use(ElementUI)
installI18nNormalizer(Vue, {
  languageKey: 'server_login_language',
  localeGetter: getServerLanguage
})
Vue.prototype.$axios = request

Vue.config.productionTip = false

let trialMonitorTimer = null
let trialExpiredHandled = false

function installContextMenuGuard() {
  document.addEventListener('contextmenu', event => {
    event.preventDefault()
  })
}

async function ensureTrialAvailable() {
  if (!import.meta.env.PROD) {
    return true
  }

  const status = await invoke('get_trial_status')
  if (status.valid) {
    return true
  }

  const message = translateApiMessage(status.message || '试用已过期，请联系供应商')
  renderBootMessage(message)
  alert(message)
  return false
}

function startTrialMonitor() {
  if (!import.meta.env.PROD || trialMonitorTimer) {
    return
  }

  trialMonitorTimer = window.setInterval(async () => {
    if (trialExpiredHandled) {
      return
    }

    try {
      const status = await invoke('get_trial_status')
      if (!status.valid) {
        trialExpiredHandled = true
        const message = translateApiMessage(status.message || '试用已过期，请联系供应商')
        renderBootMessage(message)
        alert(message)
        clearInterval(trialMonitorTimer)
        await getCurrentWindow().close()
      }
    } catch (error) {
      console.log('Trial monitor check failed', error)
    }
  }, 15000)
}

function renderBootMessage(message) {
  const app = document.getElementById('app')
  if (!app) {
    return
  }

  app.innerHTML = `
    <div class="app-boot">
      <div class="app-boot__card">${message}</div>
    </div>
  `
}

// 等待后端就绪
async function waitForBackend(retries = 30, options = {}) {
  const { showAlert = true } = options
  for (let i = 0; i < retries; i++) {
    try {
      await request.get('/healthz', { timeout: 1000, suppressErrorMessage: true })
      console.log(`Backend is ready: ${getRequestBaseURL()}`)
      return true
    } catch (error) {
      console.log(`Waiting for backend... (${i + 1}/${retries})`, error)
      renderBootMessage(translateApiMessage(`正在启动服务端... (${i + 1}/${retries})`))
      await new Promise(resolve => setTimeout(resolve, 1000))
    }
  }

  renderBootMessage(translateApiMessage('服务端启动失败，请检查应用配置'))
  if (showAlert) {
    alert(translateApiMessage('后端服务启动失败，请检查应用配置'))
  }
  return false
}

async function initApp() {
  installContextMenuGuard()

  renderBootMessage(translateApiMessage('正在检查试用状态...'))
  const trialOk = await ensureTrialAvailable()
  if (!trialOk) {
    return
  }

  renderBootMessage(translateApiMessage('正在准备服务端...'))
  await initRequestBaseURL()

  const backendReady = await waitForBackend(import.meta.env.PROD ? 30 : 10, {
    showAlert: import.meta.env.PROD
  })
  if (!backendReady) {
    return
  }

  new Vue({
    router,
    render: h => h(App)
  }).$mount('#app')

  startTrialMonitor()
}

initApp()

window.addEventListener('boer-language-change', applyElementLanguage)
