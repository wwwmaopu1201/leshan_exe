<template>
  <div>
    <div class="page-title">权限角色</div>
    <el-card>
      <el-form :inline="true" :model="searchForm" style="margin-bottom: 12px;">
        <el-form-item label="角色名称">
          <el-input
            v-model="searchForm.keyword"
            clearable
            placeholder="输入角色名称或备注"
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
        <el-button type="primary" icon="el-icon-plus" @click="openCreateDialog">新增角色</el-button>
        <el-button icon="el-icon-refresh" @click="loadData">刷新</el-button>
      </div>

      <el-table :data="roles" v-loading="loading" style="width: 100%;">
        <el-table-column prop="name" label="角色名称" min-width="120" />
        <el-table-column prop="remark" label="备注" min-width="180" show-overflow-tooltip />
        <el-table-column prop="createTime" label="创建时间" width="170" />
        <el-table-column label="父子联动" width="100">
          <template slot-scope="{ row }">
            <el-tag :type="row.parentChildLink ? 'success' : 'info'" size="mini">
              {{ row.parentChildLink ? '开启' : '关闭' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="权限模块" min-width="260">
          <template slot-scope="{ row }">
            <el-tag
              v-for="item in getPermissionTags(row.permissions)"
              :key="`${row.id}-${item}`"
              size="mini"
              style="margin-right: 4px; margin-bottom: 4px;"
            >
              {{ item }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="180">
          <template slot-scope="{ row }">
            <el-button size="small" icon="el-icon-edit" @click="openEditDialog(row)">编辑</el-button>
            <el-button size="small" type="danger" icon="el-icon-delete" @click="deleteRole(row)">删除</el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <el-dialog
      :title="form.id ? '编辑角色' : '新增角色'"
      :visible.sync="dialogVisible"
      width="720px"
      @close="resetForm"
    >
      <el-form ref="roleFormRef" :model="form" :rules="rules" label-width="110px">
        <el-form-item label="角色名称" prop="name">
          <el-input v-model="form.name" maxlength="10" show-word-limit placeholder="不超过10个字" />
        </el-form-item>

        <el-form-item label="备注">
          <el-input v-model="form.remark" type="textarea" :rows="2" placeholder="可选" />
        </el-form-item>

        <el-form-item label="父子联动">
          <el-switch
            v-model="form.parentChildLink"
            active-text="开启"
            inactive-text="关闭"
            @change="handleLinkModeChange"
          />
        </el-form-item>

        <el-form-item label="菜单权限" required>
          <div style="margin-bottom: 8px;">
            <el-button size="mini" @click="checkAllPermissions">全选</el-button>
            <el-button size="mini" @click="clearAllPermissions">清空</el-button>
          </div>
          <div class="permission-tree-wrap">
            <el-tree
              ref="permissionTreeRef"
              :data="permissionTree"
              node-key="id"
              show-checkbox
              default-expand-all
              :check-strictly="!form.parentChildLink"
              :props="{ label: 'label', children: 'children' }"
            />
          </div>
        </el-form-item>
      </el-form>

      <span slot="footer" class="dialog-footer">
        <el-button @click="dialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="saveRole">保存</el-button>
      </span>
    </el-dialog>
  </div>
</template>

<script>
const DEFAULT_ROLE_PERMISSIONS = [
  'home',
  'dashboard',
  'deviceManagement',
  'remoteMonitoring',
  'fileManagement',
  'statistics',
  'employeeManagement'
]

export default {
  name: 'Roles',
  data() {
    return {
      loading: false,
      saving: false,
      roles: [],
      searchForm: {
        keyword: '',
        dateRange: []
      },
      dialogVisible: false,
      permissionTree: [
        {
          id: 'home',
          label: '首页'
        },
        {
          id: 'dashboard',
          label: '数据看板'
        },
        {
          id: 'deviceManagement',
          label: '设备管理',
          children: [
            { id: 'deviceInfo', label: '设备信息' },
            { id: 'remoteMonitoring', label: '远程监控' }
          ]
        },
        {
          id: 'fileManagement',
          label: '文件管理',
          children: [
            { id: 'patternFiles', label: '花型文件' },
            { id: 'devicePatternFiles', label: '设备花型文件' },
            { id: 'downloadLog', label: '下发日志' }
          ]
        },
        {
          id: 'statistics',
          label: '数据统计',
          children: [
            { id: 'salaryStatistics', label: '工资统计' },
            { id: 'statusStatistics', label: '状态统计' }
          ]
        },
        {
          id: 'employeeManagement',
          label: '员工管理'
        }
      ],
      permissionLabels: {
        home: '首页',
        dashboard: '数据看板',
        deviceManagement: '设备管理',
        remoteMonitoring: '远程监控',
        fileManagement: '文件管理',
        statistics: '数据统计',
        employeeManagement: '员工管理'
      },
      form: {
        id: null,
        name: '',
        remark: '',
        parentChildLink: true,
        permissionKeys: [...DEFAULT_ROLE_PERMISSIONS]
      },
      rules: {
        name: [
          { required: true, message: '请输入角色名称', trigger: 'blur' },
          {
            validator: (rule, value, callback) => {
              if (!value || !value.trim()) {
                return callback(new Error('请输入角色名称'))
              }
              if ([...value.trim()].length > 10) {
                return callback(new Error('角色名称不能超过10个字'))
              }
              return callback()
            },
            trigger: 'blur'
          }
        ]
      }
    }
  },
  mounted() {
    this.loadData()
  },
  methods: {
    parsePermissionMap(raw) {
      if (!raw) return {}
      if (Array.isArray(raw)) {
        return raw.reduce((acc, key) => {
          acc[key] = true
          return acc
        }, {})
      }
      if (typeof raw === 'object') return raw
      try {
        const parsed = JSON.parse(raw)
        if (Array.isArray(parsed)) {
          return parsed.reduce((acc, key) => {
            acc[key] = true
            return acc
          }, {})
        }
        if (parsed && typeof parsed === 'object') return parsed
      } catch {
        return {}
      }
      return {}
    },
    getPermissionTags(raw) {
      const map = this.parsePermissionMap(raw)
      return Object.keys(this.permissionLabels)
        .filter(key => map[key])
        .map(key => this.permissionLabels[key])
    },
    getCheckedPermissionKeys() {
      const tree = this.$refs.permissionTreeRef
      if (!tree) return []
      const checked = tree.getCheckedKeys() || []
      const halfChecked = tree.getHalfCheckedKeys ? tree.getHalfCheckedKeys() : []
      return Array.from(new Set([...checked, ...halfChecked]))
    },
    buildPermissionJSON() {
      const checkedKeys = this.getCheckedPermissionKeys()
      const permissionMap = {}
      checkedKeys.forEach(key => {
        permissionMap[key] = true
      })
      return JSON.stringify(permissionMap)
    },
    applyTreeCheckedKeys(keys) {
      this.$nextTick(() => {
        if (this.$refs.permissionTreeRef) {
          this.$refs.permissionTreeRef.setCheckedKeys(keys)
        }
      })
    },
    checkAllPermissions() {
      const allKeys = []
      const walk = (nodes) => {
        nodes.forEach(node => {
          allKeys.push(node.id)
          if (node.children?.length) {
            walk(node.children)
          }
        })
      }
      walk(this.permissionTree)
      this.applyTreeCheckedKeys(allKeys)
    },
    clearAllPermissions() {
      this.applyTreeCheckedKeys([])
    },
    handleLinkModeChange() {
      const checked = this.getCheckedPermissionKeys()
      this.applyTreeCheckedKeys(checked)
    },
    async loadData() {
      this.loading = true
      try {
        const params = {
          keyword: this.searchForm.keyword,
          startDate: this.searchForm.dateRange?.[0],
          endDate: this.searchForm.dateRange?.[1]
        }
        const res = await this.$axios.get('/role/list', { params })
        if (res.code === 0) {
          this.roles = Array.isArray(res.data) ? res.data : []
        }
      } catch (error) {
        console.error('加载角色失败', error)
      } finally {
        this.loading = false
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
    openCreateDialog() {
      this.dialogVisible = true
      this.form = {
        id: null,
        name: '',
        remark: '',
        parentChildLink: true,
        permissionKeys: [...DEFAULT_ROLE_PERMISSIONS]
      }
      this.applyTreeCheckedKeys(this.form.permissionKeys)
    },
    openEditDialog(row) {
      const permissions = this.parsePermissionMap(row.permissions)
      this.dialogVisible = true
      this.form = {
        id: row.id || row.ID,
        name: row.name,
        remark: row.remark || '',
        parentChildLink: row.parentChildLink !== false,
        permissionKeys: Object.keys(permissions).filter(key => permissions[key])
      }
      this.applyTreeCheckedKeys(this.form.permissionKeys)
    },
    resetForm() {
      this.$refs.roleFormRef?.resetFields()
      this.clearAllPermissions()
    },
    async saveRole() {
      try {
        await this.$refs.roleFormRef.validate()
        const checkedKeys = this.getCheckedPermissionKeys()
        if (!checkedKeys.length) {
          this.$message.warning('请至少勾选一个权限')
          return
        }

        this.saving = true
        const payload = {
          name: this.form.name.trim(),
          remark: this.form.remark.trim(),
          parentChildLink: this.form.parentChildLink,
          permissions: this.buildPermissionJSON()
        }

        if (this.form.id) {
          const res = await this.$axios.put(`/role/${this.form.id}`, payload)
          if (res.code === 0) {
            this.$message.success('更新成功')
            this.dialogVisible = false
            await this.loadData()
          }
          return
        }

        const res = await this.$axios.post('/role', payload)
        if (res.code === 0) {
          this.$message.success('创建成功')
          this.dialogVisible = false
          await this.loadData()
        }
      } catch (error) {
        console.error('保存角色失败', error)
      } finally {
        this.saving = false
      }
    },
    async deleteRole(row) {
      try {
        await this.$confirm(`确定删除角色「${row.name}」吗?`, '警告', { type: 'warning' })
        const res = await this.$axios.delete(`/role/${row.id || row.ID}`)
        if (res.code === 0) {
          this.$message.success('删除成功')
          await this.loadData()
        }
      } catch (error) {
        if (error !== 'cancel') {
          console.error('删除角色失败', error)
        }
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

.permission-tree-wrap {
  border: 1px solid #e4e7ed;
  border-radius: 4px;
  padding: 10px;
  max-height: 300px;
  overflow: auto;
}
</style>
