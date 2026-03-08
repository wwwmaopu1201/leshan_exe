<template>
  <div>
    <div class="page-title">分组管理</div>
    <el-card>
      <el-button type="primary" @click="showAddGroupDialog" icon="el-icon-plus">新建分组</el-button>
      <el-tree
        :data="groupTree"
        :props="{ label: 'name', children: 'children' }"
        style="margin-top: 20px;"
      >
        <span slot-scope="{ node, data }" class="tree-node">
          <span>{{ node.label }}</span>
          <span>
            <el-button size="mini" @click.stop="editGroup(data)" icon="el-icon-edit">编辑</el-button>
            <el-button size="mini" type="danger" @click.stop="deleteGroup(data)" icon="el-icon-delete">删除</el-button>
          </span>
        </span>
      </el-tree>
    </el-card>
  </div>
</template>

<script>
export default {
  name: 'Groups',
  data() {
    return {
      groupTree: []
    }
  },
  mounted() {
    this.loadGroupTree()
  },
  methods: {
    async loadGroupTree() {
      try {
        const res = await this.$axios.get('/group/tree')
        if (res.code === 0) {
          this.groupTree = res.data
        }
      } catch (error) {
        console.error('加载分组树失败', error)
      }
    },
    showAddGroupDialog() {
      this.$prompt('请输入分组名称', '新建分组', {
        confirmButtonText: '确定',
        cancelButtonText: '取消'
      }).then(async ({ value }) => {
        try {
          await this.$axios.post('/group', { name: value })
          this.$message.success('创建成功')
          await this.loadGroupTree()
        } catch (error) {
          console.error('创建分组失败', error)
        }
      }).catch(() => {})
    },
    editGroup(group) {
      this.$prompt('请输入新的分组名称', '编辑分组', {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        inputValue: group.name
      }).then(async ({ value }) => {
        try {
          await this.$axios.put(`/group/${group.id}`, { name: value })
          this.$message.success('修改成功')
          await this.loadGroupTree()
        } catch (error) {
          console.error('修改分组失败', error)
        }
      }).catch(() => {})
    },
    async deleteGroup(group) {
      try {
        await this.$confirm('确定要删除该分组吗?', '警告', {
          type: 'warning'
        })
        await this.$axios.delete(`/group/${group.id}`)
        this.$message.success('删除成功')
        await this.loadGroupTree()
      } catch (error) {
        if (error !== 'cancel') {
          console.error('删除分组失败', error)
        }
      }
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

.tree-node {
  flex: 1;
  display: flex;
  align-items: center;
  justify-content: space-between;
  font-size: 14px;
  padding-right: 8px;
}
</style>
