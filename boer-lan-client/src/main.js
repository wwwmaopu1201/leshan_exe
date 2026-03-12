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

Vue.use(ElementUI, {
  size: 'medium',
  i18n: (key, value) => i18n.t(key, value)
})

installPermissionDirective(Vue)
installPermissionDisableDirective(Vue)

Vue.config.productionTip = false

let trialMonitorTimer = null
let trialExpiredHandled = false

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
  if (!import.meta.env.PROD) {
    return true
  }

  const status = await invoke('get_trial_status')
  if (status.valid) {
    return true
  }

  renderTrialMessage(status.message || '试用已过期，请联系供应商')
  alert(status.message || '试用已过期，请联系供应商')
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
        renderTrialMessage(status.message || '试用已过期，请联系供应商')
        alert(status.message || '试用已过期，请联系供应商')
        clearInterval(trialMonitorTimer)
        await getCurrentWindow().close()
      }
    } catch (error) {
      console.log('Trial monitor check failed', error)
    }
  }, 15000)
}

async function initApp() {
  renderTrialMessage('正在检查试用状态...')
  const trialOk = await ensureTrialAvailable()
  if (!trialOk) {
    return
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
