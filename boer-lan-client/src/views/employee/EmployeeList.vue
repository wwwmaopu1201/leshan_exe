<template>
  <div class="page-container">
    <div class="search-bar">
      <el-form :inline="true" :model="searchForm">
        <el-form-item :label="$t('employee.employeeName')">
          <el-input
            v-model.trim="searchForm.keyword"
            placeholder="姓名 / 备注"
            clearable
            @keyup.enter.native="handleSearch"
          />
        </el-form-item>
        <el-form-item :label="$t('employee.employeeCode')">
          <el-input
            v-model.trim="searchForm.code"
            placeholder="员工工号"
            clearable
            @keyup.enter.native="handleSearch"
          />
        </el-form-item>
        <el-form-item :label="$t('employee.phone')">
          <el-input
            v-model.trim="searchForm.phone"
            placeholder="手机号"
            clearable
            @keyup.enter.native="handleSearch"
          />
        </el-form-item>
        <el-form-item label="创建时间">
          <el-date-picker
            v-model="searchForm.dateRange"
            type="daterange"
            range-separator="至"
            start-placeholder="开始日期"
            end-placeholder="结束日期"
            value-format="yyyy-MM-dd"
            clearable
          />
        </el-form-item>
        <el-form-item :label="$t('device.group')">
          <el-select
            v-model="searchForm.groupId"
            clearable
            filterable
            placeholder="全部分组"
            popper-class="employee-group-popper"
            :popper-append-to-body="false"
            no-data-text="暂无可选分组"
            style="width: 220px;"
          >
            <el-option
              v-for="item in visibleGroupOptions"
              :key="item.id"
              :label="item.label"
              :value="item.id"
            >
              <span>{{ item.label }}</span>
            </el-option>
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

    <el-card ref="tableCard" shadow="never" class="card page-table-card">
      <div class="section-title">
        <div>
          <h3>员工管理</h3>
          <p>支持按姓名、工号、手机号快速筛选</p>
        </div>
      </div>

      <div class="table-actions flex-between">
        <div class="action-group">
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
        <div class="action-group">
          <el-button icon="el-icon-download" @click="handleExport">
            {{ $t('common.export') }}
          </el-button>
          <el-button class="refresh-circle-button" icon="el-icon-refresh" circle @click="fetchData" />
        </div>
      </div>

      <el-table
        ref="tableRef"
        v-loading="loading"
        :data="pagedTableData"
        border
        :height="tableHeight"
        empty-text="暂无数据"
        @selection-change="handleSelectionChange"
      >
        <el-table-column type="selection" width="50" align="center" />
        <el-table-column prop="code" :label="$t('employee.employeeCode')" width="120" />
        <el-table-column prop="name" :label="$t('employee.employeeName')" width="140" />
        <el-table-column prop="phone" :label="$t('employee.phone')" width="150" />
        <el-table-column prop="groupName" :label="$t('device.group')" width="160" show-overflow-tooltip />
        <el-table-column prop="createTime" :label="$t('common.createTime')" width="170" />
        <el-table-column :label="$t('common.operation')" width="160" align="center">
          <template slot-scope="scope">
            <el-button type="text" size="small" @click="handleEdit(scope.row)">
              {{ $t('common.edit') }}
            </el-button>
            <el-button type="text" size="small" class="danger-text" @click="handleDelete(scope.row)">
              {{ $t('common.delete') }}
            </el-button>
          </template>
        </el-table-column>
        <el-table-column prop="remark" :label="$t('common.remark')" min-width="220" show-overflow-tooltip />
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
    </el-card>

    <el-dialog
      :title="editForm.id ? $t('employee.editEmployee') : $t('employee.addEmployee')"
      :visible.sync="showEditDialog"
      width="520px"
      @close="resetEditForm"
    >
      <el-form ref="editFormRef" :model="editForm" :rules="editRules" label-width="84px">
        <el-form-item :label="$t('employee.employeeCode')" prop="code">
          <el-input v-model="editForm.code" placeholder="请输入11位以内员工工号" :disabled="!!editForm.id" />
        </el-form-item>
        <el-form-item :label="$t('employee.employeeName')" prop="name">
          <el-input v-model="editForm.name" />
        </el-form-item>
        <el-form-item :label="$t('employee.phone')" prop="phone">
          <el-input v-model="editForm.phone" />
        </el-form-item>
        <el-form-item :label="$t('device.group')" prop="groupId">
          <el-select
            v-model="editForm.groupId"
            clearable
            filterable
            placeholder="请选择所属分组"
            popper-class="employee-group-popper"
            :popper-append-to-body="false"
            no-data-text="暂无可选分组"
            style="width: 100%;"
          >
            <el-option
              v-for="item in visibleGroupOptions"
              :key="item.id"
              :label="item.label"
              :value="item.id"
            >
              <span>{{ item.label }}</span>
            </el-option>
          </el-select>
        </el-form-item>
        <el-form-item :label="$t('common.remark')" prop="remark">
          <el-input
            v-model="editForm.remark"
            type="textarea"
            :rows="3"
            :disabled="!!editForm.id"
            placeholder="新增员工时可填写备注"
          />
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
      width="700px"
      @close="resetImportDialog"
    >
      <div class="import-tip">
        每行格式：`员工工号,员工姓名,手机号,备注`（前三列必填）
      </div>
      <div class="import-toolbar" style="margin-bottom: 12px;">
        <span class="import-group-label">导入到分组</span>
        <el-select
          v-model="importGroupId"
          clearable
          filterable
          placeholder="请选择所属分组"
          popper-class="employee-group-popper"
          :popper-append-to-body="false"
          no-data-text="暂无可选分组"
          style="width: 260px;"
        >
          <el-option
            v-for="item in visibleGroupOptions"
            :key="item.id"
            :label="item.label"
            :value="item.id"
          >
            <span>{{ item.label }}</span>
          </el-option>
        </el-select>
      </div>
      <div class="import-toolbar">
        <el-button size="small" icon="el-icon-folder-opened" @click="triggerImportFileSelect">
          选择 CSV 文件
        </el-button>
        <span class="import-file-name">{{ importFileName || '未选择文件' }}</span>
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
        :rows="12"
        placeholder="可直接粘贴CSV内容，或点击上方“选择CSV文件”自动填充&#10;格式：&#10;员工工号,员工姓名,手机号,备注"
      />
      <span slot="footer" class="dialog-footer">
        <el-button @click="showImportDialog = false">{{ $t('common.cancel') }}</el-button>
        <el-button type="primary" :loading="importing" @click="handleImport">开始导入</el-button>
      </span>
    </el-dialog>
  </div>
</template>

<script>
import {
  getEmployeeGroups,
  getEmployeeList,
  createEmployee,
  updateEmployee,
  deleteEmployee,
  importEmployees,
  exportEmployees
} from '@/api/employee'
import { saveTextWithDialog } from '@/utils/file-export'

export default {
  name: 'EmployeeList',
  data() {
    return {
      loading: false,
      tableData: [],
      selectedRows: [],
      groupOptions: [],
      searchForm: {
        keyword: '',
        code: '',
        phone: '',
        dateRange: [],
        groupId: ''
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
      importGroupId: null,
      editForm: {
        id: null,
        code: '',
        name: '',
        phone: '',
        remark: '',
        groupId: null
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
        ],
        groupId: [{ required: true, message: '请选择所属分组', trigger: 'change' }]
      },
      tableHeight: 320
    }
  },
  computed: {
    visibleGroupOptions() {
      return (this.groupOptions || []).filter(item => String(item.label || '').trim())
    },
    pagedTableData() {
      const start = (this.pagination.page - 1) * this.pagination.pageSize
      return this.tableData.slice(start, start + this.pagination.pageSize)
    }
  },
  mounted() {
    this.fetchGroupOptions()
    this.fetchData()
    window.addEventListener('resize', this.syncTableHeight)
  },
  beforeDestroy() {
    window.removeEventListener('resize', this.syncTableHeight)
  },
  methods: {
    async fetchGroupOptions() {
      try {
        const res = await getEmployeeGroups()
        if (res.code === 0) {
          this.groupOptions = this.buildGroupOptions(res.data || [])
          if (!this.importGroupId && this.groupOptions.length === 1) {
            this.importGroupId = this.groupOptions[0].id
          }
        }
      } catch (error) {
        console.error('Failed to fetch employee group options:', error)
      }
    },
    buildGroupOptions(groups) {
      const list = Array.isArray(groups) ? groups : []
      const map = new Map()
      const normalizedList = list.map(item => ({
        id: Number(item.id ?? item.ID ?? 0),
        name: String(item.name || item.Name || '').trim(),
        parentId: item.parentId ?? item.ParentID ?? null
      })).filter(item => item.id > 0)

      normalizedList.forEach(item => {
        map.set(item.id, item)
      })
      const labelCache = new Map()
      const resolveLabel = (groupId) => {
        if (!groupId || !map.has(groupId)) {
          return ''
        }
        if (labelCache.has(groupId)) {
          return labelCache.get(groupId)
        }
        const group = map.get(groupId)
        const currentName = String(group?.name || '').trim() || `分组-${groupId}`
        const parentLabel = group.parentId ? resolveLabel(group.parentId) : ''
        const label = parentLabel ? `${parentLabel} / ${currentName}` : currentName
        labelCache.set(groupId, label)
        return label
      }
      return normalizedList
        .map(item => ({
          id: item.id,
          label: resolveLabel(item.id)
        }))
        .filter(item => String(item.label || '').trim())
    },
    async fetchData() {
      this.loading = true
      try {
        const res = await getEmployeeList({
          keyword: this.searchForm.keyword,
          code: this.searchForm.code,
          phone: this.searchForm.phone,
          startDate: this.searchForm.dateRange?.[0],
          endDate: this.searchForm.dateRange?.[1],
          groupId: this.searchForm.groupId,
          page: 1,
          pageSize: 2000
        })
        if (res.code === 0) {
          const rawList = Array.isArray(res.data) ? res.data : (res.data?.list || [])
          this.tableData = this.applyLocalFilters(rawList)
          this.pagination.total = this.tableData.length
          this.$nextTick(() => {
            this.syncTableHeight()
          })
        }
      } catch (error) {
        console.error('Failed to fetch employees:', error)
        this.$message.error('获取员工列表失败')
      } finally {
        this.loading = false
      }
    },
    applyLocalFilters(list) {
      const keyword = String(this.searchForm.keyword || '').trim().toLowerCase()
      const code = String(this.searchForm.code || '').trim().toLowerCase()
      const phone = String(this.searchForm.phone || '').trim()
      const startDate = this.searchForm.dateRange?.[0] || ''
      const endDate = this.searchForm.dateRange?.[1] || ''
      const groupId = Number(this.searchForm.groupId || 0)
      return list.filter(item => {
        const matchedKeyword = !keyword || [
          item.name,
          item.remark
        ].some(value => String(value || '').toLowerCase().includes(keyword))
        const matchedCode = !code || String(item.code || '').toLowerCase().includes(code)
        const matchedPhone = !phone || String(item.phone || '').includes(phone)
        const createDate = String(item.createTime || '').slice(0, 10)
        const matchedDate = (!startDate || createDate >= startDate) && (!endDate || createDate <= endDate)
        const matchedGroup = !groupId || Number(item.groupId || 0) === groupId
        return matchedKeyword && matchedCode && matchedPhone && matchedDate && matchedGroup
      })
    },
    handleSearch() {
      this.pagination.page = 1
      this.fetchData()
    },
    handleReset() {
      this.searchForm = {
        keyword: '',
        code: '',
        phone: '',
        dateRange: [],
        groupId: ''
      }
      this.handleSearch()
    },
    handleSelectionChange(rows) {
      this.selectedRows = rows
    },
    handleSizeChange(size) {
      this.pagination.pageSize = size
      this.syncTableHeight()
    },
    handlePageChange(page) {
      this.pagination.page = page
    },
    syncTableHeight() {
      this.$nextTick(() => {
        const card = this.$refs.tableCard && this.$refs.tableCard.$el
        if (!card) return
        const body = card.querySelector('.el-card__body')
        const table = this.$refs.tableRef && this.$refs.tableRef.$el
        if (!body || !table) return
        let occupied = 0
        Array.from(body.children).forEach(child => {
          if (child === table) return
          occupied += child.offsetHeight
        })
        this.tableHeight = Math.max(240, body.clientHeight - occupied - 16)
      })
    },
    handleAdd() {
      this.editForm = {
        id: null,
        code: '',
        name: '',
        phone: '',
        remark: '',
        groupId: this.groupOptions.length === 1 ? this.groupOptions[0].id : null
      }
      this.showEditDialog = true
    },
    handleEdit(row) {
      this.editForm = {
        id: row.id,
        code: row.code || '',
        name: row.name || '',
        phone: row.phone || '',
        remark: row.remark || '',
        groupId: row.groupId || null
      }
      this.showEditDialog = true
    },
    resetEditForm() {
      this.$refs.editFormRef?.resetFields()
    },
    resetImportDialog() {
      this.importText = ''
      this.importFileName = ''
      this.importGroupId = this.groupOptions.length === 1 ? this.groupOptions[0].id : null
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
    async downloadImportTemplate() {
      const headers = ['员工工号', '员工姓名', '手机号', '备注']
      const csv = [headers]
        .map(row => row.map(col => `"${String(col).replace(/"/g, '""')}"`).join(','))
        .join('\n')
      const content = '\uFEFF' + csv
      const filename = '员工导入模板.csv'
      const saved = await saveTextWithDialog(content, filename, {
        mimeType: 'text/csv;charset=utf-8;',
        description: 'CSV 文件',
        extensions: ['csv']
      })
      if (saved === null) {
        return
      }
      this.$message.success(saved ? '模板已保存' : '模板下载成功')
    },
    async handleSave() {
      try {
        await this.$refs.editFormRef.validate()
        let res
        if (this.editForm.id) {
          const updatePayload = {
            name: this.editForm.name,
            phone: this.editForm.phone,
            groupId: this.editForm.groupId
          }
          res = await updateEmployee(this.editForm.id, updatePayload)
        } else {
          const createPayload = {
            code: this.editForm.code,
            name: this.editForm.name,
            phone: this.editForm.phone,
            remark: this.editForm.remark || '',
            groupId: this.editForm.groupId
          }
          res = await createEmployee(createPayload)
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
      if (!this.importGroupId) {
        this.$message.warning('请选择导入所属分组')
        return
      }
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
        const res = await importEmployees(employees, this.importGroupId)
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
          keyword: this.searchForm.keyword,
          code: this.searchForm.code,
          phone: this.searchForm.phone,
          startDate: this.searchForm.dateRange?.[0],
          endDate: this.searchForm.dateRange?.[1],
          groupId: this.searchForm.groupId
        })
        if (res.code !== 0) {
          this.$message.error(res.message || '导出失败')
          return
        }

        const headers = ['员工工号', '员工姓名', '手机号', '所属分组', '备注', '创建时间']
        const rows = (Array.isArray(res.data) ? res.data : [])
          .filter(item => this.applyLocalFilters([item]).length > 0)
          .map(item => ([
            item.code || '',
            item.name || '',
            item.phone || '',
            item.groupName || '',
            item.remark || '',
            item.createTime || ''
          ]))
        const csv = [headers, ...rows]
          .map(row => row.map(col => `"${String(col).replace(/"/g, '""')}"`).join(','))
          .join('\n')

        const content = '\uFEFF' + csv
        const filename = `employees_${Date.now()}.csv`
        const saved = await saveTextWithDialog(content, filename, {
          mimeType: 'text/csv;charset=utf-8;',
          description: 'CSV 文件',
          extensions: ['csv']
        })
        if (saved === null) {
          return
        }
        this.$message.success(saved ? '导出文件已保存' : '导出成功')
      } catch (error) {
        console.error('Export employees failed:', error)
        this.$message.error('导出失败')
      }
    }
  }
}
</script>

<style lang="scss" scoped>
.action-group {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 10px;
}

.refresh-circle-button {
  width: 24px;
  height: 24px;
  padding: 0 !important;
  border-radius: 50% !important;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  line-height: 1;
}

.import-tip {
  margin-bottom: 10px;
  color: #677c9c;
}

.import-toolbar {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 10px;
  margin-bottom: 14px;
}

.import-file-name {
  color: #8b99ad;
}

.danger-text {
  color: #ef5a5a !important;
}

@media (max-width: 640px) {
  .import-toolbar {
    align-items: stretch;
    flex-direction: column;
  }

  .import-toolbar .el-select,
  .import-toolbar .el-button {
    width: 100% !important;
  }
}
</style>
<style lang="scss">
.employee-group-popper {
  .el-select-dropdown__item {
    color: #303133 !important;
    background: #ffffff !important;
  }

  .el-select-dropdown__item.hover,
  .el-select-dropdown__item:hover {
    color: #303133 !important;
    background: #f5f7fa !important;
  }

  .el-select-dropdown__item.selected {
    color: #409eff !important;
    font-weight: 600;
  }
}
</style>
