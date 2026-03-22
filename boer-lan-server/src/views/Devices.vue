<template>
  <div class="page-shell">
    <div class="page-header">
      <div class="page-title-block">
        <h2>设备管理</h2>
        <p>左侧设备树按分组筛选，右侧统一查看设备状态、分组、备注和批量移动操作。</p>
      </div>
    </div>

    <div class="panel-layout">
      <div class="panel-side">
        <div class="panel-shell">
          <div class="panel-header">
            <div>
              <div class="panel-title">设备树</div>
              <div class="panel-subtitle">按分组快速定位设备</div>
            </div>
            <div class="action-group">
              <el-button type="text" size="mini" @click="loadGroupTree">刷新</el-button>
            </div>
          </div>

          <el-input
            v-model="treeKeyword"
            size="small"
            placeholder="搜索分组"
            prefix-icon="el-icon-search"
            clearable
          />

          <div class="action-group tree-shortcuts">
            <el-button
              size="small"
              :type="treeSelection.mode === 'all' ? 'primary' : 'default'"
              plain
              @click="setTreeScope('all')"
            >
              全部设备
            </el-button>
            <el-button
              size="small"
              :type="treeSelection.mode === 'ungrouped' ? 'danger' : 'default'"
              plain
              @click="setTreeScope('ungrouped')"
            >
              未分组
            </el-button>
          </div>

          <div class="selection-strip">
            <div style="min-width: 0;">
              <span class="selection-strip__label">当前选择</span>
              <span class="selection-strip__value">{{ treeScopeLabel }}</span>
            </div>
            <el-button type="text" size="mini" @click="setTreeScope('all')">清空</el-button>
          </div>

          <div class="tree-scroll" v-loading="groupTreeLoading">
            <el-tree
              ref="groupTreeRef"
              :data="groupTree"
              node-key="id"
              default-expand-all
              highlight-current
              :filter-node-method="filterTreeNode"
              :props="{ label: 'name', children: 'children' }"
              @node-click="handleTreeNodeClick"
            >
              <div slot-scope="{ node, data }" class="tree-node">
                <div class="tree-node__main">
                  <i :class="['tree-node__icon', data.children?.length ? 'el-icon-folder-opened' : 'el-icon-folder']"></i>
                  <span class="tree-node__label" :title="node.label">{{ node.label }}</span>
                </div>
              </div>
            </el-tree>
          </div>
        </div>
      </div>

      <div class="panel-main">
        <div class="filter-panel">
          <el-form :inline="true" :model="searchForm">
            <el-form-item label="关键字">
              <el-input
                v-model.trim="searchForm.keyword"
                clearable
                placeholder="设备名/编号/类型/员工/主板号"
                @keyup.enter.native="handleSearch"
              />
            </el-form-item>
            <el-form-item label="状态">
              <el-select v-model="searchForm.status" clearable placeholder="全部状态">
                <el-option label="在线" value="online" />
                <el-option label="离线" value="offline" />
                <el-option label="缝纫中" value="working" />
                <el-option label="空闲" value="idle" />
                <el-option label="报警" value="alarm" />
              </el-select>
            </el-form-item>
            <el-form-item label="添加时间">
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
              <span>当前范围：{{ treeScopeLabel }}，共 {{ filteredDevices.length }} 台设备，未分组设备会以浅红底提示。</span>
            </div>
            <div class="action-group">
              <el-button icon="el-icon-share" :disabled="selectedDeviceIds.length === 0" @click="openMoveDialog">
                批量移动分组
              </el-button>
              <el-button icon="el-icon-refresh" @click="loadDevices">刷新</el-button>
            </div>
          </div>

          <el-table
            ref="deviceTableRef"
            :data="pagedDevices"
            v-loading="loading"
            border
            style="width: 100%; margin-top: 18px;"
            :row-class-name="rowClassName"
            @selection-change="handleSelectionChange"
          >
            <el-table-column type="selection" width="48" />
            <el-table-column label="序号" width="70" align="center">
              <template slot-scope="{ $index }">
                {{ (page - 1) * pageSize + $index + 1 }}
              </template>
            </el-table-column>
            <el-table-column prop="name" label="设备名称" min-width="130" />
            <el-table-column prop="initialName" label="初始名称" min-width="130" />
            <el-table-column prop="employeeCode" label="员工工号" width="110" />
            <el-table-column prop="employeeName" label="员工姓名" width="110" />
            <el-table-column prop="code" label="设备编号" min-width="130" />
            <el-table-column prop="type" label="设备类型" width="110" />
            <el-table-column prop="model" label="机型" width="110" />
            <el-table-column prop="mainboardSn" label="主板编号" min-width="140" />
            <el-table-column label="状态" width="100" align="center">
              <template slot-scope="{ row }">
                <span :class="['status-pill', getStatusTone(row.status)]">
                  {{ getStatusLabel(row.status) }}
                </span>
              </template>
            </el-table-column>
            <el-table-column prop="ip" label="设备 IP" width="130" />
            <el-table-column label="分组" min-width="150">
              <template slot-scope="{ row }">
                <span :class="row.groupId ? '' : 'danger-text'">
                  {{ row.group || '未分组' }}
                </span>
              </template>
            </el-table-column>
            <el-table-column prop="createTime" label="添加时间" width="170" />
            <el-table-column prop="remark" label="备注" min-width="120" show-overflow-tooltip />
            <el-table-column label="操作" width="180" fixed="right" align="center">
              <template slot-scope="{ row }">
                <el-button size="small" @click="openEditDialog(row)">编辑</el-button>
                <el-button size="small" @click="pingDevice(row.ip)">Ping</el-button>
              </template>
            </el-table-column>
          </el-table>

          <div class="compact-pagination">
            <el-pagination
              background
              layout="total, sizes, prev, pager, next, jumper"
              :current-page.sync="page"
              :page-size.sync="pageSize"
              :page-sizes="[10, 20, 50, 100]"
              :total="filteredDevices.length"
              @size-change="handlePageSizeChange"
              @current-change="handlePageChange"
            />
          </div>
        </div>
      </div>
    </div>

    <el-dialog
      title="编辑设备"
      :visible.sync="editDialogVisible"
      width="520px"
    >
      <el-form ref="editFormRef" :model="editForm" :rules="editRules" label-width="90px">
        <el-form-item label="设备名称" prop="name">
          <el-input v-model.trim="editForm.name" />
        </el-form-item>
        <el-form-item label="设备分组">
          <el-select v-model="editForm.groupId" clearable style="width: 100%;">
            <el-option label="未分组" :value="null" />
            <el-option
              v-for="item in groups"
              :key="item.id"
              :label="item.parent ? `${item.parent.name} / ${item.name}` : item.name"
              :value="item.id"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="备注">
          <el-input v-model.trim="editForm.remark" type="textarea" :rows="2" />
        </el-form-item>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="editDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="saveDevice">保存</el-button>
      </span>
    </el-dialog>

    <el-dialog
      title="批量移动设备分组"
      :visible.sync="moveDialogVisible"
      width="420px"
    >
      <el-form label-width="90px">
        <el-form-item label="目标分组">
          <el-select v-model="moveTargetGroupId" clearable placeholder="可选择“未分组”" style="width: 100%;">
            <el-option label="未分组" :value="null" />
            <el-option
              v-for="item in groups"
              :key="item.id"
              :label="item.parent ? `${item.parent.name} / ${item.name}` : item.name"
              :value="item.id"
            />
          </el-select>
        </el-form-item>
        <div class="dialog-tip">已选择 {{ selectedDeviceIds.length }} 台设备</div>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="moveDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="moving" @click="confirmMoveDevices">确定移动</el-button>
      </span>
    </el-dialog>
  </div>
</template>

<script>
export default {
  name: 'Devices',
  data() {
    return {
      loading: false,
      groupTreeLoading: false,
      saving: false,
      moving: false,
      devices: [],
      groups: [],
      groupTree: [],
      treeKeyword: '',
      refreshTimer: null,
      page: 1,
      pageSize: 20,
      selectedDeviceIds: [],
      moveDialogVisible: false,
      moveTargetGroupId: null,
      editDialogVisible: false,
      treeSelection: {
        mode: 'all',
        groupId: null,
        label: ''
      },
      searchForm: {
        keyword: '',
        status: '',
        dateRange: []
      },
      editForm: {
        id: null,
        name: '',
        groupId: null,
        remark: ''
      },
      editRules: {
        name: [{ required: true, message: '请输入设备名称', trigger: 'blur' }]
      }
    }
  },
  computed: {
    treeScopeLabel() {
      if (this.treeSelection.mode === 'group') {
        return this.treeSelection.label || '指定分组'
      }
      if (this.treeSelection.mode === 'ungrouped') {
        return '未分组设备'
      }
      return '全部设备'
    },
    filteredDevices() {
      if (this.treeSelection.mode === 'ungrouped') {
        return this.devices.filter(item => !item.groupId)
      }
      if (this.treeSelection.mode === 'group' && this.treeSelection.groupId) {
        const targetIds = this.collectDescendantGroupIds(Number(this.treeSelection.groupId))
        return this.devices.filter(item => targetIds.includes(Number(item.groupId)))
      }
      return this.devices
    },
    pagedDevices() {
      const start = (this.page - 1) * this.pageSize
      return this.filteredDevices.slice(start, start + this.pageSize)
    }
  },
  watch: {
    treeKeyword(val) {
      this.$refs.groupTreeRef?.filter(val)
    },
    treeSelection: {
      deep: true,
      handler() {
        this.page = 1
        this.selectedDeviceIds = []
        this.$nextTick(() => {
          this.$refs.deviceTableRef?.clearSelection()
          const key = this.treeSelection.mode === 'group' ? this.treeSelection.groupId : null
          this.$refs.groupTreeRef?.setCurrentKey(key || null)
        })
      }
    }
  },
  mounted() {
    this.initPage()
    this.refreshTimer = setInterval(() => {
      this.autoRefreshDevices()
    }, 5000)
  },
  beforeDestroy() {
    if (this.refreshTimer) {
      clearInterval(this.refreshTimer)
      this.refreshTimer = null
    }
  },
  methods: {
    async initPage() {
      await Promise.all([this.loadGroupTree(), this.loadDevices()])
    },
    normalizeTree(nodes = []) {
      return nodes
        .map(item => ({
          ...item,
          id: item.id || item.ID,
          parentId: item.parentId || item.ParentID || null,
          sortOrder: item.sortOrder || item.SortOrder || 0,
          children: this.normalizeTree(item.children || item.Children || [])
        }))
        .sort((a, b) => (a.sortOrder - b.sortOrder) || (a.id - b.id))
    },
    async loadGroupTree() {
      this.groupTreeLoading = true
      try {
        const [treeRes, groupsRes] = await Promise.all([
          this.$axios.get('/group/tree'),
          this.$axios.get('/group/list')
        ])
        if (treeRes.code === 0) {
          this.groupTree = this.normalizeTree(Array.isArray(treeRes.data) ? treeRes.data : [])
          this.$nextTick(() => {
            this.$refs.groupTreeRef?.filter(this.treeKeyword)
            if (this.treeSelection.mode === 'group' && this.treeSelection.groupId) {
              this.$refs.groupTreeRef?.setCurrentKey(this.treeSelection.groupId)
            }
          })
        }
        if (groupsRes.code === 0) {
          this.groups = this.normalizeGroupList(Array.isArray(groupsRes.data) ? groupsRes.data : [])
        }
      } catch (error) {
        console.error('加载分组失败', error)
      } finally {
        this.groupTreeLoading = false
      }
    },
    filterTreeNode(value, data) {
      if (!value) return true
      return String(data?.name || '').toLowerCase().includes(value.toLowerCase())
    },
    normalizeGroupList(groups = []) {
      return groups.map(item => ({
        ...item,
        id: item.id || item.ID,
        parentId: item.parentId || item.ParentID || item.parent?.id || null
      }))
    },
    collectDescendantGroupIds(groupId) {
      const target = Number(groupId)
      if (!Number.isFinite(target) || target <= 0) {
        return []
      }
      const ids = [target]
      const queue = [target]
      while (queue.length) {
        const current = queue.shift()
        this.groups.forEach(group => {
          if (Number(group.parentId) === current && !ids.includes(group.id)) {
            ids.push(group.id)
            queue.push(group.id)
          }
        })
      }
      return ids
    },
    handleTreeNodeClick(data) {
      if (!data) return
      this.treeSelection = {
        mode: 'group',
        groupId: data.id,
        label: data.name
      }
    },
    setTreeScope(mode) {
      if (mode === 'ungrouped') {
        this.treeSelection = {
          mode: 'ungrouped',
          groupId: null,
          label: '未分组设备'
        }
        return
      }
      this.treeSelection = {
        mode: 'all',
        groupId: null,
        label: ''
      }
    },
    async loadDevices(options = {}) {
      if (!options.silent) {
        this.loading = true
      }
      try {
        const params = {
          keyword: this.searchForm.keyword,
          status: this.searchForm.status,
          startDate: this.searchForm.dateRange?.[0],
          endDate: this.searchForm.dateRange?.[1],
          page: 1,
          pageSize: 5000
        }
        const res = await this.$axios.get('/device/list', { params })
        if (res.code === 0) {
          const payload = res.data || {}
          const list = Array.isArray(payload.list) ? payload.list : (Array.isArray(res.data) ? res.data : [])
          this.devices = list
        }
        this.selectedDeviceIds = []
        this.$nextTick(() => {
          this.$refs.deviceTableRef?.clearSelection()
        })
      } catch (error) {
        console.error('加载设备列表失败', error)
      } finally {
        if (!options.silent) {
          this.loading = false
        }
      }
    },
    autoRefreshDevices() {
      if (this.loading || this.saving || this.moving) {
        return
      }
      if (this.editDialogVisible || this.moveDialogVisible) {
        return
      }
      if (this.selectedDeviceIds.length > 0) {
        return
      }
      this.loadDevices({ silent: true })
    },
    handleSearch() {
      this.page = 1
      this.loadDevices()
    },
    handleReset() {
      this.searchForm = {
        keyword: '',
        status: '',
        dateRange: []
      }
      this.page = 1
      this.loadDevices()
    },
    handlePageChange(page) {
      this.page = page
    },
    handlePageSizeChange(size) {
      this.pageSize = size
      this.page = 1
    },
    rowClassName({ row }) {
      return row.groupId ? '' : 'row-ungrouped'
    },
    handleSelectionChange(rows) {
      this.selectedDeviceIds = rows.map(item => item.id || item.ID).filter(Boolean)
    },
    openMoveDialog() {
      this.moveTargetGroupId = null
      this.moveDialogVisible = true
    },
    async confirmMoveDevices() {
      if (!this.selectedDeviceIds.length) {
        this.$message.warning('请先选择设备')
        return
      }
      this.moving = true
      try {
        const res = await this.$axios.post('/device/move', {
          deviceIds: this.selectedDeviceIds,
          groupId: this.moveTargetGroupId
        })
        if (res.code === 0) {
          this.$message.success('分组移动成功')
          this.moveDialogVisible = false
          await Promise.all([this.loadDevices(), this.loadGroupTree()])
        }
      } catch (error) {
        console.error('移动设备分组失败', error)
      } finally {
        this.moving = false
      }
    },
    openEditDialog(row) {
      this.editDialogVisible = true
      this.editForm = {
        id: row.id || row.ID,
        name: row.name || '',
        groupId: row.groupId ?? null,
        remark: row.remark || ''
      }
    },
    async saveDevice() {
      const valid = await this.$refs.editFormRef.validate().catch(() => false)
      if (!valid) {
        return
      }

      try {
        this.saving = true
        const res = await this.$axios.put(`/device/${this.editForm.id}`, {
          name: this.editForm.name.trim(),
          groupId: this.editForm.groupId,
          remark: this.editForm.remark.trim()
        })
        if (res.code === 0) {
          this.$message.success('更新成功')
          this.editDialogVisible = false
          await Promise.all([this.loadDevices(), this.loadGroupTree()])
        }
      } catch (error) {
        console.error('更新设备失败', error)
      } finally {
        this.saving = false
      }
    },
    getStatusTone(status) {
      const map = {
        online: 'success',
        offline: 'info',
        working: 'primary',
        idle: 'warning',
        alarm: 'danger'
      }
      return map[status] || 'info'
    },
    getStatusLabel(status) {
      const map = {
        online: '在线',
        offline: '离线',
        working: '缝纫中',
        idle: '空闲',
        alarm: '报警'
      }
      return map[status] || status || '-'
    },
    async pingDevice(ip) {
      try {
        const res = await this.$axios.post('/system/ping', null, {
          params: { ip }
        })
        if (res.code === 0) {
          this.$alert(`<pre>${res.data.output}</pre>`, 'Ping结果', {
            dangerouslyUseHTMLString: true,
            confirmButtonText: '确定'
          })
        }
      } catch (error) {
        console.error('Ping失败', error)
      }
    }
  }
}
</script>

<style scoped>
.tree-shortcuts {
  margin-top: 12px;
}

::v-deep .row-ungrouped td {
  background: rgba(239, 90, 90, 0.045);
}

.tree-scroll ::v-deep .el-tree-node__content {
  height: 38px;
  border-radius: 12px;
  margin-bottom: 4px;
}

.tree-scroll ::v-deep .el-tree-node.is-current > .el-tree-node__content {
  background: rgba(47, 109, 246, 0.1);
}
</style>
