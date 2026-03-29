<template>
  <div class="page-container">
    <div class="support-grid">
      <div class="contact-card contact-info-card">
        <div class="section-title">
          <div>
            <h3>{{ $t('support.contactTitle') }}</h3>
            <p>售前咨询、安装协助、问题反馈统一在这里处理。</p>
          </div>
        </div>

        <div class="contact-list">
          <div class="contact-item">
            <div class="contact-icon blue"><i class="el-icon-phone"></i></div>
            <div class="contact-info">
              <div class="contact-label">{{ $t('support.customerService') }}</div>
              <div class="contact-value">400-888-8888</div>
            </div>
          </div>
          <div class="contact-item">
            <div class="contact-icon green"><i class="el-icon-message"></i></div>
            <div class="contact-info">
              <div class="contact-label">{{ $t('support.email') }}</div>
              <div class="contact-value">support@boer.com</div>
            </div>
          </div>
          <div class="contact-item">
            <div class="contact-icon orange"><i class="el-icon-time"></i></div>
            <div class="contact-info">
              <div class="contact-label">{{ $t('support.workingHours') }}</div>
              <div class="contact-value">周一至周五 9:00-18:00</div>
            </div>
          </div>
          <div class="contact-item">
            <div class="contact-icon cyan"><i class="el-icon-location"></i></div>
            <div class="contact-info">
              <div class="contact-label">{{ $t('support.address') }}</div>
              <div class="contact-value">{{ $t('support.companyAddressValue') }}</div>
              <el-link type="primary" :underline="false" @click="openMap">查看地图</el-link>
            </div>
          </div>
        </div>
      </div>

      <div class="contact-card message-card">
        <div class="section-title">
          <div>
            <h3>{{ $t('support.onlineMessage') }}</h3>
            <p>留下联系方式和问题描述，服务人员会尽快回访。</p>
          </div>
        </div>

        <el-form ref="formRef" :model="form" :rules="rules" label-width="84px" class="message-form">
          <el-form-item :label="$t('support.formName')" prop="name">
            <el-input v-model="form.name" :placeholder="$t('support.formNamePlaceholder')" />
          </el-form-item>
          <el-form-item :label="$t('support.formContact')" prop="contact">
            <el-input v-model="form.contact" :placeholder="$t('support.formContactPlaceholder')" />
          </el-form-item>
          <el-form-item :label="$t('support.formType')" prop="type">
            <el-select v-model="form.type">
              <el-option :label="$t('support.typeUsage')" value="usage" />
              <el-option :label="$t('support.typeSuggestion')" value="suggestion" />
              <el-option :label="$t('support.typeBug')" value="bug" />
              <el-option :label="$t('support.typeOther')" value="other" />
            </el-select>
          </el-form-item>
          <el-form-item :label="$t('support.formDescription')" prop="description">
            <el-input
              v-model="form.description"
              type="textarea"
              :rows="8"
              :placeholder="$t('support.formDescriptionPlaceholder')"
            />
          </el-form-item>
          <el-form-item>
            <el-button type="primary" @click="handleSubmit">{{ $t('support.submitMessage') }}</el-button>
            <el-button @click="handleReset">{{ $t('support.resetMessage') }}</el-button>
          </el-form-item>
        </el-form>
      </div>
    </div>

    <div class="contact-card faq-card">
      <div class="section-title">
        <div>
          <h3>{{ $t('support.faqTitle') }}</h3>
          <p>整理了客户最常见的使用问题，方便快速排查。</p>
        </div>
      </div>

      <el-collapse accordion>
        <el-collapse-item :title="$t('support.faq1Question')" name="1">
          <div class="faq-content">{{ $t('support.faq1Answer') }}</div>
        </el-collapse-item>
        <el-collapse-item :title="$t('support.faq2Question')" name="2">
          <div class="faq-content">{{ $t('support.faq2Answer') }}</div>
        </el-collapse-item>
        <el-collapse-item :title="$t('support.faq3Question')" name="3">
          <div class="faq-content">{{ $t('support.faq3Answer') }}</div>
        </el-collapse-item>
        <el-collapse-item :title="$t('support.faq4Question')" name="4">
          <div class="faq-content">{{ $t('support.faq4Answer') }}</div>
        </el-collapse-item>
      </el-collapse>
    </div>
  </div>
</template>

<script>
export default {
  name: 'Contact',
  data() {
    return {
      form: {
        name: '',
        contact: '',
        type: '',
        description: ''
      },
      rules: {
        name: [{ required: true, message: this.$t('support.formNameRequired'), trigger: 'blur' }],
        contact: [{ required: true, message: this.$t('support.formContactRequired'), trigger: 'blur' }],
        type: [{ required: true, message: this.$t('support.formTypeRequired'), trigger: 'change' }],
        description: [{ required: true, message: this.$t('support.formDescriptionRequired'), trigger: 'blur' }]
      }
    }
  },
  methods: {
    openMap() {
      const address = encodeURIComponent(this.$t('support.companyAddressValue'))
      window.open(`https://uri.amap.com/search?keyword=${address}`, '_blank')
    },
    async handleSubmit() {
      try {
        await this.$refs.formRef.validate()
        this.$message.success(this.$t('support.formSubmitSuccess'))
        this.handleReset()
      } catch (error) {
        console.error('Validation failed:', error)
      }
    },
    handleReset() {
      this.$refs.formRef.resetFields()
    }
  }
}
</script>

<style lang="scss" scoped>
.support-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 20px;
}

.contact-card {
  background: #fff;
  border: 1px solid rgba(221, 229, 240, 0.92);
  border-radius: 22px;
  padding: 22px;
  box-shadow: 0 18px 36px rgba(59, 87, 132, 0.08);
}

.contact-info-card,
.message-card {
  min-height: 420px;
}

.contact-list {
  display: grid;
  gap: 14px;
}

.contact-item {
  display: flex;
  align-items: flex-start;
  gap: 14px;
  padding: 16px 18px;
  border-radius: 18px;
  background: #f7faff;
}

.contact-icon {
  width: 48px;
  height: 48px;
  border-radius: 16px;
  display: inline-flex;
  align-items: center;
  justify-content: center;

  i {
    font-size: 22px;
    color: #fff;
  }

  &.blue { background: linear-gradient(135deg, #3476ff, #1953d1); }
  &.green { background: linear-gradient(135deg, #2fb46e, #1f935e); }
  &.orange { background: linear-gradient(135deg, #f0b037, #cf7b11); }
  &.cyan { background: linear-gradient(135deg, #28b5c8, #187db2); }
}

.contact-info {
  .contact-label {
    font-size: 12px;
    color: #8a99ae;
    margin-bottom: 6px;
  }

  .contact-value {
    color: #22324d;
    font-size: 15px;
    font-weight: 600;
    line-height: 1.7;
  }
}

.message-form {
  min-height: 320px;
}

.faq-card {
  margin-top: 20px;
  min-height: calc(100vh - 590px);
}

.faq-content {
  color: #687c9a;
  line-height: 1.9;
}

@media (max-width: 980px) {
  .support-grid {
    grid-template-columns: 1fr;
  }

  .faq-card {
    min-height: auto;
  }
}
</style>
