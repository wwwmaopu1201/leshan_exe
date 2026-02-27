<template>
  <div class="page-container">
    <el-row :gutter="20">
      <!-- 左侧分组树 -->
      <el-col :span="8">
        <div class="card">
          <div class="card-header flex-between">
            <span>设备分组</span>
            <el-button type="primary" size="small" icon="el-icon-plus" @click="handleAddGroup">
              新增分组
            </el-button>
          </div>
          <el-tree
            ref="groupTree"
            :data="groupTree"
            node-key="id"
            default-expand-all
            highlight-current
            :expand-on-click-node="false"
            @node-click="handleNodeClick"
          >
            <div class="tree-node flex-between" style="width: 100%" slot-scope="{ node, data }">
              <span>
                <i :class="data.children ? 'el-icon-folder' : 'el-icon-folder-opened'"></i>
                {{ node.label }}
                <span class="device-count">({{ data.deviceCount || 0 }})</span>
              </span>
              <span class="node-actions" v-if="data.id !== 1">
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

      <!-- 右侧分组详情 -->
      <el-col :span="16">
        <div class="card">
          <div class="card-header">
            分组信息 - {{ selectedGroup?.label || '请选择分组' }}
          </div>
          <template v-if="selectedGroup">
            <el-descriptions :column="2" border>
              <el-descriptions-item label="分组名称">{{ selectedGroup.label }}</el-descriptions-item>
              <el-descriptions-item label="设备数量">{{ selectedGroup.deviceCount || 0 }}</el-descriptions-item>
              <el-descriptions-item label="创建时间">2024-01-01 10:00:00</el-descriptions-item>
              <el-descriptions-item label="更新时间">2024-01-15 14:30:00</el-descriptions-item>
            </el-descriptions>

            <div class="mt-20">
              <h4>分组设备列表</h4>
              <el-table :data="groupDevices" border class="mt-10">
                <el-table-column prop="code" label="设备编码" width="120" />
                <el-table-column prop="name" label="设备名称" />
                <el-table-column prop="status" label="状态" width="100">
                  <template slot-scope="scope">
                    <el-tag :type="getStatusType(scope.row.status)" size="small">
                      {{ scope.row.status }}
                    </el-tag>
                  </template>
                </el-table-column>
                <el-table-column label="操作" width="100" align="center">
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

    <!-- 新增/编辑分组弹窗 -->
    <el-dialog
      :title="groupForm.id ? '编辑分组' : '新增分组'"
      :visible.sync="showGroupDialog"
      width="400px"
    >
      <el-form ref="groupFormRef" :model="groupForm" :rules="groupRules" label-width="80px">
        <el-form-item label="分组名称" prop="name">
          <el-input v-model="groupForm.name" placeholder="请输入分组名称" />
        </el-form-item>
        <el-form-item label="上级分组" prop="parentId">
          <el-cascader
            v-model="groupForm.parentId"
            :options="groupTree"
            :props="{ checkStrictly: true, value: 'id', label: 'label' }"
            clearable
            style="width: 100%"
          />
        </el-form-item>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="showGroupDialog = false">取消</el-button>
        <el-button type="primary" @click="handleSaveGroup">确定</el-button>
      </span>
    </el-dialog>
  </div>
</template>

<script>
export default {
  name: 'DeviceGroup',
  data() {
    return {
      groupTree: [
        {
          id: 1,
          label: '全部设备',
          deviceCount: 50,
          children: [
            { id: 2, label: 'A车间', deviceCount: 20 },
            { id: 3, label: 'B车间', deviceCount: 15 },
            { id: 4, label: 'C车间', deviceCount: 15 }
          ]
        }
      ],
      selectedGroup: null,
      groupDevices: [],
      showGroupDialog: false,
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
  methods: {
    handleNodeClick(data) {
      this.selectedGroup = data
      // Mock devices for selected group
      this.groupDevices = [
        { id: 1, code: 'A-001', name: '缝纫机A-001', status: 'online' },
        { id: 2, code: 'A-002', name: '缝纫机A-002', status: 'working' },
        { id: 3, code: 'A-003', name: '缝纫机A-003', status: 'offline' }
      ]
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
      this.groupForm = { id: null, name: '', parentId: null }
      this.showGroupDialog = true
    },
    handleEditGroup(data) {
      this.groupForm = {
        id: data.id,
        name: data.label,
        parentId: null
      }
      this.showGroupDialog = true
    },
    handleDeleteGroup(data) {
      this.$confirm(`确定要删除分组"${data.label}"吗？`, '警告', {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        type: 'warning'
      }).then(() => {
        this.$message.success('删除成功')
      }).catch(() => {})
    },
    async handleSaveGroup() {
      try {
        await this.$refs.groupFormRef.validate()
        this.$message.success('保存成功')
        this.showGroupDialog = false
      } catch (error) {
        console.error('Validation failed:', error)
      }
    },
    handleRemoveDevice(row) {
      this.$confirm(`确定要将设备"${row.name}"从当前分组移除吗？`, '提示', {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        type: 'warning'
      }).then(() => {
        this.$message.success('移除成功')
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
</style>
