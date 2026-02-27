<template>
  <div class="page-container">
    <div class="language-card">
      <h3>{{ $t('profile.selectLanguage') }}</h3>
      <div class="language-options">
        <div
          :class="['language-option', { active: currentLang === 'zh-CN' }]"
          @click="changeLang('zh-CN')"
        >
          <div class="lang-icon">中</div>
          <div class="lang-info">
            <div class="lang-name">{{ $t('profile.chinese') }}</div>
            <div class="lang-desc">简体中文</div>
          </div>
          <i v-if="currentLang === 'zh-CN'" class="el-icon-check"></i>
        </div>

        <div
          :class="['language-option', { active: currentLang === 'en-US' }]"
          @click="changeLang('en-US')"
        >
          <div class="lang-icon">En</div>
          <div class="lang-info">
            <div class="lang-name">{{ $t('profile.english') }}</div>
            <div class="lang-desc">English</div>
          </div>
          <i v-if="currentLang === 'en-US'" class="el-icon-check"></i>
        </div>
      </div>

      <div class="language-tips">
        <i class="el-icon-info"></i>
        <span>切换语言后，整个应用界面将使用新的语言显示。</span>
      </div>
    </div>
  </div>
</template>

<script>
import { mapState, mapActions } from 'vuex'

export default {
  name: 'LanguageSwitch',
  computed: {
    ...mapState(['language']),
    currentLang() {
      return this.language || 'zh-CN'
    }
  },
  methods: {
    ...mapActions(['setLanguage']),
    changeLang(lang) {
      if (lang === this.currentLang) return

      this.$i18n.locale = lang
      this.setLanguage(lang)
      this.$message.success(lang === 'zh-CN' ? '语言已切换为中文' : 'Language switched to English')
    }
  }
}
</script>

<style lang="scss" scoped>
.language-card {
  background: #fff;
  border-radius: 8px;
  padding: 30px;
  max-width: 500px;

  h3 {
    margin-bottom: 30px;
    color: #303133;
  }
}

.language-options {
  display: flex;
  flex-direction: column;
  gap: 15px;
}

.language-option {
  display: flex;
  align-items: center;
  padding: 20px;
  border: 2px solid #e4e7ed;
  border-radius: 8px;
  cursor: pointer;
  transition: all 0.3s;

  &:hover {
    border-color: #409EFF;
  }

  &.active {
    border-color: #409EFF;
    background: rgba(64, 158, 255, 0.05);
  }

  .lang-icon {
    width: 50px;
    height: 50px;
    background: linear-gradient(135deg, #409EFF, #2d8cf0);
    border-radius: 8px;
    display: flex;
    align-items: center;
    justify-content: center;
    color: #fff;
    font-size: 18px;
    font-weight: bold;
    margin-right: 15px;
  }

  .lang-info {
    flex: 1;

    .lang-name {
      font-size: 16px;
      font-weight: 600;
      color: #303133;
      margin-bottom: 5px;
    }

    .lang-desc {
      font-size: 13px;
      color: #909399;
    }
  }

  .el-icon-check {
    font-size: 24px;
    color: #409EFF;
  }
}

.language-tips {
  margin-top: 30px;
  padding: 15px;
  background: #f5f7fa;
  border-radius: 8px;
  display: flex;
  align-items: center;
  color: #909399;
  font-size: 13px;

  i {
    margin-right: 10px;
    color: #409EFF;
  }
}
</style>
