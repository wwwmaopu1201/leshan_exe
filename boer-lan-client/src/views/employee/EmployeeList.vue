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
          <el-button icon="el-icon-document" @click="downloadImportTemplate">
            下载导入模板
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
        <el-table-column prop="phone" :label="$t('employee.phone')" width="140" />
        <el-table-column prop="remark" :label="$t('common.remark')" min-width="180" show-overflow-tooltip />
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
          <el-input v-model="editForm.code" placeholder="请输入11位以内员工工号" :disabled="!!editForm.id" />
        </el-form-item>
        <el-form-item :label="$t('employee.employeeName')" prop="name">
          <el-input v-model="editForm.name" />
        </el-form-item>
        <el-form-item :label="$t('employee.phone')" prop="phone">
          <el-input v-model="editForm.phone" />
        </el-form-item>
        <el-form-item :label="$t('common.remark')" prop="remark">
          <el-input v-model="editForm.remark" type="textarea" :rows="3" />
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
      @close="resetImportDialog"
    >
      <div style="margin-bottom: 8px; color: #606266;">
        每行格式：`员工工号,员工姓名,手机号,备注`（前三列必填）
      </div>
      <div style="margin-bottom: 10px;">
        <el-button size="small" icon="el-icon-folder-opened" @click="triggerImportFileSelect">
          选择CSV文件
        </el-button>
        <span style="margin-left: 8px; color: #909399;">
          {{ importFileName || '未选择文件' }}
        </span>
        <input
          ref="importFileInput"
          type="file"
          accept=".csv,text/csv"
          style="display: none;"
          @change="handleImportFileChange"
        >
      </div>
      <el-input
        v-model="importText"
        type="textarea"
        :rows="10"
        placeholder="可直接粘贴CSV内容，或点击上方“选择CSV文件”自动填充&#10;例如：&#10;E10001,张三,13800138000,一组员工&#10;E10002,李四,13900139000,二组员工"
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
        keyword: ''
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
      importFileName: '',
      editForm: {
        id: null,
        code: '',
        name: '',
        phone: '',
        remark: ''
      },
      editRules: {
        code: [
          { required: true, message: '请输入员工工号', trigger: 'blur' },
          { max: 11, message: '员工工号不能超过11位', trigger: 'blur' }
        ],
        name: [{ required: true, message: '请输入员工姓名', trigger: 'blur' }],
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
      this.searchForm = { keyword: '' }
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
        code: '',
        name: '',
        phone: '',
        remark: ''
      }
      this.showEditDialog = true
    },
    handleEdit(row) {
      this.editForm = {
        id: row.id,
        code: row.code || '',
        name: row.name || '',
        phone: row.phone || '',
        remark: row.remark || ''
      }
      this.showEditDialog = true
    },
    resetEditForm() {
      this.$refs.editFormRef?.resetFields()
    },
    resetImportDialog() {
      this.importText = ''
      this.importFileName = ''
      if (this.$refs.importFileInput) {
        this.$refs.importFileInput.value = ''
      }
    },
    triggerImportFileSelect() {
      if (!this.$refs.importFileInput) {
        return
      }
      this.$refs.importFileInput.value = ''
      this.$refs.importFileInput.click()
    },
    async handleImportFileChange(event) {
      const file = event?.target?.files?.[0]
      if (!file) {
        return
      }
      if (!/\.csv$/i.test(file.name)) {
        this.$message.warning('请选择CSV文件')
        return
      }
      try {
        const text = await this.readImportFile(file)
        this.importText = text
        this.importFileName = file.name
        this.$message.success(`已读取文件：${file.name}`)
      } catch (error) {
        console.error('Read import file failed:', error)
        this.$message.error('读取CSV文件失败，请重试')
      }
    },
    readImportFile(file) {
      return new Promise((resolve, reject) => {
        const reader = new FileReader()
        reader.onload = () => {
          const text = typeof reader.result === 'string' ? reader.result : ''
          resolve(text.replace(/^\uFEFF/, ''))
        }
        reader.onerror = () => reject(reader.error || new Error('读取文件失败'))
        reader.readAsText(file, 'utf-8')
      })
    },
    parseCsvLine(line) {
      const values = []
      let current = ''
      let inQuotes = false

      for (let i = 0; i < line.length; i += 1) {
        const char = line[i]
        if (char === '"') {
          if (inQuotes && line[i + 1] === '"') {
            current += '"'
            i += 1
          } else {
            inQuotes = !inQuotes
          }
          continue
        }
        if (char === ',' && !inQuotes) {
          values.push(current.trim())
          current = ''
          continue
        }
        current += char
      }
      values.push(current.trim())

      return values
    },
    isImportHeader(parts) {
      const first = (parts[0] || '').replace(/\s/g, '').toLowerCase()
      return first === '员工工号' || first === 'employeecode' || first === 'code'
    },
    parseImportText() {
      const lines = this.importText
        .replace(/^\uFEFF/, '')
        .split(/\r?\n/)
        .map(line => line.trim())
        .filter(Boolean)

      const employees = []
      const lineErrors = []

      lines.forEach((line, index) => {
        const parts = this.parseCsvLine(line).map(part => part.trim())
        if (index === 0 && this.isImportHeader(parts)) {
          return
        }
        const [code, name, phone, remark] = parts
        const lineNo = index + 1
        if (!code || !name || !phone) {
          lineErrors.push(`第${lineNo}行格式错误: ${line}`)
          return
        }
        if (code.length > 11) {
          lineErrors.push(`第${lineNo}行工号超过11位: ${line}`)
          return
        }
        if (!/^1[3-9]\d{9}$/.test(phone)) {
          lineErrors.push(`第${lineNo}行手机号格式错误: ${line}`)
          return
        }
        employees.push({
          code,
          name,
          phone,
          remark: remark || ''
        })
      })

      return { employees, lineErrors }
    },
    downloadImportTemplate() {
      const headers = ['员工工号', '员工姓名', '手机号', '备注']
      const examples = [
        ['E10001', '张三', '13800138000', '一组员工'],
        ['E10002', '李四', '13900139000', '二组员工']
      ]
      const csv = [headers, ...examples]
        .map(row => row.map(col => `\"${String(col).replace(/\"/g, '\"\"')}\"`).join(','))
        .join('\n')
      const blob = new Blob(['\uFEFF' + csv], { type: 'text/csv;charset=utf-8;' })
      const url = window.URL.createObjectURL(blob)
      const link = document.createElement('a')
      link.href = url
      link.download = '员工导入模板.csv'
      document.body.appendChild(link)
      link.click()
      document.body.removeChild(link)
      window.URL.revokeObjectURL(url)
      this.$message.success('模板下载成功')
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
      if (!this.importText.trim()) {
        this.$message.warning('请输入导入内容')
        return
      }

      const { employees, lineErrors } = this.parseImportText()

      if (!employees.length) {
        this.$message.warning('未解析到有效员工数据')
        return
      }
      if (lineErrors.length) {
        this.$alert(lineErrors.join('\n'), '导入内容存在错误', { type: 'warning' })
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
          keyword: this.searchForm.keyword
        })
        if (res.code !== 0) {
          this.$message.error(res.message || '导出失败')
          return
        }

        const headers = ['员工工号', '员工姓名', '手机号', '备注', '创建时间']
        const rows = (Array.isArray(res.data) ? res.data : []).map(item => ([
          item.code || '',
          item.name || '',
          item.phone || '',
          item.remark || '',
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
</style>
