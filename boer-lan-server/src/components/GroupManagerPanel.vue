<template>
  <div class="panel-shell">
    <div class="panel-header">
      <div>
        <div class="panel-title">{{ title }}</div>
      </div>
      <div class="action-group">
        <el-button type="primary" size="mini" icon="el-icon-plus" circle @click="createGroup(null)" />
        <el-button type="text" size="mini" @click="loadGroupTree">刷新</el-button>
      </div>
    </div>

    <el-input
      v-model="keyword"
      size="small"
      placeholder="搜索分组"
      prefix-icon="el-icon-search"
      clearable
    />

    <div v-if="showSelection" class="selection-strip">
      <div style="min-width: 0;">
        <span class="selection-strip__label">当前选择</span>
        <span class="selection-strip__value">{{ currentLabel }}</span>
      </div>
      <el-button type="text" size="mini" @click="clearSelection">清空</el-button>
    </div>

    <div class="tree-scroll" :style="{ maxHeight: `${height}px` }" v-loading="loading">
      <el-tree
        ref="groupTreeRef"
        :data="groupTree"
        node-key="id"
        default-expand-all
        highlight-current
        :filter-node-method="filterNode"
        :props="{ label: 'name', children: 'children' }"
        @node-click="handleNodeClick"
      >
        <div slot-scope="{ node, data }" class="tree-node">
          <div class="tree-node__main">
            <i :class="['tree-node__icon', getNodeIcon(data)]"></i>
            <span class="tree-node__label" :title="node.label">{{ node.label }}</span>
          </div>
          <div class="tree-node__actions">
            <el-button type="text" size="mini" title="同级分组" @click.stop="addSibling(data)">
              <i class="el-icon-plus"></i>
            </el-button>
            <el-button type="text" size="mini" title="子分组" @click.stop="addChild(data)">
              <i class="el-icon-folder-add"></i>
            </el-button>
            <el-button type="text" size="mini" :disabled="!canMoveUp(data)" title="上移" @click.stop="moveUp(data)">
              <i class="el-icon-top"></i>
            </el-button>
            <el-button type="text" size="mini" :disabled="!canMoveDown(data)" title="下移" @click.stop="moveDown(data)">
              <i class="el-icon-bottom"></i>
            </el-button>
            <el-button type="text" size="mini" title="重命名" @click.stop="editGroup(data)">
              <i class="el-icon-edit"></i>
            </el-button>
            <el-button type="text" size="mini" class="danger-text" title="删除分组" @click.stop="deleteGroup(data)">
              <i class="el-icon-delete"></i>
            </el-button>
          </div>
        </div>
      </el-tree>
    </div>
  </div>
</template>

<script>
export default {
  name: 'GroupManagerPanel',
  props: {
    value: {
      type: Object,
      default: () => ({})
    },
    title: {
      type: String,
      default: '分组管理'
    },
    subtitle: {
      type: String,
      default: '按工厂或区域维护分组层级'
    },
    showSelection: {
      type: Boolean,
      default: true
    },
    height: {
      type: Number,
      default: 560
    }
  },
  data() {
    return {
      loading: false,
      keyword: '',
      groupTree: []
    }
  },
  computed: {
    currentLabel() {
      return this.value?.label || '全部账号'
    }
  },
  watch: {
    keyword(val) {
      this.$refs.groupTreeRef?.filter(val)
    },
    value: {
      deep: true,
      handler(val) {
        const key = this.resolveNodeKey(val)
        this.$nextTick(() => {
          this.$refs.groupTreeRef?.setCurrentKey(key || null)
        })
      }
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
          this.$nextTick(() => {
            this.$refs.groupTreeRef?.filter(this.keyword)
            const key = this.resolveNodeKey(this.value)
            if (key) {
              this.$refs.groupTreeRef?.setCurrentKey(key)
            }
          })
          this.$emit('loaded', this.groupTree)
          this.$emit('refresh')
        }
      } catch (error) {
        console.error('加载分组树失败', error)
      } finally {
        this.loading = false
      }
    },
    resolveNodeKey(value) {
      if (!value || value.mode !== 'group') return ''
      return value.groupId || ''
    },
    filterNode(value, data) {
      if (!value) return true
      return String(data?.name || '').toLowerCase().includes(value.toLowerCase())
    },
    getNodeIcon(data) {
      return data?.children?.length ? 'el-icon-folder-opened' : 'el-icon-folder'
    },
    handleNodeClick(data) {
      if (!data) return
      const payload = {
        mode: 'group',
        groupId: data.id,
        label: data.name
      }
      this.$emit('input', payload)
      this.$emit('change', payload)
    },
    clearSelection() {
      this.$refs.groupTreeRef?.setCurrentKey(null)
      const payload = {
        mode: 'all',
        groupId: null,
        label: ''
      }
      this.$emit('input', payload)
      this.$emit('change', payload)
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
          '确定要删除该分组吗？删除后该分组下账号、设备、操作员将转为未分组，子分组会提升到当前层级。',
          '警告',
          { type: 'warning' }
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
.tree-scroll ::v-deep .el-tree-node__content {
  height: 38px;
  border-radius: 12px;
  margin-bottom: 4px;
}

.tree-scroll ::v-deep .el-tree-node.is-current > .el-tree-node__content {
  background: rgba(47, 109, 246, 0.1);
}
</style>
