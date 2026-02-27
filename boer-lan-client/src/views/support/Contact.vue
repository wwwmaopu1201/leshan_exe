<template>
  <div class="page-container">
    <el-row :gutter="20">
      <el-col :span="12">
        <div class="contact-card">
          <h3>{{ $t('menu.contact') }}</h3>
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
                <div class="contact-value">浙江省杭州市滨江区科技园区XXX号</div>
              </div>
            </div>
          </div>
        </div>
      </el-col>

      <el-col :span="12">
        <div class="contact-card">
          <h3>在线留言</h3>
          <el-form ref="formRef" :model="form" :rules="rules" label-width="80px">
            <el-form-item label="姓名" prop="name">
              <el-input v-model="form.name" placeholder="请输入您的姓名" />
            </el-form-item>
            <el-form-item label="联系方式" prop="contact">
              <el-input v-model="form.contact" placeholder="请输入手机号或邮箱" />
            </el-form-item>
            <el-form-item label="问题类型" prop="type">
              <el-select v-model="form.type" style="width: 100%">
                <el-option label="软件使用问题" value="usage" />
                <el-option label="功能建议" value="suggestion" />
                <el-option label="Bug反馈" value="bug" />
                <el-option label="其他" value="other" />
              </el-select>
            </el-form-item>
            <el-form-item label="问题描述" prop="description">
              <el-input
                v-model="form.description"
                type="textarea"
                :rows="4"
                placeholder="请详细描述您的问题"
              />
            </el-form-item>
            <el-form-item>
              <el-button type="primary" @click="handleSubmit">提交</el-button>
              <el-button @click="handleReset">重置</el-button>
            </el-form-item>
          </el-form>
        </div>
      </el-col>
    </el-row>

    <div class="contact-card mt-20">
      <h3>常见问题</h3>
      <el-collapse accordion>
        <el-collapse-item title="1. 如何连接设备？" name="1">
          <div class="faq-content">
            首先确保设备已正确连接到局域网，然后在设备管理中添加设备，输入设备IP地址即可完成连接。
          </div>
        </el-collapse-item>
        <el-collapse-item title="2. 花型文件支持哪些格式？" name="2">
          <div class="faq-content">
            目前支持 .dst, .dsb, .exp, .pes, .jef 等常见绣花格式文件。
          </div>
        </el-collapse-item>
        <el-collapse-item title="3. 如何批量下发花型？" name="3">
          <div class="faq-content">
            在花型管理中选择多个花型文件，然后点击"批量下发"按钮，选择目标设备即可。
          </div>
        </el-collapse-item>
        <el-collapse-item title="4. 数据统计如何导出？" name="4">
          <div class="faq-content">
            在各统计页面右上角有"导出Excel"按钮，点击即可将当前统计数据导出为Excel文件。
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
        name: [{ required: true, message: '请输入姓名', trigger: 'blur' }],
        contact: [{ required: true, message: '请输入联系方式', trigger: 'blur' }],
        type: [{ required: true, message: '请选择问题类型', trigger: 'change' }],
        description: [{ required: true, message: '请描述您的问题', trigger: 'blur' }]
      }
    }
  },
  methods: {
    async handleSubmit() {
      try {
        await this.$refs.formRef.validate()
        this.$message.success('留言提交成功，我们会尽快与您联系')
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
