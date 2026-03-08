<template>
  <div>
    <div class="page-title">用户管理</div>
    <el-card>
      <div style="margin-bottom: 16px; display: flex; gap: 8px;">
        <el-button type="primary" icon="el-icon-plus" @click="openCreateDialog">新建用户</el-button>
        <el-button icon="el-icon-refresh" @click="loadData">刷新</el-button>
      </div>

      <el-table :data="users" v-loading="loading" style="width: 100%;">
        <el-table-column prop="username" label="用户名" min-width="130" />
        <el-table-column prop="nickname" label="昵称" min-width="120" />
        <el-table-column prop="role" label="角色" width="100" />
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
          <el-input v-model="form.username" :disabled="!!form.id" placeholder="请输入用户名" />
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
          <el-select v-model="form.role" style="width: 100%;">
            <el-option label="管理员" value="admin" />
            <el-option label="普通用户" value="user" />
          </el-select>
        </el-form-item>

        <el-form-item label="邮箱">
          <el-input v-model="form.email" placeholder="可选" />
        </el-form-item>

        <el-form-item label="手机号">
          <el-input v-model="form.phone" placeholder="可选" />
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

        <el-form-item label="功能权限">
          <el-checkbox-group v-model="form.permissionKeys">
            <el-checkbox
              v-for="item in permissionOptions"
              :key="item.key"
              :label="item.key"
            >
              {{ item.label }}
            </el-checkbox>
          </el-checkbox-group>
        </el-form-item>
      </el-form>

      <span slot="footer" class="dialog-footer">
        <el-button @click="dialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="saveUser">保存</el-button>
      </span>
    </el-dialog>
  </div>
</template>

<script>
const DEFAULT_PERMISSION_KEYS = ['fileManagement', 'remoteMonitoring', 'statistics', 'deviceManagement']

export default {
  name: 'Users',
  data() {
    return {
      loading: false,
      saving: false,
      users: [],
      groups: [],
      dialogVisible: false,
      permissionOptions: [
        { key: 'fileManagement', label: '文件管理' },
        { key: 'remoteMonitoring', label: '远程监控' },
        { key: 'statistics', label: '数据统计' },
        { key: 'deviceManagement', label: '设备管理' }
      ],
      form: {
        id: null,
        username: '',
        password: '',
        nickname: '',
        role: 'user',
        email: '',
        phone: '',
        groupId: null,
        disabled: false,
        permissionKeys: [...DEFAULT_PERMISSION_KEYS]
      },
      rules: {
        username: [{ required: true, message: '请输入用户名', trigger: 'blur' }],
        password: [{ required: true, message: '请输入密码', trigger: 'blur' }],
        nickname: [{ required: true, message: '请输入昵称', trigger: 'blur' }],
        role: [{ required: true, message: '请选择角色', trigger: 'change' }]
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
        const [usersRes, groupsRes] = await Promise.all([
          this.$axios.get('/user/all'),
          this.$axios.get('/group/list')
        ])
        if (usersRes.code === 0) {
          this.users = (Array.isArray(usersRes.data) ? usersRes.data : []).map(item => ({
            ...item,
            id: item.id || item.ID
          }))
        }
        if (groupsRes.code === 0) {
          this.groups = Array.isArray(groupsRes.data) ? groupsRes.data : []
        }
      } catch (error) {
        console.error('加载用户数据失败', error)
      } finally {
        this.loading = false
      }
    },
    parsePermissions(raw) {
      if (!raw) {
        return {
          fileManagement: true,
          remoteMonitoring: true,
          statistics: true,
          deviceManagement: true
        }
      }
      if (typeof raw === 'object') return raw
      try {
        return JSON.parse(raw)
      } catch {
        return {
          fileManagement: true,
          remoteMonitoring: true,
          statistics: true,
          deviceManagement: true
        }
      }
    },
    toPermissionKeys(raw) {
      const permissions = this.parsePermissions(raw)
      return Object.keys(permissions).filter(key => permissions[key])
    },
    buildPermissionJSON(keys) {
      const permissionMap = {
        fileManagement: false,
        remoteMonitoring: false,
        statistics: false,
        deviceManagement: false
      }
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
    formatGroupName(user) {
      if (!user.group) return '-'
      if (user.group.parent) {
        return `${user.group.parent.name} / ${user.group.name}`
      }
      return user.group.name
    },
    openCreateDialog() {
      this.dialogVisible = true
      this.form = {
        id: null,
        username: '',
        password: '',
        nickname: '',
        role: 'user',
        email: '',
        phone: '',
        groupId: null,
        disabled: false,
        permissionKeys: [...DEFAULT_PERMISSION_KEYS]
      }
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
          phone: this.form.phone,
          groupId: this.form.groupId,
          disabled: this.form.disabled,
          permissions: this.buildPermissionJSON(this.form.permissionKeys)
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
