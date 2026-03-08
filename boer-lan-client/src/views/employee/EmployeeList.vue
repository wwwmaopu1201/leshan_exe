<template>
  <div class="page-container">
    <!-- 搜索栏 -->
    <div class="search-bar">
      <el-form :inline="true" :model="searchForm">
        <el-form-item :label="$t('employee.employeeName')">
          <el-input
            v-model="searchForm.keyword"
            :placeholder="$t('common.search')"
            clearable
            @keyup.enter.native="handleSearch"
          />
        </el-form-item>
        <el-form-item :label="$t('employee.department')">
          <el-select v-model="searchForm.department" clearable>
            <el-option label="全部" value="" />
            <el-option label="生产部" value="生产部" />
            <el-option label="质检部" value="质检部" />
            <el-option label="技术部" value="技术部" />
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

    <!-- 操作栏和表格 -->
    <div class="card">
      <div class="table-actions flex-between">
        <div>
          <el-button type="primary" icon="el-icon-plus" @click="handleAdd">
            {{ $t('employee.addEmployee') }}
          </el-button>
          <el-button icon="el-icon-upload2" @click="showImportDialog = true">
            批量导入
          </el-button>
          <el-button
            type="danger"
            icon="el-icon-delete"
            :disabled="!selectedRows.length"
            @click="handleBatchDelete"
          >
            批量删除
          </el-button>
        </div>
        <div>
          <el-button icon="el-icon-download" @click="handleExport">
            {{ $t('common.export') }}
          </el-button>
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
        <el-table-column prop="code" :label="$t('employee.employeeCode')" width="100" />
        <el-table-column prop="name" :label="$t('employee.employeeName')" width="120" />
        <el-table-column prop="department" :label="$t('employee.department')" width="100" />
        <el-table-column prop="position" :label="$t('employee.position')" width="100" />
        <el-table-column prop="phone" :label="$t('employee.phone')" width="140" />
        <el-table-column prop="bindDevice" label="绑定设备" min-width="150">
          <template slot-scope="scope">
            <el-tag v-for="device in scope.row.bindDevices" :key="device" size="small" class="mr-5">
              {{ device }}
            </el-tag>
            <span v-if="!scope.row.bindDevices || !scope.row.bindDevices.length" class="text-muted">未绑定</span>
          </template>
        </el-table-column>
        <el-table-column prop="createTime" :label="$t('common.createTime')" width="160" />
        <el-table-column :label="$t('common.operation')" width="150" align="center" fixed="right">
          <template slot-scope="scope">
            <el-button type="text" size="small" @click="handleEdit(scope.row)">
              {{ $t('common.edit') }}
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
      :title="editForm.id ? $t('employee.editEmployee') : $t('employee.addEmployee')"
      :visible.sync="showEditDialog"
      width="500px"
      @close="resetEditForm"
    >
      <el-form ref="editFormRef" :model="editForm" :rules="editRules" label-width="80px">
        <el-form-item :label="$t('employee.employeeCode')" prop="code">
          <el-input v-model="editForm.code" placeholder="员工编号会自动生成" :disabled="!!editForm.id" />
        </el-form-item>
        <el-form-item :label="$t('employee.employeeName')" prop="name">
          <el-input v-model="editForm.name" />
        </el-form-item>
        <el-form-item :label="$t('employee.department')" prop="department">
          <el-select v-model="editForm.department" style="width: 100%">
            <el-option label="生产部" value="生产部" />
            <el-option label="质检部" value="质检部" />
            <el-option label="技术部" value="技术部" />
          </el-select>
        </el-form-item>
        <el-form-item :label="$t('employee.position')" prop="position">
          <el-input v-model="editForm.position" />
        </el-form-item>
        <el-form-item :label="$t('employee.phone')" prop="phone">
          <el-input v-model="editForm.phone" />
        </el-form-item>
        <el-form-item label="绑定设备">
          <el-select v-model="editForm.bindDevices" multiple style="width: 100%" placeholder="选择要绑定的设备">
            <el-option label="A-001" value="A-001" />
            <el-option label="A-002" value="A-002" />
            <el-option label="B-001" value="B-001" />
            <el-option label="B-002" value="B-002" />
          </el-select>
        </el-form-item>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="showEditDialog = false">{{ $t('common.cancel') }}</el-button>
        <el-button type="primary" @click="handleSave">{{ $t('common.confirm') }}</el-button>
      </span>
    </el-dialog>

    <el-dialog
      title="批量导入员工"
      :visible.sync="showImportDialog"
      width="680px"
    >
      <div style="margin-bottom: 8px; color: #606266;">
        每行格式：`员工工号,员工姓名,部门,职位,手机号`（至少需要前两列）
      </div>
      <el-input
        v-model="importText"
        type="textarea"
        :rows="10"
        placeholder="例如：&#10;E10001,张三,生产部,组长,13800138000&#10;E10002,李四,质检部,质检员,13900139000"
      />
      <span slot="footer" class="dialog-footer">
        <el-button @click="showImportDialog = false">{{ $t('common.cancel') }}</el-button>
        <el-button type="primary" :loading="importing" @click="handleImport">开始导入</el-button>
      </span>
    </el-dialog>
  </div>
</template>

<script>
import { getEmployeeList, createEmployee, updateEmployee, deleteEmployee, importEmployees, exportEmployees } from '@/api/employee'

export default {
  name: 'EmployeeList',
  data() {
    return {
      loading: false,
      tableData: [],
      selectedRows: [],
      searchForm: {
        keyword: '',
        department: ''
      },
      pagination: {
        page: 1,
        pageSize: 10,
        total: 0
      },
      showEditDialog: false,
      showImportDialog: false,
      importing: false,
      importText: '',
      editForm: {
        id: null,
        code: '',
        name: '',
        department: '',
        position: '',
        phone: '',
        bindDevices: []
      },
      editRules: {
        name: [{ required: true, message: '请输入员工姓名', trigger: 'blur' }],
        department: [{ required: true, message: '请选择部门', trigger: 'change' }],
        position: [{ required: true, message: '请输入职位', trigger: 'blur' }],
        phone: [
          { required: true, message: '请输入联系电话', trigger: 'blur' },
          { pattern: /^1[3-9]\d{9}$/, message: '请输入正确的手机号', trigger: 'blur' }
        ]
      }
    }
  },
  mounted() {
    this.fetchData()
  },
  methods: {
    async fetchData() {
      this.loading = true
      try {
        const res = await getEmployeeList({
          keyword: this.searchForm.keyword,
          department: this.searchForm.department,
          page: this.pagination.page,
          pageSize: this.pagination.pageSize
        })
        if (res.code === 0) {
          this.tableData = res.data.list || []
          this.pagination.total = res.data.total || 0
        }
      } catch (error) {
        console.error('Failed to fetch employees:', error)
        this.$message.error('获取员工列表失败')
      } finally {
        this.loading = false
      }
    },
    handleSearch() {
      this.pagination.page = 1
      this.fetchData()
    },
    handleReset() {
      this.searchForm = { keyword: '', department: '' }
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
    handleAdd() {
      this.editForm = {
        id: null,
        code: 'E' + String(Date.now()).slice(-5),
        name: '',
        department: '',
        position: '',
        phone: '',
        bindDevices: []
      }
      this.showEditDialog = true
    },
    handleEdit(row) {
      this.editForm = { ...row }
      this.showEditDialog = true
    },
    resetEditForm() {
      this.$refs.editFormRef?.resetFields()
    },
    async handleSave() {
      try {
        await this.$refs.editFormRef.validate()
        let res
        if (this.editForm.id) {
          res = await updateEmployee(this.editForm.id, this.editForm)
        } else {
          res = await createEmployee(this.editForm)
        }
        if (res.code === 0) {
          this.$message.success(this.$t('common.success'))
          this.showEditDialog = false
          this.fetchData()
        } else {
          this.$message.error(res.message || '保存失败')
        }
      } catch (error) {
        console.error('Save employee failed:', error)
        this.$message.error('保存员工失败')
      }
    },
    handleDelete(row) {
      this.$confirm(this.$t('employee.confirmDelete'), this.$t('common.warning'), {
        confirmButtonText: this.$t('common.confirm'),
        cancelButtonText: this.$t('common.cancel'),
        type: 'warning'
      }).then(async () => {
        try {
          const res = await deleteEmployee(row.id)
          if (res.code === 0) {
            this.$message.success(this.$t('common.success'))
            this.fetchData()
          } else {
            this.$message.error(res.message || '删除失败')
          }
        } catch (error) {
          console.error('Delete employee failed:', error)
          this.$message.error('删除员工失败')
        }
      }).catch(() => {})
    },
    handleBatchDelete() {
      this.$confirm(`确定要删除选中的 ${this.selectedRows.length} 个员工吗？`, this.$t('common.warning'), {
        confirmButtonText: this.$t('common.confirm'),
        cancelButtonText: this.$t('common.cancel'),
        type: 'warning'
      }).then(async () => {
        try {
          for (const row of this.selectedRows) {
            await deleteEmployee(row.id)
          }
          this.$message.success(this.$t('common.success'))
          this.fetchData()
        } catch (error) {
          console.error('Batch delete employees failed:', error)
          this.$message.error('批量删除失败')
        }
      }).catch(() => {})
    },
    async handleImport() {
      const lines = this.importText
        .split('\n')
        .map(line => line.trim())
        .filter(Boolean)

      if (!lines.length) {
        this.$message.warning('请输入导入内容')
        return
      }

      const employees = []
      for (const line of lines) {
        const [code, name, department, position, phone] = line.split(',').map(part => part?.trim())
        if (!code || !name) continue
        employees.push({
          code,
          name,
          department: department || '',
          position: position || '',
          phone: phone || ''
        })
      }

      if (!employees.length) {
        this.$message.warning('未解析到有效员工数据')
        return
      }

      try {
        this.importing = true
        const res = await importEmployees(employees)
        if (res.code === 0) {
          const successCount = res.data?.successCount || 0
          const errors = res.data?.errors || []
          this.$message.success(`导入完成，成功 ${successCount} 条`)
          if (errors.length) {
            this.$alert(errors.join('\n'), '导入失败明细', { type: 'warning' })
          }
          this.importText = ''
          this.showImportDialog = false
          this.fetchData()
        } else {
          this.$message.error(res.message || '导入失败')
        }
      } catch (error) {
        console.error('Import employees failed:', error)
        this.$message.error('导入失败')
      } finally {
        this.importing = false
      }
    },
    async handleExport() {
      try {
        const res = await exportEmployees({
          keyword: this.searchForm.keyword,
          department: this.searchForm.department
        })
        if (res.code !== 0) {
          this.$message.error(res.message || '导出失败')
          return
        }

        const headers = ['员工工号', '员工姓名', '部门', '职位', '手机号', '创建时间']
        const rows = (Array.isArray(res.data) ? res.data : []).map(item => ([
          item.code || '',
          item.name || '',
          item.department || '',
          item.position || '',
          item.phone || '',
          item.createTime || ''
        ]))
        const csv = [headers, ...rows]
          .map(row => row.map(col => `\"${String(col).replace(/\"/g, '\"\"')}\"`).join(','))
          .join('\n')

        const blob = new Blob(['\uFEFF' + csv], { type: 'text/csv;charset=utf-8;' })
        const url = window.URL.createObjectURL(blob)
        const link = document.createElement('a')
        link.href = url
        link.download = `employees_${Date.now()}.csv`
        document.body.appendChild(link)
        link.click()
        document.body.removeChild(link)
        window.URL.revokeObjectURL(url)
        this.$message.success('导出成功')
      } catch (error) {
        console.error('Export employees failed:', error)
        this.$message.error('导出失败')
      }
    }
  }
}
</script>

<style lang="scss" scoped>
.danger-text {
  color: #F56C6C !important;
}

.text-muted {
  color: #909399;
}

.mr-5 {
  margin-right: 5px;
}
</style>
