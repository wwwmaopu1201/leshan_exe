<template>
  <div class="login-container">
    <el-card class="login-card">
      <div slot="header" class="login-header">
        博尔局域网服务器管理
      </div>
      <el-form :model="loginForm">
        <el-form-item label="用户名">
          <el-input v-model="loginForm.username" placeholder="请输入用户名" />
        </el-form-item>
        <el-form-item label="密码">
          <el-input
            v-model="loginForm.password"
            type="password"
            placeholder="请输入密码"
            @keyup.enter.native="login"
          />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" @click="login" style="width: 100%;">登录</el-button>
        </el-form-item>
      </el-form>
    </el-card>
  </div>
</template>

<script>
export default {
  name: 'Login',
  data() {
    return {
      loginForm: {
        username: '',
        password: ''
      }
    }
  },
  methods: {
    async login() {
      try {
        const res = await this.$axios.post('/auth/login', this.loginForm)
        if (res.code === 0) {
          localStorage.setItem('token', res.data.token)
          this.$message.success('登录成功')
          this.$router.push('/home')
        }
      } catch (error) {
        console.error('登录失败', error)
      }
    }
  }
}
</script>

<style scoped>
.login-container {
  display: flex;
  align-items: center;
  justify-content: center;
  height: 100%;
  background: #f0f2f5;
}

.login-card {
  width: 400px;
}

.login-header {
  text-align: center;
  font-weight: bold;
  font-size: 18px;
}
</style>
