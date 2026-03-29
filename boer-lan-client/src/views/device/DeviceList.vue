<template>
  <div class="page-container">
    <div class="device-page-layout">
      <aside class="device-page-side">
        <device-tree-panel
          ref="treePanel"
          v-model="treeFilter"
          title="设备树与分组"
          :management="true"
          :min-height="560"
          @change="handleTreeChange"
          @refresh="handleTreeRefresh"
        />
      </aside>

      <section class="device-page-main">
        <div class="search-bar">
          <el-form :inline="true" :model="searchForm">
            <el-form-item :label="$t('device.deviceName')">
              <el-input
                v-model.trim="searchForm.keyword"
                :placeholder="$t('common.search')"
                clearable
                @keyup.enter.native="handleSearch"
              />
            </el-form-item>
            <el-form-item :label="$t('device.deviceStatus')">
              <el-select v-model="searchForm.status" clearable placeholder="全部状态">
                <el-option label="全部" value="" />
                <el-option :label="$t('device.online')" value="online" />
                <el-option :label="$t('device.working')" value="working" />
                <el-option :label="$t('device.idle')" value="idle" />
                <el-option :label="$t('device.offline')" value="offline" />
                <el-option :label="$t('device.alarm')" value="alarm" />
              </el-select>
            </el-form-item>
            <el-form-item :label="$t('common.createTime')">
              <el-date-picker
                v-model="searchForm.dateRange"
                type="daterange"
                value-format="yyyy-MM-dd"
                range-separator="至"
                start-placeholder="开始日期"
                end-placeholder="结束日期"
              />
            </el-form-item>
            <el-form-item>
              <el-button type="primary" icon="el-icon-search" @click="handleSearch">
                {{ $t('common.search') }}
              </el-button>
              <el-button icon="el-icon-refresh" @click="handleReset">
                {{ $t('common.reset') }}
              </el-button>
            </el-form-item>
          </el-form>
        </div>

        <div class="card">
          <div class="table-actions flex-between">
            <div class="action-group">
              <el-button type="primary" icon="el-icon-plus" @click="handleAdd">
                {{ $t('device.addDevice') }}
              </el-button>
              <el-button
                type="danger"
                icon="el-icon-delete"
                :disabled="!selectedRows.length"
                @click="handleBatchDelete"
              >
                {{ $t('device.batchRemoveFromGroup') }}
              </el-button>
              <el-button
                icon="el-icon-folder-add"
                :disabled="!selectedRows.length"
                @click="openMoveDialog"
              >
                {{ $t('device.moveToGroup') }}
              </el-button>
            </div>
            <el-button icon="el-icon-refresh" circle @click="fetchData" />
          </div>

          <el-table
            v-loading="loading"
            :data="pagedTableData"
            border
            :row-class-name="getRowClassName"
            empty-text="暂无数据"
            @selection-change="handleSelectionChange"
            @row-dblclick="handleRowDoubleClick"
          >
            <el-table-column type="selection" width="50" align="center" />
            <el-table-column label="序号" width="70" align="center">
              <template slot-scope="scope">
                {{ (pagination.page - 1) * pagination.pageSize + scope.$index + 1 }}
              </template>
            </el-table-column>
            <el-table-column prop="code" :label="$t('device.deviceCode')" width="120" />
            <el-table-column :label="$t('device.deviceName')" min-width="200">
              <template slot-scope="scope">
                <span>{{ formatDeviceName(scope.row) }}</span>
              </template>
            </el-table-column>
            <el-table-column prop="initialName" :label="$t('device.initialName')" width="130" />
            <el-table-column prop="type" :label="$t('device.deviceType')" width="100" />
            <el-table-column prop="model" :label="$t('device.deviceModel')" width="110" />
            <el-table-column prop="employeeCode" :label="$t('employee.employeeCode')" width="120" />
            <el-table-column prop="employeeName" :label="$t('employee.employeeName')" width="120" />
            <el-table-column prop="mainboardSn" :label="$t('device.mainboardSn')" width="150" />
            <el-table-column prop="ip" :label="$t('device.ipAddress')" width="140" />
            <el-table-column prop="group" :label="$t('device.group')" width="130">
              <template slot-scope="scope">
                <span v-if="scope.row.group">{{ scope.row.group }}</span>
                <span v-else class="ungrouped-text">未分组</span>
              </template>
            </el-table-column>
            <el-table-column prop="remark" :label="$t('common.remark')" min-width="180" show-overflow-tooltip />
            <el-table-column prop="createTime" :label="$t('common.createTime')" width="170" />
            <el-table-column prop="status" :label="$t('device.deviceStatus')" width="110" align="center">
              <template slot-scope="scope">
                <span class="status-dot" :class="'status-' + scope.row.status"></span>
                <el-tag :type="getStatusType(scope.row.status)" size="small">
                  {{ getStatusText(scope.row.status) }}
                </el-tag>
              </template>
            </el-table-column>
            <el-table-column :label="$t('common.operation')" width="190" align="center" fixed="right">
              <template slot-scope="scope">
                <el-button type="text" size="small" @click="handleEdit(scope.row)">
                  {{ $t('common.edit') }}
                </el-button>
                <el-button type="text" size="small" @click="handleMonitor(scope.row)">
                  监控
                </el-button>
                <el-button type="text" size="small" class="danger-text" @click="handleDelete(scope.row)">
                  {{ $t('device.removeFromGroup') }}
                </el-button>
              </template>
            </el-table-column>
          </el-table>

          <el-pagination
            :current-page="pagination.page"
            :page-size="pagination.pageSize"
            :total="pagination.total"
            :page-sizes="[10, 20, 50, 100]"
            layout="total, sizes, prev, pager, next, jumper"
            @size-change="handleSizeChange"
            @current-change="handlePageChange"
          />
        </div>
      </section>
    </div>

    <el-dialog
      :title="editForm.id ? $t('device.editDevice') : $t('device.addDevice')"
      :visible.sync="showEditDialog"
      width="620px"
      @close="resetEditForm"
    >
      <el-form ref="editFormRef" :model="editForm" :rules="editRules" label-width="110px">
        <el-form-item :label="$t('device.deviceCode')" prop="code">
          <el-input v-model="editForm.code" />
        </el-form-item>
        <el-form-item :label="$t('device.deviceName')" prop="name">
          <el-input v-model="editForm.name" />
        </el-form-item>
        <el-form-item :label="$t('device.initialName')" prop="initialName">
          <el-input v-model="editForm.initialName" />
        </el-form-item>
        <el-form-item :label="$t('device.deviceType')" prop="type">
          <el-select v-model="editForm.type">
            <el-option label="缝纫机" value="缝纫机" />
            <el-option label="绣花机" value="绣花机" />
          </el-select>
        </el-form-item>
        <el-form-item :label="$t('device.deviceModel')" prop="model">
          <el-select v-model="editForm.model">
            <el-option label="BM-2000" value="BM-2000" />
            <el-option label="BM-3000" value="BM-3000" />
            <el-option label="BM-5000" value="BM-5000" />
          </el-select>
        </el-form-item>
        <el-form-item :label="$t('device.ipAddress')" prop="ip">
          <el-input v-model="editForm.ip" placeholder="192.168.1.xxx" />
        </el-form-item>
        <el-form-item :label="$t('employee.employeeCode')" prop="employeeCode">
          <el-input v-model="editForm.employeeCode" />
        </el-form-item>
        <el-form-item :label="$t('employee.employeeName')" prop="employeeName">
          <el-input v-model="editForm.employeeName" />
        </el-form-item>
        <el-form-item :label="$t('device.mainboardSn')" prop="mainboardSn">
          <el-input v-model="editForm.mainboardSn" />
        </el-form-item>
        <el-form-item :label="$t('common.remark')" prop="remark">
          <el-input v-model="editForm.remark" type="textarea" :rows="3" />
        </el-form-item>
        <el-form-item :label="$t('device.group')" prop="groupId">
          <el-select v-model="editForm.groupId" clearable placeholder="未分组">
            <el-option
              v-for="group in groupOptions"
              :key="group.id"
              :label="group.name"
              :value="group.id"
            />
          </el-select>
        </el-form-item>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="showEditDialog = false">{{ $t('common.cancel') }}</el-button>
        <el-button type="primary" @click="handleSaveDevice">{{ $t('common.confirm') }}</el-button>
      </span>
    </el-dialog>

    <el-dialog
      :title="$t('device.moveToGroup')"
      :visible.sync="showMoveDialog"
      width="420px"
    >
      <el-form label-width="80px">
        <el-form-item :label="$t('device.group')">
          <el-select v-model="moveTargetGroupId" clearable placeholder="未分组">
            <el-option label="未分组（移出分组）" :value="null" />
            <el-option
              v-for="group in groupOptions"
              :key="group.id"
              :label="group.name"
              :value="group.id"
            />
          </el-select>
        </el-form-item>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="showMoveDialog = false">{{ $t('common.cancel') }}</el-button>
        <el-button type="primary" @click="handleMoveToGroup">{{ $t('common.confirm') }}</el-button>
      </span>
    </el-dialog>
  </div>
</template>

<script>
import DeviceTreePanel from '@/components/DeviceTreePanel.vue'
import {
  getDeviceList,
  createDevice,
  updateDevice,
  deleteDevice,
  batchDeleteDevices,
  moveToGroup,
  getDeviceGroups
} from '@/api/device'

const defaultTreeFilter = () => ({
  label: '',
  nodeType: '',
  groupId: '',
  deviceId: '',
  deviceIds: []
})

export default {
  name: 'DeviceList',
  components: {
    DeviceTreePanel
  },
  data() {
    return {
      loading: false,
      tableData: [],
      groupOptions: [],
      selectedRows: [],
      treeFilter: defaultTreeFilter(),
      searchForm: {
        keyword: '',
        status: '',
        dateRange: []
      },
      pagination: {
        page: 1,
        pageSize: 10,
        total: 0
      },
      showEditDialog: false,
      editForm: {
        id: null,
        code: '',
        name: '',
        initialName: '',
        type: '',
        model: '',
        ip: '',
        employeeCode: '',
        employeeName: '',
        mainboardSn: '',
        remark: '',
        groupId: null
      },
      editRules: {
        code: [{ required: true, message: '请输入设备编码', trigger: 'blur' }],
        name: [{ required: true, message: '请输入设备名称', trigger: 'blur' }],
        type: [{ required: true, message: '请选择设备类型', trigger: 'change' }],
        model: [{ required: true, message: '请选择设备型号', trigger: 'change' }],
        ip: [{ required: true, message: '请输入IP地址', trigger: 'blur' }]
      },
      showMoveDialog: false,
      moveTargetGroupId: null
    }
  },
  computed: {
    currentScopeLabel() {
      return this.treeFilter.label || '全部设备'
    },
    currentScopeTypeText() {
      if (this.treeFilter.nodeType === 'device') return '单台设备'
      if (this.treeFilter.nodeType === 'group') return '设备分组'
      return '全部设备'
    },
    pagedTableData() {
      const start = (this.pagination.page - 1) * this.pagination.pageSize
      return this.tableData.slice(start, start + this.pagination.pageSize)
    }
  },
  mounted() {
    this.fetchGroups()
    this.fetchData()
  },
  methods: {
    async fetchGroups() {
      try {
        const res = await getDeviceGroups()
        if (res.code === 0) {
          this.groupOptions = Array.isArray(res.data) ? res.data : []
        }
      } catch (error) {
        console.error('Failed to fetch groups:', error)
      }
    },
    async fetchData() {
      this.loading = true
      try {
        const res = await getDeviceList({
          keyword: this.searchForm.keyword,
          status: this.searchForm.status,
          groupId: this.treeFilter.nodeType === 'group' ? this.treeFilter.groupId : '',
          startDate: this.searchForm.dateRange?.[0] || '',
          endDate: this.searchForm.dateRange?.[1] || '',
          page: 1,
          pageSize: 2000
        })
        if (res.code === 0) {
          const rawList = Array.isArray(res.data) ? res.data : (res.data?.list || [])
          this.tableData = this.applyTreeFilter(rawList)
          this.pagination.total = this.tableData.length
        }
      } catch (error) {
        console.error('Failed to fetch devices:', error)
        this.$message.error('获取设备列表失败')
      } finally {
        this.loading = false
      }
    },
    applyTreeFilter(list) {
      if (this.treeFilter.nodeType === 'device' && this.treeFilter.deviceId) {
        return list.filter(item => String(item.id) === String(this.treeFilter.deviceId))
      }
      if (this.treeFilter.nodeType === 'group' && this.treeFilter.deviceIds.length) {
        const allowedIds = new Set(this.treeFilter.deviceIds.map(id => String(id)))
        return list.filter(item => allowedIds.has(String(item.id)))
      }
      return list
    },
    handleSearch() {
      this.pagination.page = 1
      this.fetchData()
    },
    handleReset() {
      this.searchForm = {
        keyword: '',
        status: '',
        dateRange: []
      }
      this.treeFilter = defaultTreeFilter()
      this.$refs.treePanel?.clearSelection()
      this.handleSearch()
    },
    handleTreeChange(payload) {
      this.treeFilter = payload
      this.pagination.page = 1
      this.fetchData()
    },
    handleTreeRefresh() {
      this.fetchGroups()
      this.fetchData()
    },
    handleSelectionChange(rows) {
      this.selectedRows = rows
    },
    handleSizeChange(size) {
      this.pagination.pageSize = size
    },
    handlePageChange(page) {
      this.pagination.page = page
    },
    getStatusType(status) {
      const map = {
        online: 'success',
        working: 'primary',
        idle: 'success',
        offline: 'info',
        alarm: 'danger'
      }
      return map[status] || 'info'
    },
    getStatusText(status) {
      const map = {
        online: this.$t('device.online'),
        working: this.$t('device.working'),
        idle: this.$t('device.idle'),
        offline: this.$t('device.offline'),
        alarm: this.$t('device.alarm')
      }
      return map[status] || status
    },
    getRowClassName({ row }) {
      if (!row.groupId) {
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
    handleAdd() {
      this.editForm = {
        id: null,
        code: '',
        name: '',
        initialName: '',
        type: '',
        model: '',
        ip: '',
        employeeCode: '',
        employeeName: '',
        mainboardSn: '',
        remark: '',
        groupId: null
      }
      this.showEditDialog = true
    },
    handleEdit(row) {
      this.editForm = {
        id: row.id,
        code: row.code,
        name: row.name,
        initialName: row.initialName || '',
        type: row.type,
        model: row.model,
        ip: row.ip,
        employeeCode: row.employeeCode || '',
        employeeName: row.employeeName || '',
        mainboardSn: row.mainboardSn || '',
        remark: row.remark || '',
        groupId: row.groupId || null
      }
      this.showEditDialog = true
    },
    handleRowDoubleClick(row) {
      this.handleEdit(row)
    },
    handleMonitor(row) {
      this.$router.push('/device/monitor?id=' + row.id)
    },
    openMoveDialog() {
      this.moveTargetGroupId = null
      this.showMoveDialog = true
    },
    resetEditForm() {
      this.$refs.editFormRef?.resetFields()
    },
    async handleSaveDevice() {
      try {
        await this.$refs.editFormRef.validate()
        const payload = {
          code: this.editForm.code,
          name: this.editForm.name,
          initialName: this.editForm.initialName,
          type: this.editForm.type,
          model: this.editForm.model,
          ip: this.editForm.ip,
          employeeCode: this.editForm.employeeCode,
          employeeName: this.editForm.employeeName,
          mainboardSn: this.editForm.mainboardSn,
          remark: this.editForm.remark,
          groupId: this.editForm.groupId
        }
        let res
        if (this.editForm.id) {
          res = await updateDevice(this.editForm.id, payload)
        } else {
          res = await createDevice(payload)
        }
        if (res.code === 0) {
          this.$message.success(this.$t('common.success'))
          this.showEditDialog = false
          this.fetchGroups()
          this.fetchData()
        } else {
          this.$message.error(res.message || '保存失败')
        }
      } catch (error) {
        console.error('Save device failed:', error)
        this.$message.error('保存设备失败')
      }
    },
    handleDelete(row) {
      this.$confirm(this.$t('device.confirmRemoveFromGroup'), this.$t('common.warning'), {
        confirmButtonText: this.$t('common.confirm'),
        cancelButtonText: this.$t('common.cancel'),
        type: 'warning'
      }).then(async () => {
        try {
          const res = await deleteDevice(row.id)
          if (res.code === 0) {
            this.$message.success(res.message || this.$t('device.removedFromGroup'))
            this.fetchData()
          } else {
            this.$message.error(res.message || '移出分组失败')
          }
        } catch (error) {
          console.error('Delete device failed:', error)
          this.$message.error('移出分组失败')
        }
      }).catch(() => {})
    },
    handleBatchDelete() {
      this.$confirm(
        this.$t('device.confirmBatchRemoveFromGroup', { count: this.selectedRows.length }),
        this.$t('common.warning'),
        {
          confirmButtonText: this.$t('common.confirm'),
          cancelButtonText: this.$t('common.cancel'),
          type: 'warning'
        }
      ).then(async () => {
        try {
          const ids = this.selectedRows.map(row => row.id)
          const res = await batchDeleteDevices(ids)
          if (res.code === 0) {
            this.$message.success(res.message || this.$t('device.batchRemovedFromGroup'))
            this.fetchData()
          } else {
            this.$message.error(res.message || '批量移出分组失败')
          }
        } catch (error) {
          console.error('Batch delete devices failed:', error)
          this.$message.error('批量移出分组失败')
        }
      }).catch(() => {})
    },
    async handleMoveToGroup() {
      try {
        const isUngroup = this.moveTargetGroupId === null || this.moveTargetGroupId === undefined || this.moveTargetGroupId === ''
        const deviceIds = this.selectedRows.map(row => row.id)
        const res = await moveToGroup(deviceIds, isUngroup ? null : this.moveTargetGroupId)
        if (res.code === 0) {
          this.$message.success(isUngroup ? '已移出分组' : this.$t('common.success'))
          this.showMoveDialog = false
          this.moveTargetGroupId = null
          this.fetchData()
        } else {
          this.$message.error(res.message || '移动失败')
        }
      } catch (error) {
        console.error('Move devices failed:', error)
        this.$message.error('移动设备失败')
      }
    }
  }
}
</script>

<style lang="scss" scoped>
.scope-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  margin-bottom: 18px;
}

.scope-copy {
  h3 {
    font-size: 20px;
    color: #243654;
    margin-bottom: 6px;
  }

  p {
    color: #7d8da4;
    font-size: 13px;
  }
}

.scope-tag {
  min-width: 90px;
  height: 34px;
  padding: 0 14px;
  border-radius: 999px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  font-size: 12px;
  font-weight: 700;
  color: #24416f;
  background: #eff4fc;

  &.group {
    background: rgba(47, 109, 246, 0.12);
    color: #2f6df6;
  }

  &.device {
    background: rgba(47, 180, 110, 0.12);
    color: #2fb46e;
  }
}

.action-group {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
}

.danger-text {
  color: #ef5a5a !important;
}

.ungrouped-text {
  color: #ef5a5a;
  font-weight: 700;
}

.status-dot {
  display: inline-block;
  width: 8px;
  height: 8px;
  border-radius: 50%;
  margin-right: 6px;

  &.status-idle,
  &.status-online {
    background: #2fb46e;
  }

  &.status-working {
    background: #2f6df6;
  }

  &.status-offline {
    background: #8a98ad;
  }

  &.status-alarm {
    background: #ef5a5a;
  }
}

::v-deep .el-table .row-ungrouped > td {
  background: #fff2f2;
}
</style>
