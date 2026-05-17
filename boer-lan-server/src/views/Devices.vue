<template>
  <div class="page-shell">
    <div class="page-header">
      <div class="page-title-block">
        <h2>设备管理</h2>
        <p>左侧设备树按分组筛选，右侧统一查看设备状态、分组、协议实时信息和批量移动操作。</p>
      </div>
    </div>

    <div class="panel-layout">
      <div class="panel-side">
        <div class="device-tree-panel panel-shell">
          <div class="panel-header">
            <div>
              <div class="panel-title">设备树与分组</div>
            </div>
            <div class="panel-actions">
              <el-button type="text" size="mini" @click="loadGroupTree">刷新</el-button>
            </div>
          </div>

          <el-input
            v-model="treeKeyword"
            size="small"
            placeholder="搜索设备或分组"
            prefix-icon="el-icon-search"
            clearable
          />

          <div class="selection-bar">
            <span class="selection-label">当前选择</span>
            <span class="selection-value">{{ treeScopeLabel }}</span>
            <el-button type="text" size="mini" @click="setTreeScope('all')">清空</el-button>
          </div>

          <div class="tree-wrapper" v-loading="groupTreeLoading">
            <el-tree
              ref="groupTreeRef"
              :data="displayGroupTree"
              :props="{ label: 'label', children: 'children' }"
              default-expand-all
              highlight-current
              node-key="_nodeKey"
              :filter-node-method="filterTreeNode"
              @node-click="handleTreeNodeClick"
              @node-contextmenu="handleTreeNodeContextMenu"
            >
              <div
                slot-scope="{ node, data }"
                class="tree-node"
                :class="{
                  'tree-node-device': data.type === 'device',
                  'tree-node-drop-target': isManualDragDropTarget(data),
                  'tree-node-drop-hover': manualDrag.targetKey === data._nodeKey
                }"
                :data-tree-node-key="data._nodeKey"
                @mousedown.left="handleManualDeviceMouseDown($event, data)"
              >
                <div class="tree-node-main" :class="{ ungrouped: isUngroupedNode(data) }">
                  <i :class="['tree-node-icon', getTreeNodeIcon(data)]"></i>
                  <span class="tree-node-label" :title="node.label">{{ node.label }}</span>
                  <span v-if="data.type === 'device'" :class="['status-dot', data.status]"></span>
                </div>
              </div>
            </el-tree>
            <div
              v-if="manualDrag.active && manualDrag.moved"
              class="manual-drag-ghost"
              :style="{ left: `${manualDrag.x + 12}px`, top: `${manualDrag.y + 12}px` }"
            >
              {{ manualDrag.device?.label || '移动设备' }}
            </div>
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
                <el-option label="空闲" value="idle" />
                <el-option label="缝纫" value="working" />
                <el-option label="关机" value="offline" />
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

        <el-card shadow="never" class="surface-card">
          <div class="action-row">
            <div class="soft-note">
              <i class="el-icon-info"></i>
              <span>当前范围：{{ treeScopeLabel }}，共 {{ filteredDevices.length }} 台设备，未分组设备会以浅红底提示。</span>
            </div>
            <div class="action-group">
              <el-button icon="el-icon-share" :disabled="selectedDeviceIds.length === 0" @click="openMoveDialog">
                批量移动分组
              </el-button>
              <el-button
                type="danger"
                plain
                icon="el-icon-delete"
                :disabled="selectedDeviceIds.length === 0"
                @click="confirmBatchDeleteDevices"
              >
                批量删除设备
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
            <el-table-column label="设备名称" min-width="130">
              <template slot-scope="{ row }">
                {{ formatDeviceName(row) }}
              </template>
            </el-table-column>
            <el-table-column prop="initialName" label="初始名称" min-width="130" />
            <el-table-column prop="employeeCode" label="员工工号" width="110" />
            <el-table-column prop="employeeName" label="员工姓名" width="110" />
            <el-table-column prop="code" label="设备编号" min-width="130" />
            <el-table-column prop="type" label="设备类型" width="110" />
            <!-- <el-table-column prop="model" label="机型" width="110" /> -->
            <el-table-column prop="mainboardSn" label="主板编号" min-width="140" />
            <el-table-column prop="identifiedBy" label="识别方式" width="110">
              <template slot-scope="{ row }">
                {{ formatIdentifiedBy(row.identifiedBy) }}
              </template>
            </el-table-column>
            <el-table-column label="状态" width="100" align="center">
              <template slot-scope="{ row }">
                <span :class="['status-pill', getStatusTone(row.status)]">
                  {{ getStatusLabel(row.status) }}
                </span>
              </template>
            </el-table-column>
            <el-table-column prop="currentPatternName" label="当前花型" min-width="160" show-overflow-tooltip />
            <el-table-column prop="alarmCode" label="报警码" width="90" align="center" />
            <el-table-column label="速度" width="120" align="center">
              <template slot-scope="{ row }">
                {{ row.currentSpeed || 0 }}/{{ row.maxSpeedValue || 0 }}
              </template>
            </el-table-column>
            <el-table-column label="产量" width="110" align="center">
              <template slot-scope="{ row }">
                {{ row.productionCurrent || 0 }}/{{ row.productionTotal || 0 }}
              </template>
            </el-table-column>
            <el-table-column prop="lastProtocolAt" label="最近协议时间" width="170" />
            <el-table-column prop="ip" label="设备 IP" width="130" />
            <el-table-column label="分组" min-width="150">
              <template slot-scope="{ row }">
                <span :class="row.groupId ? '' : 'danger-text'">
                  {{ getDeviceGroupName(row) || '未分组' }}
                </span>
              </template>
            </el-table-column>
            <el-table-column prop="createTime" label="添加时间" width="170" />
            <el-table-column prop="remark" label="备注" min-width="120" show-overflow-tooltip />
            <el-table-column label="操作" width="240" fixed="right" align="center">
              <template slot-scope="{ row }">
                <el-button size="small" @click="openEditDialog(row)">编辑</el-button>
                <el-button size="small" @click="pingDevice(row.ip)">Ping</el-button>
                <el-button size="small" type="danger" @click="confirmDeleteDevice(row)">删除</el-button>
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
        </el-card>
      </div>
    </div>

    <el-dialog
      title="编辑设备"
      :visible.sync="editDialogVisible"
      width="620px"
    >
      <el-form ref="editFormRef" :model="editForm" :rules="currentEditRules" label-width="110px">
        <el-form-item label="设备编号" prop="code">
          <el-input v-model="editForm.code" disabled />
        </el-form-item>
        <el-form-item label="设备名称" prop="name">
          <el-input v-model.trim="editForm.name" />
        </el-form-item>
        <el-form-item label="初始名称" prop="initialName">
          <el-input v-model="editForm.initialName" disabled />
        </el-form-item>
        <el-form-item label="设备类型" prop="type">
          <div class="device-type-control">
            <el-select v-model="editForm.type" filterable placeholder="请选择设备类型">
              <el-option
                v-for="type in deviceTypeOptions"
                :key="type"
                :label="type"
                :value="type"
              />
            </el-select>
            <el-button icon="el-icon-plus" @click="openDeviceTypeDialog">添加类型</el-button>
          </div>
        </el-form-item>
        <!-- <el-form-item label="设备型号" prop="model">
          <el-select v-model="editForm.model">
            <el-option label="BM-2000" value="BM-2000" />
            <el-option label="BM-3000" value="BM-3000" />
            <el-option label="BM-5000" value="BM-5000" />
          </el-select>
        </el-form-item> -->
        <el-form-item label="IP地址" prop="ip">
          <el-input v-model="editForm.ip" disabled placeholder="192.168.1.xxx" />
        </el-form-item>
        <el-form-item label="员工工号" prop="employeeCode">
          <el-input v-model="editForm.employeeCode" />
        </el-form-item>
        <el-form-item label="员工姓名" prop="employeeName">
          <el-input v-model="editForm.employeeName" />
        </el-form-item>
        <el-form-item label="主板编号" prop="mainboardSn">
          <el-input v-model="editForm.mainboardSn" disabled />
        </el-form-item>
        <el-form-item label="备注" prop="remark">
          <el-input v-model="editForm.remark" type="textarea" :rows="3" />
        </el-form-item>
        <el-form-item label="设备分组" prop="groupId">
          <el-select v-model="editForm.groupId" clearable placeholder="未分组" style="width: 100%;">
            <el-option
              v-for="item in groupTreeOptions"
              :key="item.id"
              :label="item.name"
              :value="item.id"
            >
              <div class="group-option" :style="{ paddingLeft: `${item.level * 18}px` }">
                <i :class="item.hasChildren ? 'el-icon-folder-opened' : 'el-icon-folder'"></i>
                <span>{{ item.name }}</span>
              </div>
            </el-option>
          </el-select>
        </el-form-item>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="editDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="saveDevice">保存</el-button>
      </span>
    </el-dialog>

    <el-dialog
      title="新增设备类型"
      :visible.sync="showDeviceTypeDialog"
      width="420px"
      @closed="resetDeviceTypeForm"
    >
      <el-form label-width="90px">
        <el-form-item label="类型名称">
          <el-input
            v-model.trim="deviceTypeForm.value"
            placeholder="请输入设备类型"
            @keyup.enter.native="handleCreateDeviceType"
          />
        </el-form-item>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="showDeviceTypeDialog = false">取消</el-button>
        <el-button type="primary" :loading="creatingDeviceType" @click="handleCreateDeviceType">确定</el-button>
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
              v-for="item in groupTreeOptions"
              :key="item.id"
              :label="item.name"
              :value="item.id"
            >
              <div class="group-option" :style="{ paddingLeft: `${item.level * 18}px` }">
                <i :class="item.hasChildren ? 'el-icon-folder-opened' : 'el-icon-folder'"></i>
                <span>{{ item.name }}</span>
              </div>
            </el-option>
          </el-select>
        </el-form-item>
        <div class="dialog-tip">已选择 {{ selectedDeviceIds.length }} 台设备</div>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="moveDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="moving" @click="confirmMoveDevices">确定移动</el-button>
      </span>
    </el-dialog>

    <ul
      v-if="contextMenu.visible"
      class="group-context-menu"
      :style="{ left: `${contextMenu.x}px`, top: `${contextMenu.y}px` }"
      @click.stop
      @contextmenu.prevent
    >
      <template v-if="contextMenu.node && contextMenu.node.type === 'device'">
        <li class="danger" @click="handleContextMenuAction('removeDeviceFromGroup')">删除设备</li>
        <li @click="handleContextMenuAction('renameDevice')">重命名</li>
      </template>
      <template v-else>
        <li @click="handleContextMenuAction('addRoot')">新增分组</li>
      </template>
      <template v-if="contextMenu.node && isGroupContextNode(contextMenu.node)">
        <template v-if="canModifyGroup(contextMenu.node)">
          <li @click="handleContextMenuAction('addSibling')">新增同级分组</li>
        </template>
        <li @click="handleContextMenuAction('addChild')">新增子分组</li>
        <li :class="{ disabled: !canMoveSelectedToGroup(contextMenu.node) }" @click="handleContextMenuAction('moveSelectedHere')">移动已选设备到当前组</li>
        <template v-if="canModifyGroup(contextMenu.node)">
          <li :class="{ disabled: !canMoveUp(contextMenu.node) }" @click="handleContextMenuAction('moveUp')">上移</li>
          <li :class="{ disabled: !canMoveDown(contextMenu.node) }" @click="handleContextMenuAction('moveDown')">下移</li>
          <li @click="handleContextMenuAction('edit')">重命名</li>
          <li class="danger" @click="handleContextMenuAction('delete')">删除分组</li>
        </template>
      </template>
      <li @click="handleContextMenuAction('refresh')">刷新</li>
    </ul>
  </div>
</template>

<script>
const DEFAULT_DEVICE_TYPE = '电控类型'

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
      showDeviceTypeDialog: false,
      creatingDeviceType: false,
      deviceTypeOptions: [DEFAULT_DEVICE_TYPE],
      deviceTypeForm: {
        value: ''
      },
      contextMenu: {
        visible: false,
        x: 0,
        y: 0,
        node: null
      },
      draggingDeviceNode: null,
      manualDrag: {
        active: false,
        moved: false,
        device: null,
        targetKey: '',
        targetNode: null,
        startX: 0,
        startY: 0,
        x: 0,
        y: 0
      },
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
        code: '',
        name: '',
        initialName: '',
        type: DEFAULT_DEVICE_TYPE,
        model: '',
        ip: '',
        employeeCode: '',
        employeeName: '',
        mainboardSn: '',
        groupId: null,
        remark: ''
      },
      editRules: {
        name: [{ required: true, message: '请输入设备名称', trigger: 'blur' }],
        type: [{ required: true, message: '请选择设备类型', trigger: 'change' }]
      }
    }
  },
  computed: {
    currentEditRules() {
      return this.editRules
    },
    groupNameMap() {
      return new Map((this.groups || []).map(group => [Number(group.id), group.name]))
    },
    groupTreeOptions() {
      return this.flattenGroupTree(this.groupTree)
    },
    displayGroupTree() {
      const groupedDevices = new Map()
      const ungroupedDevices = []
      this.devices.forEach(device => {
        const groupId = Number(device.groupId || 0)
        if (groupId > 0) {
          if (!groupedDevices.has(groupId)) {
            groupedDevices.set(groupId, [])
          }
          groupedDevices.get(groupId).push(device)
          return
        }
        ungroupedDevices.push(device)
      })

      const toDeviceNode = device => ({
        id: `device-${device.id || device.ID}`,
        _nodeKey: `device-${device.id || device.ID}`,
        type: 'device',
        deviceId: device.id || device.ID,
        groupId: device.groupId || null,
        name: device.name || device.displayName || '',
        label: this.formatDeviceName(device),
        status: device.status || 'offline'
      })

      const sortDevices = list => [...list].sort((a, b) => {
        const sortDiff = Number(a.sortOrder || 0) - Number(b.sortOrder || 0)
        if (sortDiff !== 0) return sortDiff
        return String(a.code || '').localeCompare(String(b.code || ''))
      })

      const attachDeviceNodes = (nodes = []) => {
        return nodes.map(node => {
          const childGroups = attachDeviceNodes(node.children || [])
          const deviceNodes = sortDevices(groupedDevices.get(Number(node.id || 0)) || []).map(toDeviceNode)
          return {
            ...node,
            _nodeKey: `group-${node.id}`,
            type: 'group',
            label: node.name,
            children: [...childGroups, ...deviceNodes]
          }
        })
      }

      const groupNodes = attachDeviceNodes(this.groupTree)
      const totalNode = groupNodes.length === 1 && this.isTotalGroupNode(groupNodes[0])
        ? {
            ...groupNodes[0],
            _nodeKey: `group-${groupNodes[0].id}`,
            label: '总分组',
            type: 'group'
          }
        : {
            id: 'all',
            _nodeKey: 'group-all',
            label: '总分组',
            type: 'group',
            children: groupNodes,
            isVirtual: true
          }
      return [
        {
          id: 'ungrouped',
          _nodeKey: 'group-ungrouped',
          label: '未分组设备',
          type: 'group',
          children: sortDevices(ungroupedDevices).map(toDeviceNode),
          isVirtual: true
        },
        totalNode
      ]
    },
    treeScopeLabel() {
      if (this.treeSelection.mode === 'device') {
        return this.treeSelection.label || '单台设备'
      }
      if (this.treeSelection.mode === 'group') {
        return this.treeSelection.label || '指定分组'
      }
      if (this.treeSelection.mode === 'ungrouped') {
        return '未分组设备'
      }
      return '总分组'
    },
    filteredDevices() {
      if (this.treeSelection.mode === 'ungrouped') {
        return this.devices.filter(item => !item.groupId)
      }
      if (this.treeSelection.mode === 'device' && this.treeSelection.deviceId) {
        return this.devices.filter(item => Number(item.id || item.ID) === Number(this.treeSelection.deviceId))
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
          this.$refs.groupTreeRef?.setCurrentKey(this.resolveTreeSelectionKey())
        })
      }
    }
  },
  mounted() {
    document.addEventListener('click', this.hideContextMenu)
    window.addEventListener('blur', this.hideContextMenu)
    this.initPage()
    this.refreshTimer = setInterval(() => {
      this.autoRefreshDevices()
    }, 5000)
  },
  beforeDestroy() {
    document.removeEventListener('click', this.hideContextMenu)
    window.removeEventListener('blur', this.hideContextMenu)
    this.removeManualDragListeners()
    if (this.refreshTimer) {
      clearInterval(this.refreshTimer)
      this.refreshTimer = null
    }
  },
  methods: {
    async initPage() {
      await Promise.all([this.loadGroupTree(), this.loadDevices(), this.loadDeviceTypes()])
    },
    async loadDeviceTypes() {
      try {
        const res = await this.$axios.get('/device/types')
        if (res.code === 0 && Array.isArray(res.data)) {
          const values = res.data
            .map(item => (typeof item === 'string' ? item : item?.value))
            .filter(Boolean)
          this.deviceTypeOptions = values.length ? values : [DEFAULT_DEVICE_TYPE]
        }
      } catch (error) {
        console.error('加载设备类型失败', error)
      }
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
            this.$refs.groupTreeRef?.setCurrentKey(this.resolveTreeSelectionKey())
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
      return String(data?.label || data?.name || '').toLowerCase().includes(value.toLowerCase())
    },
    resolveTreeSelectionKey() {
      if (this.treeSelection.mode === 'device' && this.treeSelection.deviceId) {
        return `device-${this.treeSelection.deviceId}`
      }
      if (this.treeSelection.mode === 'group' && this.treeSelection.groupId) {
        return `group-${this.treeSelection.groupId}`
      }
      if (this.treeSelection.mode === 'ungrouped') {
        return 'group-ungrouped'
      }
      return 'group-all'
    },
    isEditableGroupNode(data) {
      return data && data.type !== 'device' && !data.isVirtual && data.id !== 'all' && data.id !== 'ungrouped' && !this.isTotalGroupNode(data)
    },
    isGroupContextNode(data) {
      return data && data.type !== 'device' && !data.isVirtual && data.id !== 'all' && data.id !== 'ungrouped'
    },
    isTotalGroupNode(data) {
      const label = String(data?.label || data?.name || '').trim()
      const id = String(data?.id || '')
      return label === '总分组' || label === '全部设备' || id === 'all'
    },
    canDeleteGroup(data) {
      return this.canModifyGroup(data)
    },
    canModifyGroup(data) {
      return this.isEditableGroupNode(data)
    },
    canMoveSelectedToGroup(data) {
      return this.isGroupContextNode(data) && Number(data?.id || 0) > 0 && this.selectedDeviceIds.length > 0
    },
    allowTreeDrag(node) {
      return node?.data?.type === 'device'
    },
    allowTreeDrop(draggingNode, dropNode, type) {
      const dragging = draggingNode?.data
      if (!dragging) {
        return false
      }
      if (dragging.type !== 'device') {
        return false
      }
      const targetGroupId = this.resolveDropTargetGroupId(dropNode, type)
      return targetGroupId === null || targetGroupId > 0
    },
    resolveDropTargetGroupId(dropNode, dropType) {
      const target = dropNode?.data
      if (!target) {
        return undefined
      }
      if (this.isUngroupedNode(target)) {
        return null
      }
      if (target.type === 'device') {
        const groupId = Number(target.groupId || 0)
        return groupId > 0 ? groupId : null
      }
      if (this.isGroupContextNode(target) && Number(target.id || 0) > 0) {
        return Number(target.id)
      }
      if (dropType !== 'inner') {
        const parent = dropNode?.parent?.data
        if (this.isUngroupedNode(parent)) {
          return null
        }
        if (this.isGroupContextNode(parent) && Number(parent.id || 0) > 0) {
          return Number(parent.id)
        }
      }
      return undefined
    },
    async handleTreeNodeDrop(draggingNode, dropNode, dropType) {
      const dragging = draggingNode?.data
      if (!dragging || dragging.type !== 'device') {
        return
      }
      const deviceId = Number(dragging.deviceId || 0)
      const targetGroupId = this.resolveDropTargetGroupId(dropNode, dropType)
      if (!deviceId || targetGroupId === undefined || (targetGroupId !== null && !targetGroupId)) {
        await Promise.all([this.loadDevices(), this.loadGroupTree()])
        return
      }
      if (Number(dragging.groupId || 0) === Number(targetGroupId || 0)) {
        this.$message.info('设备已在目标分组')
        await Promise.all([this.loadDevices(), this.loadGroupTree()])
        return
      }
      try {
        const res = await this.$axios.post('/device/move', {
          deviceIds: [deviceId],
          groupId: targetGroupId
        })
        if (res.code === 0) {
          this.$message.success(targetGroupId ? '设备已移动到目标分组' : '设备已移出分组')
          await Promise.all([this.loadDevices(), this.loadGroupTree()])
        } else {
          this.$message.error(res.message || '拖动设备失败')
        }
      } catch (error) {
        console.error('拖动设备失败', error)
        this.$message.error('拖动设备失败')
      }
    },
    handleNativeDeviceDragStart(event, data) {
      if (data?.type !== 'device') {
        event.preventDefault()
        return
      }
      this.draggingDeviceNode = data
      event.dataTransfer.effectAllowed = 'move'
      event.dataTransfer.setData('application/x-boer-device-id', String(data.deviceId || ''))
      event.dataTransfer.setData('text/plain', String(data.deviceId || ''))
    },
    handleNativeDeviceDragEnd() {
      this.draggingDeviceNode = null
    },
    handleNativeDeviceDragOver(event, data) {
      const targetGroupId = this.resolveNativeDropTargetGroupId(data)
      if (targetGroupId === undefined) return
      event.dataTransfer.dropEffect = 'move'
    },
    resolveNativeDropTargetGroupId(target) {
      if (!target) return undefined
      if (this.isUngroupedNode(target)) return null
      if (target.type === 'device') {
        const groupId = Number(target.groupId || 0)
        return groupId > 0 ? groupId : null
      }
      if (this.isGroupContextNode(target) && Number(target.id || 0) > 0) {
        return Number(target.id)
      }
      return undefined
    },
    async handleNativeDeviceDrop(event, target) {
      const dragging = this.draggingDeviceNode
      if (!dragging || dragging.type !== 'device') return
      const deviceId = Number(dragging.deviceId || event.dataTransfer.getData('application/x-boer-device-id') || 0)
      const targetGroupId = this.resolveNativeDropTargetGroupId(target)
      if (!deviceId || targetGroupId === undefined) {
        await Promise.all([this.loadDevices(), this.loadGroupTree()])
        return
      }
      if (Number(dragging.groupId || 0) === Number(targetGroupId || 0)) {
        this.$message.info('设备已在目标分组')
        await Promise.all([this.loadDevices(), this.loadGroupTree()])
        return
      }
      try {
        const res = await this.$axios.post('/device/move', {
          deviceIds: [deviceId],
          groupId: targetGroupId
        })
        if (res.code === 0) {
          this.$message.success(targetGroupId ? '设备已移动到目标分组' : '设备已移出分组')
          await Promise.all([this.loadDevices(), this.loadGroupTree()])
        } else {
          this.$message.error(res.message || '拖动设备失败')
        }
      } catch (error) {
        console.error('拖动设备失败', error)
        this.$message.error('拖动设备失败')
      } finally {
        this.draggingDeviceNode = null
      }
    },
    handleManualDeviceMouseDown(event, data) {
      if (event.button !== 0 || data?.type !== 'device') {
        return
      }
      this.hideContextMenu()
      this.manualDrag = {
        active: true,
        moved: false,
        device: data,
        targetKey: '',
        targetNode: null,
        startX: event.clientX,
        startY: event.clientY,
        x: event.clientX,
        y: event.clientY
      }
      document.addEventListener('mousemove', this.handleManualDeviceMouseMove)
      document.addEventListener('mouseup', this.handleManualDeviceMouseUp)
    },
    handleManualDeviceMouseMove(event) {
      if (!this.manualDrag.active) return
      const dx = Math.abs(event.clientX - this.manualDrag.startX)
      const dy = Math.abs(event.clientY - this.manualDrag.startY)
      if (!this.manualDrag.moved && dx + dy < 4) {
        return
      }
      event.preventDefault()
      if (!this.manualDrag.moved) {
        this.manualDrag.moved = true
        document.body.classList.add('device-manual-dragging')
      }
      this.manualDrag.x = event.clientX
      this.manualDrag.y = event.clientY

      const target = this.findManualDropTargetFromPoint(event.clientX, event.clientY)
      this.manualDrag.targetNode = target
      this.manualDrag.targetKey = target?._nodeKey || ''
    },
    async handleManualDeviceMouseUp(event) {
      if (!this.manualDrag.active) return
      const dragState = { ...this.manualDrag }
      this.removeManualDragListeners()
      this.resetManualDrag()

      if (!dragState.moved) {
        return
      }
      event.preventDefault()
      event.stopPropagation()
      await this.moveDraggedDeviceToTarget(dragState.device, dragState.targetNode)
    },
    removeManualDragListeners() {
      document.removeEventListener('mousemove', this.handleManualDeviceMouseMove)
      document.removeEventListener('mouseup', this.handleManualDeviceMouseUp)
      document.body.classList.remove('device-manual-dragging')
    },
    resetManualDrag() {
      this.manualDrag = {
        active: false,
        moved: false,
        device: null,
        targetKey: '',
        targetNode: null,
        startX: 0,
        startY: 0,
        x: 0,
        y: 0
      }
    },
    findManualDropTargetFromPoint(x, y) {
      const element = document.elementFromPoint(x, y)
      const nodeElement = element?.closest?.('[data-tree-node-key]')
      const key = nodeElement?.getAttribute('data-tree-node-key')
      if (!key) return null
      const node = this.findDisplayTreeNodeByKey(key)
      return this.resolveManualDropTargetGroupId(node) !== undefined ? node : null
    },
    findDisplayTreeNodeByKey(key, nodes = this.displayGroupTree) {
      const stack = [...(nodes || [])]
      while (stack.length) {
        const current = stack.shift()
        if (!current) continue
        if (current._nodeKey === key) return current
        if (current.children?.length) {
          stack.push(...current.children)
        }
      }
      return null
    },
    isManualDragDropTarget(data) {
      return this.manualDrag.active && this.manualDrag.moved && this.resolveManualDropTargetGroupId(data) !== undefined
    },
    resolveManualDropTargetGroupId(target) {
      if (!target) return undefined
      if (this.isUngroupedNode(target)) return null
      if (target.type === 'device') {
        const groupId = Number(target.groupId || 0)
        return groupId > 0 ? groupId : null
      }
      if (this.isGroupContextNode(target) && Number(target.id || 0) > 0) {
        return Number(target.id)
      }
      return undefined
    },
    async moveDraggedDeviceToTarget(device, target) {
      const deviceId = Number(device?.deviceId || 0)
      const targetGroupId = this.resolveManualDropTargetGroupId(target)
      if (!deviceId || targetGroupId === undefined) {
        await Promise.all([this.loadDevices(), this.loadGroupTree()])
        return
      }
      if (Number(device.groupId || 0) === Number(targetGroupId || 0)) {
        this.$message.info('设备已在目标分组')
        await Promise.all([this.loadDevices(), this.loadGroupTree()])
        return
      }
      try {
        const res = await this.$axios.post('/device/move', {
          deviceIds: [deviceId],
          groupId: targetGroupId
        })
        if (res.code === 0) {
          this.$message.success(targetGroupId ? '设备已移动到目标分组' : '设备已移出分组')
          await Promise.all([this.loadDevices(), this.loadGroupTree()])
        } else {
          this.$message.error(res.message || '拖动设备失败')
        }
      } catch (error) {
        console.error('拖动设备失败', error)
        this.$message.error('拖动设备失败')
      }
    },
    isUngroupedNode(data) {
      const label = String(data?.label || data?.name || '')
      const id = String(data?.id || '')
      return label.includes('未分组') || id === 'ungrouped'
    },
    getTreeNodeIcon(data) {
      if (data?.type === 'device') return 'el-icon-monitor'
      if (data?.id === 'all') return 'el-icon-menu'
      if (data?.id === 'ungrouped') return 'el-icon-folder'
      return data?.children?.length ? 'el-icon-folder-opened' : 'el-icon-folder'
    },
    normalizeGroupList(groups = []) {
      return groups.map(item => ({
        ...item,
        id: item.id || item.ID,
        parentId: item.parentId || item.ParentID || item.parent?.id || null
      }))
    },
    flattenGroupTree(nodes = [], level = 0) {
      const result = []
      nodes.forEach(node => {
        const children = Array.isArray(node.children) ? node.children : []
        result.push({
          id: node.id,
          name: node.name,
          level,
          hasChildren: children.length > 0
        })
        result.push(...this.flattenGroupTree(children, level + 1))
      })
      return result
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
      if (data.id === 'all') {
        this.setTreeScope('all')
        this.hideContextMenu()
        return
      }
      if (data.id === 'ungrouped') {
        this.setTreeScope('ungrouped')
        this.hideContextMenu()
        return
      }
      if (data.type === 'device') {
        this.treeSelection = {
          mode: 'device',
          groupId: data.groupId || null,
          deviceId: data.deviceId,
          label: data.label
        }
        this.hideContextMenu()
        return
      }
      this.treeSelection = {
        mode: 'group',
        groupId: data.id,
        label: data.label || data.name
      }
      this.hideContextMenu()
    },
    handleTreeNodeContextMenu(event, data) {
      if (!data) {
        return
      }
      event.preventDefault()
      this.handleTreeNodeClick(data)
      this.contextMenu = {
        visible: true,
        x: Math.min(event.clientX, window.innerWidth - 180),
        y: Math.min(event.clientY, window.innerHeight - 240),
        node: data
      }
    },
    hideContextMenu() {
      if (!this.contextMenu.visible) {
        return
      }
      this.contextMenu.visible = false
      this.contextMenu.node = null
    },
    handleContextMenuAction(action) {
      const node = this.contextMenu.node
      if (node?.type === 'device') {
        this.hideContextMenu()
        if (action === 'removeDeviceFromGroup') {
          this.removeDeviceFromGroup(node)
          return
        }
        if (action === 'renameDevice') {
          this.renameDevice(node)
          return
        }
        if (action === 'refresh') {
          Promise.all([this.loadDevices(), this.loadGroupTree()])
        }
        return
      }

      const group = node
      if ((action !== 'addRoot' && action !== 'refresh') && !group) {
        return
      }
      if ((action !== 'addRoot' && action !== 'refresh') && !this.isGroupContextNode(group)) {
        return
      }
      if (['addSibling', 'moveUp', 'moveDown', 'edit', 'delete'].includes(action) && !this.canModifyGroup(group)) {
        return
      }
      if ((action === 'moveUp' && !this.canMoveUp(group)) || (action === 'moveDown' && !this.canMoveDown(group))) {
        return
      }
      this.hideContextMenu()
      if (action === 'addRoot') {
        this.createGroup(null)
        return
      }
      if (action === 'addSibling') {
        if (!this.canModifyGroup(group)) return
        this.addSibling(group)
        return
      }
      if (action === 'addChild') {
        this.addChild(group)
        return
      }
      if (action === 'moveSelectedHere') {
        this.moveSelectedDevicesToGroup(group)
        return
      }
      if (action === 'moveUp') {
        if (!this.canMoveUp(group)) return
        this.moveUp(group)
        return
      }
      if (action === 'moveDown') {
        if (!this.canMoveDown(group)) return
        this.moveDown(group)
        return
      }
      if (action === 'edit') {
        this.editGroup(group)
        return
      }
      if (action === 'delete') {
        if (!this.canDeleteGroup(group)) {
          this.$message.warning('总分组不能删除')
          return
        }
        this.deleteGroup(group)
        return
      }
      if (action === 'refresh') {
        this.loadGroupTree()
      }
    },
    getDeviceRowByNode(node) {
      const deviceId = Number(node?.deviceId || 0)
      if (!deviceId) return null
      return this.devices.find(item => Number(item.id || item.ID) === deviceId) || null
    },
    getDevicePromptName(node) {
      const row = this.getDeviceRowByNode(node)
      return String(row?.name || row?.displayName || node?.label || '').trim()
    },
    async removeDeviceFromGroup(node) {
      const deviceId = Number(node?.deviceId || 0)
      if (!deviceId) {
        this.$message.warning('设备参数错误')
        return
      }
      try {
        const res = await this.$axios.post('/device/move', {
          deviceIds: [deviceId],
          groupId: null
        })
        if (res.code === 0) {
          this.$message.success('设备已移到未分组设备')
          await Promise.all([this.loadDevices(), this.loadGroupTree()])
        } else {
          this.$message.error(res.message || '删除设备失败')
        }
      } catch (error) {
        console.error('删除设备失败', error)
        this.$message.error('删除设备失败')
      }
    },
    async renameDevice(node) {
      const deviceId = Number(node?.deviceId || 0)
      if (!deviceId) {
        this.$message.warning('设备参数错误')
        return
      }
      try {
        const { value } = await this.$prompt('请输入设备名称', '重命名设备', {
          confirmButtonText: '确定',
          cancelButtonText: '取消',
          inputValue: this.getDevicePromptName(node),
          inputValidator: value => String(value || '').trim().length > 0,
          inputErrorMessage: '设备名称不能为空'
        })
        const name = String(value || '').trim()
        const res = await this.$axios.put(`/device/${deviceId}`, { name })
        if (res.code === 0) {
          this.$message.success('设备名称已更新')
          await Promise.all([this.loadDevices(), this.loadGroupTree()])
        } else {
          this.$message.error(res.message || '重命名设备失败')
        }
      } catch (error) {
        if (error !== 'cancel') {
          console.error('重命名设备失败', error)
          this.$message.error('重命名设备失败')
        }
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
        label: '总分组'
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
    getDeviceGroupName(row) {
      const directName = String(row?.group || row?.groupName || '').trim()
      if (directName) {
        return directName
      }
      const groupId = Number(row?.groupId || 0)
      return groupId > 0 ? (this.groupNameMap.get(groupId) || '') : ''
    },
    handleSelectionChange(rows) {
      this.selectedDeviceIds = rows.map(item => item.id || item.ID).filter(Boolean)
    },
    openMoveDialog() {
      this.moveTargetGroupId = null
      this.moveDialogVisible = true
    },
    async moveSelectedDevicesToGroup(group) {
      if (!this.canMoveSelectedToGroup(group)) {
        this.$message.warning('请先勾选设备')
        return
      }
      const targetGroupId = Number(group?.id || 0)
      this.moving = true
      try {
        const res = await this.$axios.post('/device/move', {
          deviceIds: this.selectedDeviceIds,
          groupId: targetGroupId
        })
        if (res.code === 0) {
          this.$message.success('分组移动成功')
          await Promise.all([this.loadDevices(), this.loadGroupTree()])
        }
      } catch (error) {
        console.error('移动设备到当前分组失败', error)
      } finally {
        this.moving = false
      }
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
        code: row.code || '',
        name: row.name || '',
        initialName: row.initialName || '',
        type: row.type || DEFAULT_DEVICE_TYPE,
        model: row.model || '',
        ip: row.ip || '',
        employeeCode: row.employeeCode || '',
        employeeName: row.employeeName || '',
        mainboardSn: row.mainboardSn || '',
        groupId: row.groupId ?? null,
        remark: row.remark || ''
      }
    },
    openDeviceTypeDialog() {
      this.deviceTypeForm.value = ''
      this.showDeviceTypeDialog = true
    },
    resetDeviceTypeForm() {
      this.creatingDeviceType = false
      this.deviceTypeForm.value = ''
    },
    async handleCreateDeviceType() {
      const value = String(this.deviceTypeForm.value || '').trim()
      if (!value) {
        this.$message.warning('设备类型不能为空')
        return
      }
      this.creatingDeviceType = true
      try {
        const res = await this.$axios.post('/device/type-summary', { value })
        if (res.code === 0) {
          this.$message.success('设备类型已新增')
          this.showDeviceTypeDialog = false
          await this.loadDeviceTypes()
          this.editForm.type = res.data?.value || value
        } else {
          this.$message.error(res.message || '设备类型新增失败')
        }
      } catch (error) {
        console.error('新增设备类型失败', error)
        this.$message.error('设备类型新增失败')
      } finally {
        this.creatingDeviceType = false
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
          type: this.editForm.type,
          employeeCode: this.editForm.employeeCode,
          employeeName: this.editForm.employeeName,
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
        offline: 'info',
        working: 'primary',
        idle: 'warning'
      }
      return map[status] || 'info'
    },
    formatIdentifiedBy(value) {
      const map = {
        protocol: '协议编号',
        mainboard: '主板号',
        'ip-pending': 'IP占位'
      }
      return map[value] || '-'
    },
    formatDeviceName(row) {
      const displayName = String(row?.displayName || '').trim()
      const initialName = String(row?.initialName || '').trim()
      const code = String(row?.code || '').trim()
      const name = String(row?.name || '').trim()
      return displayName || initialName || code || name || '-'
    },
    getStatusLabel(status) {
      const map = {
        offline: '关机',
        working: '缝纫',
        idle: '空闲'
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
    },
    async confirmDeleteDevice(row) {
      const deviceId = row?.id || row?.ID
      if (!deviceId) {
        this.$message.warning('设备ID无效')
        return
      }
      try {
        await this.$confirm(
          `确定要删除设备“${row.name || row.code || deviceId}”吗？删除后将从设备列表中移除。`,
          '警告',
          { type: 'warning' }
        )
        const res = await this.$axios.delete(`/device/${deviceId}/hard`)
        if (res.code === 0) {
          this.$message.success(res.message || '设备已删除')
          await Promise.all([this.loadDevices(), this.loadGroupTree()])
        }
      } catch (error) {
        if (error !== 'cancel') {
          console.error('删除设备失败', error)
        }
      }
    },
    async confirmBatchDeleteDevices() {
      if (!this.selectedDeviceIds.length) {
        this.$message.warning('请先选择设备')
        return
      }
      try {
        await this.$confirm(
          `确定要删除选中的 ${this.selectedDeviceIds.length} 台设备吗？删除后将从设备列表中移除。`,
          '警告',
          { type: 'warning' }
        )
        const res = await this.$axios.delete('/device/batch/hard', {
          data: { ids: this.selectedDeviceIds }
        })
        if (res.code === 0) {
          this.$message.success(res.message || '设备已批量删除')
          await Promise.all([this.loadDevices(), this.loadGroupTree()])
        }
      } catch (error) {
        if (error !== 'cancel') {
          console.error('批量删除设备失败', error)
        }
      }
    },
    createGroup(parentId) {
      const title = parentId ? '新增子分组' : '新增分组'
      this.$prompt('请输入分组名称', title, {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        inputValidator: value => {
          if (!value || !value.trim()) return '分组名称不能为空'
          if ([...value.trim()].length > 50) return '分组名称不能超过50个字符'
          return true
        }
      }).then(async ({ value }) => {
        try {
          const payload = { name: value.trim() }
          if (parentId) {
            payload.parentId = parentId
          }
          await this.$axios.post('/group', payload)
          this.$message.success('创建成功')
          await this.loadGroupTree()
        } catch (error) {
          console.error('创建设备分组失败', error)
        }
      }).catch(() => {})
    },
    addSibling(group) {
      this.createGroup(group.parentId || null)
    },
    addChild(group) {
      this.createGroup(group.id)
    },
    editGroup(group) {
      this.$prompt('请输入新的分组名称', '重命名分组', {
        confirmButtonText: '确定',
        cancelButtonText: '取消',
        inputValue: group.name,
        inputValidator: value => {
          if (!value || !value.trim()) return '分组名称不能为空'
          if ([...value.trim()].length > 50) return '分组名称不能超过50个字符'
          return true
        }
      }).then(async ({ value }) => {
        try {
          await this.$axios.put(`/group/${group.id}`, { name: value.trim() })
          this.$message.success('修改成功')
          await Promise.all([this.loadGroupTree(), this.loadDevices()])
        } catch (error) {
          console.error('修改分组失败', error)
        }
      }).catch(() => {})
    },
    findNodeContext(targetId, nodes = this.groupTree, parent = null) {
      for (let i = 0; i < nodes.length; i++) {
        const current = nodes[i]
        if (current.id === targetId) {
          return {
            parent,
            siblings: nodes,
            index: i
          }
        }
        if (current.children && current.children.length > 0) {
          const found = this.findNodeContext(targetId, current.children, current)
          if (found) return found
        }
      }
      return null
    },
    canMoveUp(group) {
      const context = this.findNodeContext(group.id)
      return !!context && context.index > 0
    },
    canMoveDown(group) {
      const context = this.findNodeContext(group.id)
      return !!context && context.index < context.siblings.length - 1
    },
    async persistSort(siblings) {
      const payload = siblings.map((item, index) => ({
        id: item.id,
        sortOrder: index + 1
      }))
      const res = await this.$axios.post('/group/sort', payload)
      if (res.code === 0) {
        this.$message.success('排序已更新')
      }
      await this.loadGroupTree()
    },
    async moveUp(group) {
      const context = this.findNodeContext(group.id)
      if (!context || context.index <= 0) return
      const { siblings, index } = context
      const current = siblings[index]
      siblings.splice(index, 1)
      siblings.splice(index - 1, 0, current)
      await this.persistSort(siblings)
    },
    async moveDown(group) {
      const context = this.findNodeContext(group.id)
      if (!context || context.index >= context.siblings.length - 1) return
      const { siblings, index } = context
      const current = siblings[index]
      siblings.splice(index, 1)
      siblings.splice(index + 1, 0, current)
      await this.persistSort(siblings)
    },
    async deleteGroup(group) {
      if (!this.canDeleteGroup(group)) {
        this.$message.warning('总分组不能删除')
        return
      }
      try {
        await this.$confirm(
          '确定要删除该分组吗？删除后该分组下设备将转为未分组，子分组会提升到当前层级。',
          '警告',
          { type: 'warning' }
        )
        const res = await this.$axios.delete(`/group/${group.id}`)
        this.$message.success(res.msg || '删除成功')
        if (this.treeSelection.mode === 'group' && Number(this.treeSelection.groupId) === Number(group.id)) {
          this.setTreeScope('all')
        }
        await Promise.all([this.loadGroupTree(), this.loadDevices()])
      } catch (error) {
        if (error !== 'cancel') {
          console.error('删除分组失败', error)
        }
      }
    }
  }
}
</script>

<style scoped>
.panel-shell {
  padding: 8px 10px;
  height: 100%;
  min-height: 0;
  display: flex;
  flex-direction: column;
}

.panel-header {
  gap: 8px;
  margin-bottom: 8px;
}

::v-deep .row-ungrouped td {
  background: rgba(239, 90, 90, 0.045);
}

.panel-actions {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 4px;
}

.device-type-control {
  display: flex;
  align-items: center;
  gap: 8px;
}

.device-type-control .el-select {
  flex: 1 1 auto;
  min-width: 0;
}

.device-type-control .el-button {
  flex: 0 0 auto;
}

.selection-bar {
  margin-top: 8px;
  margin-bottom: 8px;
  padding: 6px 8px;
  border-radius: 2px;
  background: #f7f9fc;
  border: 1px solid #e0e6ee;
  display: flex;
  align-items: center;
  gap: 8px;
}

.selection-label {
  color: #9099a5;
  font-size: 11px;
}

.selection-value {
  flex: 1;
  min-width: 0;
  color: #4c5768;
  font-weight: 600;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.tree-wrapper {
  flex: 1;
  min-height: 0;
  margin-top: 6px;
  border: 1px solid #dfe6ee;
  border-radius: 2px;
  padding: 6px;
  overflow: auto;
  background: #ffffff;
}

.tree-node {
  width: 100%;
  min-width: 0;
  display: flex;
  align-items: center;
  overflow: hidden;
  user-select: none;
}

.tree-node-device {
  cursor: grab;
}

.tree-node-device:active {
  cursor: grabbing;
}

.tree-node-drop-target {
  border-radius: 2px;
}

.tree-node-drop-hover {
  background: rgba(47, 109, 246, 0.12);
  box-shadow: inset 0 0 0 1px rgba(47, 109, 246, 0.38);
}

.manual-drag-ghost {
  position: fixed;
  z-index: 3000;
  max-width: 260px;
  padding: 6px 10px;
  border-radius: 3px;
  background: rgba(31, 41, 55, 0.92);
  color: #fff;
  font-size: 12px;
  line-height: 1.2;
  pointer-events: none;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.tree-node-main {
  width: 100%;
  display: inline-flex;
  align-items: center;
  gap: 8px;
  min-width: 0;
}

.tree-node-main.ungrouped {
  color: #d94a4a;
  font-weight: 700;
}

.tree-node-icon {
  color: #6d7e94;
}

.tree-node-label {
  flex: 1;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.status-dot {
  flex-shrink: 0;
  margin-left: auto;
  width: 8px;
  height: 8px;
  border-radius: 50%;
}

.status-dot.idle {
  background: #2fb46e;
}

.status-dot.working {
  background: #2f6df6;
}

.status-dot.offline {
  background: #8a98ad;
}

::v-deep .el-tree {
  background: transparent;
}

::v-deep .el-tree-node__content {
  height: 30px;
  border-radius: 2px;
  margin-bottom: 2px;
  padding-right: 4px;
}

::v-deep .el-tree-node__content:hover {
  background: #f5f8fc;
}

::v-deep .el-tree-node.is-current > .el-tree-node__content {
  background: #e8f2ff;
}

.group-option {
  display: flex;
  align-items: center;
  gap: 6px;
  min-width: 0;
}

.group-option i {
  color: #7a8599;
  font-size: 14px;
  flex: none;
}

.group-option span {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.group-context-menu {
  position: fixed;
  z-index: 3000;
  margin: 0;
  padding: 6px 0;
  list-style: none;
  min-width: 152px;
  background: #fff;
  border: 1px solid #e4e7ed;
  border-radius: 2px;
  box-shadow: 0 6px 18px rgba(0, 0, 0, 0.15);
}

.group-context-menu li {
  padding: 6px 12px;
  font-size: 12px;
  color: #303133;
  cursor: pointer;
  user-select: none;
}

.group-context-menu li:hover {
  background: #f5f7fa;
}

.group-context-menu li.danger {
  color: #f56c6c;
}

.group-context-menu li.disabled {
  color: #c0c4cc;
  cursor: not-allowed;
}

.group-context-menu li.disabled:hover {
  background: transparent;
}
</style>
