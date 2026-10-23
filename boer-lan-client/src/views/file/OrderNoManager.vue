<template>
  <div class="page-container">
    <div class="search-bar">
      <el-form :inline="true" :model="searchForm">
        <el-form-item label="订单编号">
          <el-input
            v-model.trim="searchForm.keyword"
            clearable
            placeholder="输入订单编号关键字"
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
          <h3>订单编号管理</h3>
          <p>用于统一维护花型文件的订单编号。清空编号时仅移除关联花型的订单编号字段，不删除花型文件。</p>
        </div>
      </div>

      <div class="table-actions flex-between">
        <div class="action-group">
          <el-button type="primary" icon="el-icon-plus" @click="openCreateDialog">
            新增编号
          </el-button>
          <el-button
            icon="el-icon-edit-outline"
            :disabled="selectedRows.length !== 1"
            @click="openRenameDialog(selectedRows[0])"
          >
            修改编号
          </el-button>
          <el-button
            type="danger"
            icon="el-icon-delete"
            :disabled="!selectedRows.length"
            @click="deleteSelected"
          >
            清空编号
          </el-button>
        </div>
        <div>
          <el-button icon="el-icon-refresh" @click="fetchData">刷新</el-button>
        </div>
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
        <el-table-column prop="value" label="订单编号" min-width="220"  show-overflow-tooltip />
        <el-table-column prop="patternCount" label="关联花型数" width="120" align="center"  show-overflow-tooltip ><template slot-scope="scope"><el-button type="text" @click="showRelated(scope.row)">{{ scope.row.patternCount }}</el-button></template></el-table-column>
        <el-table-column prop="updateTime" label="最近更新时间" width="180" align="center"  show-overflow-tooltip />
        <el-table-column label="操作" width="180" align="center">
          <template slot-scope="scope">
            <el-button type="text" size="small" @click="openRenameDialog(scope.row)">
              修改
            </el-button>
            <el-button type="text" size="small" class="danger-text" @click="handleClear(scope.row)">
              清空
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
      :title="renameDialog.mode === 'create' ? '新增订单编号' : '修改订单编号'"
      :visible.sync="renameDialog.visible"
      width="420px"
      @closed="resetRenameDialog"
     :close-on-click-modal="false" :close-on-press-escape="false">
      <el-form label-width="92px">
        <el-form-item v-if="renameDialog.mode !== 'create'" label="原编号">
          <el-input :value="renameDialog.oldValue" disabled />
        </el-form-item>
        <el-form-item required :label="renameDialog.mode === 'create' ? '编号名称' : '新编号'">
          <el-input
            v-model.trim="renameDialog.newValue" maxlength="100" show-word-limit
            placeholder="请输入新的订单编号"
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
import { clearOrderNo, createOrderNo, getOrderSummary, renameOrderNo } from '@/api/pattern'

export default {
  name: 'OrderNoManager',
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
  },
  mounted() {
    this.fetchData()
  },
  methods: {
    async deleteSelected() {
      const rows = this.selectedRows.slice()
      if (!rows.length) return
      try {
        await this.$confirm(`确定清理选中的 ${rows.length} 个条目吗？关联文件和设备会保留。`, this.$t('common.warning'), { type: 'warning' })
        for (const row of rows) {
          const res = await clearOrderNo({ value: row.value })
          if (res.code !== 0) throw new Error(res.message || '删除失败')
        }
        this.$message.success(this.$t('common.success'))
      } catch (error) {
        if (error !== 'cancel' && error !== 'close') this.$message.error(error.message || '删除失败')
      } finally { this.fetchData() }
    },
    showRelated(row) {
      this.$router.push({ path: '/file/pattern', query: { ['orderNo']: row.value } })
    },
    async fetchData() {
      this.catalogPage = 1
      this.loading = true
      try {
        const res = await getOrderSummary({
          keyword: this.searchForm.keyword
        })
        if (res.code === 0) {
          this.tableData = res.data || []
        }
      } catch (error) {
        console.error('Failed to fetch order summary:', error)
        this.$message.error('获取订单编号失败')
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
        this.$message.warning('订单编号不能为空')
        return
      }
      if (newValue.length > 100) { this.$message.warning('名称不能超过100个字符'); return }
      if (newValue !== oldValue && this.tableData.some(row => row.value === newValue)) { this.$message.warning('名称已存在'); return }
      this.submitting = true
      try {
        const res = this.renameDialog.mode === 'create'
          ? await createOrderNo({ value: newValue })
          : await renameOrderNo({ oldValue, newValue })
        if (res.code === 0) {
          this.$message.success(this.renameDialog.mode === 'create' ? '订单编号已新增' : '订单编号已更新')
          this.renameDialog.visible = false
          this.fetchData()
        } else {
          this.$message.error(res.message || (this.renameDialog.mode === 'create' ? '订单编号新增失败' : '订单编号更新失败'))
        }
      } catch (error) {
        console.error('Rename order no failed:', error)
        this.$message.error(this.renameDialog.mode === 'create' ? '订单编号新增失败' : '订单编号更新失败')
      } finally {
        this.submitting = false
      }
    },
    handleClear(row) {
      if (!row?.value) return
      this.$confirm(`确定清空订单编号“${row.value}”吗？该操作不会删除花型文件。`, this.$t('common.warning'), {
        confirmButtonText: this.$t('common.confirm'),
        cancelButtonText: this.$t('common.cancel'),
        type: 'warning'
      }).then(async () => {
        const res = await clearOrderNo({ value: row.value })
        if (res.code === 0) {
          this.$message.success('订单编号已清空')
          this.fetchData()
        } else {
          this.$message.error(res.message || '清空订单编号失败')
        }
      }).catch(() => {})
    }
  }
}
</script>
