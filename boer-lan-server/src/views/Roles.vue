<template>
  <div class="page-shell">
    <div class="page-header">
      <div class="page-title-block">
        <h2>权限角色</h2>
        <p>维护角色权限范围和父子联动规则，分组管理已合并到账号管理页面中处理。</p>
      </div>
    </div>

    <div class="filter-panel">
      <el-form :inline="true" :model="searchForm">
        <el-form-item label="角色名称">
          <el-input
            v-model.trim="searchForm.keyword"
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
    </div>

    <div class="surface-card">
      <div class="action-row">
        <div class="soft-note">
          <i class="el-icon-info"></i>
          <span>角色权限用于约束客户端和服务端的功能可见范围，建议按工厂或业务场景拆分。</span>
        </div>
        <div class="action-group">
          <el-button type="primary" icon="el-icon-plus" @click="openCreateDialog">新增角色</el-button>
          <el-button icon="el-icon-refresh" @click="loadData">刷新</el-button>
        </div>
      </div>

      <el-table :data="roles" v-loading="loading" border style="width: 100%; margin-top: 18px;">
        <el-table-column label="序号" width="70" align="center">
          <template slot-scope="{ $index }">
            {{ $index + 1 }}
          </template>
        </el-table-column>
        <el-table-column prop="name" label="角色名称" min-width="140" />
        <el-table-column prop="createTime" label="创建时间" width="170" />
        <el-table-column label="父子联动" width="110" align="center">
          <template slot-scope="{ row }">
            <span :class="['status-pill', row.parentChildLink ? 'success' : 'info']">
              {{ row.parentChildLink ? '开启' : '关闭' }}
            </span>
          </template>
        </el-table-column>
        <el-table-column label="权限模块" min-width="300">
          <template slot-scope="{ row }">
            <el-tag
              v-for="item in getPermissionTags(row.permissions)"
              :key="`${row.id}-${item}`"
              size="mini"
              effect="plain"
              style="margin-right: 6px; margin-bottom: 6px;"
            >
              {{ item }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="190" align="center">
          <template slot-scope="{ row }">
            <el-button size="small" @click="openEditDialog(row)">编辑</el-button>
            <el-button size="small" type="danger" @click="deleteRole(row)">删除</el-button>
          </template>
        </el-table-column>
        <el-table-column prop="remark" label="备注" min-width="200" show-overflow-tooltip />
      </el-table>
    </div>

    <el-dialog
      :title="form.id ? '编辑角色' : '新增角色'"
      :visible.sync="dialogVisible"
      width="720px"
      @close="resetForm"
    >
      <el-form ref="roleFormRef" :model="form" :rules="rules" label-width="110px">
        <el-form-item label="角色名称" prop="name">
          <el-input v-model="form.name" maxlength="10" show-word-limit placeholder="不超过 10 个字" />
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
          <div class="permission-toolbar">
            <el-checkbox v-model="form.showDetailPermissions" @change="handlePermissionModeChange">
              展开细分权限
            </el-checkbox>
            <el-button size="mini" @click="checkAllPermissions">全选</el-button>
            <el-button size="mini" @click="clearAllPermissions">清空</el-button>
          </div>
          <div v-if="!form.showDetailPermissions" class="dialog-tip">
            当前仅展示一级菜单模块；若需要细分权限，可开启展开细分权限。
          </div>
          <div class="permission-tree-wrap">
            <el-tree
              :key="form.showDetailPermissions ? 'detail' : 'module'"
              ref="permissionTreeRef"
              :data="currentPermissionTree"
              node-key="id"
              show-checkbox
              :default-expand-all="form.showDetailPermissions"
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
const DEFAULT_ROLE_MODULE_PERMISSIONS = [
  'home',
  'dashboard',
  'deviceManagement',
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
        showDetailPermissions: false,
        permissionKeys: [...DEFAULT_ROLE_MODULE_PERMISSIONS]
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
  computed: {
    currentPermissionTree() {
      if (this.form.showDetailPermissions) {
        return this.permissionTree
      }
      return this.permissionTree.map(item => ({ id: item.id, label: item.label }))
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
    toModulePermissionKeys(keys) {
      const keySet = new Set(keys || [])
      return this.permissionTree
        .filter(node => keySet.has(node.id) || node.children?.some(child => keySet.has(child.id)))
        .map(node => node.id)
    },
    expandModulePermissions(keys) {
      const keySet = new Set(keys || [])
      this.permissionTree.forEach(node => {
        if (!node.children?.length || !keySet.has(node.id)) {
          return
        }
        node.children.forEach(child => keySet.add(child.id))
      })
      return Array.from(keySet)
    },
    shouldUseDetailMode(permissionMap) {
      return this.permissionTree.some(node => {
        if (!node.children?.length) {
          return false
        }
        const parentSelected = permissionMap[node.id] === true
        const selectedCount = node.children.filter(child => permissionMap[child.id] === true).length
        if (selectedCount > 0 && selectedCount < node.children.length) {
          return true
        }
        if (selectedCount > 0 && !parentSelected) {
          return true
        }
        return false
      })
    },
    buildPermissionJSON() {
      let checkedKeys = this.getCheckedPermissionKeys()
      if (!this.form.showDetailPermissions) {
        checkedKeys = this.expandModulePermissions(checkedKeys)
      }
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
      walk(this.currentPermissionTree)
      this.applyTreeCheckedKeys(allKeys)
    },
    clearAllPermissions() {
      this.applyTreeCheckedKeys([])
    },
    handlePermissionModeChange(enabled) {
      const checked = this.getCheckedPermissionKeys()
      const nextKeys = enabled
        ? this.expandModulePermissions(checked)
        : this.toModulePermissionKeys(checked)
      this.form.permissionKeys = nextKeys
      this.applyTreeCheckedKeys(nextKeys)
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
        showDetailPermissions: false,
        permissionKeys: [...DEFAULT_ROLE_MODULE_PERMISSIONS]
      }
      this.applyTreeCheckedKeys(this.form.permissionKeys)
    },
    openEditDialog(row) {
      const permissions = this.parsePermissionMap(row.permissions)
      const allPermissionKeys = Object.keys(permissions).filter(key => permissions[key])
      const useDetailMode = this.shouldUseDetailMode(permissions)
      this.dialogVisible = true
      this.form = {
        id: row.id || row.ID,
        name: row.name,
        remark: row.remark || '',
        parentChildLink: row.parentChildLink !== false,
        showDetailPermissions: useDetailMode,
        permissionKeys: useDetailMode
          ? allPermissionKeys
          : this.toModulePermissionKeys(allPermissionKeys)
      }
      this.applyTreeCheckedKeys(this.form.permissionKeys)
    },
    resetForm() {
      this.$refs.roleFormRef?.resetFields()
      this.clearAllPermissions()
    },
    async saveRole() {
      const valid = await this.$refs.roleFormRef.validate().catch(() => false)
      if (!valid) {
        return
      }

      try {
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
        await this.$confirm(`确定删除角色「${row.name}」吗？`, '警告', { type: 'warning' })
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
.permission-toolbar {
  margin-bottom: 10px;
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.permission-tree-wrap {
  margin-top: 12px;
  border: 1px solid rgba(219, 228, 240, 0.92);
  border-radius: 18px;
  padding: 12px;
  max-height: 320px;
  overflow: auto;
  background: #f9fbff;
}
</style>
