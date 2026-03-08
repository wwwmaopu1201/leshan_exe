<template>
  <div>
    <div class="page-title">设备管理</div>
    <el-card>
      <el-form :inline="true" :model="searchForm" style="margin-bottom: 12px;">
        <el-form-item label="关键字">
          <el-input
            v-model="searchForm.keyword"
            clearable
            placeholder="设备名/编号/类型/员工/主板号"
            @keyup.enter.native="handleSearch"
          />
        </el-form-item>
        <el-form-item label="状态">
          <el-select v-model="searchForm.status" clearable placeholder="全部状态">
            <el-option label="在线" value="online" />
            <el-option label="离线" value="offline" />
            <el-option label="缝纫中" value="working" />
            <el-option label="空闲" value="idle" />
            <el-option label="报警" value="alarm" />
          </el-select>
        </el-form-item>
        <el-form-item label="分组">
          <el-select v-model="searchForm.groupId" clearable placeholder="全部分组">
            <el-option
              v-for="item in groups"
              :key="item.id"
              :label="item.parent ? `${item.parent.name} / ${item.name}` : item.name"
              :value="item.id"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="添加时间">
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
          <el-button type="primary" icon="el-icon-search" @click="handleSearch">搜索</el-button>
          <el-button icon="el-icon-refresh" @click="handleReset">重置</el-button>
        </el-form-item>
      </el-form>

      <div style="margin-bottom: 12px; display: flex; gap: 8px;">
        <el-button icon="el-icon-share" :disabled="selectedDeviceIds.length === 0" @click="openMoveDialog">
          批量移动分组
        </el-button>
        <el-button icon="el-icon-refresh" @click="loadDevices">刷新</el-button>
      </div>

      <el-table
        ref="deviceTableRef"
        :data="devices"
        v-loading="loading"
        style="width: 100%;"
        :row-class-name="rowClassName"
        @selection-change="handleSelectionChange"
      >
        <el-table-column type="selection" width="48" />
        <el-table-column prop="name" label="设备名称" min-width="130" />
        <el-table-column prop="initialName" label="初始名称" min-width="130" />
        <el-table-column prop="employeeCode" label="员工工号" width="100" />
        <el-table-column prop="employeeName" label="员工姓名" width="100" />
        <el-table-column prop="code" label="设备编号" min-width="130" />
        <el-table-column prop="type" label="设备类型" width="110" />
        <el-table-column prop="model" label="机型" width="110" />
        <el-table-column prop="mainboardSn" label="主板编号" min-width="140" />
        <el-table-column label="状态" width="100">
          <template slot-scope="{ row }">
            <el-tag :type="getStatusType(row.status)" size="mini">
              {{ getStatusLabel(row.status) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="ip" label="设备IP" width="130" />
        <el-table-column label="分组" min-width="150">
          <template slot-scope="{ row }">
            <span :style="{ color: row.groupId ? '#303133' : '#f56c6c', fontWeight: row.groupId ? 'normal' : 'bold' }">
              {{ row.group || '未分组' }}
            </span>
          </template>
        </el-table-column>
        <el-table-column prop="createTime" label="添加时间" width="170" />
        <el-table-column prop="remark" label="备注" min-width="120" show-overflow-tooltip />
        <el-table-column label="操作" width="180" fixed="right">
          <template slot-scope="{ row }">
            <el-button size="small" icon="el-icon-edit" @click="openEditDialog(row)">编辑</el-button>
            <el-button size="small" @click="pingDevice(row.ip)" icon="el-icon-connection">Ping</el-button>
          </template>
        </el-table-column>
      </el-table>

      <div style="display: flex; justify-content: flex-end; margin-top: 12px;">
        <el-pagination
          background
          layout="total, sizes, prev, pager, next, jumper"
          :current-page.sync="page"
          :page-size.sync="pageSize"
          :page-sizes="[10, 20, 50, 100]"
          :total="total"
          @size-change="handlePageSizeChange"
          @current-change="handlePageChange"
        />
      </div>
    </el-card>

    <el-dialog
      title="编辑设备"
      :visible.sync="editDialogVisible"
      width="520px"
    >
      <el-form ref="editFormRef" :model="editForm" :rules="editRules" label-width="90px">
        <el-form-item label="设备名称" prop="name">
          <el-input v-model="editForm.name" />
        </el-form-item>
        <el-form-item label="设备分组">
          <el-select v-model="editForm.groupId" clearable style="width: 100%;">
            <el-option label="未分组" :value="null" />
            <el-option
              v-for="item in groups"
              :key="item.id"
              :label="item.parent ? `${item.parent.name} / ${item.name}` : item.name"
              :value="item.id"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="备注">
          <el-input v-model="editForm.remark" type="textarea" :rows="2" />
        </el-form-item>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="editDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="saveDevice">保存</el-button>
      </span>
    </el-dialog>

    <el-dialog
      title="批量移动设备分组"
      :visible.sync="moveDialogVisible"
      width="420px"
    >
      <el-form label-width="90px">
        <el-form-item label="目标分组">
          <el-select v-model="moveTargetGroupId" clearable placeholder="可选择“未分组”" style="width: 100%;">
            <el-option label="未分组" :value="null" />
            <el-option
              v-for="item in groups"
              :key="item.id"
              :label="item.parent ? `${item.parent.name} / ${item.name}` : item.name"
              :value="item.id"
            />
          </el-select>
        </el-form-item>
        <div style="color: #606266;">已选择 {{ selectedDeviceIds.length }} 台设备</div>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="moveDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="moving" @click="confirmMoveDevices">确定移动</el-button>
      </span>
    </el-dialog>
  </div>
</template>

<script>
export default {
  name: 'Devices',
  data() {
    return {
      loading: false,
      saving: false,
      moving: false,
      devices: [],
      groups: [],
      total: 0,
      page: 1,
      pageSize: 20,
      selectedDeviceIds: [],
      moveDialogVisible: false,
      moveTargetGroupId: null,
      editDialogVisible: false,
      searchForm: {
        keyword: '',
        status: '',
        groupId: null,
        dateRange: []
      },
      editForm: {
        id: null,
        name: '',
        groupId: null,
        remark: ''
      },
      editRules: {
        name: [{ required: true, message: '请输入设备名称', trigger: 'blur' }]
      }
    }
  },
  mounted() {
    this.loadGroups()
    this.loadDevices()
  },
  methods: {
    async loadGroups() {
      try {
        const res = await this.$axios.get('/group/list')
        if (res.code === 0) {
          this.groups = Array.isArray(res.data) ? res.data : []
        }
      } catch (error) {
        console.error('加载分组失败', error)
      }
    },
    async loadDevices() {
      this.loading = true
      try {
        const params = {
          keyword: this.searchForm.keyword,
          status: this.searchForm.status,
          groupId: this.searchForm.groupId,
          startDate: this.searchForm.dateRange?.[0],
          endDate: this.searchForm.dateRange?.[1],
          page: this.page,
          pageSize: this.pageSize
        }
        const res = await this.$axios.get('/device/list', { params })
        if (res.code === 0) {
          const payload = res.data || {}
          const list = Array.isArray(payload.list) ? payload.list : (Array.isArray(res.data) ? res.data : [])
          this.devices = list
          this.total = Number(payload.total || list.length || 0)
        }
        this.selectedDeviceIds = []
        this.$nextTick(() => {
          this.$refs.deviceTableRef?.clearSelection()
        })
      } catch (error) {
        console.error('加载设备列表失败', error)
      } finally {
        this.loading = false
      }
    },
    handleSearch() {
      this.page = 1
      this.loadDevices()
    },
    handleReset() {
      this.searchForm = {
        keyword: '',
        status: '',
        groupId: null,
        dateRange: []
      }
      this.page = 1
      this.loadDevices()
    },
    handlePageChange(page) {
      this.page = page
      this.loadDevices()
    },
    handlePageSizeChange(size) {
      this.pageSize = size
      this.page = 1
      this.loadDevices()
    },
    rowClassName({ row }) {
      return row.groupId ? '' : 'row-ungrouped'
    },
    handleSelectionChange(rows) {
      this.selectedDeviceIds = rows.map(item => item.id || item.ID).filter(Boolean)
    },
    openMoveDialog() {
      this.moveTargetGroupId = null
      this.moveDialogVisible = true
    },
    async confirmMoveDevices() {
      if (!this.selectedDeviceIds.length) {
        this.$message.warning('请先选择设备')
        return
      }
      this.moving = true
      try {
        const res = await this.$axios.post('/device/move', {
          deviceIds: this.selectedDeviceIds,
          groupId: this.moveTargetGroupId
        })
        if (res.code === 0) {
          this.$message.success('分组移动成功')
          this.moveDialogVisible = false
          await this.loadDevices()
        }
      } catch (error) {
        console.error('移动设备分组失败', error)
      } finally {
        this.moving = false
      }
    },
    openEditDialog(row) {
      this.editDialogVisible = true
      this.editForm = {
        id: row.id || row.ID,
        name: row.name || '',
        groupId: row.groupId ?? null,
        remark: row.remark || ''
      }
    },
    async saveDevice() {
      try {
        await this.$refs.editFormRef.validate()
        this.saving = true
        const res = await this.$axios.put(`/device/${this.editForm.id}`, {
          name: this.editForm.name.trim(),
          groupId: this.editForm.groupId,
          remark: this.editForm.remark.trim()
        })
        if (res.code === 0) {
          this.$message.success('更新成功')
          this.editDialogVisible = false
          await this.loadDevices()
        }
      } catch (error) {
        console.error('更新设备失败', error)
      } finally {
        this.saving = false
      }
    },
    getStatusType(status) {
      const map = {
        online: 'success',
        offline: 'info',
        working: 'danger',
        idle: 'warning',
        alarm: 'danger'
      }
      return map[status] || 'info'
    },
    getStatusLabel(status) {
      const map = {
        online: '在线',
        offline: '离线',
        working: '缝纫中',
        idle: '空闲',
        alarm: '报警'
      }
      return map[status] || status || '-'
    },
    async pingDevice(ip) {
      try {
        const res = await this.$axios.post('/system/ping', null, {
          params: { ip }
        })
        if (res.code === 0) {
          this.$alert(`<pre>${res.data.output}</pre>`, 'Ping结果', {
            dangerouslyUseHTMLString: true,
            confirmButtonText: '确定'
          })
        }
      } catch (error) {
        console.error('Ping失败', error)
      }
    }
  }
}
</script>

<style scoped>
.page-title {
  font-size: 20px;
  font-weight: bold;
  margin-bottom: 20px;
}

::v-deep .row-ungrouped {
  background: #fff6f6;
}
</style>
