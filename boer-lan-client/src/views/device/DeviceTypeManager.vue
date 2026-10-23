<template>
  <div class="page-container">
    <div class="search-bar">
      <el-form :inline="true" :model="searchForm">
        <el-form-item :label="catalogLabel">
          <el-input
            v-model.trim="searchForm.keyword"
            clearable
            :placeholder="catalogKeywordPlaceholder"
            @keyup.enter.native="fetchData"
          />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" icon="el-icon-search" @click="fetchData">
            {{ $t('common.search') }}
          </el-button>
          <el-button icon="el-icon-refresh" @click="resetSearch">
            {{ $t('common.reset') }}
          </el-button>
        </el-form-item>
      </el-form>
    </div>

    <el-card shadow="never" class="card page-table-card">
      <div class="section-title">
        <div>
          <h3>{{ catalogLabel }}</h3>
          <p>{{ catalogDescription }}</p>
        </div>
      </div>

      <div class="table-actions flex-between">
        <div class="action-group">
          <el-button type="primary" icon="el-icon-plus" @click="openCreateDialog">
            新增类型
          </el-button>
          <el-button
            icon="el-icon-edit-outline"
            :disabled="selectedRows.length !== 1"
            @click="openRenameDialog(selectedRows[0])"
          >
            修改名称
          </el-button>
          <el-button
            type="danger"
            icon="el-icon-delete"
            :disabled="!selectedRows.length"
            @click="deleteSelected"
          >
            删除类型
          </el-button>
        </div>
        <el-button icon="el-icon-refresh" @click="fetchData">刷新</el-button>
      </div>

      <el-table
        v-loading="loading"
        :data="pagedRows"
        border
        empty-text="暂无数据"
        @selection-change="handleSelectionChange"
      >
        <el-table-column type="selection" width="48" align="center" />
        <el-table-column type="index" label="序号" width="60" align="center" />
        <el-table-column prop="value" :label="catalogLabel" min-width="220"  show-overflow-tooltip />
        <el-table-column prop="deviceCount" label="关联设备数" width="120" align="center"  show-overflow-tooltip ><template slot-scope="scope"><el-button type="text" @click="showRelated(scope.row)">{{ scope.row.deviceCount }}</el-button></template></el-table-column>
        <el-table-column prop="updateTime" label="最近更新时间" width="180" align="center"  show-overflow-tooltip />
        <el-table-column label="操作" width="180" align="center">
          <template slot-scope="scope">
            <el-button
              type="text"
              size="small"
              @click="openRenameDialog(scope.row)"
            >
              修改
            </el-button>
            <el-button
              type="text"
              size="small"
              class="danger-text"
              @click="handleDelete(scope.row)"
            >
              删除
            </el-button>
          </template>
        </el-table-column>
      </el-table>
      <el-pagination :current-page.sync="catalogPage" :page-size="catalogPageSize" :total="filteredRows.length"
        :page-sizes="[10, 20, 50, 100]" layout="total, sizes, prev, pager, next"
        @size-change="size => { catalogPageSize = size; catalogPage = 1 }" />
    </el-card>

    <el-dialog
      append-to-body
      :title="renameDialogTitle"
      :visible.sync="renameDialog.visible"
      width="420px"
      @closed="resetRenameDialog"
     :close-on-click-modal="false" :close-on-press-escape="false">
      <el-form label-width="92px">
        <el-form-item v-if="renameDialog.mode !== 'create'" label="原类型">
          <el-input :value="renameDialog.oldValue" disabled />
        </el-form-item>
        <el-form-item required :label="renameDialog.mode === 'create' ? '类型名称' : '新类型'">
          <el-input
            v-model.trim="renameDialog.newValue" :maxlength="currentCatalog.maxLength" show-word-limit
            :placeholder="catalogInputPlaceholder"
            @keyup.enter.native="submitRename"
          />
        </el-form-item>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="renameDialog.visible = false">{{ $t('common.cancel') }}</el-button>
        <el-button type="primary" :loading="submitting" @click="submitRename">
          {{ $t('common.save') }}
        </el-button>
      </span>
    </el-dialog>
  </div>
</template>

<script>
import {
  createElectricControlType,
  createDeviceType,
  deleteElectricControlType,
  deleteDeviceType,
  getElectricControlTypeSummary,
  getDeviceTypeSummary,
  renameElectricControlType,
  renameDeviceType
} from '@/api/device'

export default {
  name: 'DeviceTypeManager',
  data() {
    return {
      catalogPage: 1,
      catalogPageSize: 20,
      loading: false,
      submitting: false,
      searchForm: {
        keyword: ''
      },
      tableData: [],
      selectedRows: [],
      renameDialog: {
        visible: false,
        mode: 'rename',
        oldValue: '',
        newValue: ''
      }
    }
  },
  computed: {
    filteredRows() {
      const keyword = this.searchForm.keyword.trim().toLowerCase()
      return this.tableData.filter(row => String(row.value).toLowerCase().includes(keyword))
        .slice().sort((a, b) => String(b.updateTime).localeCompare(String(a.updateTime)) || String(a.value).localeCompare(String(b.value)))
    },
    pagedRows() {
      const page = Math.min(this.catalogPage, Math.max(1, Math.ceil(this.filteredRows.length / this.catalogPageSize)))
      return this.filteredRows.slice((page - 1) * this.catalogPageSize, page * this.catalogPageSize)
    },

    currentCatalog() {
      if (this.$route.meta?.catalog === 'electricControl') {
        return {
          label: '电控类型',
          maxLength: 100,
          labelEn: 'Electric Control Type',
          fetchSummary: getElectricControlTypeSummary,
          create: createElectricControlType,
          rename: renameElectricControlType,
          delete: deleteElectricControlType
        }
      }
      return {
        label: '设备类型',
        maxLength: 50,
        labelEn: 'Device Type',
        fetchSummary: getDeviceTypeSummary,
        create: createDeviceType,
        rename: renameDeviceType,
        delete: deleteDeviceType
      }
    },
    isEnglish() {
      return this.$i18n?.locale === 'en-US' || localStorage.getItem('language') === 'en-US'
    },
    catalogLabel() {
      return this.isEnglish ? this.currentCatalog.labelEn : this.currentCatalog.label
    },
    catalogDescription() {
      if (this.isEnglish) {
        return `Manage ${this.catalogLabel}. Deleting a type clears the field on related devices.`
      }
      return `用于统一维护${this.currentCatalog.label}。删除类型时，关联设备会清空对应字段。`
    },
    catalogKeywordPlaceholder() {
      return this.isEnglish
        ? `Enter ${this.catalogLabel} keyword`
        : `输入${this.currentCatalog.label}关键字`
    },
    catalogInputPlaceholder() {
      return this.isEnglish
        ? `Please enter ${this.catalogLabel}`
        : `请输入${this.currentCatalog.label}`
    },
    renameDialogTitle() {
      if (this.isEnglish) {
        return `${this.renameDialog.mode === 'create' ? 'Add' : 'Edit'} ${this.catalogLabel}`
      }
      return this.renameDialog.mode === 'create'
        ? `新增${this.currentCatalog.label}`
        : `修改${this.currentCatalog.label}`
    }
  },
  mounted() {
    this.fetchData()
  },
  watch: {
    '$route.name'() {
      this.selectedRows = []
      this.searchForm.keyword = ''
      this.fetchData()
    }
  },
  methods: {
    async deleteSelected() {
      const rows = this.selectedRows.slice()
      if (!rows.length) return
      try {
        await this.$confirm(`确定清理选中的 ${rows.length} 个条目吗？关联文件和设备会保留。`, this.$t('common.warning'), { type: 'warning' })
        for (const row of rows) {
          const res = await this.currentCatalog.delete({ value: row.value })
          if (res.code !== 0) throw new Error(res.message || '删除失败')
        }
        this.$message.success(this.$t('common.success'))
      } catch (error) {
        if (error !== 'cancel' && error !== 'close') this.$message.error(error.message || '删除失败')
      } finally { this.fetchData() }
    },
    showRelated(row) {
      this.$router.push({ path: '/device/list', query: { [(this.$route.meta?.catalog === 'electricControl' ? 'electricControlType' : 'type')]: row.value } })
    },
    async fetchData() {
      this.catalogPage = 1
      this.loading = true
      try {
        const res = await this.currentCatalog.fetchSummary({
          keyword: this.searchForm.keyword
        })
        if (res.code === 0) {
          this.tableData = res.data || []
        }
      } catch (error) {
        console.error('Failed to fetch type summary:', error)
        this.$message.error(this.isEnglish ? `Failed to get ${this.catalogLabel}` : `获取${this.currentCatalog.label}失败`)
      } finally {
        this.loading = false
      }
    },
    resetSearch() {
      this.searchForm.keyword = ''
      this.fetchData()
    },
    handleSelectionChange(rows) {
      this.selectedRows = rows || []
    },
    openCreateDialog() {
      this.renameDialog = {
        visible: true,
        mode: 'create',
        oldValue: '',
        newValue: ''
      }
    },
    openRenameDialog(row) {
      if (!row) return
      this.renameDialog = {
        visible: true,
        mode: 'rename',
        oldValue: row.value,
        newValue: row.value
      }
    },
    resetRenameDialog() {
      this.submitting = false
      this.renameDialog = {
        visible: false,
        mode: 'rename',
        oldValue: '',
        newValue: ''
      }
    },
    async submitRename() {
      if (this.submitting) return
      const newValue = String(this.renameDialog.newValue || '').trim()
      const oldValue = String(this.renameDialog.oldValue || '').trim()
      if (!newValue) {
        this.$message.warning(this.isEnglish ? `${this.catalogLabel} is required` : `${this.currentCatalog.label}不能为空`)
        return
      }
      if ([...newValue].length > this.currentCatalog.maxLength) { this.$message.warning(`名称不能超过${this.currentCatalog.maxLength}个字符`); return }
      if (newValue !== oldValue && this.tableData.some(row => row.value === newValue)) { this.$message.warning('名称已存在'); return }
      this.submitting = true
      try {
        const res = this.renameDialog.mode === 'create'
          ? await this.currentCatalog.create({ value: newValue })
          : await this.currentCatalog.rename({ oldValue, newValue })
        if (res.code === 0) {
          this.$message.success(this.renameDialog.mode === 'create'
            ? (this.isEnglish ? `${this.catalogLabel} added` : `${this.currentCatalog.label}已新增`)
            : (this.isEnglish ? `${this.catalogLabel} updated` : `${this.currentCatalog.label}已更新`))
          this.renameDialog.visible = false
          this.fetchData()
        } else {
          this.$message.error(res.message || (this.renameDialog.mode === 'create'
            ? (this.isEnglish ? `Failed to add ${this.catalogLabel}` : `${this.currentCatalog.label}新增失败`)
            : (this.isEnglish ? `Failed to update ${this.catalogLabel}` : `${this.currentCatalog.label}更新失败`)))
        }
      } catch (error) {
        console.error('Save type failed:', error)
        this.$message.error(this.renameDialog.mode === 'create'
          ? (this.isEnglish ? `Failed to add ${this.catalogLabel}` : `${this.currentCatalog.label}新增失败`)
          : (this.isEnglish ? `Failed to update ${this.catalogLabel}` : `${this.currentCatalog.label}更新失败`))
      } finally {
        this.submitting = false
      }
    },
    handleDelete(row) {
      if (!row?.value) return
      const confirmMessage = this.isEnglish
        ? `Delete ${this.catalogLabel} "${row.value}"? Related devices will clear this field.`
        : `确定删除${this.currentCatalog.label}“${row.value}”吗？关联设备会清空对应字段。`
      this.$confirm(confirmMessage, this.$t('common.warning'), {
        confirmButtonText: this.$t('common.confirm'),
        cancelButtonText: this.$t('common.cancel'),
        type: 'warning'
      }).then(async () => {
        const res = await this.currentCatalog.delete({ value: row.value })
        if (res.code === 0) {
          this.$message.success(this.isEnglish ? `${this.catalogLabel} deleted` : `${this.currentCatalog.label}已删除`)
          this.fetchData()
        } else {
          this.$message.error(res.message || (this.isEnglish ? `Failed to delete ${this.catalogLabel}` : `删除${this.currentCatalog.label}失败`))
        }
      }).catch(() => {})
    }
  }
}
</script>
