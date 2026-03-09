<template>
  <div class="page-container">
    <el-row :gutter="20">
      <el-col :span="12">
        <div class="contact-card">
          <h3>{{ $t('support.contactTitle') }}</h3>
          <div class="contact-list">
            <div class="contact-item">
              <div class="contact-icon blue">
                <i class="el-icon-phone"></i>
              </div>
              <div class="contact-info">
                <div class="contact-label">{{ $t('support.customerService') }}</div>
                <div class="contact-value">400-888-8888</div>
              </div>
            </div>

            <div class="contact-item">
              <div class="contact-icon green">
                <i class="el-icon-message"></i>
              </div>
              <div class="contact-info">
                <div class="contact-label">{{ $t('support.email') }}</div>
                <div class="contact-value">support@boer.com</div>
              </div>
            </div>

            <div class="contact-item">
              <div class="contact-icon orange">
                <i class="el-icon-time"></i>
              </div>
              <div class="contact-info">
                <div class="contact-label">{{ $t('support.workingHours') }}</div>
                <div class="contact-value">周一至周五 9:00-18:00</div>
              </div>
            </div>

            <div class="contact-item">
              <div class="contact-icon purple">
                <i class="el-icon-location"></i>
              </div>
              <div class="contact-info">
                <div class="contact-label">{{ $t('support.address') }}</div>
                <div class="contact-value">{{ $t('support.companyAddressValue') }}</div>
                <el-link type="primary" :underline="false" @click="openMap" style="padding: 0;">
                  {{ $t('support.viewMap') }}
                </el-link>
              </div>
            </div>

            <div class="contact-item">
              <div class="contact-icon cyan">
                <i class="el-icon-video-play"></i>
              </div>
              <div class="contact-info">
                <div class="contact-label">{{ $t('support.douyin') }}</div>
                <div class="contact-value">@boer_lan</div>
              </div>
            </div>

            <div class="contact-item">
              <div class="contact-icon teal">
                <i class="el-icon-chat-dot-square"></i>
              </div>
              <div class="contact-info">
                <div class="contact-label">{{ $t('support.wechatOfficial') }}</div>
                <div class="contact-value">博尔智能缝制</div>
              </div>
            </div>
          </div>
        </div>
      </el-col>

      <el-col :span="12">
        <div class="contact-card">
          <h3>{{ $t('support.onlineMessage') }}</h3>
          <el-form ref="formRef" :model="form" :rules="rules" label-width="80px">
            <el-form-item :label="$t('support.formName')" prop="name">
              <el-input v-model="form.name" :placeholder="$t('support.formNamePlaceholder')" />
            </el-form-item>
            <el-form-item :label="$t('support.formContact')" prop="contact">
              <el-input v-model="form.contact" :placeholder="$t('support.formContactPlaceholder')" />
            </el-form-item>
            <el-form-item :label="$t('support.formType')" prop="type">
              <el-select v-model="form.type" style="width: 100%">
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
                :rows="4"
                :placeholder="$t('support.formDescriptionPlaceholder')"
              />
            </el-form-item>
            <el-form-item>
              <el-button type="primary" @click="handleSubmit">{{ $t('support.submitMessage') }}</el-button>
              <el-button @click="handleReset">{{ $t('support.resetMessage') }}</el-button>
            </el-form-item>
          </el-form>
        </div>
      </el-col>
    </el-row>

    <div class="contact-card mt-20">
      <h3>{{ $t('support.faqTitle') }}</h3>
      <el-collapse accordion>
        <el-collapse-item :title="$t('support.faq1Question')" name="1">
          <div class="faq-content">
            {{ $t('support.faq1Answer') }}
          </div>
        </el-collapse-item>
        <el-collapse-item :title="$t('support.faq2Question')" name="2">
          <div class="faq-content">
            {{ $t('support.faq2Answer') }}
          </div>
        </el-collapse-item>
        <el-collapse-item :title="$t('support.faq3Question')" name="3">
          <div class="faq-content">
            {{ $t('support.faq3Answer') }}
          </div>
        </el-collapse-item>
        <el-collapse-item :title="$t('support.faq4Question')" name="4">
          <div class="faq-content">
            {{ $t('support.faq4Answer') }}
          </div>
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
.contact-card {
  background: #fff;
  border-radius: 8px;
  padding: 30px;

  h3 {
    margin-bottom: 25px;
    color: #303133;
  }
}

.contact-list {
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.contact-item {
  display: flex;
  align-items: center;

  .contact-icon {
    width: 50px;
    height: 50px;
    border-radius: 10px;
    display: flex;
    align-items: center;
    justify-content: center;
    margin-right: 15px;

    i { font-size: 24px; color: #fff; }

    &.blue { background: linear-gradient(135deg, #409EFF, #2d8cf0); }
    &.green { background: linear-gradient(135deg, #67C23A, #5daf34); }
    &.orange { background: linear-gradient(135deg, #E6A23C, #d69330); }
    &.purple { background: linear-gradient(135deg, #9b59b6, #8e44ad); }
    &.cyan { background: linear-gradient(135deg, #36cfc9, #13c2c2); }
    &.teal { background: linear-gradient(135deg, #52c41a, #389e0d); }
  }

  .contact-info {
    .contact-label {
      font-size: 13px;
      color: #909399;
      margin-bottom: 5px;
    }

    .contact-value {
      font-size: 15px;
      color: #303133;
      font-weight: 500;
    }
  }
}

.faq-content {
  color: #606266;
  line-height: 1.8;
}

.mt-20 {
  margin-top: 20px;
}
</style>
