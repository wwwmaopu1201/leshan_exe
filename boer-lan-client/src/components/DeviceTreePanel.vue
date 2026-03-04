<template>
  <div class="device-tree-panel">
    <div class="panel-header">
      <span>设备树筛选</span>
      <el-button type="text" size="small" @click="clearSelection">清空</el-button>
    </div>
    <el-input
      v-model="keyword"
      size="small"
      placeholder="搜索设备或分组"
      prefix-icon="el-icon-search"
      clearable
    />
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
        <span slot-scope="{ node, data }" class="tree-node">
          <i :class="getNodeIcon(data)"></i>
          <span>{{ node.label }}</span>
          <span v-if="data.type === 'device'" :class="['status-dot', data.status]"></span>
        </span>
      </el-tree>
    </div>
    <div class="current-selection">
      当前选择: {{ value?.label || '全部设备' }}
    </div>
  </div>
</template>

<script>
import { getDeviceTree } from '@/api/device'

export default {
  name: 'DeviceTreePanel',
  props: {
    value: {
      type: Object,
      default: () => ({})
    }
  },
  data() {
    return {
      keyword: '',
      deviceTree: [],
      treeProps: {
        children: 'children',
        label: 'label'
      }
    }
  },
  watch: {
    keyword(val) {
      if (this.$refs.deviceTree) {
        this.$refs.deviceTree.filter(val)
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
    filterNode(value, data) {
      if (!value) return true
      return data.label.toLowerCase().includes(value.toLowerCase())
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
        deviceId: data.type === 'device' ? String(data.id) : '',
        deviceIds: this.collectDeviceIds(data)
      }
      this.$emit('input', payload)
      this.$emit('change', payload)
    },
    clearSelection() {
      const payload = {
        label: '',
        nodeType: '',
        deviceId: '',
        deviceIds: []
      }
      this.$emit('input', payload)
      this.$emit('change', payload)
    }
  }
}
</script>

<style lang="scss" scoped>
.device-tree-panel {
  background: #fff;
  border-radius: 8px;
  padding: 12px;
}

.panel-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 8px;
  font-weight: 600;
  color: #303133;
}

.tree-wrapper {
  margin-top: 8px;
  max-height: calc(100vh - 260px);
  min-height: 300px;
  overflow: auto;
  border: 1px solid #ebeef5;
  border-radius: 6px;
  padding: 8px;
}

.tree-node {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.current-selection {
  margin-top: 8px;
  font-size: 12px;
  color: #606266;
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;

  &.online { background: #67C23A; }
  &.working { background: #F56C6C; }
  &.idle { background: #409EFF; }
  &.alarm { background: #E6A23C; }
  &.offline { background: #909399; }
}
</style>
