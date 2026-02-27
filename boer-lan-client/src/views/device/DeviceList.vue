<template>
  <div class="page-container">
    <!-- 搜索栏 -->
    <div class="search-bar">
      <el-form :inline="true" :model="searchForm">
        <el-form-item :label="$t('device.deviceName')">
          <el-input
            v-model="searchForm.keyword"
            :placeholder="$t('common.search')"
            clearable
            @keyup.enter.native="handleSearch"
          />
        </el-form-item>
        <el-form-item :label="$t('device.deviceStatus')">
          <el-select v-model="searchForm.status" clearable>
            <el-option label="全部" value="" />
            <el-option :label="$t('device.online')" value="online" />
            <el-option :label="$t('device.working')" value="working" />
            <el-option :label="$t('device.idle')" value="idle" />
            <el-option :label="$t('device.offline')" value="offline" />
            <el-option :label="$t('device.alarm')" value="alarm" />
          </el-select>
        </el-form-item>
        <el-form-item :label="$t('device.group')">
          <el-select v-model="searchForm.group" clearable>
            <el-option label="全部" value="" />
            <el-option label="A车间" value="A车间" />
            <el-option label="B车间" value="B车间" />
            <el-option label="C车间" value="C车间" />
          </el-select>
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

    <!-- 操作栏 -->
    <div class="card">
      <div class="table-actions flex-between">
        <div>
          <el-button type="primary" icon="el-icon-plus" @click="handleAdd">
            {{ $t('device.addDevice') }}
          </el-button>
          <el-button
            type="danger"
            icon="el-icon-delete"
            :disabled="!selectedRows.length"
            @click="handleBatchDelete"
          >
            {{ $t('device.batchDelete') }}
          </el-button>
          <el-button
            icon="el-icon-folder-add"
            :disabled="!selectedRows.length"
            @click="showMoveDialog = true"
          >
            {{ $t('device.moveToGroup') }}
          </el-button>
        </div>
        <div>
          <el-button icon="el-icon-refresh" circle @click="fetchData" />
        </div>
      </div>

      <!-- 数据表格 -->
      <el-table
        v-loading="loading"
        :data="tableData"
        border
        @selection-change="handleSelectionChange"
      >
        <el-table-column type="selection" width="50" align="center" />
        <el-table-column prop="code" :label="$t('device.deviceCode')" width="120" />
        <el-table-column prop="name" :label="$t('device.deviceName')" min-width="150" />
        <el-table-column prop="type" :label="$t('device.deviceType')" width="100" />
        <el-table-column prop="model" :label="$t('device.deviceModel')" width="100" />
        <el-table-column prop="ip" :label="$t('device.ipAddress')" width="140" />
        <el-table-column prop="group" :label="$t('device.group')" width="100" />
        <el-table-column prop="status" :label="$t('device.deviceStatus')" width="100" align="center">
          <template slot-scope="scope">
            <el-tag :type="getStatusType(scope.row.status)" size="small">
              {{ getStatusText(scope.row.status) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column :label="$t('common.operation')" width="180" align="center" fixed="right">
          <template slot-scope="scope">
            <el-button type="text" size="small" @click="handleEdit(scope.row)">
              {{ $t('common.edit') }}
            </el-button>
            <el-button type="text" size="small" @click="handleMonitor(scope.row)">
              监控
            </el-button>
            <el-button type="text" size="small" class="danger-text" @click="handleDelete(scope.row)">
              {{ $t('common.delete') }}
            </el-button>
          </template>
        </el-table-column>
      </el-table>

      <!-- 分页 -->
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

    <!-- 新增/编辑弹窗 -->
    <el-dialog
      :title="editForm.id ? $t('device.editDevice') : $t('device.addDevice')"
      :visible.sync="showEditDialog"
      width="500px"
      @close="resetEditForm"
    >
      <el-form ref="editFormRef" :model="editForm" :rules="editRules" label-width="100px">
        <el-form-item :label="$t('device.deviceCode')" prop="code">
          <el-input v-model="editForm.code" />
        </el-form-item>
        <el-form-item :label="$t('device.deviceName')" prop="name">
          <el-input v-model="editForm.name" />
        </el-form-item>
        <el-form-item :label="$t('device.deviceType')" prop="type">
          <el-select v-model="editForm.type" style="width: 100%">
            <el-option label="缝纫机" value="缝纫机" />
            <el-option label="绣花机" value="绣花机" />
          </el-select>
        </el-form-item>
        <el-form-item :label="$t('device.deviceModel')" prop="model">
          <el-select v-model="editForm.model" style="width: 100%">
            <el-option label="BM-2000" value="BM-2000" />
            <el-option label="BM-3000" value="BM-3000" />
            <el-option label="BM-5000" value="BM-5000" />
          </el-select>
        </el-form-item>
        <el-form-item :label="$t('device.ipAddress')" prop="ip">
          <el-input v-model="editForm.ip" placeholder="192.168.1.xxx" />
        </el-form-item>
        <el-form-item :label="$t('device.group')" prop="group">
          <el-select v-model="editForm.group" style="width: 100%">
            <el-option label="A车间" value="A车间" />
            <el-option label="B车间" value="B车间" />
            <el-option label="C车间" value="C车间" />
          </el-select>
        </el-form-item>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="showEditDialog = false">{{ $t('common.cancel') }}</el-button>
        <el-button type="primary" @click="handleSaveDevice">{{ $t('common.confirm') }}</el-button>
      </span>
    </el-dialog>

    <!-- 移动到分组弹窗 -->
    <el-dialog
      :title="$t('device.moveToGroup')"
      :visible.sync="showMoveDialog"
      width="400px"
    >
      <el-form label-width="80px">
        <el-form-item :label="$t('device.group')">
          <el-select v-model="moveTargetGroup" style="width: 100%">
            <el-option label="A车间" value="A车间" />
            <el-option label="B车间" value="B车间" />
            <el-option label="C车间" value="C车间" />
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
import { getDeviceList, createDevice, updateDevice, deleteDevice, batchDeleteDevices, moveToGroup, getDeviceGroups } from '@/api/device'

export default {
  name: 'DeviceList',
  data() {
    return {
      loading: false,
      tableData: [],
      selectedRows: [],
      searchForm: {
        keyword: '',
        status: '',
        group: ''
      },
      pagination: {
        page: 1,
        pageSize: 10,
        total: 7
      },
      showEditDialog: false,
      editForm: {
        id: null,
        code: '',
        name: '',
        type: '',
        model: '',
        ip: '',
        group: ''
      },
      editRules: {
        code: [{ required: true, message: '请输入设备编码', trigger: 'blur' }],
        name: [{ required: true, message: '请输入设备名称', trigger: 'blur' }],
        type: [{ required: true, message: '请选择设备类型', trigger: 'change' }],
        model: [{ required: true, message: '请选择设备型号', trigger: 'change' }],
        ip: [{ required: true, message: '请输入IP地址', trigger: 'blur' }],
        group: [{ required: true, message: '请选择分组', trigger: 'change' }]
      },
      showMoveDialog: false,
      moveTargetGroup: ''
    }
  },
  mounted() {
    this.fetchData()
  },
  methods: {
    async fetchData() {
      this.loading = true
      try {
        const res = await getDeviceList({
          keyword: this.searchForm.keyword,
          status: this.searchForm.status,
          group: this.searchForm.group,
          page: this.pagination.page,
          pageSize: this.pagination.pageSize
        })
        if (res.code === 0) {
          this.tableData = res.data.list || []
          this.pagination.total = res.data.total || 0
        }
      } catch (error) {
        console.error('Failed to fetch devices:', error)
        this.$message.error('获取设备列表失败')
      } finally {
        this.loading = false
      }
    },
    handleSearch() {
      this.pagination.page = 1
      this.fetchData()
    },
    handleReset() {
      this.searchForm = { keyword: '', status: '', group: '' }
      this.handleSearch()
    },
    handleSelectionChange(rows) {
      this.selectedRows = rows
    },
    handleSizeChange(size) {
      this.pagination.pageSize = size
      this.fetchData()
    },
    handlePageChange(page) {
      this.pagination.page = page
      this.fetchData()
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
    handleAdd() {
      this.editForm = {
        id: null,
        code: '',
        name: '',
        type: '',
        model: '',
        ip: '',
        group: ''
      }
      this.showEditDialog = true
    },
    handleEdit(row) {
      this.editForm = { ...row }
      this.showEditDialog = true
    },
    handleMonitor(row) {
      this.$router.push('/device/monitor?id=' + row.id)
    },
    resetEditForm() {
      this.$refs.editFormRef?.resetFields()
    },
    async handleSaveDevice() {
      try {
        await this.$refs.editFormRef.validate()
        let res
        if (this.editForm.id) {
          res = await updateDevice(this.editForm.id, this.editForm)
        } else {
          res = await createDevice(this.editForm)
        }
        if (res.code === 0) {
          this.$message.success(this.$t('common.success'))
          this.showEditDialog = false
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
      this.$confirm(this.$t('device.confirmDelete'), this.$t('common.warning'), {
        confirmButtonText: this.$t('common.confirm'),
        cancelButtonText: this.$t('common.cancel'),
        type: 'warning'
      }).then(async () => {
        try {
          const res = await deleteDevice(row.id)
          if (res.code === 0) {
            this.$message.success(this.$t('common.success'))
            this.fetchData()
          } else {
            this.$message.error(res.message || '删除失败')
          }
        } catch (error) {
          console.error('Delete device failed:', error)
          this.$message.error('删除设备失败')
        }
      }).catch(() => {})
    },
    handleBatchDelete() {
      this.$confirm(`确定要删除选中的 ${this.selectedRows.length} 个设备吗？`, this.$t('common.warning'), {
        confirmButtonText: this.$t('common.confirm'),
        cancelButtonText: this.$t('common.cancel'),
        type: 'warning'
      }).then(async () => {
        try {
          const ids = this.selectedRows.map(r => r.id)
          const res = await batchDeleteDevices(ids)
          if (res.code === 0) {
            this.$message.success(this.$t('common.success'))
            this.fetchData()
          } else {
            this.$message.error(res.message || '批量删除失败')
          }
        } catch (error) {
          console.error('Batch delete devices failed:', error)
          this.$message.error('批量删除设备失败')
        }
      }).catch(() => {})
    },
    async handleMoveToGroup() {
      if (!this.moveTargetGroup) {
        this.$message.warning('请选择目标分组')
        return
      }
      try {
        const deviceIds = this.selectedRows.map(r => r.id)
        const res = await moveToGroup(deviceIds, this.moveTargetGroup)
        if (res.code === 0) {
          this.$message.success(this.$t('common.success'))
          this.showMoveDialog = false
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
.danger-text {
  color: #F56C6C !important;
}
</style>
