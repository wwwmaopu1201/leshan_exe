<template>
  <div>
    <div class="page-title">用户管理</div>
    <el-card>
      <el-form :inline="true" :model="searchForm" style="margin-bottom: 12px;">
        <el-form-item label="账号/昵称">
          <el-input
            v-model="searchForm.keyword"
            clearable
            placeholder="输入账号/昵称/手机号"
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
        <el-form-item label="角色">
          <el-select v-model="searchForm.role" clearable placeholder="全部角色">
            <el-option
              v-for="item in roles"
              :key="item.id || item.ID"
              :label="item.name"
              :value="item.name"
            />
          </el-select>
        </el-form-item>
        <el-form-item>
          <el-button type="primary" icon="el-icon-search" @click="handleSearch">搜索</el-button>
          <el-button icon="el-icon-refresh" @click="handleReset">重置</el-button>
        </el-form-item>
      </el-form>

      <div style="margin-bottom: 16px; display: flex; gap: 8px;">
        <el-button type="primary" icon="el-icon-plus" @click="openCreateDialog">新建用户</el-button>
        <el-button icon="el-icon-share" :disabled="selectedUserIds.length === 0" @click="openMoveDialog">
          批量移动分组
        </el-button>
        <el-button icon="el-icon-refresh" @click="loadData">刷新</el-button>
      </div>

      <el-table ref="userTableRef" :data="users" v-loading="loading" style="width: 100%;" @selection-change="handleSelectionChange">
        <el-table-column type="selection" width="48" />
        <el-table-column prop="username" label="用户名" min-width="130" />
        <el-table-column prop="nickname" label="昵称" min-width="120" />
        <el-table-column prop="role" label="角色" width="100" />
        <el-table-column prop="createTime" label="创建时间" width="170" />
        <el-table-column label="所属分组" min-width="180">
          <template slot-scope="{ row }">
            {{ formatGroupName(row) }}
          </template>
        </el-table-column>
        <el-table-column label="权限" min-width="260">
          <template slot-scope="{ row }">
            <el-tag
              v-for="item in getPermissionTags(row.permissions)"
              :key="`${row.id || row.ID}-${item.key}`"
              size="mini"
              style="margin-right: 4px; margin-bottom: 4px;"
            >
              {{ item.label }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="disabled" label="状态" width="100">
          <template slot-scope="{ row }">
            <el-tag :type="row.disabled ? 'danger' : 'success'">
              {{ row.disabled ? '已禁用' : '正常' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="180">
          <template slot-scope="{ row }">
            <el-button size="small" @click="openEditDialog(row)" icon="el-icon-edit">编辑</el-button>
            <el-button size="small" type="danger" @click="deleteUser(row)" icon="el-icon-delete">删除</el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <el-dialog
      :title="form.id ? '编辑用户' : '新建用户'"
      :visible.sync="dialogVisible"
      width="640px"
      @close="resetForm"
    >
      <el-form ref="userFormRef" :model="form" :rules="rules" label-width="110px">
        <el-form-item label="用户名" prop="username">
          <el-input v-model="form.username" :disabled="!!form.id" placeholder="请输入用户名">
            <template v-if="!form.id" slot="append">
              <el-button @click="generateUsername">随机生成</el-button>
            </template>
          </el-input>
        </el-form-item>

        <el-form-item :label="form.id ? '新密码(可选)' : '密码'" :prop="form.id ? '' : 'password'">
          <el-input
            v-model="form.password"
            type="password"
            show-password
            :placeholder="form.id ? '不填写则不修改密码' : '请输入密码'"
          />
        </el-form-item>

        <el-form-item label="昵称" prop="nickname">
          <el-input v-model="form.nickname" placeholder="请输入昵称" />
        </el-form-item>

        <el-form-item label="角色" prop="role">
          <el-select v-model="form.role" style="width: 100%;" @change="handleRoleChange">
            <el-option
              v-for="item in roles"
              :key="item.id || item.ID"
              :label="item.name"
              :value="item.name"
            />
          </el-select>
        </el-form-item>

        <el-form-item label="邮箱">
          <el-input v-model="form.email" placeholder="可选" />
        </el-form-item>

        <el-form-item label="手机号" prop="phone">
          <el-input v-model="form.phone" placeholder="请输入手机号" />
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

        <el-form-item label="账户状态">
          <el-switch
            v-model="form.disabled"
            active-text="禁用"
            inactive-text="启用"
          />
        </el-form-item>

        <el-form-item label="角色权限">
          <div v-if="getRolePermissionTags(form.role).length">
            <el-tag
              v-for="item in getRolePermissionTags(form.role)"
              :key="item.key"
              size="mini"
              style="margin-right: 4px; margin-bottom: 4px;"
            >
              {{ item.label }}
            </el-tag>
          </div>
          <div v-else style="color: #909399;">当前角色未配置权限</div>
        </el-form-item>
      </el-form>

      <span slot="footer" class="dialog-footer">
        <el-button @click="dialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="saveUser">保存</el-button>
      </span>
    </el-dialog>

    <el-dialog
      title="批量移动用户分组"
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
        <div style="color: #606266;">已选择 {{ selectedUserIds.length }} 个用户</div>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="moveDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="moving" @click="confirmMoveUsers">确定移动</el-button>
      </span>
    </el-dialog>
  </div>
</template>

<script>
const DEFAULT_PERMISSION_KEYS = ['home', 'dashboard', 'deviceManagement', 'remoteMonitoring', 'fileManagement', 'statistics', 'employeeManagement']

export default {
  name: 'Users',
  data() {
    return {
      loading: false,
      saving: false,
      moving: false,
      users: [],
      groups: [],
      roles: [],
      selectedUserIds: [],
      moveDialogVisible: false,
      moveTargetGroupId: null,
      searchForm: {
        keyword: '',
        dateRange: [],
        role: ''
      },
      dialogVisible: false,
      permissionOptions: [
        { key: 'home', label: '首页' },
        { key: 'dashboard', label: '数据看板' },
        { key: 'employeeManagement', label: '员工管理' },
        { key: 'deviceManagement', label: '设备管理' },
        { key: 'fileManagement', label: '文件管理' },
        { key: 'remoteMonitoring', label: '远程监控' },
        { key: 'statistics', label: '数据统计' }
      ],
      form: {
        id: null,
        username: '',
        password: '',
        nickname: '',
        role: '',
        email: '',
        phone: '',
        groupId: null,
        disabled: false,
        permissionKeys: [...DEFAULT_PERMISSION_KEYS]
      },
      rules: {
        username: [
          { required: true, message: '请输入用户名', trigger: 'blur' },
          { pattern: /^[a-zA-Z0-9]+$/, message: '账号仅支持字母数字', trigger: 'blur' },
          { max: 11, message: '账号不能超过11位', trigger: 'blur' }
        ],
        password: [
          { required: true, message: '请输入密码', trigger: 'blur' },
          { min: 6, max: 32, message: '密码长度需在6-32位', trigger: 'blur' }
        ],
        nickname: [{ required: true, message: '请输入昵称', trigger: 'blur' }],
        role: [{ required: true, message: '请选择角色', trigger: 'change' }],
        phone: [{ validator: (rule, value, callback) => {
          const normalized = String(value || '').trim()
          if (!normalized) return callback(new Error('请输入手机号'))
          if (!/^1[3-9]\d{9}$/.test(normalized)) return callback(new Error('手机号格式不正确'))
          return callback()
        }, trigger: 'blur' }]
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
          endDate: this.searchForm.dateRange?.[1],
          role: this.searchForm.role
        }
        const [usersRes, groupsRes, rolesRes] = await Promise.all([
          this.$axios.get('/user/all', { params }),
          this.$axios.get('/group/list'),
          this.$axios.get('/role/list')
        ])
        if (usersRes.code === 0) {
          this.users = (Array.isArray(usersRes.data) ? usersRes.data : []).map(item => ({
            ...item,
            id: item.id || item.ID,
            createTime: item.createTime || item.createdAt || item.CreatedAt || ''
          }))
        }
        if (groupsRes.code === 0) {
          this.groups = Array.isArray(groupsRes.data) ? groupsRes.data : []
        }
        if (rolesRes.code === 0) {
          this.roles = Array.isArray(rolesRes.data) ? rolesRes.data : []
          if (!this.form.id && !this.form.role && this.roles.length > 0) {
            const defaultRole = this.roles.find(item => item.name === 'user') || this.roles[0]
            this.form.role = defaultRole.name
          }
        }
        this.selectedUserIds = []
        this.$nextTick(() => {
          this.$refs.userTableRef?.clearSelection()
        })
      } catch (error) {
        console.error('加载用户数据失败', error)
      } finally {
        this.loading = false
      }
    },
    handleSelectionChange(rows) {
      this.selectedUserIds = rows.map(item => item.id || item.ID).filter(Boolean)
    },
    openMoveDialog() {
      this.moveTargetGroupId = null
      this.moveDialogVisible = true
    },
    async confirmMoveUsers() {
      if (!this.moveTargetGroupId) {
        this.$message.warning('请选择目标分组')
        return
      }
      if (!this.selectedUserIds.length) {
        this.$message.warning('请先选择用户')
        return
      }
      this.moving = true
      try {
        const res = await this.$axios.post('/user/move', {
          userIds: this.selectedUserIds,
          groupId: this.moveTargetGroupId
        })
        if (res.code === 0) {
          this.$message.success('移动成功')
          this.moveDialogVisible = false
          await this.loadData()
        }
      } catch (error) {
        console.error('移动用户分组失败', error)
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
        dateRange: [],
        role: ''
      }
      this.loadData()
    },
    parsePermissions(raw) {
      const defaultMap = this.permissionOptions.reduce((acc, item) => {
        acc[item.key] = true
        return acc
      }, {})
      if (!raw) {
        return defaultMap
      }
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
        return parsed
      } catch {
        return defaultMap
      }
    },
    toPermissionKeys(raw) {
      const permissions = this.parsePermissions(raw)
      return Object.keys(permissions).filter(key => permissions[key])
    },
    buildPermissionJSON(keys) {
      const permissionMap = this.permissionOptions.reduce((acc, item) => {
        acc[item.key] = false
        return acc
      }, {})
      keys.forEach(key => {
        if (Object.prototype.hasOwnProperty.call(permissionMap, key)) {
          permissionMap[key] = true
        }
      })
      return JSON.stringify(permissionMap)
    },
    getPermissionTags(raw) {
      const permissions = this.parsePermissions(raw)
      return this.permissionOptions.filter(item => permissions[item.key])
    },
    getRoleByName(roleName) {
      return this.roles.find(item => item.name === roleName)
    },
    getRolePermissionTags(roleName) {
      const role = this.getRoleByName(roleName)
      if (!role) return []
      return this.getPermissionTags(role.permissions)
    },
    applyRolePermissions(roleName) {
      const role = this.getRoleByName(roleName)
      if (!role) return
      this.form.permissionKeys = this.toPermissionKeys(role.permissions)
    },
    handleRoleChange(value) {
      this.applyRolePermissions(value)
    },
    formatGroupName(user) {
      if (!user.group) return '-'
      if (user.group.parent) {
        return `${user.group.parent.name} / ${user.group.name}`
      }
      return user.group.name
    },
    openCreateDialog() {
      const defaultRole = this.roles.find(item => item.name === 'user') || this.roles[0]
      this.dialogVisible = true
      this.form = {
        id: null,
        username: '',
        password: '',
        nickname: '',
        role: defaultRole ? defaultRole.name : '',
        email: '',
        phone: '',
        groupId: null,
        disabled: false,
        permissionKeys: [...DEFAULT_PERMISSION_KEYS]
      }
      if (this.form.role) {
        this.applyRolePermissions(this.form.role)
      }
    },
    generateUsername() {
      const chars = 'ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz0123456789'
      const build = (length = 8) => {
        let value = ''
        for (let i = 0; i < length; i++) {
          value += chars.charAt(Math.floor(Math.random() * chars.length))
        }
        return value
      }

      let candidate = build(8)
      const exists = (name) => this.users.some(item => item.username === name)
      let tries = 0
      while (exists(candidate) && tries < 20) {
        candidate = build(8)
        tries++
      }
      this.form.username = candidate
    },
    openEditDialog(row) {
      this.dialogVisible = true
      this.form = {
        id: row.id || row.ID,
        username: row.username,
        password: '',
        nickname: row.nickname || '',
        role: row.role || 'user',
        email: row.email || '',
        phone: row.phone || '',
        groupId: row.groupId || null,
        disabled: !!row.disabled,
        permissionKeys: this.toPermissionKeys(row.permissions)
      }
      if (this.form.role) {
        this.applyRolePermissions(this.form.role)
      }
    },
    resetForm() {
      this.$refs.userFormRef?.resetFields()
    },
    async saveUser() {
      try {
        await this.$refs.userFormRef.validate()
        this.saving = true

        const payload = {
          username: this.form.username,
          nickname: this.form.nickname,
          role: this.form.role,
          email: this.form.email,
          phone: String(this.form.phone || '').trim(),
          groupId: this.form.groupId,
          disabled: this.form.disabled,
          permissions: this.getRoleByName(this.form.role)?.permissions || this.buildPermissionJSON(this.form.permissionKeys)
        }

        if (this.form.id) {
          if (this.form.password) {
            payload.password = this.form.password
          }
          const res = await this.$axios.put(`/user/${this.form.id}`, payload)
          if (res.code === 0) {
            this.$message.success('更新成功')
            this.dialogVisible = false
            await this.loadData()
          }
          return
        }

        payload.password = this.form.password
        const res = await this.$axios.post('/user', payload)
        if (res.code === 0) {
          this.$message.success('创建成功')
          this.dialogVisible = false
          await this.loadData()
        }
      } catch (error) {
        console.error('保存用户失败', error)
      } finally {
        this.saving = false
      }
    },
    async deleteUser(row) {
      try {
        await this.$confirm(`确定要删除用户「${row.username}」吗?`, '警告', {
          type: 'warning'
        })
        const res = await this.$axios.delete('/user', {
          data: { ids: [row.id || row.ID] }
        })
        if (res.code === 0) {
          this.$message.success('删除成功')
          await this.loadData()
        }
      } catch (error) {
        if (error !== 'cancel') {
          console.error('删除用户失败', error)
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
</style>
