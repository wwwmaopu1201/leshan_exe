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
            :expand-on-click-node="false"
            @node-click="handleNodeClick"
          >
            <div class="tree-node flex-between" style="width: 100%" slot-scope="{ node, data }">
              <span>
                <i :class="(data.children && data.children.length) ? 'el-icon-folder-opened' : 'el-icon-folder'"></i>
                {{ node.label }}
                <span class="device-count">({{ data.deviceCount || 0 }})</span>
              </span>
              <span class="node-actions" v-if="!data.isRoot">
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
            <el-descriptions :column="2" border>
              <el-descriptions-item label="分组名称">{{ selectedGroup.label }}</el-descriptions-item>
              <el-descriptions-item label="设备数量">{{ groupDevices.length }}</el-descriptions-item>
              <el-descriptions-item label="分组ID">{{ selectedGroup.id }}</el-descriptions-item>
              <el-descriptions-item label="上级分组">{{ selectedGroup.parentLabel || '无' }}</el-descriptions-item>
            </el-descriptions>

            <div class="mt-20">
              <h4>分组设备列表</h4>
              <el-table
                :data="groupDevices"
                border
                class="mt-10"
                v-loading="loadingDevices"
                :row-class-name="getDeviceRowClass"
              >
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
                <el-table-column label="操作" width="100" align="center" v-if="selectedGroup.id !== 'all'">
                  <template slot-scope="scope">
                    <el-button type="text" size="small" @click="handleRemoveDevice(scope.row)">
                      移除
                    </el-button>
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
  </div>
</template>

<script>
import {
  getDeviceGroups,
  createDeviceGroup,
  updateDeviceGroup,
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
      flatGroups: [],
      groupTree: [],
      allDevices: [],
      selectedGroup: null,
      groupDevices: [],
      showGroupDialog: false,
      groupDialogMode: 'addRoot',
      groupForm: {
        id: null,
        name: '',
        parentId: null
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
    this.fetchAll()
  },
  methods: {
    async fetchAll() {
      await Promise.all([this.fetchGroups(), this.fetchDevices()])
      this.buildGroupTree()
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

      const ungroupedCount = this.allDevices.filter(device => !(device.groupId && Number(device.groupId) > 0)).length
      const ungroupedNode = {
        id: 'ungrouped',
        label: '未分组设备',
        parentId: null,
        parentLabel: '',
        children: [],
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
    syncGroupDevices() {
      if (!this.selectedGroup) {
        this.groupDevices = []
        return
      }

      if (this.selectedGroup.id === 'all') {
        this.groupDevices = this.sortDevicesForDisplay(this.allDevices)
        return
      }
      if (this.selectedGroup.id === 'ungrouped') {
        this.groupDevices = this.sortDevicesForDisplay(
          this.allDevices.filter(device => !(device.groupId && Number(device.groupId) > 0))
        )
        return
      }

      const groupIdSet = this.getCurrentGroupIdSet()
      const devices = this.allDevices.filter(device => groupIdSet.has(Number(device.groupId || 0)))
      this.groupDevices = this.sortDevicesForDisplay(devices)
    },
    handleNodeClick(data) {
      this.selectedGroup = data
      this.syncGroupDevices()
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
      this.$confirm(`确定要将设备"${row.name}"从当前分组移除吗？`, '提示', {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        type: 'warning'
      }).then(async () => {
        try {
          const res = await moveToGroup([row.id], null)
          if (res.code === 0) {
            this.$message.success('移除成功')
            await this.fetchDevices()
            this.buildGroupTree()
            this.syncGroupDevices()
          } else {
            this.$message.error(res.message || '移除失败')
          }
        } catch (error) {
          console.error('Remove device from group failed:', error)
          this.$message.error('移除设备失败')
        }
      }).catch(() => {})
    }
  }
}
</script>

<style lang="scss" scoped>
.tree-node {
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
</style>
