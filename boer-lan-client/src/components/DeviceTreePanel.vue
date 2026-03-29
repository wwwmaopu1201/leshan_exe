<template>
  <div class="device-tree-panel panel-shell">
    <div class="panel-header">
      <div>
        <div class="panel-title">{{ title }}</div>
      </div>
      <div class="panel-actions">
        <el-button
          v-if="management"
          type="primary"
          size="mini"
          icon="el-icon-plus"
          circle
          @click="openAddRootDialog"
        />
        <el-button type="text" size="mini" @click="refreshTree">刷新</el-button>
      </div>
    </div>

    <el-input
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
      >
        <div slot-scope="{ node, data }" class="tree-node">
          <div class="tree-node-main" :class="{ ungrouped: isUngroupedNode(data) }">
            <i :class="['tree-node-icon', getNodeIcon(data)]"></i>
            <span class="tree-node-label" :title="node.label">{{ node.label }}</span>
            <span v-if="data.type === 'device'" :class="['status-dot', data.status]"></span>
          </div>
          <div v-if="management && isEditableGroup(data)" class="tree-node-tools">
            <el-button type="text" size="mini" title="新增子组" @click.stop="openAddChildDialog(data)">
              <i class="el-icon-plus"></i>
            </el-button>
            <el-button type="text" size="mini" title="移动分组" @click.stop="openMoveDialog(data)">
              <i class="el-icon-rank"></i>
            </el-button>
            <el-button type="text" size="mini" title="重命名" @click.stop="openEditDialog(data)">
              <i class="el-icon-edit"></i>
            </el-button>
            <el-button type="text" size="mini" class="danger-text" title="删除分组" @click.stop="handleDeleteGroup(data)">
              <i class="el-icon-delete"></i>
            </el-button>
          </div>
        </div>
      </el-tree>
    </div>

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
    this.fetchDeviceTree()
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
  padding: 18px;
}

.panel-header {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 14px;
}

.panel-title {
  font-size: 16px;
  font-weight: 700;
  color: #243654;
}

.panel-subtitle {
  margin-top: 4px;
  font-size: 12px;
  color: #8494ab;
}

.panel-actions {
  display: flex;
  align-items: center;
  gap: 6px;
}

.selection-bar {
  margin-top: 12px;
  margin-bottom: 10px;
  padding: 10px 12px;
  border-radius: 14px;
  background: #f5f8fd;
  display: flex;
  align-items: center;
  gap: 8px;
}

.selection-label {
  color: #7a8aa3;
  font-size: 12px;
}

.selection-value {
  flex: 1;
  min-width: 0;
  color: #24416f;
  font-weight: 600;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.tree-wrapper {
  margin-top: 10px;
  border: 1px solid #e6edf7;
  border-radius: 18px;
  padding: 10px;
  overflow: auto;
  background: #fbfdff;
}

.tree-node {
  width: 100%;
  min-width: 0;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
}

.tree-node-main {
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
  color: #4f7bc3;
}

.tree-node-label {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.tree-node-tools {
  display: inline-flex;
  align-items: center;
  gap: 2px;
  flex-shrink: 0;
}

.danger-text {
  color: #ef5a5a !important;
}

.status-dot {
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
  height: 38px;
  border-radius: 12px;
  margin-bottom: 4px;
  padding-right: 6px;
}

::v-deep .el-tree-node__content:hover {
  background: #f0f5fd;
}

::v-deep .el-tree--highlight-current .el-tree-node.is-current > .el-tree-node__content {
  background: linear-gradient(135deg, rgba(47, 109, 246, 0.14), rgba(77, 168, 255, 0.1));
  color: #244d97;
}
</style>
