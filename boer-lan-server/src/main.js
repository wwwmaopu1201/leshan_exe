import Vue from 'vue'
import ElementUI from 'element-ui'
import 'element-ui/lib/theme-chalk/index.css'
import App from './App.vue'
import router from './router'
import request, { getRequestBaseURL, initRequestBaseURL } from './utils/request'
import { invoke } from '@tauri-apps/api/core'
import { getCurrentWindow } from '@tauri-apps/api/window'

Vue.use(ElementUI)
Vue.prototype.$axios = request

Vue.config.productionTip = false

let trialMonitorTimer = null
let trialExpiredHandled = false

async function ensureTrialAvailable() {
  if (!import.meta.env.PROD) {
    return true
  }

  const status = await invoke('get_trial_status')
  if (status.valid) {
    return true
  }

  renderBootMessage(status.message || '试用已过期，请联系供应商')
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
        renderBootMessage(status.message || '试用已过期，请联系供应商')
        alert(status.message || '试用已过期，请联系供应商')
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
async function waitForBackend(retries = 30) {
  for (let i = 0; i < retries; i++) {
    try {
      await request.get('/healthz', { timeout: 1000, suppressErrorMessage: true })
      console.log(`Backend is ready: ${getRequestBaseURL()}`)
      return true
    } catch (error) {
      console.log(`Waiting for backend... (${i + 1}/${retries})`, error)
      renderBootMessage(`正在启动服务端... (${i + 1}/${retries})`)
      await new Promise(resolve => setTimeout(resolve, 1000))
    }
  }

  renderBootMessage('服务端启动失败，请检查应用配置')
  alert('后端服务启动失败，请检查应用配置')
  return false
}

async function initApp() {
  renderBootMessage('正在检查试用状态...')
  const trialOk = await ensureTrialAvailable()
  if (!trialOk) {
    return
  }

  renderBootMessage('正在准备服务端...')
  await initRequestBaseURL()

  // 生产环境下等待后端启动
  if (import.meta.env.PROD) {
    await waitForBackend()
  }

  new Vue({
    router,
    render: h => h(App)
  }).$mount('#app')

  startTrialMonitor()
}

initApp()
