<template>
  <div class="page-container">
    <el-row :gutter="20">
      <el-col :span="8">
        <div class="card">
          <div class="card-header flex-between">
            <span>设备分组</span>
            <div>
              <el-button type="primary" size="small" icon="el-icon-plus" @click="handleAddGroup">新增分组</el-button>
              <el-button size="small" icon="el-icon-refresh" @click="fetchAll">刷新</el-button>
            </div>
          </div>

          <el-tree
            ref="groupTree"
            :data="groupTree"
            node-key="id"
            v-loading="loadingGroups"
            default-expand-all
            highlight-current
            show-checkbox
            :check-strictly="true"
            draggable
            :expand-on-click-node="false"
            :allow-drag="allowDragGroupNode"
            :allow-drop="allowDropGroupNode"
            @node-click="handleNodeClick"
            @check="handleTreeCheck"
            @node-contextmenu="handleNodeContextMenu"
            @node-drop="handleGroupNodeDrop"
          >
            <div class="tree-node flex-between" style="width: 100%" slot-scope="{ node, data }">
              <span class="tree-node-label" @dblclick.stop="handleNodeDoubleClick(data)" :title="data.isDevice ? '双击可重命名设备' : '双击可重命名分组'">
                <i :class="getTreeNodeIcon(data)"></i>
                {{ node.label }}
                <span v-if="!data.isDevice" class="device-count">({{ data.deviceCount || 0 }})</span>
                <span v-if="data.isDevice" :class="['node-status-dot', `status-${data.status || 'offline'}`]"></span>
              </span>
              <span class="node-actions" v-if="!data.isRoot && !data.isVirtual && !data.isDevice">
                <el-button type="text" size="mini" @click.stop="handleAddSibling(data)" title="新增平级组">
                  <i class="el-icon-plus"></i>
                </el-button>
                <el-button type="text" size="mini" @click.stop="handleAddChild(data)" title="新增子组">
                  <i class="el-icon-circle-plus-outline"></i>
                </el-button>
                <el-button type="text" size="mini" @click.stop="handleMoveGroup(data, 'up')" title="上移">
                  <i class="el-icon-top"></i>
                </el-button>
                <el-button type="text" size="mini" @click.stop="handleMoveGroup(data, 'down')" title="下移">
                  <i class="el-icon-bottom"></i>
                </el-button>
                <el-button type="text" size="mini" @click.stop="handleEditGroup(data)">
                  <i class="el-icon-edit"></i>
                </el-button>
                <el-button type="text" size="mini" class="danger-text" @click.stop="handleDeleteGroup(data)">
                  <i class="el-icon-delete"></i>
                </el-button>
              </span>
            </div>
          </el-tree>
        </div>
      </el-col>

      <el-col :span="16">
        <div class="card">
          <div class="card-header">
            分组信息 - {{ selectedGroup?.label || '请选择分组' }}
          </div>

          <template v-if="selectedGroup">
            <el-descriptions v-if="selectedGroup.isDevice" :column="2" border>
              <el-descriptions-item label="设备名称">{{ selectedGroup.label }}</el-descriptions-item>
              <el-descriptions-item label="设备编码">{{ selectedGroup.code || '-' }}</el-descriptions-item>
              <el-descriptions-item label="所属分组">{{ selectedGroup.parentLabel || '未分组' }}</el-descriptions-item>
              <el-descriptions-item label="设备状态">{{ selectedGroup.status || '-' }}</el-descriptions-item>
            </el-descriptions>
            <el-descriptions v-else :column="2" border>
              <el-descriptions-item label="分组名称">{{ selectedGroup.label }}</el-descriptions-item>
              <el-descriptions-item label="设备数量">{{ groupDevices.length }}</el-descriptions-item>
              <el-descriptions-item label="分组ID">{{ selectedGroup.id }}</el-descriptions-item>
              <el-descriptions-item label="上级分组">{{ selectedGroup.parentLabel || '无' }}</el-descriptions-item>
            </el-descriptions>

            <div class="mt-20">
              <div class="flex-between">
                <h4>分组设备列表</h4>
                <div>
                  <el-button
                    size="mini"
                    icon="el-icon-folder-add"
                    :disabled="selectedDeviceIds.length === 0"
                    @click="openMoveDevicesDialog"
                  >
                    批量移动分组
                  </el-button>
                  <el-button
                    size="mini"
                    type="warning"
                    icon="el-icon-remove-outline"
                    :disabled="selectedDeviceIds.length === 0 || selectedGroup?.id === 'ungrouped' || selectedGroup?.parentLabel === '未分组'"
                    @click="removeSelectedDevicesFromGroup"
                  >
                    批量删除设备
                  </el-button>
                </div>
              </div>
              <el-table
                ref="groupDeviceTableRef"
                :data="groupDevices"
                border
                class="mt-10"
                v-loading="loadingDevices"
                :row-class-name="getDeviceRowClass"
                @selection-change="handleDeviceSelectionChange"
                @row-click="handleGroupDeviceRowClick"
              >
                <el-table-column type="selection" width="48" />
                <el-table-column label="序号" width="70" align="center">
                  <template slot-scope="scope">
                    {{ scope.$index + 1 }}
                  </template>
                </el-table-column>
                <el-table-column prop="code" label="设备编码" width="120" />
                <el-table-column label="设备名称">
                  <template slot-scope="scope">
                    <span>{{ formatDeviceName(scope.row) }}</span>
                  </template>
                </el-table-column>
                <el-table-column prop="group" label="所属分组" width="120">
                  <template slot-scope="scope">
                    <span v-if="scope.row.group">{{ scope.row.group }}</span>
                    <el-tag v-else size="mini" type="danger">未分组</el-tag>
                  </template>
                </el-table-column>
                <el-table-column prop="ip" label="IP地址" width="140" />
                <el-table-column prop="status" label="状态" width="100">
                  <template slot-scope="scope">
                    <el-tag :type="getStatusType(scope.row.status)" size="small">
                      {{ scope.row.status }}
                    </el-tag>
                  </template>
                </el-table-column>
                <el-table-column label="操作" width="180" align="center" v-if="selectedGroup.id !== 'all' && selectedGroup.id !== 'ungrouped'">
                  <template slot-scope="scope">
                    <template v-if="scope.row.groupId && Number(scope.row.groupId) > 0">
                      <el-button
                        type="text"
                        size="small"
                        icon="el-icon-top"
                        :disabled="!canMoveDevice(scope.row, 'up')"
                        @click="handleMoveDevice(scope.row, 'up')"
                      />
                      <el-button
                        type="text"
                        size="small"
                        icon="el-icon-bottom"
                        :disabled="!canMoveDevice(scope.row, 'down')"
                        @click="handleMoveDevice(scope.row, 'down')"
                      />
                      <el-button
                        type="text"
                        size="small"
                        @click="handleRemoveDevice(scope.row)"
                      >
                        删除
                      </el-button>
                    </template>
                    <span v-else class="text-muted">-</span>
                  </template>
                </el-table-column>
              </el-table>
            </div>
          </template>

          <template v-else>
            <div class="empty-state">
              <i class="el-icon-folder-opened"></i>
              <p>请从左侧选择一个分组查看详情</p>
            </div>
          </template>
        </div>
      </el-col>
    </el-row>

    <el-dialog
      :title="groupDialogTitle"
      :visible.sync="showGroupDialog"
      width="400px"
      @close="resetGroupForm"
    >
      <el-form ref="groupFormRef" :model="groupForm" :rules="groupRules" label-width="80px">
        <el-form-item label="分组名称" prop="name">
          <el-input v-model="groupForm.name" placeholder="请输入分组名称" />
        </el-form-item>
        <el-form-item label="上级分组" prop="parentId">
          <el-select
            v-model="groupForm.parentId"
            style="width: 100%"
            clearable
          >
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
        <el-button @click="showGroupDialog = false">取消</el-button>
        <el-button type="primary" :loading="savingGroup" @click="handleSaveGroup">确定</el-button>
      </span>
    </el-dialog>

    <el-dialog
      title="批量移动设备分组"
      :visible.sync="showMoveDevicesDialog"
      width="420px"
    >
      <el-form label-width="90px">
        <el-form-item label="目标分组">
          <el-select v-model="moveTargetGroupId" style="width: 100%;">
            <el-option label="未分组（移出分组）" :value="0" />
            <el-option
              v-for="item in flatGroups"
              :key="item.id"
              :label="item.name"
              :value="item.id"
            />
          </el-select>
        </el-form-item>
        <div style="color: #606266;">已选择 {{ selectedDeviceIds.length }} 台设备</div>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="showMoveDevicesDialog = false">取消</el-button>
        <el-button type="primary" :loading="movingDevices" @click="confirmMoveDevicesToGroup">确定</el-button>
      </span>
    </el-dialog>

    <el-dialog
      title="未分组设备快速分组"
      :visible.sync="showQuickAssignDialog"
      width="420px"
      @close="resetQuickAssignForm"
    >
      <el-form label-width="90px">
        <el-form-item label="设备编码">
          <span>{{ quickAssignForm.code || '-' }}</span>
        </el-form-item>
        <el-form-item label="初始名称">
          <span>{{ quickAssignForm.initialName || '-' }}</span>
        </el-form-item>
        <el-form-item label="设备名称" required>
          <el-input v-model.trim="quickAssignForm.name" />
        </el-form-item>
        <el-form-item label="设备备注">
          <el-input v-model.trim="quickAssignForm.remark" type="textarea" :rows="2" />
        </el-form-item>
        <el-form-item label="目标分组" required>
          <el-select v-model="quickAssignForm.groupId" style="width: 100%;">
            <el-option
              v-for="item in flatGroups"
              :key="item.id"
              :label="item.name"
              :value="item.id"
            />
          </el-select>
        </el-form-item>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="showQuickAssignDialog = false">取消</el-button>
        <el-button type="primary" :loading="quickAssignSaving" @click="handleQuickAssignSave">确定</el-button>
      </span>
    </el-dialog>

    <ul
      v-if="contextMenu.visible"
      class="group-context-menu"
      :style="{ left: `${contextMenu.x}px`, top: `${contextMenu.y}px` }"
      @click.stop
      @contextmenu.prevent
    >
      <li @click="handleContextMenuAction('addRoot')">新增分组</li>
      <template v-if="contextMenu.node && !contextMenu.node.isRoot && !contextMenu.node.isVirtual && !contextMenu.node.isDevice">
        <li @click="handleContextMenuAction('addSibling')">新增平级组</li>
        <li @click="handleContextMenuAction('addChild')">新增子组</li>
        <li @click="handleContextMenuAction('moveSelectedHere')">移动已选设备到当前组</li>
        <li @click="handleContextMenuAction('moveUp')">上移</li>
        <li @click="handleContextMenuAction('moveDown')">下移</li>
        <li @click="handleContextMenuAction('edit')">重命名</li>
        <li class="danger" @click="handleContextMenuAction('delete')">删除分组</li>
      </template>
    </ul>
  </div>
</template>

<script>
import {
  getDeviceGroups,
  createDeviceGroup,
  updateDeviceGroup,
  updateDevice,
  deleteDevice,
  batchDeleteDevices,
  deleteDeviceGroup,
  getDeviceList,
  moveToGroup
} from '@/api/device'

export default {
  name: 'DeviceGroup',
  data() {
    return {
      loadingGroups: false,
      loadingDevices: false,
      savingGroup: false,
      movingDevices: false,
      draggingGroup: false,
      flatGroups: [],
      groupTree: [],
      allDevices: [],
      selectedGroup: null,
      groupDevices: [],
      checkedTreeNodes: [],
      selectedDeviceIds: [],
      showGroupDialog: false,
      showMoveDevicesDialog: false,
      showQuickAssignDialog: false,
      moveTargetGroupId: 0,
      quickAssignSaving: false,
      quickAssignForm: {
        id: null,
        code: '',
        initialName: '',
        name: '',
        remark: '',
        groupId: null
      },
      groupDialogMode: 'addRoot',
      groupForm: {
        id: null,
        name: '',
        parentId: null
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
    groupDialogTitle() {
      const titleMap = {
        addRoot: '新增分组',
        addSibling: '新增平级组',
        addChild: '新增子组',
        edit: '编辑分组'
      }
      return titleMap[this.groupDialogMode] || '新增分组'
    },
    parentOptions() {
      const currentId = this.groupForm.id
      if (!currentId) {
        return this.flatGroups.map(group => ({ id: group.id, label: group.name }))
      }

      const descendants = new Set(this.getDescendantGroupIdsById(currentId))
      descendants.add(currentId)
      return this.flatGroups
        .filter(group => !descendants.has(group.id))
        .map(group => ({ id: group.id, label: group.name }))
    }
  },
  mounted() {
    document.addEventListener('click', this.hideContextMenu)
    window.addEventListener('resize', this.hideContextMenu)
    this.fetchAll()
  },
  beforeDestroy() {
    document.removeEventListener('click', this.hideContextMenu)
    window.removeEventListener('resize', this.hideContextMenu)
  },
  methods: {
    async fetchAll() {
      await Promise.all([this.fetchGroups(), this.fetchDevices()])
      this.buildGroupTree()
      this.checkedTreeNodes = []
      this.$nextTick(() => {
        this.$refs.groupTree?.setCheckedKeys([])
      })
      if (!this.selectedGroup && this.groupTree.length > 0) {
        this.selectedGroup = this.groupTree[0]
      }
      this.syncGroupDevices()
    },
    async fetchGroups() {
      this.loadingGroups = true
      try {
        const res = await getDeviceGroups()
        if (res.code === 0) {
          this.flatGroups = Array.isArray(res.data) ? res.data : []
        }
      } catch (error) {
        console.error('Failed to fetch groups:', error)
        this.$message.error('获取分组失败')
      } finally {
        this.loadingGroups = false
      }
    },
    async fetchDevices() {
      this.loadingDevices = true
      try {
        const res = await getDeviceList({
          page: 1,
          pageSize: 2000
        })
        if (res.code === 0) {
          this.allDevices = Array.isArray(res.data?.list) ? res.data.list : []
        }
      } catch (error) {
        console.error('Failed to fetch devices:', error)
        this.$message.error('获取设备列表失败')
      } finally {
        this.loadingDevices = false
      }
    },
    toDeviceNode(device, parentLabel = '') {
      return {
        id: `device-${device.id}`,
        deviceId: Number(device.id),
        label: this.formatDeviceName(device),
        code: device.code || '',
        status: device.status || '',
        parentLabel: parentLabel || device.group || '',
        isDevice: true,
        isRoot: false,
        isVirtual: false,
        children: []
      }
    },
    findTreeNode(predicate, nodes = this.groupTree) {
      const stack = [...(nodes || [])]
      while (stack.length > 0) {
        const current = stack.pop()
        if (!current) continue
        if (predicate(current)) {
          return current
        }
        if (Array.isArray(current.children) && current.children.length > 0) {
          stack.push(...current.children)
        }
      }
      return null
    },
    buildGroupTree() {
      const nodeMap = new Map()
      this.flatGroups.forEach(group => {
        nodeMap.set(group.id, {
          id: group.id,
          label: group.name,
          parentId: group.parentId || null,
          parentLabel: '',
          sortOrder: group.sortOrder || 0,
          children: [],
          deviceCount: 0,
          isRoot: false
        })
      })

      const roots = []
      nodeMap.forEach(node => {
        if (node.parentId && nodeMap.has(node.parentId)) {
          const parent = nodeMap.get(node.parentId)
          node.parentLabel = parent.label
          parent.children.push(node)
        } else {
          roots.push(node)
        }
      })

      const directDeviceCount = new Map()
      this.allDevices.forEach(device => {
        const groupId = Number(device.groupId || 0)
        if (!groupId) return
        directDeviceCount.set(groupId, (directDeviceCount.get(groupId) || 0) + 1)
      })

      const calcCount = (node) => {
        const childCount = node.children.reduce((sum, child) => sum + calcCount(child), 0)
        const selfCount = directDeviceCount.get(node.id) || 0
        node.deviceCount = selfCount + childCount
        node.children.sort((a, b) => (a.sortOrder - b.sortOrder) || (a.id - b.id))
        return node.deviceCount
      }

      roots.forEach(calcCount)
      roots.sort((a, b) => (a.sortOrder - b.sortOrder) || (a.id - b.id))

      // Append direct device nodes after child groups.
      const groupedDevices = new Map()
      this.allDevices.forEach(device => {
        const groupId = Number(device.groupId || 0)
        if (!groupId || !nodeMap.has(groupId)) return
        if (!groupedDevices.has(groupId)) {
          groupedDevices.set(groupId, [])
        }
        groupedDevices.get(groupId).push(device)
      })
      nodeMap.forEach(node => {
        const devices = groupedDevices.get(Number(node.id)) || []
        if (!devices.length) return
        devices.sort((a, b) => {
          const sortDiff = Number(a.sortOrder || 0) - Number(b.sortOrder || 0)
          if (sortDiff !== 0) {
            return sortDiff
          }
          return String(a.code || '').localeCompare(String(b.code || ''))
        })
        const deviceNodes = devices.map(device => this.toDeviceNode(device, node.label))
        node.children = [...node.children, ...deviceNodes]
      })

      const ungroupedCount = this.allDevices.filter(device => !(device.groupId && Number(device.groupId) > 0)).length
      const ungroupedDevices = this.allDevices
        .filter(device => !(device.groupId && Number(device.groupId) > 0))
        .sort((a, b) => String(a.code || '').localeCompare(String(b.code || '')))
      const ungroupedNode = {
        id: 'ungrouped',
        label: '未分组设备',
        parentId: null,
        parentLabel: '',
        children: ungroupedDevices.map(device => this.toDeviceNode(device, '未分组')),
        deviceCount: ungroupedCount,
        isRoot: true,
        isVirtual: true
      }

      this.groupTree = [{
        id: 'all',
        label: '全部设备',
        parentId: null,
        parentLabel: '',
        children: [ungroupedNode, ...roots],
        deviceCount: this.allDevices.length,
        isRoot: true
      }]
    },
    getDescendantGroupIdsById(groupId) {
      const childMap = new Map()
      this.flatGroups.forEach(group => {
        const parentId = group.parentId || null
        if (!childMap.has(parentId)) {
          childMap.set(parentId, [])
        }
        childMap.get(parentId).push(group.id)
      })

      const result = []
      const stack = [groupId]
      while (stack.length > 0) {
        const current = stack.pop()
        const children = childMap.get(current) || []
        children.forEach(childId => {
          result.push(childId)
          stack.push(childId)
        })
      }
      return result
    },
    getCurrentGroupIdSet() {
      if (!this.selectedGroup) return new Set()
      if (this.selectedGroup.isDevice) return new Set()
      if (this.selectedGroup.id === 'all') {
        return new Set(this.flatGroups.map(group => group.id))
      }

      const ids = [this.selectedGroup.id, ...this.getDescendantGroupIdsById(this.selectedGroup.id)]
      return new Set(ids)
    },
    sortDevicesForDisplay(devices) {
      return [...devices].sort((a, b) => {
        const aUngrouped = !(a.groupId && Number(a.groupId) > 0)
        const bUngrouped = !(b.groupId && Number(b.groupId) > 0)
        if (aUngrouped !== bUngrouped) {
          return aUngrouped ? -1 : 1
        }
        const groupDiff = Number(a.groupId || 0) - Number(b.groupId || 0)
        if (groupDiff !== 0) {
          return groupDiff
        }
        const sortDiff = Number(a.sortOrder || 0) - Number(b.sortOrder || 0)
        if (sortDiff !== 0) {
          return sortDiff
        }
        return String(a.code || '').localeCompare(String(b.code || ''))
      })
    },
    getDeviceRowClass({ row }) {
      if (!(row.groupId && Number(row.groupId) > 0)) {
        return 'row-ungrouped'
      }
      return ''
    },
    formatDeviceName(row) {
      const name = String(row?.name || '').trim()
      const employeeName = String(row?.employeeName || '').trim()
      if (!employeeName) {
        return name
      }
      if (!name) {
        return employeeName
      }
      if (name.includes(`（${employeeName}）`) || name.includes(`(${employeeName})`)) {
        return name
      }
      return `${name}（${employeeName}）`
    },
    allowDragGroupNode(draggingNode) {
      const data = draggingNode?.data
      if (!data) return false
      if (data.isDevice) {
        return true
      }
      if (data.isRoot || data.isVirtual || data.id === 'all' || data.id === 'ungrouped') {
        return false
      }
      return true
    },
    isDescendantGroup(descendantId, ancestorId) {
      const parentMap = new Map()
      this.flatGroups.forEach(group => {
        parentMap.set(Number(group.id), Number(group.parentId || 0))
      })
      let current = Number(descendantId || 0)
      const target = Number(ancestorId || 0)
      while (current > 0) {
        const parentId = Number(parentMap.get(current) || 0)
        if (parentId === target) {
          return true
        }
        current = parentId
      }
      return false
    },
    allowDropGroupNode(draggingNode, dropNode, type) {
      const dragging = draggingNode?.data
      const target = dropNode?.data
      if (!dragging || !target) {
        return false
      }
      if (dragging.isDevice) {
        if (type !== 'inner') {
          return false
        }
        if (target.isDevice || target.id === 'all') {
          return false
        }
        return true
      }
      if (target.isDevice) {
        return false
      }
      if (!this.allowDragGroupNode(draggingNode)) {
        return false
      }
      if (target.isVirtual) {
        return type !== 'inner'
      }
      if (target.id === 'all') {
        return type === 'inner'
      }
      if (target.id === dragging.id) {
        return false
      }
      if (this.isDescendantGroup(Number(target.id), Number(dragging.id))) {
        return false
      }
      return true
    },
    async handleGroupNodeDrop(draggingNode, dropNode) {
      const dragging = draggingNode?.data
      const target = dropNode?.data
      if (!dragging || !target) {
        return
      }

      if (dragging.isDevice) {
        const targetGroupId = target.id === 'ungrouped' ? null : Number(target.id || 0)
        if (target.id !== 'ungrouped' && targetGroupId <= 0) {
          this.$message.warning('请选择有效的目标分组')
          return
        }
        try {
          const res = await moveToGroup([Number(dragging.deviceId)], targetGroupId)
          if (res.code === 0) {
            this.$message.success(targetGroupId ? '设备已移动到目标分组' : '设备已移出分组')
            await this.fetchDevices()
            this.buildGroupTree()
            this.syncGroupDevices()
          } else {
            this.$message.error(res.message || '拖动设备失败')
          }
        } catch (error) {
          console.error('Drag device failed:', error)
          this.$message.error('拖动设备失败')
        }
        return
      }

      const root = this.groupTree.find(node => node.id === 'all')
      if (!root) {
        return
      }

      const snapshotMap = new Map(
        this.flatGroups.map(group => [Number(group.id), group])
      )
      const updates = []
      const walk = (nodes, parentId) => {
        const validNodes = (nodes || []).filter(node => !node.isVirtual && !node.isDevice && node.id !== 'all')
        validNodes.forEach((node, index) => {
          const original = snapshotMap.get(Number(node.id))
          if (!original) {
            return
          }
          const nextParentId = parentId || null
          const nextSortOrder = (index + 1) * 10
          const prevParent = Number(original.parentId || 0)
          const currParent = Number(nextParentId || 0)
          const prevSort = Number(original.sortOrder || 0)
          if (prevParent !== currParent || prevSort !== nextSortOrder) {
            updates.push({
              id: original.id,
              name: original.name,
              parentId: nextParentId,
              sortOrder: nextSortOrder
            })
          }
          walk(node.children || [], node.id)
        })
      }
      walk(root.children || [], null)

      if (!updates.length) {
        return
      }

      this.draggingGroup = true
      try {
        const results = await Promise.all(
          updates.map(item => updateDeviceGroup(item.id, {
            name: item.name,
            parentId: item.parentId,
            sortOrder: item.sortOrder
          }))
        )
        if (results.every(item => item.code === 0)) {
          this.$message.success('拖动分组已生效')
          await this.fetchAll()
          return
        }
        this.$message.error('部分分组保存失败，已刷新数据')
        await this.fetchAll()
      } catch (error) {
        console.error('Drag group failed:', error)
        this.$message.error('拖动分组失败，已回滚')
        await this.fetchAll()
      } finally {
        this.draggingGroup = false
      }
    },
    syncGroupDevices() {
      const resetSelection = () => {
        this.selectedDeviceIds = []
        this.$nextTick(() => {
          this.$refs.groupDeviceTableRef?.clearSelection()
        })
      }

      if (this.checkedTreeNodes.length > 0) {
        let includeAll = false
        let includeUngrouped = false
        const groupIdSet = new Set()
        const deviceIdSet = new Set()

        this.checkedTreeNodes.forEach(node => {
          if (!node) return
          if (node.id === 'all') {
            includeAll = true
            return
          }
          if (node.id === 'ungrouped') {
            includeUngrouped = true
            return
          }
          if (node.isDevice) {
            deviceIdSet.add(Number(node.deviceId))
            return
          }
          const currentGroupId = Number(node.id || 0)
          if (currentGroupId <= 0) return
          groupIdSet.add(currentGroupId)
          this.getDescendantGroupIdsById(currentGroupId).forEach(id => groupIdSet.add(Number(id)))
        })

        if (includeAll) {
          this.groupDevices = this.sortDevicesForDisplay(this.allDevices)
          resetSelection()
          return
        }

        if (groupIdSet.size > 0) {
          this.allDevices.forEach(device => {
            if (groupIdSet.has(Number(device.groupId || 0))) {
              deviceIdSet.add(Number(device.id))
            }
          })
        }
        if (includeUngrouped) {
          this.allDevices.forEach(device => {
            if (!(device.groupId && Number(device.groupId) > 0)) {
              deviceIdSet.add(Number(device.id))
            }
          })
        }

        this.groupDevices = this.sortDevicesForDisplay(
          this.allDevices.filter(device => deviceIdSet.has(Number(device.id)))
        )
        resetSelection()
        return
      }

      if (!this.selectedGroup) {
        this.groupDevices = []
        resetSelection()
        return
      }

      if (this.selectedGroup.id === 'all') {
        this.groupDevices = this.sortDevicesForDisplay(this.allDevices)
        resetSelection()
        return
      }
      if (this.selectedGroup.id === 'ungrouped') {
        this.groupDevices = this.sortDevicesForDisplay(
          this.allDevices.filter(device => !(device.groupId && Number(device.groupId) > 0))
        )
        resetSelection()
        return
      }
      if (this.selectedGroup.isDevice) {
        this.groupDevices = this.sortDevicesForDisplay(
          this.allDevices.filter(device => Number(device.id) === Number(this.selectedGroup.deviceId))
        )
        resetSelection()
        return
      }

      const groupIdSet = this.getCurrentGroupIdSet()
      const devices = this.allDevices.filter(device => groupIdSet.has(Number(device.groupId || 0)))
      this.groupDevices = this.sortDevicesForDisplay(devices)
      resetSelection()
    },
    handleDeviceSelectionChange(rows) {
      this.selectedDeviceIds = rows.map(item => item.id).filter(Boolean)
    },
    handleGroupDeviceRowClick(row, column) {
      if (!row || !column || column.type === 'selection') {
        return
      }
      const isUngrouped = !(row.groupId && Number(row.groupId) > 0)
      if (!isUngrouped) {
        return
      }
      this.openQuickAssignDialog(row)
    },
    openQuickAssignDialog(row) {
      this.quickAssignForm = {
        id: row.id,
        code: row.code || '',
        initialName: row.initialName || '',
        name: row.name || '',
        remark: row.remark || '',
        groupId: null
      }
      this.showQuickAssignDialog = true
    },
    resetQuickAssignForm() {
      this.quickAssignForm = {
        id: null,
        code: '',
        initialName: '',
        name: '',
        remark: '',
        groupId: null
      }
    },
    async handleQuickAssignSave() {
      const deviceID = Number(this.quickAssignForm.id || 0)
      if (deviceID <= 0) {
        this.$message.warning('设备参数错误')
        return
      }
      const name = String(this.quickAssignForm.name || '').trim()
      if (!name) {
        this.$message.warning('设备名称不能为空')
        return
      }
      const targetGroupId = Number(this.quickAssignForm.groupId || 0)
      if (targetGroupId <= 0) {
        this.$message.warning('请选择目标分组')
        return
      }

      this.quickAssignSaving = true
      try {
        const payload = {
          name,
          remark: String(this.quickAssignForm.remark || '').trim(),
          groupId: targetGroupId
        }
        const res = await updateDevice(deviceID, payload)
        if (res.code === 0) {
          this.$message.success('设备分组成功')
          this.showQuickAssignDialog = false
          await this.fetchDevices()
          this.buildGroupTree()
          this.syncGroupDevices()
          return
        }
        this.$message.error(res.message || '设备分组失败')
      } catch (error) {
        console.error('Quick assign device failed:', error)
        this.$message.error('设备分组失败')
      } finally {
        this.quickAssignSaving = false
      }
    },
    openMoveDevicesDialog() {
      if (!this.selectedDeviceIds.length) {
        this.$message.warning('请先选择设备')
        return
      }
      this.moveTargetGroupId = 0
      this.showMoveDevicesDialog = true
    },
    async confirmMoveDevicesToGroup() {
      if (!this.selectedDeviceIds.length) {
        this.$message.warning('请先选择设备')
        return
      }
      this.movingDevices = true
      try {
        const targetGroupId = Number(this.moveTargetGroupId || 0)
        const res = await moveToGroup(this.selectedDeviceIds, targetGroupId > 0 ? targetGroupId : null)
        if (res.code === 0) {
          this.$message.success(targetGroupId > 0 ? '设备分组移动成功' : '已移出分组')
          this.showMoveDevicesDialog = false
          await this.fetchDevices()
          this.buildGroupTree()
          this.syncGroupDevices()
          return
        }
        this.$message.error(res.message || '移动设备失败')
      } catch (error) {
        console.error('Move selected devices failed:', error)
        this.$message.error('移动设备失败')
      } finally {
        this.movingDevices = false
      }
    },
    async removeSelectedDevicesFromGroup() {
      if (!this.selectedDeviceIds.length) {
        this.$message.warning('请先选择设备')
        return
      }
      try {
        await this.$confirm(
          `确定删除选中的 ${this.selectedDeviceIds.length} 台设备吗？删除后设备将转为未分组并恢复初始名称。`,
          '提示',
          {
          confirmButtonText: '确定',
          cancelButtonText: '取消',
          type: 'warning'
          }
        )
        const res = await batchDeleteDevices(this.selectedDeviceIds)
        if (res.code === 0) {
          this.$message.success(res.message || '批量删除成功')
          await this.fetchDevices()
          this.buildGroupTree()
          this.syncGroupDevices()
          return
        }
        this.$message.error(res.message || '批量删除失败')
      } catch (error) {
        if (error !== 'cancel') {
          console.error('Batch remove devices failed:', error)
          this.$message.error('批量删除失败')
        }
      }
    },
    handleNodeClick(data) {
      this.selectedGroup = data
      this.syncGroupDevices()
      this.hideContextMenu()
    },
    handleTreeCheck(data, checkedInfo) {
      const checkedNodes = checkedInfo?.checkedNodes || []
      this.checkedTreeNodes = checkedNodes.filter(node => !node?.isVirtual)
      this.syncGroupDevices()
    },
    handleNodeContextMenu(event, data) {
      event.preventDefault()
      if (data?.isDevice) {
        this.hideContextMenu()
        return
      }
      this.selectedGroup = data
      this.syncGroupDevices()
      this.contextMenu = {
        visible: true,
        x: event.clientX,
        y: event.clientY,
        node: data
      }
    },
    hideContextMenu() {
      if (!this.contextMenu.visible) {
        return
      }
      this.contextMenu.visible = false
    },
    handleContextMenuAction(action) {
      const data = this.contextMenu.node
      this.hideContextMenu()
      if (action === 'addRoot') {
        this.handleAddGroup()
        return
      }
      if (!data || data.isRoot || data.isVirtual || data.isDevice) {
        return
      }
      if (action === 'addSibling') {
        this.handleAddSibling(data)
        return
      }
      if (action === 'addChild') {
        this.handleAddChild(data)
        return
      }
      if (action === 'moveSelectedHere') {
        this.moveSelectedDevicesToGroup(data)
        return
      }
      if (action === 'moveUp') {
        this.handleMoveGroup(data, 'up')
        return
      }
      if (action === 'moveDown') {
        this.handleMoveGroup(data, 'down')
        return
      }
      if (action === 'edit') {
        this.handleEditGroup(data)
        return
      }
      if (action === 'delete') {
        this.handleDeleteGroup(data)
      }
    },
    async moveSelectedDevicesToGroup(groupNode) {
      if (!this.selectedDeviceIds.length) {
        this.$message.warning('请先在右侧列表勾选设备')
        return
      }
      const targetGroupId = Number(groupNode?.id || 0)
      if (targetGroupId <= 0) {
        this.$message.warning('目标分组无效')
        return
      }
      try {
        const res = await moveToGroup(this.selectedDeviceIds, targetGroupId)
        if (res.code === 0) {
          this.$message.success('已移动到当前分组')
          await this.fetchDevices()
          this.buildGroupTree()
          this.syncGroupDevices()
          return
        }
        this.$message.error(res.message || '移动设备失败')
      } catch (error) {
        console.error('Move selected devices to context group failed:', error)
        this.$message.error('移动设备失败')
      }
    },
    handleNodeDoubleClick(data) {
      if (!data || data.isRoot || data.isVirtual || data.id === 'all' || data.id === 'ungrouped') {
        return
      }
      if (data.isDevice) {
        this.renameDevice(data)
        return
      }
      this.handleEditGroup(data)
    },
    getTreeNodeIcon(data) {
      if (data?.isDevice) {
        return 'el-icon-monitor'
      }
      return (data.children && data.children.length) ? 'el-icon-folder-opened' : 'el-icon-folder'
    },
    async renameDevice(data) {
      const deviceId = Number(data?.deviceId || 0)
      if (!deviceId) {
        return
      }
      const currentDevice = this.allDevices.find(item => Number(item.id) === deviceId)
      if (!currentDevice) {
        this.$message.warning('未找到设备信息，请先刷新')
        return
      }
      try {
        const { value } = await this.$prompt('请输入新的设备名称', '重命名设备', {
          inputValue: currentDevice.name || '',
          confirmButtonText: '确定',
          cancelButtonText: '取消',
          inputPlaceholder: '请输入设备名称'
        })
        const name = String(value || '').trim()
        if (!name) {
          this.$message.warning('设备名称不能为空')
          return
        }
        if (name === String(currentDevice.name || '').trim()) {
          return
        }
        const res = await updateDevice(deviceId, { name })
        if (res.code === 0) {
          this.$message.success('设备名称已更新')
          await this.fetchDevices()
          this.buildGroupTree()
          this.selectedGroup = this.findTreeNode(node => node.isDevice && Number(node.deviceId) === deviceId) || (this.groupTree[0] || null)
          this.syncGroupDevices()
          return
        }
        this.$message.error(res.message || '重命名失败')
      } catch (error) {
        if (error !== 'cancel') {
          console.error('Rename device failed:', error)
          this.$message.error('重命名失败')
        }
      }
    },
    getStatusType(status) {
      const map = {
        online: 'success',
        working: 'primary',
        idle: 'warning',
        offline: 'info',
        alarm: 'danger'
      }
      return map[status] || 'info'
    },
    handleAddGroup() {
      this.groupDialogMode = 'addRoot'
      this.groupForm = { id: null, name: '', parentId: null }
      this.showGroupDialog = true
    },
    handleAddSibling(data) {
      this.groupDialogMode = 'addSibling'
      this.groupForm = {
        id: null,
        name: '',
        parentId: data.parentId || null
      }
      this.showGroupDialog = true
    },
    handleAddChild(data) {
      this.groupDialogMode = 'addChild'
      this.groupForm = {
        id: null,
        name: '',
        parentId: data.id
      }
      this.showGroupDialog = true
    },
    handleEditGroup(data) {
      this.groupDialogMode = 'edit'
      this.groupForm = {
        id: data.id,
        name: data.label,
        parentId: data.parentId || null
      }
      this.showGroupDialog = true
    },
    async handleMoveGroup(data, direction) {
      const siblings = this.flatGroups
        .filter(group => Number(group.parentId || 0) === Number(data.parentId || 0))
        .sort((a, b) => (a.sortOrder - b.sortOrder) || (a.id - b.id))

      const currentIndex = siblings.findIndex(group => group.id === data.id)
      if (currentIndex === -1) return

      const targetIndex = direction === 'up' ? currentIndex - 1 : currentIndex + 1
      if (targetIndex < 0 || targetIndex >= siblings.length) {
        this.$message.warning(direction === 'up' ? '已经是第一个分组' : '已经是最后一个分组')
        return
      }

      const current = siblings[currentIndex]
      const target = siblings[targetIndex]
      try {
        const [resA, resB] = await Promise.all([
          updateDeviceGroup(current.id, {
            name: current.name,
            parentId: current.parentId || null,
            sortOrder: target.sortOrder || 0
          }),
          updateDeviceGroup(target.id, {
            name: target.name,
            parentId: target.parentId || null,
            sortOrder: current.sortOrder || 0
          })
        ])

        if (resA.code === 0 && resB.code === 0) {
          this.$message.success('分组顺序已更新')
          await this.fetchAll()
        } else {
          this.$message.error('更新分组顺序失败')
        }
      } catch (error) {
        console.error('Move group failed:', error)
        this.$message.error('更新分组顺序失败')
      }
    },
    getSortedGroupDevices(groupId) {
      return this.groupDevices
        .filter(item => Number(item.groupId || 0) === Number(groupId || 0))
        .sort((a, b) => {
          const sortDiff = Number(a.sortOrder || 0) - Number(b.sortOrder || 0)
          if (sortDiff !== 0) {
            return sortDiff
          }
          return String(a.code || '').localeCompare(String(b.code || ''))
        })
    },
    canMoveDevice(row, direction) {
      const groupId = Number(row?.groupId || 0)
      if (groupId <= 0) {
        return false
      }
      const siblings = this.getSortedGroupDevices(groupId)
      const currentIndex = siblings.findIndex(item => Number(item.id) === Number(row.id))
      if (currentIndex < 0) {
        return false
      }
      if (direction === 'up') {
        return currentIndex > 0
      }
      return currentIndex < siblings.length - 1
    },
    async handleMoveDevice(row, direction) {
      const groupId = Number(row?.groupId || 0)
      if (groupId <= 0) {
        return
      }

      const siblings = this.getSortedGroupDevices(groupId)
      const normalized = siblings.map((item, index) => ({
        ...item,
        normalizedSortOrder: Number(item.sortOrder || 0) > 0 ? Number(item.sortOrder) : (index + 1) * 10
      }))
      const currentIndex = normalized.findIndex(item => Number(item.id) === Number(row.id))
      if (currentIndex < 0) {
        return
      }

      const targetIndex = direction === 'up' ? currentIndex - 1 : currentIndex + 1
      if (targetIndex < 0 || targetIndex >= normalized.length) {
        this.$message.warning(direction === 'up' ? '已经是第一个设备' : '已经是最后一个设备')
        return
      }

      const current = normalized[currentIndex]
      const target = normalized[targetIndex]
      try {
        const [resA, resB] = await Promise.all([
          updateDevice(current.id, { sortOrder: target.normalizedSortOrder }),
          updateDevice(target.id, { sortOrder: current.normalizedSortOrder })
        ])
        if (resA.code === 0 && resB.code === 0) {
          this.$message.success('设备顺序已更新')
          await this.fetchDevices()
          this.buildGroupTree()
          this.syncGroupDevices()
        } else {
          this.$message.error('更新设备顺序失败')
        }
      } catch (error) {
        console.error('Move device failed:', error)
        this.$message.error('更新设备顺序失败')
      }
    },
    handleDeleteGroup(data) {
      this.$confirm(`确定要删除分组"${data.label}"吗？`, '警告', {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        type: 'warning'
      }).then(async () => {
        try {
          const res = await deleteDeviceGroup(data.id)
          if (res.code === 0) {
            this.$message.success('删除成功')
            if (this.selectedGroup?.id === data.id) {
              this.selectedGroup = null
            }
            await this.fetchAll()
          } else {
            this.$message.error(res.message || '删除失败')
          }
        } catch (error) {
          console.error('Delete group failed:', error)
          this.$message.error('删除分组失败')
        }
      }).catch(() => {})
    },
    async handleSaveGroup() {
      try {
        await this.$refs.groupFormRef.validate()
        this.savingGroup = true
        const payload = {
          name: this.groupForm.name,
          parentId: this.groupForm.parentId || null
        }
        const res = this.groupForm.id
          ? await updateDeviceGroup(this.groupForm.id, payload)
          : await createDeviceGroup(payload)

        if (res.code === 0) {
          this.$message.success('保存成功')
          this.showGroupDialog = false
          await this.fetchAll()
        } else {
          this.$message.error(res.message || '保存失败')
        }
      } catch (error) {
        console.error('Save group failed:', error)
        this.$message.error('保存分组失败')
      } finally {
        this.savingGroup = false
      }
    },
    resetGroupForm() {
      this.$refs.groupFormRef?.resetFields()
      this.groupDialogMode = 'addRoot'
    },
    async handleRemoveDevice(row) {
      this.$confirm(`确定删除设备"${row.name}"吗？删除后设备将转为未分组并恢复初始名称。`, '提示', {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        type: 'warning'
      }).then(async () => {
        try {
          const res = await deleteDevice(row.id)
          if (res.code === 0) {
            this.$message.success(res.message || '删除成功')
            await this.fetchDevices()
            this.buildGroupTree()
            this.syncGroupDevices()
          } else {
            this.$message.error(res.message || '删除失败')
          }
        } catch (error) {
          console.error('Remove device from group failed:', error)
          this.$message.error('删除设备失败')
        }
      }).catch(() => {})
    }
  }
}
</script>

<style lang="scss" scoped>
.tree-node {
  .tree-node-label {
    cursor: pointer;
    display: inline-flex;
    align-items: center;
    gap: 5px;
  }

  .node-status-dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;

    &.status-online,
    &.status-idle {
      background: #67C23A;
    }

    &.status-working,
    &.status-alarm {
      background: #F56C6C;
    }

    &.status-offline {
      background: #909399;
    }
  }

  .device-count {
    color: #909399;
    font-size: 12px;
    margin-left: 5px;
  }

  .node-actions {
    opacity: 0;
    transition: opacity 0.3s;
  }

  &:hover .node-actions {
    opacity: 1;
  }
}

.danger-text {
  color: #F56C6C !important;
}

.text-muted {
  color: #909399;
}

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 60px 0;
  color: #909399;

  i {
    font-size: 60px;
    margin-bottom: 15px;
  }
}

.mt-10 {
  margin-top: 10px;
}

.mt-20 {
  margin-top: 20px;
}

::v-deep .el-table .row-ungrouped > td {
  background: #fff1f0;
}

.group-context-menu {
  position: fixed;
  z-index: 3000;
  margin: 0;
  padding: 6px 0;
  list-style: none;
  min-width: 140px;
  background: #fff;
  border: 1px solid #e4e7ed;
  border-radius: 6px;
  box-shadow: 0 6px 18px rgba(0, 0, 0, 0.15);

  li {
    padding: 7px 14px;
    font-size: 13px;
    color: #303133;
    cursor: pointer;
    user-select: none;

    &:hover {
      background: #f5f7fa;
    }

    &.danger {
      color: #f56c6c;
    }
  }
}
</style>
