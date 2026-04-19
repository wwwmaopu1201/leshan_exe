<template>
  <div class="page-shell">
    <div class="page-header">
      <div class="page-title-block">
        <h2>花型统计</h2>
        <p>查看按花型汇总的生产统计，以及逐件时长明细。</p>
      </div>
    </div>

    <div class="filter-panel">
      <el-form :inline="true" :model="searchForm">
        <el-form-item label="时间范围">
          <el-date-picker
            v-model="searchForm.dateRange"
            type="daterange"
            value-format="yyyy-MM-dd"
            range-separator="至"
            start-placeholder="开始日期"
            end-placeholder="结束日期"
          />
        </el-form-item>
        <el-form-item label="设备">
          <el-select
            v-model="searchForm.deviceId"
            clearable
            filterable
            placeholder="全部设备"
            style="width: 240px;"
          >
            <el-option
              v-for="item in deviceOptions"
              :key="item.id"
              :label="item.label"
              :value="item.id"
            />
          </el-select>
        </el-form-item>
        <el-form-item>
          <el-button type="primary" icon="el-icon-search" @click="handleSearch">查询</el-button>
          <el-button icon="el-icon-refresh" @click="handleReset">重置</el-button>
        </el-form-item>
      </el-form>
    </div>

    <el-card shadow="never" class="surface-card">
      <div class="section-title">
        <div>
          <h3>花型生产统计表</h3>
          <p>按花型查看累计件数、累计时长、最近完成和最近信息。</p>
        </div>
      </div>

      <el-table
        :data="patternTable"
        v-loading="loading"
        border
        style="width: 100%;"
        empty-text="暂无统计数据"
      >
        <el-table-column type="index" label="序号" width="60" align="center" />
        <el-table-column prop="patternNo" label="花型编号" width="110" align="center" />
        <el-table-column prop="patternName" label="花型名称" min-width="180" show-overflow-tooltip />
        <el-table-column prop="totalPieces" label="累计件数" width="110" align="right" />
        <el-table-column prop="totalDurationText" label="累计时长" width="150" />
        <el-table-column prop="lastCompleted" label="最近完成" width="170" />
        <el-table-column prop="recentInfo" label="最近信息" min-width="240" show-overflow-tooltip />
      </el-table>
    </el-card>

    <el-card shadow="never" class="surface-card">
      <div class="section-title">
        <div>
          <h3>每件时长明细表</h3>
          <p>查看每件生产的开始时间、结束时间和单件时长。</p>
        </div>
      </div>

      <el-table
        :data="detailTable"
        v-loading="loading"
        border
        style="width: 100%;"
        empty-text="暂无明细数据"
      >
        <el-table-column type="index" label="序号" width="60" align="center" />
        <el-table-column prop="patternNo" label="花型编号" width="110" align="center" />
        <el-table-column prop="patternName" label="花型名称" min-width="180" show-overflow-tooltip />
        <el-table-column prop="pieceIndex" label="第几件" width="100" align="center" />
        <el-table-column prop="startTime" label="开始时间" width="170" />
        <el-table-column prop="endTime" label="结束时间" width="170" />
        <el-table-column prop="durationText" label="单件时长" width="140" />
      </el-table>
    </el-card>
  </div>
</template>

<script>
const getDefaultRange = () => {
  const end = new Date()
  const start = new Date()
  start.setDate(end.getDate() - 6)
  const format = (date) => {
    const year = date.getFullYear()
    const month = String(date.getMonth() + 1).padStart(2, '0')
    const day = String(date.getDate()).padStart(2, '0')
    return `${year}-${month}-${day}`
  }
  return [format(start), format(end)]
}

export default {
  name: 'PatternStats',
  data() {
    return {
      loading: false,
      patternTable: [],
      detailTable: [],
      deviceOptions: [],
      searchForm: {
        dateRange: getDefaultRange(),
        deviceId: ''
      }
    }
  },
  mounted() {
    this.fetchDeviceOptions()
    this.fetchData()
  },
  methods: {
    async fetchDeviceOptions() {
      try {
        const res = await this.$axios.get('/device/list', {
          params: { page: 1, pageSize: 2000 }
        })
        if (res.code === 0) {
          const list = Array.isArray(res.data?.list) ? res.data.list : []
          this.deviceOptions = list.map(item => ({
            id: item.id,
            label: item.displayName || item.initialName || item.code || item.name || `设备${item.id}`
          }))
        }
      } catch (error) {
        console.error('加载设备选项失败', error)
      }
    },
    async fetchData() {
      this.loading = true
      try {
        const res = await this.$axios.get('/statistics/device-patterns', {
          params: {
            startDate: this.searchForm.dateRange?.[0],
            endDate: this.searchForm.dateRange?.[1],
            deviceId: this.searchForm.deviceId || undefined
          }
        })
        if (res.code === 0) {
          this.patternTable = Array.isArray(res.data?.patternTable) ? res.data.patternTable : []
          this.detailTable = Array.isArray(res.data?.detailTable) ? res.data.detailTable : []
        }
      } catch (error) {
        console.error('加载花型统计失败', error)
      } finally {
        this.loading = false
      }
    },
    handleSearch() {
      this.fetchData()
    },
    handleReset() {
      this.searchForm = {
        dateRange: getDefaultRange(),
        deviceId: ''
      }
      this.fetchData()
    }
  }
}
</script>
