<template>
  <div class="page-container">
    <!-- 搜索栏 -->
    <div class="search-bar">
      <el-form :inline="true" :model="searchForm">
        <el-form-item :label="$t('file.fileName')">
          <el-input
            v-model="searchForm.keyword"
            :placeholder="$t('common.search')"
            clearable
            @keyup.enter.native="handleSearch"
          />
        </el-form-item>
        <el-form-item label="上传时间">
          <el-date-picker
            v-model="searchForm.dateRange"
            type="daterange"
            range-separator="至"
            start-placeholder="开始日期"
            end-placeholder="结束日期"
            value-format="yyyy-MM-dd"
          />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" icon="el-icon-search" @click="handleSearch">
            {{ $t('common.search') }}
          </el-button>
          <el-button icon="el-icon-refresh" @click="handleReset">
            {{ $t('common.reset') }}
          </el-button>
        </el-form-item>
      </el-form>
    </div>

    <!-- 操作栏和表格 -->
    <div class="card">
      <div class="table-actions flex-between">
        <div>
          <el-button type="primary" icon="el-icon-upload2" @click="showUploadDialog = true">
            {{ $t('file.uploadFile') }}
          </el-button>
          <el-button
            type="success"
            icon="el-icon-download"
            :disabled="!selectedRows.length"
            @click="handleBatchDownload"
          >
            {{ $t('file.batchDownload') }}
          </el-button>
          <el-button
            type="danger"
            icon="el-icon-delete"
            :disabled="!selectedRows.length"
            @click="handleBatchDelete"
          >
            批量删除
          </el-button>
        </div>
        <div>
          <el-button icon="el-icon-refresh" circle @click="fetchData" />
        </div>
      </div>

      <!-- 数据表格 -->
      <el-table
        v-loading="loading"
        :data="tableData"
        border
        @selection-change="handleSelectionChange"
      >
        <el-table-column type="selection" width="50" align="center" />
        <el-table-column type="index" label="序号" width="60" align="center" />
        <el-table-column prop="name" :label="$t('file.fileName')" min-width="200">
          <template slot-scope="scope">
            <i class="el-icon-document" style="margin-right: 5px; color: #409EFF;"></i>
            {{ scope.row.name }}
          </template>
        </el-table-column>
        <el-table-column prop="size" :label="$t('file.fileSize')" width="100" />
        <el-table-column prop="uploadTime" :label="$t('file.uploadTime')" width="180" />
        <el-table-column :label="$t('common.operation')" width="200" align="center">
          <template slot-scope="scope">
            <el-button type="text" size="small" @click="handlePreview(scope.row)">
              预览
            </el-button>
            <el-button type="text" size="small" @click="handleDownload(scope.row)">
              {{ $t('file.download') }}
            </el-button>
            <el-button type="text" size="small" class="danger-text" @click="handleDelete(scope.row)">
              {{ $t('common.delete') }}
            </el-button>
          </template>
        </el-table-column>
      </el-table>

      <!-- 分页 -->
      <el-pagination
        :current-page="pagination.page"
        :page-size="pagination.pageSize"
        :total="pagination.total"
        :page-sizes="[10, 20, 50, 100]"
        layout="total, sizes, prev, pager, next, jumper"
        @size-change="handleSizeChange"
        @current-change="handlePageChange"
      />
    </div>

    <!-- 上传文件弹窗 -->
    <el-dialog
      :title="$t('file.uploadFile')"
      :visible.sync="showUploadDialog"
      width="500px"
    >
      <el-upload
        ref="uploadRef"
        class="upload-area"
        drag
        action="#"
        :auto-upload="false"
        :file-list="uploadFileList"
        :on-change="handleFileChange"
        :on-remove="handleFileRemove"
        accept=".dst,.dsb,.exp,.pes,.jef"
        multiple
      >
        <i class="el-icon-upload"></i>
        <div class="el-upload__text">将花型文件拖到此处，或<em>点击上传</em></div>
        <div class="el-upload__tip" slot="tip">支持 .dst, .dsb, .exp, .pes, .jef 格式</div>
      </el-upload>
      <span slot="footer" class="dialog-footer">
        <el-button @click="showUploadDialog = false">{{ $t('common.cancel') }}</el-button>
        <el-button type="primary" :loading="uploading" @click="handleUpload">
          开始上传
        </el-button>
      </span>
    </el-dialog>

    <!-- 下发到设备弹窗 -->
    <el-dialog
      :title="$t('file.selectDevice')"
      :visible.sync="showDeviceDialog"
      width="600px"
    >
      <el-tree
        ref="deviceTree"
        :data="deviceTree"
        show-checkbox
        node-key="id"
        default-expand-all
        :props="{ children: 'children', label: 'label' }"
      />
      <span slot="footer" class="dialog-footer">
        <el-button @click="showDeviceDialog = false">{{ $t('common.cancel') }}</el-button>
        <el-button type="primary" @click="confirmDownload">确认下发</el-button>
      </span>
    </el-dialog>

    <!-- 预览弹窗 -->
    <el-dialog
      title="花型预览"
      :visible.sync="showPreviewDialog"
      width="600px"
    >
      <div class="preview-container">
        <div class="preview-placeholder">
          <i class="el-icon-picture-outline"></i>
          <p>花型预览图</p>
        </div>
        <div class="preview-info">
          <el-descriptions :column="2" border size="small">
            <el-descriptions-item label="文件名">{{ previewPattern?.name }}</el-descriptions-item>
            <el-descriptions-item label="文件大小">{{ previewPattern?.size }}</el-descriptions-item>
            <el-descriptions-item label="针数">12580</el-descriptions-item>
            <el-descriptions-item label="色数">6</el-descriptions-item>
            <el-descriptions-item label="宽度">120mm</el-descriptions-item>
            <el-descriptions-item label="高度">80mm</el-descriptions-item>
          </el-descriptions>
        </div>
      </div>
    </el-dialog>
  </div>
</template>

<script>
import { getPatternList, uploadPattern, deletePattern, downloadToDevice, batchDownload } from '@/api/pattern'
import { getDeviceTree } from '@/api/device'

export default {
  name: 'PatternList',
  data() {
    return {
      loading: false,
      tableData: [],
      selectedRows: [],
      searchForm: {
        keyword: '',
        dateRange: []
      },
      pagination: {
        page: 1,
        pageSize: 10,
        total: 8
      },
      showUploadDialog: false,
      uploadFileList: [],
      uploading: false,
      showDeviceDialog: false,
      downloadPatternIds: [],
      deviceTree: [],
      showPreviewDialog: false,
      previewPattern: null
    }
  },
  mounted() {
    this.fetchData()
    this.fetchDeviceTree()
  },
  methods: {
    async fetchData() {
      this.loading = true
      try {
        const res = await getPatternList({
          keyword: this.searchForm.keyword,
          startDate: this.searchForm.dateRange?.[0],
          endDate: this.searchForm.dateRange?.[1],
          page: this.pagination.page,
          pageSize: this.pagination.pageSize
        })
        if (res.code === 0) {
          this.tableData = res.data.list || []
          this.pagination.total = res.data.total || 0
        }
      } catch (error) {
        console.error('Failed to fetch patterns:', error)
        this.$message.error('获取花型列表失败')
      } finally {
        this.loading = false
      }
    },
    async fetchDeviceTree() {
      try {
        const res = await getDeviceTree()
        if (res.code === 0) {
          this.deviceTree = res.data || []
        }
      } catch (error) {
        console.error('Failed to fetch device tree:', error)
      }
    },
    handleSearch() {
      this.pagination.page = 1
      this.fetchData()
    },
    handleReset() {
      this.searchForm = { keyword: '', dateRange: [] }
      this.handleSearch()
    },
    handleSelectionChange(rows) {
      this.selectedRows = rows
    },
    handleSizeChange(size) {
      this.pagination.pageSize = size
      this.fetchData()
    },
    handlePageChange(page) {
      this.pagination.page = page
      this.fetchData()
    },
    handleFileChange(file, fileList) {
      this.uploadFileList = fileList
    },
    handleFileRemove(file, fileList) {
      this.uploadFileList = fileList
    },
    async handleUpload() {
      if (this.uploadFileList.length === 0) {
        this.$message.warning('请选择要上传的文件')
        return
      }
      this.uploading = true
      try {
        for (const file of this.uploadFileList) {
          const formData = new FormData()
          formData.append('file', file.raw)
          await uploadPattern(formData)
        }
        this.$message.success('上传成功')
        this.showUploadDialog = false
        this.uploadFileList = []
        this.fetchData()
      } catch (error) {
        console.error('Upload failed:', error)
        this.$message.error('上传失败')
      } finally {
        this.uploading = false
      }
    },
    handlePreview(row) {
      this.previewPattern = row
      this.showPreviewDialog = true
    },
    handleDownload(row) {
      this.downloadPatternIds = [row.id]
      this.showDeviceDialog = true
    },
    handleBatchDownload() {
      this.downloadPatternIds = this.selectedRows.map(r => r.id)
      this.showDeviceDialog = true
    },
    async confirmDownload() {
      const checkedNodes = this.$refs.deviceTree.getCheckedNodes()
      const deviceIds = checkedNodes.filter(n => !n.children).map(n => n.id)

      if (deviceIds.length === 0) {
        this.$message.warning('请选择要下发的设备')
        return
      }

      try {
        let res
        if (this.downloadPatternIds.length === 1) {
          res = await downloadToDevice(this.downloadPatternIds[0], deviceIds)
        } else {
          res = await batchDownload(this.downloadPatternIds, deviceIds)
        }
        if (res.code === 0) {
          this.$message.success(`已添加到下发队列，共 ${this.downloadPatternIds.length} 个文件下发到 ${deviceIds.length} 台设备`)
          this.showDeviceDialog = false
        } else {
          this.$message.error(res.message || '下发失败')
        }
      } catch (error) {
        console.error('Download to device failed:', error)
        this.$message.error('下发失败')
      }
    },
    handleDelete(row) {
      this.$confirm('确定要删除该花型文件吗？', this.$t('common.warning'), {
        confirmButtonText: this.$t('common.confirm'),
        cancelButtonText: this.$t('common.cancel'),
        type: 'warning'
      }).then(async () => {
        try {
          const res = await deletePattern(row.id)
          if (res.code === 0) {
            this.$message.success(this.$t('common.success'))
            this.fetchData()
          } else {
            this.$message.error(res.message || '删除失败')
          }
        } catch (error) {
          console.error('Delete pattern failed:', error)
          this.$message.error('删除失败')
        }
      }).catch(() => {})
    },
    handleBatchDelete() {
      this.$confirm(`确定要删除选中的 ${this.selectedRows.length} 个文件吗？`, this.$t('common.warning'), {
        confirmButtonText: this.$t('common.confirm'),
        cancelButtonText: this.$t('common.cancel'),
        type: 'warning'
      }).then(async () => {
        try {
          for (const row of this.selectedRows) {
            await deletePattern(row.id)
          }
          this.$message.success(this.$t('common.success'))
          this.fetchData()
        } catch (error) {
          console.error('Batch delete patterns failed:', error)
          this.$message.error('批量删除失败')
        }
      }).catch(() => {})
    }
  }
}
</script>

<style lang="scss" scoped>
.danger-text {
  color: #F56C6C !important;
}

.upload-area {
  width: 100%;

  ::v-deep .el-upload {
    width: 100%;
  }

  ::v-deep .el-upload-dragger {
    width: 100%;
  }
}

.preview-container {
  .preview-placeholder {
    height: 200px;
    background: #f5f7fa;
    border-radius: 8px;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    color: #909399;
    margin-bottom: 20px;

    i {
      font-size: 60px;
      margin-bottom: 10px;
    }
  }
}
</style>
