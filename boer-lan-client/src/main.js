import Vue from 'vue'
import App from './App.vue'
import router from './router'
import store from './store'
import ElementUI from 'element-ui'
import 'element-ui/lib/theme-chalk/index.css'
import i18n from './i18n'
import './assets/styles/global.scss'
import { invoke } from '@tauri-apps/api/core'
import { getCurrentWindow } from '@tauri-apps/api/window'
import { installPermissionDirective, installPermissionDisableDirective } from './utils/permission'
import { installI18nNormalizer, translateApiMessage } from './utils/i18n-normalizer'

Vue.use(ElementUI, {
  size: 'medium',
  i18n: (key, value) => i18n.t(key, value)
})

installI18nNormalizer(Vue, {
  languageKey: 'language',
  localeGetter: () => i18n.locale || localStorage.getItem('language') || 'zh-CN'
})
installPermissionDirective(Vue)
installPermissionDisableDirective(Vue)

Vue.config.productionTip = false

let trialMonitorTimer = null
let trialExpiredHandled = false
const isDevMode = import.meta.env.DEV || !import.meta.env.PROD

function installInteractionGuards() {
  document.addEventListener('contextmenu', event => {
    event.preventDefault()
  })

  document.addEventListener('copy', event => {
    event.preventDefault()
  })

  document.addEventListener('cut', event => {
    event.preventDefault()
  })

  document.addEventListener('keydown', event => {
    const key = String(event.key || '').toLowerCase()
    if ((event.ctrlKey || event.metaKey) && (key === 'c' || key === 'x')) {
      event.preventDefault()
    }
  })
}

function renderTrialMessage(message) {
  const app = document.getElementById('app')
  if (!app) {
    return
  }

  app.innerHTML = `
    <div style="display:flex;min-height:100vh;align-items:center;justify-content:center;background:#f5f7fa;padding:24px;">
      <div style="min-width:320px;max-width:520px;padding:28px 32px;border-radius:12px;background:#fff;box-shadow:0 12px 32px rgba(31,35,41,0.08);text-align:center;color:#303133;font-size:16px;line-height:1.7;">${message}</div>
    </div>
  `
}

async function ensureTrialAvailable() {
  if (isDevMode) {
    return true
  }

  const status = await invoke('get_trial_status')
  if (status.valid) {
    return true
  }

  const message = translateApiMessage(status.message || '试用已过期，请联系供应商')
  renderTrialMessage(message)
  alert(message)
  return false
}

function startTrialMonitor() {
  if (isDevMode || trialMonitorTimer) {
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
        renderTrialMessage(message)
        alert(message)
        clearInterval(trialMonitorTimer)
        await getCurrentWindow().close()
      }
    } catch (error) {
      console.log('Trial monitor check failed', error)
    }
  }, 15000)
}

async function initApp() {
  installInteractionGuards()

  if (!isDevMode) {
    renderTrialMessage(translateApiMessage('正在检查试用状态...'))
    const trialOk = await ensureTrialAvailable()
    if (!trialOk) {
      return
    }
  }

  new Vue({
    router,
    store,
    i18n,
    render: h => h(App)
  }).$mount('#app')

  startTrialMonitor()
}

initApp()
