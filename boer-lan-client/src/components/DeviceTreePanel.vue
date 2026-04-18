<template>
  <div class="device-tree-panel panel-shell">
    <div class="panel-header">
      <div>
        <div class="panel-title">{{ title }}</div>
      </div>
      <div v-if="showRefresh" class="panel-actions">
        <el-button type="text" size="mini" @click="refreshTree">刷新</el-button>
      </div>
    </div>

    <el-input
      v-if="showSearch"
      v-model="keyword"
      size="small"
      :placeholder="searchPlaceholder"
      prefix-icon="el-icon-search"
      clearable
    />

    <div class="selection-bar" v-if="showSelection">
      <span class="selection-label">当前选择</span>
      <span class="selection-value">{{ value?.label || '全部设备' }}</span>
      <el-button type="text" size="mini" @click="clearSelection">清空</el-button>
    </div>

    <div class="tree-wrapper" :style="{ minHeight: wrapperMinHeight }" v-loading="loading">
      <el-tree
        ref="deviceTree"
        :data="deviceTree"
        :props="treeProps"
        :filter-node-method="filterNode"
        node-key="_nodeKey"
        highlight-current
        default-expand-all
        @node-click="handleNodeClick"
        @node-contextmenu="handleNodeContextMenu"
      >
        <div slot-scope="{ node, data }" class="tree-node">
          <div class="tree-node-main" :class="{ ungrouped: isUngroupedNode(data) }">
            <i :class="['tree-node-icon', getNodeIcon(data)]"></i>
            <span class="tree-node-label" :title="node.label">{{ node.label }}</span>
            <span v-if="data.type === 'device'" :class="['status-dot', data.status]"></span>
          </div>
        </div>
      </el-tree>
    </div>

    <ul
      v-if="management && contextMenu.visible"
      class="tree-context-menu"
      :style="{ left: `${contextMenu.x}px`, top: `${contextMenu.y}px` }"
      @click.stop
      @contextmenu.prevent
    >
      <li @click="handleContextMenuAction('addRoot')">新增顶层分组</li>
      <template v-if="contextMenu.node && isEditableGroup(contextMenu.node)">
        <li @click="handleContextMenuAction('addChild')">新增子组</li>
        <li @click="handleContextMenuAction('move')">移动分组</li>
        <li @click="handleContextMenuAction('edit')">重命名</li>
        <li class="danger" @click="handleContextMenuAction('delete')">删除分组</li>
      </template>
      <li @click="handleContextMenuAction('refresh')">刷新</li>
    </ul>

    <el-dialog
      :title="groupDialogTitle"
      :visible.sync="groupDialog.visible"
      width="420px"
      append-to-body
      @closed="resetGroupDialog"
    >
      <el-form ref="groupFormRef" :model="groupDialog.form" :rules="groupRules" label-width="84px">
        <el-form-item label="分组名称" prop="name">
          <el-input v-model.trim="groupDialog.form.name" placeholder="请输入分组名称" />
        </el-form-item>
        <el-form-item label="上级分组">
          <el-select v-model="groupDialog.form.parentId" clearable placeholder="顶级分组">
            <el-option
              v-for="item in parentOptions"
              :key="item.id"
              :label="item.label"
              :value="item.id"
            />
          </el-select>
        </el-form-item>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="groupDialog.visible = false">{{ $t('common.cancel') }}</el-button>
        <el-button type="primary" :loading="groupSaving" @click="submitGroupDialog">
          {{ $t('common.confirm') }}
        </el-button>
      </span>
    </el-dialog>
  </div>
</template>

<script>
import { getDeviceTree, createDeviceGroup, updateDeviceGroup, deleteDeviceGroup } from '@/api/device'

const defaultValue = () => ({
  label: '',
  nodeType: '',
  groupId: '',
  deviceId: '',
  deviceIds: []
})

export default {
  name: 'DeviceTreePanel',
  props: {
    value: {
      type: Object,
      default: defaultValue
    },
    title: {
      type: String,
      default: '设备树'
    },
    searchPlaceholder: {
      type: String,
      default: '搜索设备或分组'
    },
    management: {
      type: Boolean,
      default: false
    },
    showSelection: {
      type: Boolean,
      default: true
    },
    showSearch: {
      type: Boolean,
      default: true
    },
    showRefresh: {
      type: Boolean,
      default: true
    },
    minHeight: {
      type: Number,
      default: 420
    }
  },
  data() {
    return {
      loading: false,
      keyword: '',
      deviceTree: [],
      treeProps: {
        children: 'children',
        label: 'label'
      },
      groupSaving: false,
      groupDialog: {
        visible: false,
        mode: 'addRoot',
        currentNode: null,
        form: {
          id: null,
          name: '',
          parentId: null
        }
      },
      contextMenu: {
        visible: false,
        x: 0,
        y: 0,
        node: null
      },
      groupRules: {
        name: [{ required: true, message: '请输入分组名称', trigger: 'blur' }]
      }
    }
  },
  computed: {
    wrapperMinHeight() {
      return `${this.minHeight}px`
    },
    groupDialogTitle() {
      const map = {
        addRoot: '新增分组',
        addChild: '新增子组',
        edit: '重命名分组',
        move: '移动分组'
      }
      return map[this.groupDialog.mode] || '分组设置'
    },
    flatGroups() {
      const result = []
      const walk = (nodes = [], parentLabel = '') => {
        nodes.forEach(node => {
          if (!node || node.type === 'device') {
            return
          }
          result.push({
            id: node.id,
            label: parentLabel ? `${parentLabel} / ${node.label}` : node.label
          })
          walk(node.children || [], parentLabel ? `${parentLabel} / ${node.label}` : node.label)
        })
      }
      walk(this.deviceTree)
      return result
    },
    parentOptions() {
      const currentId = this.groupDialog.form.id
      if (!currentId) {
        return this.flatGroups
      }
      const descendants = new Set(this.getDescendantGroupIds(currentId))
      descendants.add(currentId)
      return this.flatGroups.filter(item => !descendants.has(item.id))
    }
  },
  watch: {
    keyword(val) {
      this.$refs.deviceTree?.filter(val)
    },
    value: {
      deep: true,
      handler(val) {
        const key = this.resolveNodeKey(val)
        if (key) {
          this.$nextTick(() => {
            this.$refs.deviceTree?.setCurrentKey(key)
          })
        }
      }
    }
  },
  mounted() {
    document.addEventListener('click', this.hideContextMenu)
    window.addEventListener('blur', this.hideContextMenu)
    window.addEventListener('resize', this.hideContextMenu)
    this.fetchDeviceTree()
  },
  beforeDestroy() {
    document.removeEventListener('click', this.hideContextMenu)
    window.removeEventListener('blur', this.hideContextMenu)
    window.removeEventListener('resize', this.hideContextMenu)
  },
  methods: {
    async fetchDeviceTree() {
      this.loading = true
      try {
        const res = await getDeviceTree()
        if (res.code === 0) {
          this.deviceTree = this.attachNodeKeys(res.data || [])
          this.$nextTick(() => {
            this.$refs.deviceTree?.filter(this.keyword)
            const key = this.resolveNodeKey(this.value)
            if (key) {
              this.$refs.deviceTree?.setCurrentKey(key)
            }
          })
        }
      } catch (error) {
        console.error('Failed to fetch device tree:', error)
      } finally {
        this.loading = false
      }
    },
    refreshTree() {
      this.hideContextMenu()
      this.fetchDeviceTree()
      this.$emit('refresh')
    },
    attachNodeKeys(nodes) {
      return (nodes || []).map(node => {
        const nodeType = node.type === 'device' ? 'device' : 'group'
        return {
          ...node,
          _nodeKey: `${nodeType}-${node.id}`,
          children: this.attachNodeKeys(node.children || [])
        }
      })
    },
    resolveNodeKey(value) {
      if (value?.nodeType === 'device' && value?.deviceId) {
        return `device-${value.deviceId}`
      }
      if (value?.nodeType === 'group' && value?.groupId) {
        return `group-${value.groupId}`
      }
      return ''
    },
    filterNode(value, data) {
      if (!value) return true
      return String(data.label || '').toLowerCase().includes(String(value).toLowerCase())
    },
    getNodeIcon(data) {
      if (data.type === 'device') return 'el-icon-monitor'
      return data.children && data.children.length ? 'el-icon-folder-opened' : 'el-icon-folder'
    },
    collectDeviceIds(node) {
      if (!node) return []
      if (node.type === 'device') return [Number(node.id)]

      const ids = []
      const stack = [...(node.children || [])]
      while (stack.length > 0) {
        const current = stack.pop()
        if (!current) continue
        if (current.type === 'device') {
          ids.push(Number(current.id))
        } else if (current.children?.length > 0) {
          stack.push(...current.children)
        }
      }
      return ids
    },
    handleNodeClick(data) {
      if (!data) return
      const payload = {
        label: data.label,
        nodeType: data.type === 'device' ? 'device' : 'group',
        groupId: data.type === 'group' ? String(data.id) : String(data.groupId || data.parentId || ''),
        deviceId: data.type === 'device' ? String(data.id) : '',
        deviceIds: this.collectDeviceIds(data)
      }
      this.$emit('input', payload)
      this.$emit('change', payload)
      this.hideContextMenu()
    },
    clearSelection() {
      const payload = defaultValue()
      this.$refs.deviceTree?.setCurrentKey(null)
      this.$emit('input', payload)
      this.$emit('change', payload)
    },
    isEditableGroup(data) {
      return data?.type !== 'device' && data?.id !== undefined && !this.isUngroupedNode(data)
    },
    isUngroupedNode(data) {
      const label = String(data?.label || '')
      const id = String(data?.id || '')
      return label.includes('未分组') || id === 'ungrouped'
    },
    handleNodeContextMenu(event, data) {
      if (!this.management) {
        return
      }
      event.preventDefault()
      if (data) {
        this.handleNodeClick(data)
      }
      this.contextMenu = {
        visible: true,
        x: Math.min(event.clientX, window.innerWidth - 180),
        y: Math.min(event.clientY, window.innerHeight - 220),
        node: data || null
      }
    },
    hideContextMenu() {
      if (!this.contextMenu.visible) {
        return
      }
      this.contextMenu.visible = false
      this.contextMenu.node = null
    },
    handleContextMenuAction(action) {
      const node = this.contextMenu.node
      this.hideContextMenu()

      if (action === 'addRoot') {
        this.openAddRootDialog()
        return
      }

      if (action === 'refresh') {
        this.refreshTree()
        return
      }

      if (!node || !this.isEditableGroup(node)) {
        return
      }

      if (action === 'addChild') {
        this.openAddChildDialog(node)
        return
      }
      if (action === 'move') {
        this.openMoveDialog(node)
        return
      }
      if (action === 'edit') {
        this.openEditDialog(node)
        return
      }
      if (action === 'delete') {
        this.handleDeleteGroup(node)
      }
    },
    openAddRootDialog() {
      this.groupDialog = {
        visible: true,
        mode: 'addRoot',
        currentNode: null,
        form: {
          id: null,
          name: '',
          parentId: null
        }
      }
    },
    openAddChildDialog(node) {
      this.groupDialog = {
        visible: true,
        mode: 'addChild',
        currentNode: node,
        form: {
          id: null,
          name: '',
          parentId: node.id
        }
      }
    },
    openEditDialog(node) {
      this.groupDialog = {
        visible: true,
        mode: 'edit',
        currentNode: node,
        form: {
          id: node.id,
          name: node.label,
          parentId: node.parentId || null
        }
      }
    },
    openMoveDialog(node) {
      this.groupDialog = {
        visible: true,
        mode: 'move',
        currentNode: node,
        form: {
          id: node.id,
          name: node.label,
          parentId: node.parentId || null
        }
      }
    },
    resetGroupDialog() {
      this.groupSaving = false
      this.$refs.groupFormRef?.resetFields()
      this.groupDialog = {
        visible: false,
        mode: 'addRoot',
        currentNode: null,
        form: {
          id: null,
          name: '',
          parentId: null
        }
      }
    },
    async submitGroupDialog() {
      try {
        await this.$refs.groupFormRef.validate()
      } catch (error) {
        return
      }

      this.groupSaving = true
      const payload = {
        name: this.groupDialog.form.name,
        parentId: this.groupDialog.form.parentId || null
      }

      try {
        let res
        if (this.groupDialog.mode === 'edit' || this.groupDialog.mode === 'move') {
          res = await updateDeviceGroup(this.groupDialog.form.id, payload)
        } else {
          res = await createDeviceGroup(payload)
        }

        if (res.code === 0) {
          this.$message.success('分组已更新')
          this.groupDialog.visible = false
          await this.fetchDeviceTree()
          this.$emit('refresh')
        } else {
          this.$message.error(res.message || '分组更新失败')
        }
      } catch (error) {
        console.error('Save group failed:', error)
        this.$message.error('分组更新失败')
      } finally {
        this.groupSaving = false
      }
    },
    async handleDeleteGroup(node) {
      this.$confirm(`确定删除分组“${node.label}”吗？`, this.$t('common.warning'), {
        confirmButtonText: this.$t('common.confirm'),
        cancelButtonText: this.$t('common.cancel'),
        type: 'warning'
      }).then(async () => {
        try {
          const res = await deleteDeviceGroup(node.id)
          if (res.code === 0) {
            this.$message.success('分组已删除')
            await this.fetchDeviceTree()
            this.$emit('refresh')
          } else {
            this.$message.error(res.message || '删除分组失败')
          }
        } catch (error) {
          console.error('Delete group failed:', error)
          this.$message.error('删除分组失败')
        }
      }).catch(() => {})
    },
    getDescendantGroupIds(groupId) {
      const descendants = []
      const walk = (nodes = []) => {
        nodes.forEach(node => {
          if (!node || node.type === 'device') return
          if (node.id === groupId) {
            collect(node.children || [])
          } else {
            walk(node.children || [])
          }
        })
      }
      const collect = (nodes = []) => {
        nodes.forEach(node => {
          if (!node || node.type === 'device') return
          descendants.push(node.id)
          collect(node.children || [])
        })
      }
      walk(this.deviceTree)
      return descendants
    }
  }
}
</script>

<style lang="scss" scoped>
.device-tree-panel {
  padding: 8px 10px;
  height: 100%;
  min-height: 0;
  display: flex;
  flex-direction: column;
}

.panel-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  margin-bottom: 8px;
}

.panel-title {
  font-size: 13px;
  font-weight: 700;
  color: #4c5768;
}

.panel-subtitle {
  margin-top: 4px;
  font-size: 12px;
  color: #8494ab;
}

.panel-actions {
  display: flex;
  align-items: center;
  gap: 4px;
}

.selection-bar {
  margin-top: 8px;
  margin-bottom: 8px;
  padding: 6px 8px;
  border-radius: 2px;
  background: #f7f9fc;
  border: 1px solid #e0e6ee;
  display: flex;
  align-items: center;
  gap: 8px;
}

.selection-label {
  color: #9099a5;
  font-size: 11px;
}

.selection-value {
  flex: 1;
  min-width: 0;
  color: #4c5768;
  font-weight: 600;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.tree-wrapper {
  flex: 1;
  min-height: 0;
  margin-top: 6px;
  border: 1px solid #dfe6ee;
  border-radius: 2px;
  padding: 6px;
  overflow: auto;
  background: #ffffff;
}

.tree-node {
  width: 100%;
  min-width: 0;
  display: flex;
  align-items: center;
  overflow: hidden;
}

.tree-node-main {
  width: 100%;
  display: inline-flex;
  align-items: center;
  gap: 8px;
  min-width: 0;

  &.ungrouped {
    color: #d94a4a;
    font-weight: 700;
  }
}

.tree-node-icon {
  color: #6d7e94;
}

.tree-node-label {
  flex: 1;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.danger-text {
  color: #ef5a5a !important;
}

.status-dot {
  flex-shrink: 0;
  margin-left: auto;
  width: 8px;
  height: 8px;
  border-radius: 50%;

  &.online,
  &.idle {
    background: #2fb46e;
  }

  &.working {
    background: #2f6df6;
  }

  &.alarm {
    background: #ef5a5a;
  }

  &.offline {
    background: #8a98ad;
  }
}

::v-deep .el-tree {
  background: transparent;
}

::v-deep .el-tree-node__content {
  height: 30px;
  border-radius: 2px;
  margin-bottom: 2px;
  padding-right: 4px;
}

::v-deep .el-tree-node__content:hover {
  background: #f5f8fc;
}

::v-deep .el-tree--highlight-current .el-tree-node.is-current > .el-tree-node__content {
  background: #e8f2ff;
  color: #3388ff;
}

.tree-context-menu {
  position: fixed;
  z-index: 3000;
  margin: 0;
  padding: 6px 0;
  min-width: 152px;
  list-style: none;
  background: #fff;
  border: 1px solid #dfe6ee;
  border-radius: 2px;
  box-shadow: 0 6px 18px rgba(0, 0, 0, 0.15);

  li {
    padding: 6px 12px;
    font-size: 12px;
    line-height: 1.5;
    color: #4c5768;
    cursor: pointer;

    &:hover {
      background: #f5f8fc;
    }

    &.danger {
      color: #ef5a5a;
    }
  }
}
</style>
