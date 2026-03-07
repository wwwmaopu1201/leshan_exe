import Vue from 'vue'
import App from './App.vue'
import router from './router'
import store from './store'
import ElementUI from 'element-ui'
import 'element-ui/lib/theme-chalk/index.css'
import i18n from './i18n'
import './assets/styles/global.scss'
import { installPermissionDirective, installPermissionDisableDirective } from './utils/permission'

Vue.use(ElementUI, {
  size: 'medium',
  i18n: (key, value) => i18n.t(key, value)
})

// 注册权限指令
installPermissionDirective(Vue)
installPermissionDisableDirective(Vue)

Vue.config.productionTip = false

new Vue({
  router,
  store,
  i18n,
  render: h => h(App)
}).$mount('#app')
