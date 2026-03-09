<template>
  <div class="device-tree-filter">
    <el-popover
      v-model="visible"
      placement="bottom-start"
      width="340"
      trigger="click"
      popper-class="device-tree-filter-popover"
    >
      <div class="tree-tools">
        <el-input
          v-model="keyword"
          size="small"
          placeholder="搜索设备或分组"
          prefix-icon="el-icon-search"
          clearable
        />
        <el-button type="text" size="small" @click="clearSelection">清空</el-button>
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
          <span slot-scope="{ node, data }" class="tree-node">
            <i :class="getNodeIcon(data)"></i>
            <span>{{ node.label }}</span>
            <span v-if="data.type === 'device'" :class="['status-dot', data.status]"></span>
          </span>
        </el-tree>
      </div>
      <el-button slot="reference" class="filter-btn">
        <i class="el-icon-s-operation"></i>
        {{ displayLabel }}
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
      const deviceIds = this.collectDeviceIds(data)
      const payload = {
        label: data.label,
        nodeType: data.type === 'device' ? 'device' : 'group',
        deviceId: data.type === 'device' ? String(data.id) : '',
        deviceIds
      }
      this.$emit('input', payload)
      this.$emit('change', payload)
      this.visible = false
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
      this.visible = false
    }
  }
}
</script>

<style lang="scss" scoped>
.device-tree-filter {
  display: inline-block;
}

.filter-btn {
  min-width: 180px;
  text-align: left;
}

.tree-tools {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
}

.tree-wrapper {
  max-height: 280px;
  overflow: auto;
}

.tree-node {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;

  &.online { background: #67C23A; }
  &.working { background: #F56C6C; }
  &.idle { background: #67C23A; }
  &.alarm { background: #F56C6C; }
  &.offline { background: #909399; }
}
</style>
