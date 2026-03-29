<template>
  <div class="device-tree-filter">
    <el-popover
      v-model="visible"
      placement="bottom-start"
      width="360"
      trigger="click"
      popper-class="device-tree-filter-popover"
    >
      <div class="filter-panel">
        <div class="filter-header">
          <div>
            <div class="filter-title">设备范围</div>
            <div class="filter-subtitle">支持按设备、分组快速筛选</div>
          </div>
          <el-button type="text" size="mini" @click="fetchDeviceTree">刷新</el-button>
        </div>

        <el-input
          v-model="keyword"
          size="small"
          placeholder="搜索设备或分组"
          prefix-icon="el-icon-search"
          clearable
        />

        <div class="selection-bar">
          <div class="selection-text">
            <span class="selection-label">当前选择</span>
            <span class="selection-value">{{ displayLabel }}</span>
          </div>
          <el-button type="text" size="mini" @click="clearSelection">清空</el-button>
        </div>

        <div class="tree-wrapper">
          <el-tree
            ref="deviceTree"
            :data="deviceTree"
            :props="treeProps"
            :filter-node-method="filterNode"
            node-key="_nodeKey"
            highlight-current
            default-expand-all
            @node-click="handleNodeClick"
          >
            <div slot-scope="{ node, data }" class="tree-node">
              <div class="tree-node-main">
                <i :class="['tree-node-icon', getNodeIcon(data)]"></i>
                <span class="tree-node-label" :title="node.label">{{ node.label }}</span>
              </div>
              <span v-if="data.type === 'device'" :class="['status-dot', data.status]"></span>
            </div>
          </el-tree>
        </div>
      </div>

      <el-button slot="reference" class="filter-btn" :class="{ active: Boolean(value?.label) }">
        <div class="filter-btn-content">
          <i class="el-icon-s-operation filter-btn-icon"></i>
          <div class="filter-btn-text">
            <span class="filter-btn-label">设备筛选</span>
            <span class="filter-btn-value">{{ displayLabel }}</span>
          </div>
        </div>
        <i class="el-icon-arrow-down"></i>
      </el-button>
    </el-popover>
  </div>
</template>

<script>
import { getDeviceTree } from '@/api/device'

export default {
  name: 'DeviceTreeFilter',
  props: {
    value: {
      type: Object,
      default: () => ({})
    },
    placeholder: {
      type: String,
      default: '全部设备'
    }
  },
  data() {
    return {
      visible: false,
      keyword: '',
      deviceTree: [],
      treeProps: {
        children: 'children',
        label: 'label'
      }
    }
  },
  computed: {
    displayLabel() {
      return this.value?.label || this.placeholder
    }
  },
  watch: {
    keyword(val) {
      if (this.$refs.deviceTree) {
        this.$refs.deviceTree.filter(val)
      }
    },
    value: {
      deep: true,
      handler(val) {
        const key = this.resolveNodeKey(val)
        this.$nextTick(() => {
          this.$refs.deviceTree?.setCurrentKey(key || null)
        })
      }
    }
  },
  mounted() {
    this.fetchDeviceTree()
  },
  methods: {
    async fetchDeviceTree() {
      try {
        const res = await getDeviceTree()
        if (res.code === 0) {
          this.deviceTree = this.attachNodeKeys(res.data || [])
          this.$nextTick(() => {
            this.$refs.deviceTree?.filter(this.keyword)
            const key = this.resolveNodeKey(this.value)
            if (key) {
              this.$refs.deviceTree?.setCurrentKey(key)
            }
          })
        }
      } catch (error) {
        console.error('Failed to fetch device tree:', error)
      }
    },
    attachNodeKeys(nodes) {
      return (nodes || []).map(node => {
        const nodeType = node.type === 'device' ? 'device' : 'group'
        const children = node.children ? this.attachNodeKeys(node.children) : []
        return {
          ...node,
          _nodeKey: `${nodeType}-${node.id}`,
          children
        }
      })
    },
    resolveNodeKey(value) {
      if (!value) return ''
      if (value.nodeType === 'device' && value.deviceId) {
        return `device-${value.deviceId}`
      }
      if (value.nodeType === 'group' && value.groupId) {
        return `group-${value.groupId}`
      }
      return ''
    },
    filterNode(value, data) {
      if (!value) return true
      return (data.label || '').toLowerCase().includes(value.toLowerCase())
    },
    getNodeIcon(data) {
      if (data.type === 'device') return 'el-icon-monitor'
      return data.children && data.children.length ? 'el-icon-folder-opened' : 'el-icon-folder'
    },
    collectDeviceIds(node) {
      if (!node) return []
      if (node.type === 'device') return [node.id]

      const ids = []
      const stack = [...(node.children || [])]
      while (stack.length > 0) {
        const current = stack.pop()
        if (!current) continue
        if (current.type === 'device') {
          ids.push(current.id)
        } else if (current.children?.length > 0) {
          stack.push(...current.children)
        }
      }
      return ids
    },
    handleNodeClick(data) {
      if (!data) return
      const payload = {
        label: data.label,
        nodeType: data.type === 'device' ? 'device' : 'group',
        groupId: data.type === 'group' ? String(data.id) : '',
        deviceId: data.type === 'device' ? String(data.id) : '',
        deviceIds: this.collectDeviceIds(data)
      }
      this.$emit('input', payload)
      this.$emit('change', payload)
      this.visible = false
    },
    clearSelection() {
      const payload = {
        label: '',
        nodeType: '',
        groupId: '',
        deviceId: '',
        deviceIds: []
      }
      this.$refs.deviceTree?.setCurrentKey(null)
      this.$emit('input', payload)
      this.$emit('change', payload)
      this.visible = false
    }
  }
}
</script>

<style lang="scss" scoped>
.device-tree-filter {
  display: inline-block;
}

.filter-panel {
  display: grid;
  gap: 12px;
}

.filter-header {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 12px;
}

.filter-title {
  font-size: 15px;
  font-weight: 700;
  color: #22324d;
}

.filter-subtitle {
  margin-top: 4px;
  color: #8090a8;
  font-size: 12px;
}

.selection-bar {
  min-height: 42px;
  padding: 0 14px;
  border-radius: 16px;
  background: #f5f8ff;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.selection-text {
  min-width: 0;
}

.selection-label {
  display: block;
  font-size: 12px;
  color: #8190a5;
}

.selection-value {
  display: block;
  margin-top: 3px;
  color: #22324d;
  font-weight: 600;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.tree-wrapper {
  max-height: 320px;
  padding-right: 4px;
  overflow: auto;
}

.tree-node {
  width: 100%;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
}

.tree-node-main {
  min-width: 0;
  display: inline-flex;
  align-items: center;
  gap: 8px;
}

.tree-node-icon {
  width: 24px;
  height: 24px;
  border-radius: 8px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  background: rgba(47, 109, 246, 0.1);
  color: #2f6df6;
  font-size: 13px;
}

.tree-node-label {
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  flex-shrink: 0;

  &.online {
    background: #2fb46e;
  }

  &.working {
    background: #2f6df6;
  }

  &.idle {
    background: #2fb46e;
  }

  &.alarm {
    background: #ef5a5a;
  }

  &.offline {
    background: #8a98ad;
  }
}

.filter-btn {
  min-width: 230px;
  min-height: 52px;
  padding: 10px 14px;
  border-radius: 16px;
  border: 1px solid rgba(219, 228, 240, 0.92);
  background: #ffffff;
  display: inline-flex;
  align-items: center;
  justify-content: space-between;
  gap: 14px;
  box-shadow: 0 10px 22px rgba(59, 87, 132, 0.08);

  &.active {
    border-color: rgba(47, 109, 246, 0.35);
    box-shadow: 0 14px 28px rgba(47, 109, 246, 0.14);
  }
}

.filter-btn-content {
  min-width: 0;
  display: flex;
  align-items: center;
  gap: 12px;
}

.filter-btn-icon {
  width: 32px;
  height: 32px;
  border-radius: 12px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  background: rgba(47, 109, 246, 0.12);
  color: #2f6df6;
  font-size: 16px;
}

.filter-btn-text {
  min-width: 0;
  display: flex;
  flex-direction: column;
  text-align: left;
}

.filter-btn-label {
  color: #8190a5;
  font-size: 12px;
}

.filter-btn-value {
  margin-top: 2px;
  color: #22324d;
  font-weight: 600;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

::v-deep .device-tree-filter-popover {
  border-radius: 20px;
  border: 1px solid rgba(219, 228, 240, 0.95);
  box-shadow: 0 18px 32px rgba(59, 87, 132, 0.14);
}

::v-deep .device-tree-filter-popover .popper__arrow {
  display: none;
}

::v-deep .device-tree-filter-popover .el-popover {
  padding: 0;
}

::v-deep .device-tree-filter-popover {
  padding: 16px;
}

::v-deep .device-tree-filter-popover .el-tree-node__content {
  height: 38px;
  border-radius: 12px;
  margin-bottom: 4px;
}

::v-deep .device-tree-filter-popover .el-tree-node.is-current > .el-tree-node__content {
  background: rgba(47, 109, 246, 0.1);
}
</style>
