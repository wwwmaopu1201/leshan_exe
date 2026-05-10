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
      <span class="selection-value">{{ value?.label || '总分组' }}</span>
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
        <div
          slot-scope="{ node, data }"
          class="tree-node"
          :class="{
            'tree-node-device': data.type === 'device',
            'tree-node-drop-target': isManualDragDropTarget(data),
            'tree-node-drop-hover': manualDrag.targetKey === data._nodeKey
          }"
          :data-tree-node-key="data._nodeKey"
          @mousedown.left="handleManualDeviceMouseDown($event, data)"
        >
          <div class="tree-node-main" :class="{ ungrouped: isUngroupedNode(data) }">
            <el-checkbox
              v-if="checkableDevices && data.type === 'device'"
              class="tree-node-checkbox"
              :value="isDeviceChecked(data)"
              @input="checked => handleDeviceCheckboxChange(data, checked)"
              @click.native.stop
              @mousedown.native.stop
            />
            <i :class="['tree-node-icon', getNodeIcon(data)]"></i>
            <span class="tree-node-label" :title="node.label">{{ node.label }}</span>
            <span v-if="data.type === 'device'" :class="['status-dot', data.status]"></span>
          </div>
        </div>
      </el-tree>
      <div
        v-if="manualDrag.active && manualDrag.moved"
        class="manual-drag-ghost"
        :style="{ left: `${manualDrag.x + 12}px`, top: `${manualDrag.y + 12}px` }"
      >
        {{ manualDrag.device?.label || '移动设备' }}
      </div>
    </div>

    <ul
      v-if="management && contextMenu.visible"
      class="tree-context-menu"
      :style="{ left: `${contextMenu.x}px`, top: `${contextMenu.y}px` }"
      @click.stop
      @contextmenu.prevent
    >
      <template v-if="contextMenu.node && contextMenu.node.type === 'device'">
        <li class="danger" @click="handleContextMenuAction('removeDeviceFromGroup')">删除设备</li>
        <li @click="handleContextMenuAction('renameDevice')">重命名</li>
      </template>
      <template v-else>
        <li @click="handleContextMenuAction('addRoot')">新增分组</li>
      </template>
      <template v-if="contextMenu.node && isGroupContextNode(contextMenu.node)">
        <template v-if="canModifyGroup(contextMenu.node)">
          <li @click="handleContextMenuAction('addSibling')">新增同级分组</li>
        </template>
        <li @click="handleContextMenuAction('addChild')">新增子分组</li>
        <li
          v-if="showMoveSelected"
          :class="{ disabled: !canMoveSelectedToGroup(contextMenu.node) }"
          @click="handleContextMenuAction('moveSelectedHere')"
        >
          移动已选设备到当前组
        </li>
        <template v-if="canModifyGroup(contextMenu.node)">
          <li :class="{ disabled: !canMoveUp(contextMenu.node) }" @click="handleContextMenuAction('moveUp')">上移</li>
          <li :class="{ disabled: !canMoveDown(contextMenu.node) }" @click="handleContextMenuAction('moveDown')">下移</li>
          <li @click="handleContextMenuAction('edit')">重命名</li>
          <li class="danger" @click="handleContextMenuAction('delete')">删除分组</li>
        </template>
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
import { getDeviceTree, createDeviceGroup, updateDeviceGroup, deleteDeviceGroup, moveToGroup, updateDevice } from '@/api/device'

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
    },
    selectedDeviceIds: {
      type: Array,
      default: () => []
    },
    showMoveSelected: {
      type: Boolean,
      default: false
    },
    checkableDevices: {
      type: Boolean,
      default: false
    },
    checkedDeviceIds: {
      type: Array,
      default: () => []
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
      draggingDeviceNode: null,
      manualDrag: {
        active: false,
        moved: false,
        device: null,
        targetKey: '',
        targetNode: null,
        startX: 0,
        startY: 0,
        x: 0,
        y: 0
      },
      localCheckedDeviceIds: [],
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
        addSibling: '新增同级分组',
        addChild: '新增子分组',
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
    },
    checkedDeviceIds: {
      immediate: true,
      handler(ids) {
        this.localCheckedDeviceIds = this.normalizeDeviceIds(ids)
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
    this.removeManualDragListeners()
  },
  methods: {
    async fetchDeviceTree() {
      this.loading = true
      try {
        const res = await getDeviceTree()
        if (res.code === 0) {
          this.deviceTree = this.attachNodeKeys(this.normalizeDeviceTree(res.data || []))
          this.$nextTick(() => {
            this.$refs.deviceTree?.filter(this.keyword)
            const key = this.resolveNodeKey(this.value)
            if (key) {
              this.$refs.deviceTree?.setCurrentKey(key)
            }
          })
          return this.deviceTree
        }
      } catch (error) {
        console.error('Failed to fetch device tree:', error)
      } finally {
        this.loading = false
      }
      return this.deviceTree
    },
    async refreshTree() {
      this.hideContextMenu()
      await this.fetchDeviceTree()
      const payload = this.syncCurrentSelection()
      this.$emit('refresh', payload)
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
    normalizeDeviceTree(nodes = []) {
      const cloneNode = node => ({
        ...node,
        type: node.type === 'device' ? 'device' : 'group',
        label: this.isTotalGroupNode(node) ? '总分组' : (node.label || node.name || ''),
        children: (node.children || []).map(cloneNode)
      })
      const roots = (nodes || []).map(cloneNode)
      const ungroupedNodes = []
      const groupNodes = []

      roots.forEach(node => {
        if (this.isUngroupedNode(node)) {
          ungroupedNodes.push({
            ...node,
            id: 'ungrouped',
            label: '未分组设备',
            type: 'group',
            isVirtual: true
          })
          return
        }
        groupNodes.push(node)
      })

      const ungroupedNode = ungroupedNodes[0] || {
        id: 'ungrouped',
        label: '未分组设备',
        type: 'group',
        children: [],
        isVirtual: true
      }

      return [{
        id: 'all',
        label: '总分组',
        type: 'group',
        children: [ungroupedNode, ...groupNodes],
        isVirtual: true
      }]
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
    normalizeDeviceIds(ids = []) {
      return Array.from(new Set((ids || [])
        .map(id => Number(id))
        .filter(id => Number.isFinite(id) && id > 0)))
    },
    isDeviceChecked(data) {
      const deviceId = Number(data?.id || 0)
      return deviceId > 0 && this.localCheckedDeviceIds.includes(deviceId)
    },
    handleDeviceCheckboxChange(data, checked) {
      const deviceId = Number(data?.id || 0)
      if (!deviceId) return
      const next = new Set(this.localCheckedDeviceIds)
      if (checked) {
        next.add(deviceId)
      } else {
        next.delete(deviceId)
      }
      this.localCheckedDeviceIds = Array.from(next)
      this.emitDeviceCheckChange()
    },
    collectDeviceNodesByIds(ids = this.localCheckedDeviceIds) {
      const wanted = new Set(this.normalizeDeviceIds(ids))
      const result = []
      const stack = [...(this.deviceTree || [])]
      while (stack.length > 0) {
        const current = stack.shift()
        if (!current) continue
        if (current.type === 'device' && wanted.has(Number(current.id))) {
          result.push(current)
        }
        if (current.children?.length) {
          stack.push(...current.children)
        }
      }
      return result
    },
    emitDeviceCheckChange() {
      const deviceIds = this.normalizeDeviceIds(this.localCheckedDeviceIds)
      const devices = this.collectDeviceNodesByIds(deviceIds)
      this.$emit('update:checkedDeviceIds', deviceIds)
      this.$emit('device-check-change', { deviceIds, devices })
    },
    buildSelectionPayload(data) {
      if (!data) return
      return {
        label: data.label,
        nodeType: data.type === 'device' ? 'device' : 'group',
        groupId: data.type === 'group' ? String(data.id) : String(data.groupId || data.parentId || ''),
        deviceId: data.type === 'device' ? String(data.id) : '',
        deviceIds: this.collectDeviceIds(data),
        employeeCode: data.type === 'device' ? String(data.employeeCode || '') : '',
        employeeName: data.type === 'device' ? String(data.employeeName || '') : ''
      }
    },
    findNodeByKey(key) {
      if (!key) return null
      const stack = [...(this.deviceTree || [])]
      while (stack.length > 0) {
        const current = stack.shift()
        if (!current) continue
        if (current._nodeKey === key) {
          return current
        }
        if (current.children?.length) {
          stack.push(...current.children)
        }
      }
      return null
    },
    syncCurrentSelection() {
      const key = this.resolveNodeKey(this.value)
      if (!key) {
        return this.value || defaultValue()
      }

      const node = this.findNodeByKey(key)
      if (!node) {
        const payload = defaultValue()
        this.$refs.deviceTree?.setCurrentKey(null)
        this.$emit('input', payload)
        return payload
      }

      const payload = this.buildSelectionPayload(node)
      this.$nextTick(() => {
        this.$refs.deviceTree?.setCurrentKey(key)
      })
      this.$emit('input', payload)
      return payload
    },
    async refreshAndSyncSelection() {
      await this.fetchDeviceTree()
      return this.syncCurrentSelection()
    },
    handleNodeClick(data) {
      const payload = this.buildSelectionPayload(data)
      if (!payload) return
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
    isGroupContextNode(data) {
      return this.isEditableGroup(data) && String(data?.id || '') !== 'all'
    },
    isTotalGroupNode(data) {
      const label = String(data?.label || data?.name || '').trim()
      const id = String(data?.id || '')
      return label === '总分组' || label === '全部设备' || id === 'all'
    },
    canDeleteGroup(data) {
      return this.canModifyGroup(data)
    },
    canModifyGroup(data) {
      return this.isEditableGroup(data) && !this.isTotalGroupNode(data)
    },
    canMoveSelectedToGroup(data) {
      return this.isGroupContextNode(data) && Number(data?.id || 0) > 0 && this.selectedDeviceIds.length > 0
    },
    findGroupContext(targetId, nodes = this.deviceTree, parent = null) {
      const groups = (nodes || []).filter(node => node && node.type !== 'device' && !this.isUngroupedNode(node))
      for (let i = 0; i < groups.length; i++) {
        const current = groups[i]
        if (Number(current.id) === Number(targetId)) {
          return {
            parent,
            siblings: groups,
            index: i
          }
        }
        if (current.children?.length) {
          const found = this.findGroupContext(targetId, current.children, current)
          if (found) return found
        }
      }
      return null
    },
    canMoveUp(data) {
      if (!this.canModifyGroup(data)) return false
      const context = this.findGroupContext(data.id)
      return !!context && context.index > 0
    },
    canMoveDown(data) {
      if (!this.canModifyGroup(data)) return false
      const context = this.findGroupContext(data.id)
      return !!context && context.index < context.siblings.length - 1
    },
    allowTreeDrag(node) {
      return this.management && node?.data?.type === 'device'
    },
    allowTreeDrop(draggingNode, dropNode, type) {
      const dragging = draggingNode?.data
      if (!this.management || !dragging) {
        return false
      }
      if (dragging.type !== 'device') {
        return false
      }
      const targetGroupId = this.resolveDropTargetGroupId(dropNode, type)
      return targetGroupId === null || targetGroupId > 0
    },
    resolveDropTargetGroupId(dropNode, dropType) {
      const target = dropNode?.data
      if (!target) {
        return undefined
      }
      if (this.isUngroupedNode(target)) {
        return null
      }
      if (target.type === 'device') {
        const groupId = Number(target.groupId || 0)
        return groupId > 0 ? groupId : null
      }
      if (this.isGroupContextNode(target) && Number(target.id || 0) > 0) {
        return Number(target.id)
      }
      if (dropType !== 'inner') {
        const parent = dropNode?.parent?.data
        if (this.isUngroupedNode(parent)) {
          return null
        }
        if (this.isGroupContextNode(parent) && Number(parent.id || 0) > 0) {
          return Number(parent.id)
        }
      }
      return undefined
    },
    async handleTreeNodeDrop(draggingNode, dropNode, dropType) {
      const dragging = draggingNode?.data
      if (!dragging || dragging.type !== 'device') {
        return
      }
      const deviceId = Number(dragging.id || 0)
      const targetGroupId = this.resolveDropTargetGroupId(dropNode, dropType)
      if (!deviceId || targetGroupId === undefined || (targetGroupId !== null && !targetGroupId)) {
        await this.fetchDeviceTree()
        return
      }
      if (Number(dragging.groupId || 0) === Number(targetGroupId || 0)) {
        this.$message.info('设备已在目标分组')
        await this.fetchDeviceTree()
        return
      }
      try {
        const res = await moveToGroup([deviceId], targetGroupId)
        if (res.code === 0) {
          this.$message.success(targetGroupId ? '设备已移动到目标分组' : '设备已移出分组')
          await this.fetchDeviceTree()
          this.$emit('refresh')
        } else {
          this.$message.error(res.message || '拖动设备失败')
        }
      } catch (error) {
        console.error('Drag device failed:', error)
        this.$message.error('拖动设备失败')
      }
    },
    handleNativeDeviceDragStart(event, data) {
      if (!this.management || data?.type !== 'device') {
        event.preventDefault()
        return
      }
      this.draggingDeviceNode = data
      event.dataTransfer.effectAllowed = 'move'
      event.dataTransfer.setData('application/x-boer-device-id', String(data.id || ''))
      event.dataTransfer.setData('text/plain', String(data.id || ''))
    },
    handleNativeDeviceDragEnd() {
      this.draggingDeviceNode = null
    },
    handleNativeDeviceDragOver(event, data) {
      const targetGroupId = this.resolveNativeDropTargetGroupId(data)
      if (targetGroupId === undefined) return
      event.dataTransfer.dropEffect = 'move'
    },
    resolveNativeDropTargetGroupId(target) {
      if (!target) return undefined
      if (this.isUngroupedNode(target)) return null
      if (target.type === 'device') {
        const groupId = Number(target.groupId || 0)
        return groupId > 0 ? groupId : null
      }
      if (this.isGroupContextNode(target) && Number(target.id || 0) > 0) {
        return Number(target.id)
      }
      return undefined
    },
    async handleNativeDeviceDrop(event, target) {
      const dragging = this.draggingDeviceNode
      if (!dragging || dragging.type !== 'device') return
      const deviceId = Number(dragging.id || event.dataTransfer.getData('application/x-boer-device-id') || 0)
      const targetGroupId = this.resolveNativeDropTargetGroupId(target)
      if (!deviceId || targetGroupId === undefined) {
        await this.fetchDeviceTree()
        return
      }
      if (Number(dragging.groupId || 0) === Number(targetGroupId || 0)) {
        this.$message.info('设备已在目标分组')
        await this.fetchDeviceTree()
        return
      }
      try {
        const res = await moveToGroup([deviceId], targetGroupId)
        if (res.code === 0) {
          this.$message.success(targetGroupId ? '设备已移动到目标分组' : '设备已移出分组')
          await this.fetchDeviceTree()
          this.$emit('refresh')
        } else {
          this.$message.error(res.message || '拖动设备失败')
        }
      } catch (error) {
        console.error('Native drag device failed:', error)
        this.$message.error('拖动设备失败')
      } finally {
        this.draggingDeviceNode = null
      }
    },
    handleManualDeviceMouseDown(event, data) {
      if (!this.management || event.button !== 0 || data?.type !== 'device') {
        return
      }
      this.hideContextMenu()
      this.manualDrag = {
        active: true,
        moved: false,
        device: data,
        targetKey: '',
        targetNode: null,
        startX: event.clientX,
        startY: event.clientY,
        x: event.clientX,
        y: event.clientY
      }
      document.addEventListener('mousemove', this.handleManualDeviceMouseMove)
      document.addEventListener('mouseup', this.handleManualDeviceMouseUp)
    },
    handleManualDeviceMouseMove(event) {
      if (!this.manualDrag.active) return
      const dx = Math.abs(event.clientX - this.manualDrag.startX)
      const dy = Math.abs(event.clientY - this.manualDrag.startY)
      if (!this.manualDrag.moved && dx + dy < 4) {
        return
      }
      event.preventDefault()
      if (!this.manualDrag.moved) {
        this.manualDrag.moved = true
        document.body.classList.add('device-manual-dragging')
      }
      this.manualDrag.x = event.clientX
      this.manualDrag.y = event.clientY

      const target = this.findManualDropTargetFromPoint(event.clientX, event.clientY)
      this.manualDrag.targetNode = target
      this.manualDrag.targetKey = target?._nodeKey || ''
    },
    async handleManualDeviceMouseUp(event) {
      if (!this.manualDrag.active) return
      const dragState = { ...this.manualDrag }
      this.removeManualDragListeners()
      this.resetManualDrag()

      if (!dragState.moved) {
        return
      }
      event.preventDefault()
      event.stopPropagation()
      await this.moveDraggedDeviceToTarget(dragState.device, dragState.targetNode)
    },
    removeManualDragListeners() {
      document.removeEventListener('mousemove', this.handleManualDeviceMouseMove)
      document.removeEventListener('mouseup', this.handleManualDeviceMouseUp)
      document.body.classList.remove('device-manual-dragging')
    },
    resetManualDrag() {
      this.manualDrag = {
        active: false,
        moved: false,
        device: null,
        targetKey: '',
        targetNode: null,
        startX: 0,
        startY: 0,
        x: 0,
        y: 0
      }
    },
    findManualDropTargetFromPoint(x, y) {
      const element = document.elementFromPoint(x, y)
      const nodeElement = element?.closest?.('[data-tree-node-key]')
      const key = nodeElement?.getAttribute('data-tree-node-key')
      if (!key) return null
      const node = this.findNodeByKey(key)
      return this.resolveManualDropTargetGroupId(node) !== undefined ? node : null
    },
    isManualDragDropTarget(data) {
      return this.management && this.manualDrag.active && this.manualDrag.moved && this.resolveManualDropTargetGroupId(data) !== undefined
    },
    resolveManualDropTargetGroupId(target) {
      if (!target) return undefined
      if (this.isUngroupedNode(target)) return null
      if (target.type === 'device') {
        const groupId = Number(target.groupId || 0)
        return groupId > 0 ? groupId : null
      }
      if (this.isGroupContextNode(target) && Number(target.id || 0) > 0) {
        return Number(target.id)
      }
      return undefined
    },
    async moveDraggedDeviceToTarget(device, target) {
      const deviceId = Number(device?.id || 0)
      const targetGroupId = this.resolveManualDropTargetGroupId(target)
      if (!deviceId || targetGroupId === undefined) {
        await this.fetchDeviceTree()
        return
      }
      if (Number(device.groupId || 0) === Number(targetGroupId || 0)) {
        this.$message.info('设备已在目标分组')
        await this.fetchDeviceTree()
        return
      }
      try {
        const res = await moveToGroup([deviceId], targetGroupId)
        if (res.code === 0) {
          this.$message.success(targetGroupId ? '设备已移动到目标分组' : '设备已移出分组')
          await this.fetchDeviceTree()
          this.$emit('refresh')
        } else {
          this.$message.error(res.message || '拖动设备失败')
        }
      } catch (error) {
        console.error('Manual drag device failed:', error)
        this.$message.error('拖动设备失败')
      }
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

      if (node?.type === 'device') {
        if (action === 'removeDeviceFromGroup') {
          this.removeDeviceFromGroup(node)
          return
        }
        if (action === 'renameDevice') {
          this.renameDevice(node)
          return
        }
        if (action === 'refresh') {
          this.refreshTree()
        }
        return
      }

      if (action === 'addRoot') {
        this.openAddRootDialog()
        return
      }

      if (action === 'refresh') {
        this.refreshTree()
        return
      }

      if (!node || !this.isGroupContextNode(node)) {
        return
      }

      if (action === 'addSibling') {
        if (!this.canModifyGroup(node)) return
        this.openAddSiblingDialog(node)
        return
      }
      if (action === 'addChild') {
        this.openAddChildDialog(node)
        return
      }
      if (action === 'moveSelectedHere') {
        this.moveSelectedDevicesToGroup(node)
        return
      }
      if (['moveUp', 'moveDown', 'edit', 'delete'].includes(action) && !this.canModifyGroup(node)) {
        return
      }
      if (action === 'moveUp') {
        this.moveGroup(node, 'up')
        return
      }
      if (action === 'moveDown') {
        this.moveGroup(node, 'down')
        return
      }
      if (action === 'edit') {
        this.openEditDialog(node)
        return
      }
      if (action === 'delete') {
        if (!this.canDeleteGroup(node)) {
          this.$message.warning('总分组不能删除')
          return
        }
        this.handleDeleteGroup(node)
      }
    },
    getDevicePromptName(node) {
      return String(node?.name || node?.displayName || node?.label || '').trim()
    },
    async removeDeviceFromGroup(node) {
      const deviceId = Number(node?.id || 0)
      if (!deviceId) {
        this.$message.warning('设备参数错误')
        return
      }
      try {
        const res = await moveToGroup([deviceId], null)
        if (res.code === 0) {
          this.$message.success('设备已移到未分组设备')
          await this.fetchDeviceTree()
          this.$emit('refresh')
        } else {
          this.$message.error(res.message || '删除设备失败')
        }
      } catch (error) {
        console.error('Remove device from group failed:', error)
        this.$message.error('删除设备失败')
      }
    },
    async renameDevice(node) {
      const deviceId = Number(node?.id || 0)
      if (!deviceId) {
        this.$message.warning('设备参数错误')
        return
      }
      try {
        const { value } = await this.$prompt('请输入设备名称', '重命名设备', {
          confirmButtonText: '确定',
          cancelButtonText: '取消',
          inputValue: this.getDevicePromptName(node),
          inputValidator: value => String(value || '').trim().length > 0,
          inputErrorMessage: '设备名称不能为空'
        })
        const name = String(value || '').trim()
        const res = await updateDevice(deviceId, { name })
        if (res.code === 0) {
          this.$message.success('设备名称已更新')
          await this.fetchDeviceTree()
          this.$emit('refresh')
        } else {
          this.$message.error(res.message || '重命名设备失败')
        }
      } catch (error) {
        if (error !== 'cancel') {
          console.error('Rename device failed:', error)
          this.$message.error('重命名设备失败')
        }
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
    openAddSiblingDialog(node) {
      this.groupDialog = {
        visible: true,
        mode: 'addSibling',
        currentNode: node,
        form: {
          id: null,
          name: '',
          parentId: node.parentId || null
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
    async moveSelectedDevicesToGroup(node) {
      if (!this.canMoveSelectedToGroup(node)) {
        this.$message.warning('请先在右侧列表勾选设备')
        return
      }
      try {
        const res = await moveToGroup(this.selectedDeviceIds, Number(node.id))
        if (res.code === 0) {
          this.$message.success('已移动到当前分组')
          await this.fetchDeviceTree()
          this.$emit('refresh')
        } else {
          this.$message.error(res.message || '移动设备失败')
        }
      } catch (error) {
        console.error('Move selected devices failed:', error)
        this.$message.error('移动设备失败')
      }
    },
    async moveGroup(node, direction) {
      const context = this.findGroupContext(node.id)
      if (!context) return
      const targetIndex = direction === 'up' ? context.index - 1 : context.index + 1
      if (targetIndex < 0 || targetIndex >= context.siblings.length) {
        this.$message.warning(direction === 'up' ? '已经是第一个分组' : '已经是最后一个分组')
        return
      }
      const current = context.siblings[context.index]
      const target = context.siblings[targetIndex]
      const currentSort = Number(current.sortOrder || (context.index + 1) * 10)
      const targetSort = Number(target.sortOrder || (targetIndex + 1) * 10)
      try {
        const [resA, resB] = await Promise.all([
          updateDeviceGroup(current.id, {
            parentId: current.parentId || null,
            sortOrder: targetSort
          }),
          updateDeviceGroup(target.id, {
            parentId: target.parentId || null,
            sortOrder: currentSort
          })
        ])
        if (resA.code === 0 && resB.code === 0) {
          this.$message.success('分组顺序已更新')
          await this.fetchDeviceTree()
          this.$emit('refresh')
        } else {
          this.$message.error('更新分组顺序失败')
        }
      } catch (error) {
        console.error('Move group failed:', error)
        this.$message.error('更新分组顺序失败')
      }
    },
    async handleDeleteGroup(node) {
      if (!this.canDeleteGroup(node)) {
        this.$message.warning('总分组不能删除')
        return
      }
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
  flex-wrap: wrap;
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
  user-select: none;
}

.tree-node-device {
  cursor: grab;
}

.tree-node-device:active {
  cursor: grabbing;
}

.tree-node-drop-target {
  border-radius: 2px;
}

.tree-node-drop-hover {
  background: rgba(47, 109, 246, 0.12);
  box-shadow: inset 0 0 0 1px rgba(47, 109, 246, 0.38);
}

.manual-drag-ghost {
  position: fixed;
  z-index: 3000;
  max-width: 260px;
  padding: 6px 10px;
  border-radius: 3px;
  background: rgba(31, 41, 55, 0.92);
  color: #fff;
  font-size: 12px;
  line-height: 1.2;
  pointer-events: none;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
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

.tree-node-checkbox {
  flex: 0 0 auto;
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

  &.idle {
    background: #2fb46e;
  }

  &.working {
    background: #2f6df6;
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

    &.disabled {
      color: #a8b1bf;
      cursor: not-allowed;
    }

    &.disabled:hover {
      background: transparent;
    }
  }
}

@media (max-width: 0px) {
  .device-tree-panel {
    padding: 6px;
  }

  .panel-header,
  .selection-bar {
    align-items: flex-start;
    flex-direction: column;
  }

  .panel-actions {
    width: 100%;
  }

  .tree-wrapper {
    max-height: 300px;
  }
}
</style>
