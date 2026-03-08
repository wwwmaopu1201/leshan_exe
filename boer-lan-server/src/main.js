import Vue from 'vue'
import ElementUI from 'element-ui'
import 'element-ui/lib/theme-chalk/index.css'
import App from './App.vue'
import router from './router'
import request from './utils/request'

Vue.use(ElementUI)
Vue.prototype.$axios = request

Vue.config.productionTip = false

// 等待后端就绪
async function waitForBackend(retries = 30) {
  for (let i = 0; i < retries; i++) {
    try {
      await request.get('/system/info', { timeout: 1000 })
      console.log('Backend is ready!')
      return true
    } catch {
      console.log(`Waiting for backend... (${i + 1}/${retries})`)
      await new Promise(resolve => setTimeout(resolve, 1000))
    }
  }
  alert('后端服务启动失败，请检查应用配置')
  return false
}

async function initApp() {
  // 生产环境下等待后端启动
  if (process.env.NODE_ENV === 'production') {
    await waitForBackend()
  }

  new Vue({
    router,
    render: h => h(App)
  }).$mount('#app')
}

initApp()
