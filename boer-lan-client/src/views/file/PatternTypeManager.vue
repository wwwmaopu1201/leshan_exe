<template>
  <div class="page-container">
    <div class="search-bar">
      <el-form :inline="true" :model="searchForm">
        <el-form-item label="花型类型">
          <el-input
            v-model.trim="searchForm.keyword"
            clearable
            placeholder="输入花型类型关键字"
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
          <h3>花型类型管理</h3>
          <p>用于统一维护花型类型名称。清空类型时仅移除关联花型的类型字段，不删除花型文件。</p>
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
            :disabled="selectedRows.length !== 1"
            @click="handleClear(selectedRows[0])"
          >
            清空类型
          </el-button>
        </div>
        <div>
          <el-button icon="el-icon-refresh" @click="fetchData">刷新</el-button>
        </div>
      </div>

      <el-table
        v-loading="loading"
        :data="tableData"
        border
        empty-text="暂无数据"
        @selection-change="handleSelectionChange"
      >
        <el-table-column type="selection" width="48" align="center" />
        <el-table-column type="index" label="序号" width="60" align="center" />
        <el-table-column prop="value" label="花型类型" min-width="220" />
        <el-table-column prop="patternCount" label="关联花型数" width="120" align="center" />
        <el-table-column prop="updateTime" label="最近更新时间" width="180" align="center" />
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
    </el-card>

    <el-dialog
      :title="renameDialog.mode === 'create' ? '新增花型类型' : '修改花型类型'"
      :visible.sync="renameDialog.visible"
      width="420px"
      @closed="resetRenameDialog"
    >
      <el-form label-width="92px">
        <el-form-item v-if="renameDialog.mode !== 'create'" label="原类型">
          <el-input :value="renameDialog.oldValue" disabled />
        </el-form-item>
        <el-form-item :label="renameDialog.mode === 'create' ? '类型名称' : '新类型'">
          <el-input
            v-model.trim="renameDialog.newValue"
            placeholder="请输入新的花型类型"
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
import { clearPatternType, createPatternType, getPatternTypeSummary, renamePatternType } from '@/api/pattern'

export default {
  name: 'PatternTypeManager',
  data() {
    return {
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
  mounted() {
    this.fetchData()
  },
  methods: {
    async fetchData() {
      this.loading = true
      try {
        const res = await getPatternTypeSummary({
          keyword: this.searchForm.keyword
        })
        if (res.code === 0) {
          this.tableData = res.data || []
        }
      } catch (error) {
        console.error('Failed to fetch pattern type summary:', error)
        this.$message.error('获取花型类型失败')
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
      const newValue = String(this.renameDialog.newValue || '').trim()
      const oldValue = String(this.renameDialog.oldValue || '').trim()
      if (!newValue) {
        this.$message.warning('花型类型不能为空')
        return
      }
      this.submitting = true
      try {
        const res = this.renameDialog.mode === 'create'
          ? await createPatternType({ value: newValue })
          : await renamePatternType({ oldValue, newValue })
        if (res.code === 0) {
          this.$message.success(this.renameDialog.mode === 'create' ? '花型类型已新增' : '花型类型已更新')
          this.renameDialog.visible = false
          this.fetchData()
        } else {
          this.$message.error(res.message || (this.renameDialog.mode === 'create' ? '花型类型新增失败' : '花型类型更新失败'))
        }
      } catch (error) {
        console.error('Rename pattern type failed:', error)
        this.$message.error(this.renameDialog.mode === 'create' ? '花型类型新增失败' : '花型类型更新失败')
      } finally {
        this.submitting = false
      }
    },
    handleClear(row) {
      if (!row?.value) return
      this.$confirm(`确定清空花型类型“${row.value}”吗？该操作不会删除花型文件。`, this.$t('common.warning'), {
        confirmButtonText: this.$t('common.confirm'),
        cancelButtonText: this.$t('common.cancel'),
        type: 'warning'
      }).then(async () => {
        const res = await clearPatternType({ value: row.value })
        if (res.code === 0) {
          this.$message.success('花型类型已清空')
          this.fetchData()
        } else {
          this.$message.error(res.message || '清空花型类型失败')
        }
      }).catch(() => {})
    }
  }
}
</script>
