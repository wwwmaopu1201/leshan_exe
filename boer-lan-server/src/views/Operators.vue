<template>
  <div>
    <div class="page-title">操作员管理</div>
    <el-card>
      <el-button type="primary" @click="showAddOperatorDialog" icon="el-icon-plus">新建操作员</el-button>
      <el-table :data="operators" style="margin-top: 20px;">
        <el-table-column prop="username" label="用户名" />
        <el-table-column prop="nickname" label="昵称" />
        <el-table-column prop="disabled" label="状态">
          <template slot-scope="{ row }">
            <el-tag :type="row.disabled ? 'danger' : 'success'">
              {{ row.disabled ? '已禁用' : '正常' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="200">
          <template slot-scope="{ row }">
            <el-button size="small" @click="editOperator(row)" icon="el-icon-edit">编辑</el-button>
            <el-button size="small" type="danger" @click="deleteOperator(row)" icon="el-icon-delete">删除</el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script>
export default {
  name: 'Operators',
  data() {
    return {
      operators: []
    }
  },
  mounted() {
    this.loadOperators()
  },
  methods: {
    async loadOperators() {
      try {
        const res = await this.$axios.get('/operator/all')
        if (res.code === 0) {
          this.operators = res.data
        }
      } catch (error) {
        console.error('加载操作员列表失败', error)
      }
    },
    showAddOperatorDialog() {
      this.$message.info('新建操作员功能暂未实现')
    },
    editOperator(operator) {
      this.$message.info('编辑操作员功能暂未实现')
    },
    deleteOperator(operator) {
      this.$message.info('删除操作员功能暂未实现')
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
