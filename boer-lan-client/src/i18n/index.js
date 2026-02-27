import Vue from 'vue'
import VueI18n from 'vue-i18n'
import zhCN from './zh-CN'
import enUS from './en-US'
import zhLocale from 'element-ui/lib/locale/lang/zh-CN'
import enLocale from 'element-ui/lib/locale/lang/en'

Vue.use(VueI18n)

const messages = {
  'zh-CN': {
    ...zhCN,
    ...zhLocale
  },
  'en-US': {
    ...enUS,
    ...enLocale
  }
}

const i18n = new VueI18n({
  locale: localStorage.getItem('language') || 'zh-CN',
  fallbackLocale: 'zh-CN',
  messages
})

export default i18n
