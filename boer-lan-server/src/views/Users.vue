<template>
  <div>
    <div class="page-title">用户管理</div>
    <el-card>
      <el-button type="primary" @click="showAddUserDialog" icon="el-icon-plus">新建用户</el-button>
      <el-table :data="users" style="margin-top: 20px;">
        <el-table-column prop="username" label="用户名" />
        <el-table-column prop="nickname" label="昵称" />
        <el-table-column prop="role" label="角色" />
        <el-table-column prop="disabled" label="状态">
          <template slot-scope="{ row }">
            <el-tag :type="row.disabled ? 'danger' : 'success'">
              {{ row.disabled ? '已禁用' : '正常' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="200">
          <template slot-scope="{ row }">
            <el-button size="small" @click="editUser(row)" icon="el-icon-edit">编辑</el-button>
            <el-button size="small" type="danger" @click="deleteUser(row)" icon="el-icon-delete">删除</el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script>
export default {
  name: 'Users',
  data() {
    return {
      users: []
    }
  },
  mounted() {
    this.loadUsers()
  },
  methods: {
    async loadUsers() {
      try {
        const res = await this.$axios.get('/user/all')
        if (res.code === 0) {
          this.users = res.data
        }
      } catch (error) {
        console.error('加载用户列表失败', error)
      }
    },
    showAddUserDialog() {
      this.$message.info('新建用户功能暂未实现')
    },
    editUser(user) {
      this.$message.info('编辑用户功能暂未实现')
    },
    deleteUser(user) {
      this.$message.info('删除用户功能暂未实现')
    }
  }
}
</script>

<style scoped>
.page-title {
  font-size: 20px;
  font-weight: bold;
  margin-bottom: 20px;
}
</style>
