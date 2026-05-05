<template>
  <el-card shadow="never" class="panel-shell">
    <div class="panel-header">
      <div>
        <div class="panel-title">{{ title }}</div>
      </div>
      <div class="action-group">
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
        @node-contextmenu="handleNodeContextMenu"
      >
        <div slot-scope="{ node, data }" class="tree-node">
          <div class="tree-node__main">
            <i :class="['tree-node__icon', getNodeIcon(data)]"></i>
            <span class="tree-node__label" :title="node.label">{{ node.label }}</span>
          </div>
        </div>
      </el-tree>
    </div>

    <ul
      v-if="contextMenu.visible"
      class="group-context-menu"
      :style="{ left: `${contextMenu.x}px`, top: `${contextMenu.y}px` }"
      @click.stop
      @contextmenu.prevent
    >
      <li @click="handleContextMenuAction('addRoot')">新增顶层分组</li>
      <template v-if="contextMenu.node">
        <li @click="handleContextMenuAction('addSibling')">新增同级分组</li>
        <li @click="handleContextMenuAction('addChild')">新增子分组</li>
        <li :class="{ disabled: !canMoveUp(contextMenu.node) }" @click="handleContextMenuAction('moveUp')">上移</li>
        <li :class="{ disabled: !canMoveDown(contextMenu.node) }" @click="handleContextMenuAction('moveDown')">下移</li>
        <li @click="handleContextMenuAction('edit')">重命名</li>
        <li
          class="danger"
          :class="{ disabled: !canDeleteGroup(contextMenu.node) }"
          @click="handleContextMenuAction('delete')"
        >
          删除分组
        </li>
      </template>
      <li @click="handleContextMenuAction('refresh')">刷新</li>
    </ul>
  </el-card>
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
      groupTree: [],
      contextMenu: {
        visible: false,
        x: 0,
        y: 0,
        node: null
      }
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
    document.addEventListener('click', this.hideContextMenu)
    window.addEventListener('blur', this.hideContextMenu)
    this.loadGroupTree()
  },
  beforeDestroy() {
    document.removeEventListener('click', this.hideContextMenu)
    window.removeEventListener('blur', this.hideContextMenu)
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
      this.hideContextMenu()
    },
    handleNodeContextMenu(event, data) {
      if (!data) {
        return
      }
      event.preventDefault()
      this.handleNodeClick(data)
      this.contextMenu = {
        visible: true,
        x: Math.min(event.clientX, window.innerWidth - 180),
        y: Math.min(event.clientY, window.innerHeight - 220),
        node: data
      }
    },
    hideContextMenu() {
      if (!this.contextMenu.visible) {
        return
      }
      this.contextMenu.visible = false
      this.contextMenu.node = null
    },
    isTotalGroupNode(group) {
      const label = String(group?.label || group?.name || '').trim()
      const id = String(group?.id || '')
      return label === '总分组' || label === '全部设备' || id === 'all'
    },
    canDeleteGroup(group) {
      return !!group && !this.isTotalGroupNode(group)
    },
    handleContextMenuAction(action) {
      const group = this.contextMenu.node
      if ((action !== 'addRoot' && action !== 'refresh') && !group) {
        return
      }
      if ((action === 'moveUp' && !this.canMoveUp(group)) || (action === 'moveDown' && !this.canMoveDown(group))) {
        return
      }
      this.hideContextMenu()
      if (action === 'addRoot') {
        this.createGroup(null)
        return
      }
      if (action === 'addSibling') {
        this.addSibling(group)
        return
      }
      if (action === 'addChild') {
        this.addChild(group)
        return
      }
      if (action === 'moveUp') {
        this.moveUp(group)
        return
      }
      if (action === 'moveDown') {
        this.moveDown(group)
        return
      }
      if (action === 'edit') {
        this.editGroup(group)
        return
      }
      if (action === 'delete') {
        if (!this.canDeleteGroup(group)) {
          this.$message.warning('总分组不能删除')
          return
        }
        this.deleteGroup(group)
        return
      }
      if (action === 'refresh') {
        this.loadGroupTree()
      }
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
      if (!this.canDeleteGroup(group)) {
        this.$message.warning('总分组不能删除')
        return
      }
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
.panel-shell {
  height: 100%;
  min-height: 0;
  display: flex;
  flex-direction: column;
}

.tree-scroll ::v-deep .el-tree-node__content {
  height: 30px;
  border-radius: 2px;
  margin-bottom: 2px;
}

.tree-scroll ::v-deep .el-tree-node.is-current > .el-tree-node__content {
  background: #e8f2ff;
}

.group-context-menu {
  position: fixed;
  z-index: 3000;
  margin: 0;
  padding: 6px 0;
  list-style: none;
  min-width: 152px;
  background: #fff;
  border: 1px solid #e4e7ed;
  border-radius: 2px;
  box-shadow: 0 6px 18px rgba(0, 0, 0, 0.15);
}

.group-context-menu li {
  padding: 6px 12px;
  font-size: 12px;
  color: #303133;
  cursor: pointer;
  user-select: none;
}

.group-context-menu li:hover {
  background: #f5f7fa;
}

.group-context-menu li.danger {
  color: #f56c6c;
}

.group-context-menu li.disabled {
  color: #c0c4cc;
  cursor: not-allowed;
}

.group-context-menu li.disabled:hover {
  background: transparent;
}
</style>
