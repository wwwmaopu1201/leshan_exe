<template>
  <div>
    <div class="page-title">分组管理</div>
    <el-card v-loading="loading">
      <div class="toolbar">
        <el-button type="primary" @click="createGroup(null)" icon="el-icon-plus">新建顶层分组</el-button>
        <el-button icon="el-icon-refresh" @click="loadGroupTree">刷新</el-button>
      </div>
      <el-tree
        :data="groupTree"
        node-key="id"
        default-expand-all
        :props="{ label: 'name', children: 'children' }"
        style="margin-top: 20px;"
      >
        <span slot-scope="{ node, data }" class="tree-node">
          <span class="tree-node-label">
            <span>{{ node.label }}</span>
            <el-tag size="mini" type="info">
              {{ node.level === 1 ? '一级组' : '子组' }}
            </el-tag>
          </span>
          <span>
            <el-button size="mini" @click.stop="addSibling(data)">同级</el-button>
            <el-button size="mini" @click.stop="addChild(data)">子组</el-button>
            <el-button size="mini" :disabled="!canMoveUp(data)" @click.stop="moveUp(data)">上移</el-button>
            <el-button size="mini" :disabled="!canMoveDown(data)" @click.stop="moveDown(data)">下移</el-button>
            <el-button size="mini" @click.stop="editGroup(data)" icon="el-icon-edit">重命名</el-button>
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
      loading: false,
      groupTree: []
    }
  },
  mounted() {
    this.loadGroupTree()
  },
  methods: {
    normalizeTree(nodes = []) {
      return nodes
        .map(item => ({
          ...item,
          id: item.id || item.ID,
          parentId: item.parentId || item.ParentID || null,
          sortOrder: item.sortOrder || item.SortOrder || 0,
          children: this.normalizeTree(item.children || item.Children || [])
        }))
        .sort((a, b) => (a.sortOrder - b.sortOrder) || (a.id - b.id))
    },
    async loadGroupTree() {
      this.loading = true
      try {
        const res = await this.$axios.get('/group/tree')
        if (res.code === 0) {
          this.groupTree = this.normalizeTree(Array.isArray(res.data) ? res.data : [])
        }
      } catch (error) {
        console.error('加载分组树失败', error)
      } finally {
        this.loading = false
      }
    },
    createGroup(parentId) {
      const title = parentId ? '新增子分组' : '新建顶层分组'
      this.$prompt('请输入分组名称', title, {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        inputValidator: value => {
          if (!value || !value.trim()) return '分组名称不能为空'
          if ([...value.trim()].length > 50) return '分组名称不能超过50个字符'
          return true
        }
      }).then(async ({ value }) => {
        try {
          const payload = { name: value.trim() }
          if (parentId) {
            payload.parentId = parentId
          }
          await this.$axios.post('/group', payload)
          this.$message.success('创建成功')
          await this.loadGroupTree()
        } catch (error) {
          console.error('创建分组失败', error)
        }
      }).catch(() => {})
    },
    addSibling(group) {
      this.createGroup(group.parentId || null)
    },
    addChild(group) {
      this.createGroup(group.id)
    },
    editGroup(group) {
      this.$prompt('请输入新的分组名称', '重命名分组', {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        inputValue: group.name,
        inputValidator: value => {
          if (!value || !value.trim()) return '分组名称不能为空'
          if ([...value.trim()].length > 50) return '分组名称不能超过50个字符'
          return true
        }
      }).then(async ({ value }) => {
        try {
          await this.$axios.put(`/group/${group.id}`, { name: value.trim() })
          this.$message.success('修改成功')
          await this.loadGroupTree()
        } catch (error) {
          console.error('修改分组失败', error)
        }
      }).catch(() => {})
    },
    findNodeContext(targetId, nodes = this.groupTree, parent = null) {
      for (let i = 0; i < nodes.length; i++) {
        const current = nodes[i]
        if (current.id === targetId) {
          return {
            parent,
            siblings: nodes,
            index: i
          }
        }
        if (current.children && current.children.length > 0) {
          const found = this.findNodeContext(targetId, current.children, current)
          if (found) return found
        }
      }
      return null
    },
    canMoveUp(group) {
      const context = this.findNodeContext(group.id)
      return !!context && context.index > 0
    },
    canMoveDown(group) {
      const context = this.findNodeContext(group.id)
      return !!context && context.index < context.siblings.length - 1
    },
    async persistSort(siblings) {
      const payload = siblings.map((item, index) => ({
        id: item.id,
        sortOrder: index + 1
      }))
      const res = await this.$axios.post('/group/sort', payload)
      if (res.code === 0) {
        this.$message.success('排序已更新')
      }
      await this.loadGroupTree()
    },
    async moveUp(group) {
      const context = this.findNodeContext(group.id)
      if (!context || context.index <= 0) return
      const { siblings, index } = context
      const current = siblings[index]
      siblings.splice(index, 1)
      siblings.splice(index - 1, 0, current)
      await this.persistSort(siblings)
    },
    async moveDown(group) {
      const context = this.findNodeContext(group.id)
      if (!context || context.index >= context.siblings.length - 1) return
      const { siblings, index } = context
      const current = siblings[index]
      siblings.splice(index, 1)
      siblings.splice(index + 1, 0, current)
      await this.persistSort(siblings)
    },
    async deleteGroup(group) {
      try {
        await this.$confirm(
          '确定要删除该分组吗？删除后该分组下账号、设备、操作员将转为未分组，子分组将提升到当前层级。',
          '警告',
          {
          type: 'warning'
          }
        )
        const res = await this.$axios.delete(`/group/${group.id}`)
        this.$message.success(res.msg || '删除成功')
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

.toolbar {
  margin-bottom: 8px;
  display: flex;
  gap: 8px;
}

.tree-node {
  flex: 1;
  display: flex;
  align-items: center;
  justify-content: space-between;
  font-size: 14px;
  gap: 12px;
  padding-right: 8px;
}

.tree-node-label {
  display: inline-flex;
  align-items: center;
  gap: 8px;
}
</style>
