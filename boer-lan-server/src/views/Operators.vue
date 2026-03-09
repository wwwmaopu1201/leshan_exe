<template>
  <div>
    <div class="page-title">操作员管理</div>
    <el-card>
      <el-form :inline="true" :model="searchForm" style="margin-bottom: 12px;">
        <el-form-item label="账号/操作员姓名">
          <el-input
            v-model="searchForm.keyword"
            clearable
            placeholder="输入账号或操作员姓名"
            @keyup.enter.native="handleSearch"
          />
        </el-form-item>
        <el-form-item label="创建时间">
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

      <div style="margin-bottom: 16px; display: flex; gap: 8px;">
        <el-button type="primary" icon="el-icon-plus" @click="openCreateDialog">新建操作员</el-button>
        <el-button icon="el-icon-share" :disabled="selectedOperatorIds.length === 0" @click="openMoveDialog">
          批量移动分组
        </el-button>
        <el-button icon="el-icon-upload2" @click="importDialogVisible = true">批量导入</el-button>
        <el-button icon="el-icon-download" @click="exportOperators">导出</el-button>
        <el-button icon="el-icon-refresh" @click="loadData">刷新</el-button>
      </div>

      <el-table
        ref="operatorTableRef"
        :data="operators"
        v-loading="loading"
        style="width: 100%;"
        @selection-change="handleSelectionChange"
      >
        <el-table-column type="selection" width="48" />
        <el-table-column prop="username" label="账号" min-width="140" />
        <el-table-column prop="nickname" label="操作员姓名" min-width="130" />
        <el-table-column prop="createTime" label="创建时间" width="170" />
        <el-table-column label="所属分组" min-width="180">
          <template slot-scope="{ row }">
            {{ formatGroupName(row) }}
          </template>
        </el-table-column>
        <el-table-column prop="disabled" label="状态" width="100">
          <template slot-scope="{ row }">
            <el-tag :type="row.disabled ? 'danger' : 'success'">
              {{ row.disabled ? '限制登录' : '正常' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="180">
          <template slot-scope="{ row }">
            <el-button size="small" @click="openEditDialog(row)" icon="el-icon-edit">编辑</el-button>
            <el-button size="small" type="danger" @click="deleteOperator(row)" icon="el-icon-delete">删除</el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <el-dialog
      :title="form.id ? '编辑操作员' : '新建操作员'"
      :visible.sync="dialogVisible"
      width="560px"
      @close="resetForm"
    >
      <el-form ref="operatorFormRef" :model="form" :rules="rules" label-width="110px">
        <el-form-item label="账号" prop="username">
          <el-input v-model="form.username" :disabled="!!form.id" placeholder="请输入账号" />
        </el-form-item>

        <el-form-item :label="form.id ? '新密码(可选)' : '密码'" :prop="form.id ? '' : 'password'">
          <el-input
            v-model="form.password"
            type="password"
            show-password
            :placeholder="form.id ? '不填写则不修改密码' : '请输入密码'"
          />
        </el-form-item>

        <el-form-item label="操作员姓名" prop="nickname">
          <el-input v-model="form.nickname" placeholder="请输入操作员姓名" />
        </el-form-item>

        <el-form-item label="所属分组">
          <el-select v-model="form.groupId" style="width: 100%;" clearable placeholder="可不选">
            <el-option
              v-for="item in groups"
              :key="item.id"
              :label="item.parent ? `${item.parent.name} / ${item.name}` : item.name"
              :value="item.id"
            />
          </el-select>
        </el-form-item>

        <el-form-item label="限制登录">
          <el-switch
            v-model="form.disabled"
            active-text="限制"
            inactive-text="允许"
          />
        </el-form-item>
      </el-form>

      <span slot="footer" class="dialog-footer">
        <el-button @click="dialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="saveOperator">保存</el-button>
      </span>
    </el-dialog>

    <el-dialog
      title="批量导入操作员"
      :visible.sync="importDialogVisible"
      width="640px"
    >
      <div style="margin-bottom: 8px; color: #606266;">
        每行一个操作员，格式：`账号,密码,操作员姓名,分组ID(可选)`
      </div>
      <el-input
        v-model="importText"
        type="textarea"
        :rows="10"
        placeholder="例如：\noperator01,123456,张三,2\noperator02,123456,李四"
      />
      <span slot="footer" class="dialog-footer">
        <el-button @click="importDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="importing" @click="importOperators">开始导入</el-button>
      </span>
    </el-dialog>

    <el-dialog
      title="批量移动操作员分组"
      :visible.sync="moveDialogVisible"
      width="420px"
    >
      <el-form label-width="90px">
        <el-form-item label="目标分组" required>
          <el-select v-model="moveTargetGroupId" style="width: 100%;" placeholder="请选择分组">
            <el-option
              v-for="item in groups"
              :key="item.id"
              :label="item.parent ? `${item.parent.name} / ${item.name}` : item.name"
              :value="item.id"
            />
          </el-select>
        </el-form-item>
        <div style="color: #606266;">已选择 {{ selectedOperatorIds.length }} 个操作员</div>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="moveDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="moving" @click="confirmMoveOperators">确定移动</el-button>
      </span>
    </el-dialog>
  </div>
</template>

<script>
export default {
  name: 'Operators',
  data() {
    return {
      loading: false,
      saving: false,
      moving: false,
      importing: false,
      operators: [],
      groups: [],
      selectedOperatorIds: [],
      searchForm: {
        keyword: '',
        dateRange: []
      },
      dialogVisible: false,
      importDialogVisible: false,
      moveDialogVisible: false,
      moveTargetGroupId: null,
      importText: '',
      form: {
        id: null,
        username: '',
        password: '',
        nickname: '',
        groupId: null,
        disabled: false
      },
      rules: {
        username: [
          { required: true, message: '请输入账号', trigger: 'blur' },
          { pattern: /^[a-zA-Z0-9_]+$/, message: '账号仅支持字母数字下划线', trigger: 'blur' },
          { max: 11, message: '账号不能超过11位', trigger: 'blur' }
        ],
        password: [
          { required: true, message: '请输入密码', trigger: 'blur' },
          { min: 6, max: 32, message: '密码长度需在6-32位', trigger: 'blur' }
        ],
        nickname: [{ required: true, message: '请输入操作员姓名', trigger: 'blur' }]
      }
    }
  },
  mounted() {
    this.loadData()
  },
  methods: {
    async loadData() {
      this.loading = true
      try {
        const params = {
          keyword: this.searchForm.keyword,
          startDate: this.searchForm.dateRange?.[0],
          endDate: this.searchForm.dateRange?.[1]
        }
        const [operatorsRes, groupsRes] = await Promise.all([
          this.$axios.get('/operator/all', { params }),
          this.$axios.get('/group/list')
        ])
        if (operatorsRes.code === 0) {
          this.operators = (Array.isArray(operatorsRes.data) ? operatorsRes.data : []).map(item => ({
            ...item,
            id: item.id || item.ID,
            createTime: item.createTime || item.createdAt || item.CreatedAt || ''
          }))
        }
        if (groupsRes.code === 0) {
          this.groups = Array.isArray(groupsRes.data) ? groupsRes.data : []
        }
        this.selectedOperatorIds = []
        this.$nextTick(() => {
          this.$refs.operatorTableRef?.clearSelection()
        })
      } catch (error) {
        console.error('加载操作员失败', error)
      } finally {
        this.loading = false
      }
    },
    handleSelectionChange(rows) {
      this.selectedOperatorIds = rows.map(item => item.id || item.ID).filter(Boolean)
    },
    openMoveDialog() {
      this.moveTargetGroupId = null
      this.moveDialogVisible = true
    },
    async confirmMoveOperators() {
      if (!this.moveTargetGroupId) {
        this.$message.warning('请选择目标分组')
        return
      }
      if (!this.selectedOperatorIds.length) {
        this.$message.warning('请先选择操作员')
        return
      }
      this.moving = true
      try {
        const res = await this.$axios.post('/operator/move', {
          operatorIds: this.selectedOperatorIds,
          groupId: this.moveTargetGroupId
        })
        if (res.code === 0) {
          this.$message.success('移动成功')
          this.moveDialogVisible = false
          await this.loadData()
        }
      } catch (error) {
        console.error('移动操作员分组失败', error)
      } finally {
        this.moving = false
      }
    },
    handleSearch() {
      this.loadData()
    },
    handleReset() {
      this.searchForm = {
        keyword: '',
        dateRange: []
      }
      this.loadData()
    },
    formatGroupName(row) {
      if (!row.group) return '-'
      if (row.group.parent) {
        return `${row.group.parent.name} / ${row.group.name}`
      }
      return row.group.name
    },
    openCreateDialog() {
      this.dialogVisible = true
      this.form = {
        id: null,
        username: '',
        password: '',
        nickname: '',
        groupId: null,
        disabled: false
      }
    },
    openEditDialog(row) {
      this.dialogVisible = true
      this.form = {
        id: row.id || row.ID,
        username: row.username,
        password: '',
        nickname: row.nickname || '',
        groupId: row.groupId || null,
        disabled: !!row.disabled
      }
    },
    resetForm() {
      this.$refs.operatorFormRef?.resetFields()
    },
    async saveOperator() {
      try {
        await this.$refs.operatorFormRef.validate()
        this.saving = true

        const payload = {
          username: this.form.username,
          nickname: this.form.nickname,
          groupId: this.form.groupId,
          disabled: this.form.disabled
        }

        if (this.form.id) {
          if (this.form.password) {
            payload.password = this.form.password
          }
          const res = await this.$axios.put(`/operator/${this.form.id}`, payload)
          if (res.code === 0) {
            this.$message.success('更新成功')
            this.dialogVisible = false
            await this.loadData()
          }
          return
        }

        payload.password = this.form.password
        const res = await this.$axios.post('/operator', payload)
        if (res.code === 0) {
          this.$message.success('创建成功')
          this.dialogVisible = false
          await this.loadData()
        }
      } catch (error) {
        console.error('保存操作员失败', error)
      } finally {
        this.saving = false
      }
    },
    async deleteOperator(row) {
      try {
        await this.$confirm(`确定要删除操作员「${row.username}」吗?`, '警告', {
          type: 'warning'
        })
        const res = await this.$axios.delete('/operator', {
          data: { ids: [row.id || row.ID] }
        })
        if (res.code === 0) {
          this.$message.success('删除成功')
          await this.loadData()
        }
      } catch (error) {
        if (error !== 'cancel') {
          console.error('删除操作员失败', error)
        }
      }
    },
    async importOperators() {
      const lines = this.importText
        .split('\n')
        .map(line => line.trim())
        .filter(Boolean)

      if (!lines.length) {
        this.$message.warning('请输入导入内容')
        return
      }

      const operators = []
      for (const line of lines) {
        const [username, password, nickname, groupIdRaw] = line.split(',').map(part => part?.trim())
        if (!username || !password) continue
        const payload = { username, password, nickname: nickname || username }
        const groupId = Number(groupIdRaw)
        if (groupId) payload.groupId = groupId
        operators.push(payload)
      }

      if (!operators.length) {
        this.$message.warning('未解析到有效导入数据')
        return
      }

      try {
        this.importing = true
        const res = await this.$axios.post('/operator/import', { operators })
        if (res.code === 0) {
          const successCount = res.data?.successCount || 0
          const errors = res.data?.errors || []
          this.$message.success(`导入完成，成功 ${successCount} 条`)
          if (errors.length) {
            this.$alert(errors.join('\n'), '导入失败明细', { type: 'warning' })
          }
          this.importDialogVisible = false
          this.importText = ''
          await this.loadData()
        }
      } catch (error) {
        console.error('导入操作员失败', error)
      } finally {
        this.importing = false
      }
    },
    async exportOperators() {
      try {
        const res = await this.$axios.get('/operator/export', {
          params: {
            keyword: this.searchForm.keyword,
            startDate: this.searchForm.dateRange?.[0],
            endDate: this.searchForm.dateRange?.[1]
          }
        })
        if (res.code !== 0) return

        const headers = ['账号', '操作员姓名', '状态', '分组ID', '分组名称', '创建时间']
        const rows = (Array.isArray(res.data) ? res.data : []).map(item => ([
          item.username || '',
          item.nickname || '',
          item.disabled ? '限制登录' : '正常',
          item.groupId || '',
          item.groupName || '',
          item.createTime || ''
        ]))
        const csv = [headers, ...rows]
          .map(row => row.map(col => `"${String(col).replace(/"/g, '""')}"`).join(','))
          .join('\n')

        const blob = new Blob(['\uFEFF' + csv], { type: 'text/csv;charset=utf-8;' })
        const url = window.URL.createObjectURL(blob)
        const link = document.createElement('a')
        link.href = url
        link.download = `operators_${Date.now()}.csv`
        document.body.appendChild(link)
        link.click()
        document.body.removeChild(link)
        window.URL.revokeObjectURL(url)
      } catch (error) {
        console.error('导出操作员失败', error)
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
</style>
