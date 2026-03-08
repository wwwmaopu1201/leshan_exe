<template>
  <div class="page-container">
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
        <el-form-item label="花型类型">
          <el-select
            v-model="searchForm.patternType"
            placeholder="全部类型"
            clearable
            filterable
          >
            <el-option
              v-for="item in patternTypeOptions"
              :key="item"
              :label="item"
              :value="item"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="订单编号">
          <el-input
            v-model="searchForm.orderNo"
            placeholder="支持模糊查询"
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

    <div class="card">
      <div class="table-actions flex-between">
        <div>
          <el-button type="primary" icon="el-icon-plus" @click="openUploadDialog">
            添加文件
          </el-button>
          <el-button
            icon="el-icon-edit-outline"
            :disabled="selectedRows.length !== 1"
            @click="openEditDialog(selectedRows[0])"
          >
            修改文件
          </el-button>
          <el-button
            icon="el-icon-edit"
            :disabled="!selectedRows.length"
            @click="openBatchEditDialog"
          >
            批量修改
          </el-button>
          <el-button
            type="success"
            icon="el-icon-download"
            :disabled="!selectedRows.length"
            @click="handleBatchDownload"
          >
            {{ $t('file.batchDownload') }}
          </el-button>
          <el-button icon="el-icon-upload" @click="openDeviceFileDialog">
            设备文件回传
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

      <el-table
        v-loading="loading"
        :data="tableData"
        border
        @selection-change="handleSelectionChange"
      >
        <el-table-column type="selection" width="50" align="center" />
        <el-table-column type="index" label="序号" width="60" align="center" />
        <el-table-column prop="name" :label="$t('file.fileName')" min-width="180">
          <template slot-scope="scope">
            <i class="el-icon-document" style="margin-right: 5px; color: #409EFF;"></i>
            {{ scope.row.name }}
          </template>
        </el-table-column>
        <el-table-column prop="patternType" label="花型类型" width="130" />
        <el-table-column prop="stitches" label="针数" width="100" />
        <el-table-column prop="size" :label="$t('file.fileSize')" width="110" />
        <el-table-column prop="unitPrice" label="工价" width="110" align="right">
          <template slot-scope="scope">
            {{ formatPrice(scope.row.unitPrice) }}
          </template>
        </el-table-column>
        <el-table-column prop="orderNo" label="订单编号" min-width="140" />
        <el-table-column prop="uploadTime" :label="$t('file.uploadTime')" width="170" />
        <el-table-column :label="$t('common.operation')" width="220" align="center">
          <template slot-scope="scope">
            <el-button type="text" size="small" @click="handlePreview(scope.row)">
              预览
            </el-button>
            <el-button type="text" size="small" @click="openEditDialog(scope.row)">
              编辑
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

    <el-dialog
      title="添加花型文件"
      :visible.sync="showUploadDialog"
      width="620px"
      @closed="resetUploadDialog"
    >
      <el-form :model="uploadForm" label-width="90px">
        <el-form-item label="花型名称">
          <el-input v-model.trim="uploadForm.name" placeholder="默认使用文件名" />
        </el-form-item>
        <el-form-item label="花型类型">
          <el-select
            v-model="uploadForm.patternType"
            filterable
            allow-create
            default-first-option
            clearable
            placeholder="可输入新类型"
          >
            <el-option
              v-for="item in patternTypeOptions"
              :key="item"
              :label="item"
              :value="item"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="花型针数">
          <el-input-number
            v-model="uploadForm.stitches"
            :precision="0"
            :step="1"
            :min="0"
            controls-position="right"
            style="width: 100%;"
          />
        </el-form-item>
        <el-form-item label="工价">
          <el-input-number
            v-model="uploadForm.unitPrice"
            :precision="3"
            :step="0.001"
            :min="0"
            controls-position="right"
            style="width: 100%;"
          />
        </el-form-item>
        <el-form-item label="订单编号">
          <el-input v-model.trim="uploadForm.orderNo" placeholder="支持同一订单号绑定多个花型" />
        </el-form-item>
      </el-form>

      <el-upload
        ref="uploadRef"
        class="upload-area"
        drag
        action="#"
        :auto-upload="false"
        :file-list="uploadFileList"
        :on-change="handleFileChange"
        :on-remove="handleFileRemove"
        :on-exceed="handleUploadExceed"
        :limit="1"
        accept=".dst,.dsb,.exp,.pes,.jef"
      >
        <i class="el-icon-upload"></i>
        <div class="el-upload__text">将花型文件拖到此处，或<em>点击上传</em></div>
        <div class="el-upload__tip" slot="tip">支持 .dst, .dsb, .exp, .pes, .jef 格式</div>
      </el-upload>
      <span slot="footer" class="dialog-footer">
        <el-button @click="showUploadDialog = false">{{ $t('common.cancel') }}</el-button>
        <el-button type="primary" :loading="uploading" @click="handleUpload">
          保存
        </el-button>
      </span>
    </el-dialog>

    <el-dialog
      title="修改花型文件"
      :visible.sync="showEditDialog"
      width="520px"
    >
      <el-form :model="editForm" label-width="90px">
        <el-form-item label="花型名称" required>
          <el-input v-model.trim="editForm.name" />
        </el-form-item>
        <el-form-item label="花型类型">
          <el-select
            v-model="editForm.patternType"
            filterable
            allow-create
            default-first-option
            clearable
            placeholder="可输入新类型"
          >
            <el-option
              v-for="item in patternTypeOptions"
              :key="item"
              :label="item"
              :value="item"
            />
          </el-select>
        </el-form-item>
        <el-form-item label="花型针数">
          <el-input-number
            v-model="editForm.stitches"
            :precision="0"
            :step="1"
            :min="0"
            controls-position="right"
            style="width: 100%;"
          />
        </el-form-item>
        <el-form-item label="工价">
          <el-input-number
            v-model="editForm.unitPrice"
            :precision="3"
            :step="0.001"
            :min="0"
            controls-position="right"
            style="width: 100%;"
          />
        </el-form-item>
        <el-form-item label="订单编号">
          <el-input v-model.trim="editForm.orderNo" />
        </el-form-item>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="showEditDialog = false">{{ $t('common.cancel') }}</el-button>
        <el-button type="primary" :loading="savingEdit" @click="submitEdit">保存</el-button>
      </span>
    </el-dialog>

    <el-dialog
      title="批量修改"
      :visible.sync="showBatchEditDialog"
      width="560px"
    >
      <el-form :model="batchEditForm" label-width="100px">
        <el-form-item>
          <div class="batch-tip">
            已选择 {{ selectedRows.length }} 条花型，可勾选需要批量修改的字段。
          </div>
        </el-form-item>
        <el-form-item label="花型类型">
          <div class="batch-field-row">
            <el-checkbox v-model="batchEditForm.enabled.patternType">修改</el-checkbox>
            <el-select
              v-model="batchEditForm.values.patternType"
              :disabled="!batchEditForm.enabled.patternType"
              filterable
              allow-create
              default-first-option
              clearable
              placeholder="可输入新类型"
            >
              <el-option
                v-for="item in patternTypeOptions"
                :key="item"
                :label="item"
                :value="item"
              />
            </el-select>
          </div>
        </el-form-item>
        <el-form-item label="花型针数">
          <div class="batch-field-row">
            <el-checkbox v-model="batchEditForm.enabled.stitches">修改</el-checkbox>
            <el-input-number
              v-model="batchEditForm.values.stitches"
              :disabled="!batchEditForm.enabled.stitches"
              :precision="0"
              :step="1"
              :min="0"
              controls-position="right"
            />
          </div>
        </el-form-item>
        <el-form-item label="工价">
          <div class="batch-field-row">
            <el-checkbox v-model="batchEditForm.enabled.unitPrice">修改</el-checkbox>
            <el-input-number
              v-model="batchEditForm.values.unitPrice"
              :disabled="!batchEditForm.enabled.unitPrice"
              :precision="3"
              :step="0.001"
              :min="0"
              controls-position="right"
            />
          </div>
        </el-form-item>
        <el-form-item label="订单编号">
          <div class="batch-field-row">
            <el-checkbox v-model="batchEditForm.enabled.orderNo">修改</el-checkbox>
            <el-input
              v-model.trim="batchEditForm.values.orderNo"
              :disabled="!batchEditForm.enabled.orderNo"
            />
          </div>
        </el-form-item>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="showBatchEditDialog = false">{{ $t('common.cancel') }}</el-button>
        <el-button type="primary" :loading="savingBatchEdit" @click="submitBatchEdit">
          保存
        </el-button>
      </span>
    </el-dialog>

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

    <el-dialog
      title="设备花型文件"
      :visible.sync="showDeviceFileDialog"
      width="980px"
    >
      <div class="device-file-toolbar">
        <el-select
          v-model="deviceFileQuery.deviceId"
          placeholder="选择设备"
          filterable
          style="width: 220px;"
          @change="handleDeviceFileSearch"
        >
          <el-option
            v-for="item in deviceOptions"
            :key="item.id"
            :label="item.name"
            :value="item.id"
          />
        </el-select>
        <el-input
          v-model.trim="deviceFileQuery.keyword"
          placeholder="文件名/订单号"
          clearable
          style="width: 180px;"
          @keyup.enter.native="handleDeviceFileSearch"
        />
        <el-select
          v-model="deviceFileQuery.patternType"
          placeholder="花型类型"
          clearable
          filterable
          style="width: 160px;"
          @change="handleDeviceFileSearch"
        >
          <el-option
            v-for="item in patternTypeOptions"
            :key="item"
            :label="item"
            :value="item"
          />
        </el-select>
        <el-button type="primary" icon="el-icon-search" @click="handleDeviceFileSearch">
          查询
        </el-button>
        <el-button icon="el-icon-refresh" @click="resetDeviceFileSearch">
          重置
        </el-button>
      </div>

      <div class="device-file-actions">
        <el-button
          type="primary"
          icon="el-icon-upload2"
          :disabled="!deviceFileSelectedRows.length"
          :loading="uploadingFromDevice"
          @click="handleUploadFromDevice"
        >
          上传选中文件到服务器
        </el-button>
        <el-button icon="el-icon-tickets" @click="openUploadQueueDialog">
          查看上传队列
        </el-button>
      </div>

      <el-table
        v-loading="deviceFileLoading"
        :data="deviceFileList"
        border
        @selection-change="handleDeviceFileSelectionChange"
      >
        <el-table-column type="selection" width="48" align="center" />
        <el-table-column type="index" label="序号" width="60" align="center" />
        <el-table-column prop="fileName" label="设备文件名" min-width="180" />
        <el-table-column prop="patternType" label="花型类型" width="130" />
        <el-table-column prop="stitches" label="针数" width="100" />
        <el-table-column prop="size" label="文件大小" width="100" />
        <el-table-column prop="unitPrice" label="工价" width="110" align="right">
          <template slot-scope="scope">
            {{ formatPrice(scope.row.unitPrice) }}
          </template>
        </el-table-column>
        <el-table-column prop="orderNo" label="订单号" min-width="130" />
        <el-table-column prop="updateTime" label="更新时间" width="170" />
        <el-table-column label="操作" width="90" align="center">
          <template slot-scope="scope">
            <el-button
              type="text"
              size="small"
              class="danger-text"
              @click="handleDeleteDeviceFile(scope.row)"
            >
              删除
            </el-button>
          </template>
        </el-table-column>
      </el-table>

      <el-pagination
        :current-page="deviceFilePagination.page"
        :page-size="deviceFilePagination.pageSize"
        :total="deviceFilePagination.total"
        :page-sizes="[10, 20, 50, 100]"
        layout="total, sizes, prev, pager, next, jumper"
        @size-change="handleDeviceFileSizeChange"
        @current-change="handleDeviceFilePageChange"
      />

      <span slot="footer" class="dialog-footer">
        <el-button @click="showDeviceFileDialog = false">{{ $t('common.cancel') }}</el-button>
      </span>
    </el-dialog>

    <el-dialog
      title="上传队列"
      :visible.sync="showUploadQueueDialog"
      width="920px"
    >
      <div class="upload-queue-actions">
        <el-button size="small" icon="el-icon-refresh" @click="fetchUploadQueue">
          刷新
        </el-button>
        <el-button size="small" type="danger" icon="el-icon-delete" @click="clearUploadHistory">
          清理历史
        </el-button>
      </div>
      <el-table
        v-loading="uploadQueueLoading"
        :data="uploadQueueList"
        border
      >
        <el-table-column type="index" label="序号" width="60" align="center" />
        <el-table-column prop="deviceName" label="设备" width="140" />
        <el-table-column prop="fileName" label="设备文件名" min-width="180" />
        <el-table-column prop="patternName" label="回传后花型" min-width="160" />
        <el-table-column prop="status" label="状态" width="110" align="center">
          <template slot-scope="scope">
            <el-tag :type="getUploadStatusType(scope.row.status)" size="small">
              {{ getUploadStatusText(scope.row.status) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="progress" label="进度" width="160">
          <template slot-scope="scope">
            <el-progress :percentage="scope.row.progress" :stroke-width="14" text-inside />
          </template>
        </el-table-column>
        <el-table-column prop="message" label="备注" min-width="130" />
        <el-table-column prop="createTime" label="创建时间" width="170" />
        <el-table-column label="操作" width="130" align="center">
          <template slot-scope="scope">
            <el-button
              v-if="scope.row.status === 'waiting' || scope.row.status === 'uploading'"
              type="text"
              size="small"
              @click="handlePauseUploadTask(scope.row)"
            >
              暂停
            </el-button>
            <el-button
              v-else-if="scope.row.status === 'paused'"
              type="text"
              size="small"
              @click="handleResumeUploadTask(scope.row)"
            >
              继续
            </el-button>
            <el-button
              v-if="scope.row.status === 'waiting' || scope.row.status === 'uploading' || scope.row.status === 'paused'"
              type="text"
              size="small"
              class="danger-text"
              @click="handleCancelUploadTask(scope.row)"
            >
              取消
            </el-button>
            <span v-if="scope.row.status === 'completed' || scope.row.status === 'failed' || scope.row.status === 'canceled'" class="text-muted">-</span>
          </template>
        </el-table-column>
      </el-table>
      <el-pagination
        :current-page="uploadQueuePagination.page"
        :page-size="uploadQueuePagination.pageSize"
        :total="uploadQueuePagination.total"
        :page-sizes="[10, 20, 50, 100]"
        layout="total, sizes, prev, pager, next, jumper"
        @size-change="handleUploadQueueSizeChange"
        @current-change="handleUploadQueuePageChange"
      />
    </el-dialog>

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
            <el-descriptions-item label="文件名">{{ previewPattern.name }}</el-descriptions-item>
            <el-descriptions-item label="文件大小">{{ previewPattern.size }}</el-descriptions-item>
            <el-descriptions-item label="花型类型">{{ previewPattern.patternType || '-' }}</el-descriptions-item>
            <el-descriptions-item label="针数">{{ previewPattern.stitches || 0 }}</el-descriptions-item>
            <el-descriptions-item label="工价">{{ formatPrice(previewPattern.unitPrice) }}</el-descriptions-item>
            <el-descriptions-item label="订单号">{{ previewPattern.orderNo || '-' }}</el-descriptions-item>
          </el-descriptions>
        </div>
      </div>
    </el-dialog>
  </div>
</template>

<script>
import {
  getPatternList,
  getPatternTypes,
  uploadPattern,
  updatePattern,
  batchUpdatePatterns,
  deletePattern,
  downloadToDevice,
  batchDownload,
  getDevicePatternFiles,
  deleteDevicePatternFile,
  uploadDeviceFilesToServer,
  getUploadQueue,
  pauseUploadTask,
  resumeUploadTask,
  cancelUploadTask,
  clearCompletedUploads
} from '@/api/pattern'
import { getDeviceTree, getDeviceList } from '@/api/device'

const defaultUploadForm = () => ({
  name: '',
  patternType: '',
  stitches: 0,
  unitPrice: 0,
  orderNo: ''
})

const defaultEditForm = () => ({
  id: null,
  name: '',
  patternType: '',
  stitches: 0,
  unitPrice: 0,
  orderNo: ''
})

const defaultBatchEditForm = () => ({
  enabled: {
    patternType: false,
    stitches: false,
    unitPrice: false,
    orderNo: false
  },
  values: {
    patternType: '',
    stitches: 0,
    unitPrice: 0,
    orderNo: ''
  }
})

export default {
  name: 'PatternList',
  data() {
    return {
      loading: false,
      tableData: [],
      selectedRows: [],
      searchForm: {
        keyword: '',
        patternType: '',
        orderNo: '',
        dateRange: []
      },
      pagination: {
        page: 1,
        pageSize: 10,
        total: 0
      },
      patternTypeOptions: [],
      showUploadDialog: false,
      uploadFileList: [],
      uploadForm: defaultUploadForm(),
      uploading: false,
      showEditDialog: false,
      editForm: defaultEditForm(),
      savingEdit: false,
      showBatchEditDialog: false,
      batchEditForm: defaultBatchEditForm(),
      savingBatchEdit: false,
      showDeviceDialog: false,
      downloadPatternIds: [],
      deviceTree: [],
      deviceOptions: [],
      showDeviceFileDialog: false,
      deviceFileLoading: false,
      deviceFileList: [],
      deviceFileSelectedRows: [],
      deviceFileQuery: {
        deviceId: '',
        keyword: '',
        patternType: ''
      },
      deviceFilePagination: {
        page: 1,
        pageSize: 10,
        total: 0
      },
      uploadingFromDevice: false,
      showUploadQueueDialog: false,
      uploadQueueLoading: false,
      uploadQueueList: [],
      uploadQueuePagination: {
        page: 1,
        pageSize: 10,
        total: 0
      },
      showPreviewDialog: false,
      previewPattern: {}
    }
  },
  mounted() {
    this.fetchData()
    this.fetchPatternTypes()
    this.fetchDeviceTree()
    this.fetchDeviceOptions()
  },
  methods: {
    formatPrice(value) {
      const num = Number(value || 0)
      return num.toFixed(3)
    },
    async fetchData() {
      this.loading = true
      try {
        const res = await getPatternList({
          keyword: this.searchForm.keyword,
          patternType: this.searchForm.patternType,
          orderNo: this.searchForm.orderNo,
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
    async fetchPatternTypes() {
      try {
        const res = await getPatternTypes()
        if (res.code === 0) {
          this.patternTypeOptions = res.data || []
        }
      } catch (error) {
        console.error('Failed to fetch pattern types:', error)
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
    async fetchDeviceOptions() {
      try {
        const res = await getDeviceList({ page: 1, pageSize: 1000 })
        if (res.code === 0) {
          const list = Array.isArray(res.data) ? res.data : (res.data?.list || [])
          this.deviceOptions = list.map(item => ({
            id: item.id || item.ID,
            name: item.name || item.deviceName || item.code || `设备${item.id || item.ID}`
          }))
        }
      } catch (error) {
        console.error('Failed to fetch device options:', error)
      }
    },
    handleSearch() {
      this.pagination.page = 1
      this.fetchData()
    },
    handleReset() {
      this.searchForm = { keyword: '', patternType: '', orderNo: '', dateRange: [] }
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
    openUploadDialog() {
      this.showUploadDialog = true
    },
    resetUploadDialog() {
      this.uploadFileList = []
      this.uploadForm = defaultUploadForm()
      if (this.$refs.uploadRef) {
        this.$refs.uploadRef.clearFiles()
      }
    },
    handleFileChange(file, fileList) {
      this.uploadFileList = fileList
      if (!this.uploadForm.name && file?.name) {
        this.uploadForm.name = file.name
      }
    },
    handleFileRemove(file, fileList) {
      this.uploadFileList = fileList
    },
    handleUploadExceed() {
      this.$message.warning('一次仅支持上传一个文件')
    },
    async handleUpload() {
      if (this.uploadFileList.length === 0) {
        this.$message.warning('请选择要上传的文件')
        return
      }

      this.uploading = true
      try {
        const formData = new FormData()
        formData.append('file', this.uploadFileList[0].raw)
        if (this.uploadForm.name) {
          formData.append('name', this.uploadForm.name)
        }
        if (this.uploadForm.patternType) {
          formData.append('patternType', this.uploadForm.patternType)
        }
        formData.append('stitches', String(this.uploadForm.stitches || 0))
        formData.append('unitPrice', String(Number(this.uploadForm.unitPrice || 0).toFixed(3)))
        if (this.uploadForm.orderNo) {
          formData.append('orderNo', this.uploadForm.orderNo)
        }

        const res = await uploadPattern(formData)
        if (res.code === 0) {
          this.$message.success('上传成功')
          this.showUploadDialog = false
          this.fetchPatternTypes()
          this.fetchData()
        }
      } catch (error) {
        console.error('Upload failed:', error)
        this.$message.error('上传失败')
      } finally {
        this.uploading = false
      }
    },
    async openDeviceFileDialog() {
      this.showDeviceFileDialog = true
      if (!this.deviceOptions.length) {
        await this.fetchDeviceOptions()
      }
      if (!this.deviceFileQuery.deviceId && this.deviceOptions.length) {
        this.deviceFileQuery.deviceId = this.deviceOptions[0].id
      }
      this.handleDeviceFileSearch()
    },
    handleDeviceFileSelectionChange(rows) {
      this.deviceFileSelectedRows = rows
    },
    handleDeviceFileSearch() {
      this.deviceFilePagination.page = 1
      this.fetchDeviceFileList()
    },
    resetDeviceFileSearch() {
      this.deviceFileQuery = {
        deviceId: this.deviceFileQuery.deviceId || '',
        keyword: '',
        patternType: ''
      }
      this.handleDeviceFileSearch()
    },
    handleDeviceFileSizeChange(size) {
      this.deviceFilePagination.pageSize = size
      this.fetchDeviceFileList()
    },
    handleDeviceFilePageChange(page) {
      this.deviceFilePagination.page = page
      this.fetchDeviceFileList()
    },
    async fetchDeviceFileList() {
      if (!this.deviceFileQuery.deviceId) {
        this.deviceFileList = []
        this.deviceFilePagination.total = 0
        return
      }

      this.deviceFileLoading = true
      try {
        const res = await getDevicePatternFiles({
          deviceId: this.deviceFileQuery.deviceId,
          keyword: this.deviceFileQuery.keyword,
          patternType: this.deviceFileQuery.patternType,
          page: this.deviceFilePagination.page,
          pageSize: this.deviceFilePagination.pageSize
        })
        if (res.code === 0) {
          this.deviceFileList = res.data.list || []
          this.deviceFilePagination.total = res.data.total || 0
        }
      } catch (error) {
        console.error('Failed to fetch device files:', error)
        this.$message.error('获取设备文件失败')
      } finally {
        this.deviceFileLoading = false
      }
    },
    async handleDeleteDeviceFile(row) {
      this.$confirm('确定要删除设备中的该文件吗？', this.$t('common.warning'), {
        confirmButtonText: this.$t('common.confirm'),
        cancelButtonText: this.$t('common.cancel'),
        type: 'warning'
      }).then(async () => {
        try {
          const res = await deleteDevicePatternFile(row.id)
          if (res.code === 0) {
            this.$message.success('删除成功')
            this.fetchDeviceFileList()
          }
        } catch (error) {
          console.error('Delete device file failed:', error)
          this.$message.error('删除设备文件失败')
        }
      }).catch(() => {})
    },
    async handleUploadFromDevice() {
      if (!this.deviceFileQuery.deviceId) {
        this.$message.warning('请先选择设备')
        return
      }
      if (!this.deviceFileSelectedRows.length) {
        this.$message.warning('请至少选择一个设备文件')
        return
      }

      this.uploadingFromDevice = true
      try {
        const res = await uploadDeviceFilesToServer({
          deviceId: this.deviceFileQuery.deviceId,
          fileIds: this.deviceFileSelectedRows.map(item => item.id)
        })
        if (res.code === 0) {
          this.$message.success(`回传完成：成功 ${res.data.success || 0} 条，失败 ${res.data.failed || 0} 条`)
          this.fetchData()
          this.fetchPatternTypes()
          this.fetchUploadQueue()
        }
      } catch (error) {
        console.error('Upload device files failed:', error)
        this.$message.error('回传设备文件失败')
      } finally {
        this.uploadingFromDevice = false
      }
    },
    openUploadQueueDialog() {
      this.showUploadQueueDialog = true
      this.uploadQueuePagination.page = 1
      this.fetchUploadQueue()
    },
    handleUploadQueueSizeChange(size) {
      this.uploadQueuePagination.pageSize = size
      this.fetchUploadQueue()
    },
    handleUploadQueuePageChange(page) {
      this.uploadQueuePagination.page = page
      this.fetchUploadQueue()
    },
    async fetchUploadQueue() {
      if (!this.showUploadQueueDialog) {
        return
      }

      this.uploadQueueLoading = true
      try {
        const res = await getUploadQueue({
          page: this.uploadQueuePagination.page,
          pageSize: this.uploadQueuePagination.pageSize
        })
        if (res.code === 0) {
          this.uploadQueueList = res.data.list || []
          this.uploadQueuePagination.total = res.data.total || 0
        }
      } catch (error) {
        console.error('Failed to fetch upload queue:', error)
        this.$message.error('获取上传队列失败')
      } finally {
        this.uploadQueueLoading = false
      }
    },
    getUploadStatusType(status) {
      const map = {
        waiting: 'warning',
        uploading: 'primary',
        paused: 'info',
        completed: 'success',
        failed: 'danger',
        canceled: 'info'
      }
      return map[status] || 'info'
    },
    getUploadStatusText(status) {
      const map = {
        waiting: '等待中',
        uploading: '上传中',
        paused: '已暂停',
        completed: '已完成',
        failed: '失败',
        canceled: '已取消'
      }
      return map[status] || status
    },
    async handlePauseUploadTask(row) {
      try {
        const res = await pauseUploadTask(row.id)
        if (res.code === 0) {
          this.$message.success('任务已暂停')
          this.fetchUploadQueue()
        }
      } catch (error) {
        console.error('Pause upload task failed:', error)
        this.$message.error('暂停失败')
      }
    },
    async handleResumeUploadTask(row) {
      try {
        const res = await resumeUploadTask(row.id)
        if (res.code === 0) {
          this.$message.success('任务已恢复')
          this.fetchUploadQueue()
        }
      } catch (error) {
        console.error('Resume upload task failed:', error)
        this.$message.error('恢复失败')
      }
    },
    async handleCancelUploadTask(row) {
      try {
        const res = await cancelUploadTask(row.id)
        if (res.code === 0) {
          this.$message.success('任务已取消')
          this.fetchUploadQueue()
        }
      } catch (error) {
        console.error('Cancel upload task failed:', error)
        this.$message.error('取消失败')
      }
    },
    async clearUploadHistory() {
      try {
        const res = await clearCompletedUploads()
        if (res.code === 0) {
          this.$message.success(`已清理 ${res.data.affected || 0} 条历史任务`)
          this.fetchUploadQueue()
        }
      } catch (error) {
        console.error('Clear upload history failed:', error)
        this.$message.error('清理上传历史失败')
      }
    },
    handlePreview(row) {
      this.previewPattern = { ...row }
      this.showPreviewDialog = true
    },
    openEditDialog(row) {
      if (!row) {
        return
      }
      this.editForm = {
        id: row.id,
        name: row.name || '',
        patternType: row.patternType || '',
        stitches: Number(row.stitches || 0),
        unitPrice: Number(row.unitPrice || 0),
        orderNo: row.orderNo || ''
      }
      this.showEditDialog = true
    },
    async submitEdit() {
      if (!this.editForm.name) {
        this.$message.warning('花型名称不能为空')
        return
      }

      this.savingEdit = true
      try {
        const res = await updatePattern(this.editForm.id, {
          name: this.editForm.name,
          patternType: this.editForm.patternType,
          stitches: Number(this.editForm.stitches || 0),
          unitPrice: Number(this.editForm.unitPrice || 0),
          orderNo: this.editForm.orderNo
        })
        if (res.code === 0) {
          this.$message.success('修改成功')
          this.showEditDialog = false
          this.fetchPatternTypes()
          this.fetchData()
        }
      } catch (error) {
        console.error('Update pattern failed:', error)
        this.$message.error('修改失败')
      } finally {
        this.savingEdit = false
      }
    },
    openBatchEditDialog() {
      this.batchEditForm = defaultBatchEditForm()
      this.showBatchEditDialog = true
    },
    async submitBatchEdit() {
      const payload = {
        ids: this.selectedRows.map(item => item.id)
      }

      const enabled = this.batchEditForm.enabled
      const values = this.batchEditForm.values
      if (enabled.patternType) payload.patternType = values.patternType || ''
      if (enabled.stitches) payload.stitches = Number(values.stitches || 0)
      if (enabled.unitPrice) payload.unitPrice = Number(values.unitPrice || 0)
      if (enabled.orderNo) payload.orderNo = values.orderNo || ''

      if (!enabled.patternType && !enabled.stitches && !enabled.unitPrice && !enabled.orderNo) {
        this.$message.warning('请至少勾选一个需要修改的字段')
        return
      }

      this.savingBatchEdit = true
      try {
        const res = await batchUpdatePatterns(payload)
        if (res.code === 0) {
          this.$message.success(`批量修改成功，影响 ${res.data.affected || 0} 条记录`)
          this.showBatchEditDialog = false
          this.fetchPatternTypes()
          this.fetchData()
        }
      } catch (error) {
        console.error('Batch update patterns failed:', error)
        this.$message.error('批量修改失败')
      } finally {
        this.savingBatchEdit = false
      }
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
      const deviceIds = checkedNodes
        .filter(n => !n.children || n.children.length === 0)
        .map(n => n.id)

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
          this.$message.success(`已加入下发队列，共 ${this.downloadPatternIds.length} 个花型，目标 ${deviceIds.length} 台设备`)
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
          await Promise.all(this.selectedRows.map(row => deletePattern(row.id)))
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

.batch-tip {
  width: 100%;
  padding: 8px 10px;
  background: #f5f7fa;
  border-radius: 4px;
  color: #606266;
}

.batch-field-row {
  display: flex;
  align-items: center;
  gap: 12px;

  .el-checkbox {
    min-width: 52px;
  }

  .el-select,
  .el-input,
  .el-input-number {
    flex: 1;
  }
}

.device-file-toolbar {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 12px;
}

.device-file-actions {
  margin-bottom: 12px;
}

.upload-queue-actions {
  margin-bottom: 12px;
}

.text-muted {
  color: #909399;
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
