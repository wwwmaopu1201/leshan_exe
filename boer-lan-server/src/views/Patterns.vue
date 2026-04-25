<template>
  <div class="page-shell">
    <div class="page-header">
      <div class="page-title-block">
        <h2>花型列表</h2>
        <p>维护服务器花型与设备花型，支持上传、下发、编辑、删除和设备文件回传。</p>
      </div>
    </div>

    <div class="filter-panel">
      <el-form :inline="true" :model="searchForm">
        <el-form-item label="花型名称">
          <el-input
            v-model.trim="searchForm.keyword"
            clearable
            placeholder="名称/文件名/类型/订单号"
            @keyup.enter.native="handleSearch"
          />
        </el-form-item>
        <el-form-item label="花型类型">
          <el-select
            v-model="searchForm.patternType"
            clearable
            filterable
            placeholder="全部类型"
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
            v-model.trim="searchForm.orderNo"
            clearable
            placeholder="支持模糊查询"
            @keyup.enter.native="handleSearch"
          />
        </el-form-item>
        <el-form-item label="上传时间">
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

    <el-card ref="serverPatternCard" shadow="never" class="surface-card pattern-card">
      <div class="section-title">
        <div>
          <h3>服务器花型文件</h3>
          <p>服务器端花型文件管理，支持上传、编辑和删除。</p>
        </div>
      </div>

      <div class="action-row">
        <div class="soft-note">
          <i class="el-icon-folder-opened"></i>
          <span>当前共 {{ pagination.total }} 条服务器花型记录。</span>
        </div>
        <div class="action-group">
          <el-button type="primary" icon="el-icon-plus" @click="openUploadDialog">上传花型</el-button>
          <el-button icon="el-icon-s-promotion" @click="openDownloadQueueDialog">下发队列</el-button>
          <el-button icon="el-icon-refresh" @click="fetchServerPatterns">刷新</el-button>
        </div>
      </div>

      <el-table
        ref="serverPatternTable"
        :data="tableData"
        v-loading="loading"
        border
        :height="serverTableHeight"
        empty-text="暂无花型数据"
      >
        <el-table-column label="序号" width="70" align="center">
          <template slot-scope="{ $index }">
            {{ (pagination.page - 1) * pagination.pageSize + $index + 1 }}
          </template>
        </el-table-column>
        <el-table-column prop="name" label="花型名称" min-width="190" show-overflow-tooltip />
        <el-table-column prop="patternType" label="花型类型" width="130" />
        <el-table-column prop="fileName" label="文件名" min-width="180" show-overflow-tooltip />
        <el-table-column prop="size" label="文件大小" width="110" align="right" />
        <el-table-column prop="stitches" label="针数" width="100" align="right" />
        <el-table-column label="工价" width="110" align="right">
          <template slot-scope="{ row }">
            {{ formatPrice(row.unitPrice) }}
          </template>
        </el-table-column>
        <el-table-column prop="orderNo" label="订单编号" min-width="140" show-overflow-tooltip />
        <el-table-column prop="uploadTime" label="上传时间" width="170" />
        <el-table-column label="操作" width="250" align="center" fixed="right">
          <template slot-scope="{ row }">
            <el-button size="small" type="primary" plain @click="openDownloadDialog(row)">下发</el-button>
            <el-button size="small" @click="openEditDialog(row)">编辑</el-button>
            <el-button size="small" type="danger" @click="confirmDelete(row)">删除</el-button>
          </template>
        </el-table-column>
      </el-table>

      <div class="compact-pagination">
        <el-pagination
          background
          layout="total, sizes, prev, pager, next, jumper"
          :current-page.sync="pagination.page"
          :page-size.sync="pagination.pageSize"
          :page-sizes="[10, 20, 50, 100]"
          :total="pagination.total"
          @size-change="handleSizeChange"
          @current-change="handlePageChange"
        />
      </div>
    </el-card>

    <el-card ref="deviceFileCard" shadow="never" class="surface-card pattern-card">
      <div class="section-title">
        <div>
          <h3>设备花型文件</h3>
          <p>读取设备中的花型文件，支持删除设备文件和回传到服务器。</p>
        </div>
      </div>

      <div class="device-file-toolbar">
        <el-select
          v-model="deviceFileQuery.deviceId"
          placeholder="选择设备"
          filterable
          style="width: 240px;"
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
        <el-button type="primary" icon="el-icon-search" @click="handleDeviceFileSearch">查询</el-button>
        <el-button icon="el-icon-refresh" @click="resetDeviceFileSearch">重置</el-button>
      </div>

      <div class="action-row action-row--device">
        <div class="soft-note">
          <i class="el-icon-monitor"></i>
          <span>查询按钮只读取服务器缓存；需要读取设备实时列表时请点“实时读取设备”。</span>
        </div>
        <div class="action-group">
          <el-button
            type="warning"
            icon="el-icon-refresh-right"
            :disabled="!deviceFileQuery.deviceId"
            :loading="refreshingDeviceFiles"
            @click="refreshDeviceFiles"
          >
            实时读取设备
          </el-button>
          <el-button
            type="primary"
            icon="el-icon-upload2"
            :disabled="!deviceFileSelectedRows.length"
            :loading="uploadingFromDevice"
            @click="handleUploadFromDevice"
          >
            回传选中文件
          </el-button>
          <el-button icon="el-icon-tickets" @click="openUploadQueueDialog">
            上传队列
          </el-button>
        </div>
      </div>

      <el-table
        ref="deviceFileTable"
        :data="deviceFileList"
        v-loading="deviceFileLoading"
        border
        :height="deviceFileTableHeight"
        empty-text="暂无设备花型数据"
        @selection-change="handleDeviceFileSelectionChange"
      >
        <el-table-column type="selection" width="48" align="center" />
        <el-table-column type="index" label="序号" width="60" align="center" />
        <el-table-column prop="fileName" label="设备文件名" min-width="180" show-overflow-tooltip />
        <el-table-column prop="patternType" label="花型类型" width="130" />
        <el-table-column prop="stitches" label="针数" width="100" align="right" />
        <el-table-column prop="size" label="文件大小" width="100" align="right" />
        <el-table-column label="工价" width="110" align="right">
          <template slot-scope="{ row }">
            {{ formatPrice(row.unitPrice) }}
          </template>
        </el-table-column>
        <el-table-column prop="orderNo" label="订单编号" min-width="130" show-overflow-tooltip />
        <el-table-column prop="updateTime" label="更新时间" width="170" />
        <el-table-column label="操作" width="90" align="center">
          <template slot-scope="{ row }">
            <el-button type="text" size="small" class="danger-text" @click="handleDeleteDeviceFile(row)">
              删除
            </el-button>
          </template>
        </el-table-column>
      </el-table>

      <div class="compact-pagination">
        <el-pagination
          background
          layout="total, sizes, prev, pager, next, jumper"
          :current-page.sync="deviceFilePagination.page"
          :page-size.sync="deviceFilePagination.pageSize"
          :page-sizes="[10, 20, 50, 100]"
          :total="deviceFilePagination.total"
          @size-change="handleDeviceFileSizeChange"
          @current-change="handleDeviceFilePageChange"
        />
      </div>
    </el-card>

    <el-dialog
      title="上传花型"
      :visible.sync="uploadDialogVisible"
      width="620px"
      @closed="resetUploadDialog"
    >
      <el-form ref="uploadFormRef" :model="uploadForm" :rules="uploadRules" label-width="100px">
        <el-form-item label="花型名称" prop="name">
          <el-input v-model.trim="uploadForm.name" placeholder="款式+部位+尺码" />
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
        <el-form-item label="针数">
          <el-input-number
            v-model="uploadForm.stitches"
            :min="0"
            :step="1"
            controls-position="right"
            style="width: 100%;"
          />
        </el-form-item>
        <el-form-item label="工价">
          <el-input-number
            v-model="uploadForm.unitPrice"
            :min="0"
            :step="0.001"
            :precision="3"
            controls-position="right"
            style="width: 100%;"
          />
        </el-form-item>
        <el-form-item label="订单编号">
          <el-input v-model.trim="uploadForm.orderNo" placeholder="可选" />
        </el-form-item>
        <el-form-item label="选择文件" prop="file">
          <el-upload
            ref="uploadRef"
            action="#"
            :auto-upload="false"
            :limit="1"
            :file-list="uploadFileList"
            :on-change="handleUploadFileChange"
            :on-remove="handleUploadFileRemove"
            :on-exceed="handleUploadExceed"
          >
            <el-button slot="trigger" size="small" type="primary">选择文件</el-button>
            <div slot="tip" class="el-upload__tip">支持选择单个花型文件，默认按文件名入库。</div>
          </el-upload>
        </el-form-item>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="uploadDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="uploading" @click="submitUpload">上传</el-button>
      </span>
    </el-dialog>

    <el-dialog
      title="编辑花型"
      :visible.sync="editDialogVisible"
      width="560px"
      @closed="resetEditDialog"
    >
      <el-form ref="editFormRef" :model="editForm" :rules="editRules" label-width="100px">
        <el-form-item label="花型名称" prop="name">
          <el-input v-model.trim="editForm.name" placeholder="款式+部位+尺码" />
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
        <el-form-item label="针数">
          <el-input-number
            v-model="editForm.stitches"
            :min="0"
            :step="1"
            controls-position="right"
            style="width: 100%;"
          />
        </el-form-item>
        <el-form-item label="工价">
          <el-input-number
            v-model="editForm.unitPrice"
            :min="0"
            :step="0.001"
            :precision="3"
            controls-position="right"
            style="width: 100%;"
          />
        </el-form-item>
        <el-form-item label="订单编号">
          <el-input v-model.trim="editForm.orderNo" placeholder="可选" />
        </el-form-item>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="editDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="submitEdit">保存</el-button>
      </span>
    </el-dialog>

    <el-dialog
      title="下发到设备"
      :visible.sync="downloadDialogVisible"
      width="620px"
      @closed="resetDownloadDialog"
    >
      <el-form :model="downloadForm" label-width="100px">
        <el-form-item label="花型名称">
          <el-input :value="downloadForm.patternName" disabled />
        </el-form-item>
        <el-form-item label="目标设备" required>
          <el-select
            v-model="downloadForm.deviceIds"
            multiple
            filterable
            collapse-tags
            placeholder="请选择一个或多个设备"
            style="width: 100%;"
          >
            <el-option
              v-for="item in deviceOptions"
              :key="item.id"
              :label="item.name"
              :value="item.id"
            />
          </el-select>
          <div class="dialog-tip">同一设备按队列顺序下发；设备工作中或离线时会等待，队首任务等待超过10分钟才判定失败。</div>
        </el-form-item>
      </el-form>
      <span slot="footer" class="dialog-footer">
        <el-button @click="downloadDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="downloadingToDevice" @click="submitDownloadToDevice">开始下发</el-button>
      </span>
    </el-dialog>

    <el-dialog
      title="下发队列"
      :visible.sync="downloadQueueDialogVisible"
      width="980px"
      @opened="syncDownloadQueueTableHeight"
    >
      <div class="action-row queue-toolbar">
        <div class="soft-note">
          <i class="el-icon-s-promotion"></i>
          <span>查看服务器花型下发到设备的任务状态。</span>
        </div>
        <div class="action-group">
          <el-button icon="el-icon-refresh" @click="fetchDownloadQueue">刷新</el-button>
          <el-button type="danger" plain @click="clearDownloadHistory">清理已完成</el-button>
        </div>
      </div>

      <el-table
        :data="downloadQueueList"
        v-loading="downloadQueueLoading"
        border
        :max-height="downloadQueueTableMaxHeight"
        empty-text="暂无下发任务"
      >
        <el-table-column type="index" label="序号" width="60" align="center" />
        <el-table-column prop="deviceName" label="设备名称" min-width="120" />
        <el-table-column prop="patternName" label="花型名称" min-width="180" show-overflow-tooltip />
        <el-table-column prop="patternType" label="花型类型" width="120" />
        <el-table-column prop="status" label="状态" width="110" align="center">
          <template slot-scope="{ row }">
            <span :class="['status-pill', getDownloadStatusType(row.status)]">
              {{ getDownloadStatusText(row.status) }}
            </span>
          </template>
        </el-table-column>
        <el-table-column prop="progress" label="进度" width="80" align="center" />
        <el-table-column prop="message" label="结果信息" min-width="180" show-overflow-tooltip />
        <el-table-column prop="createTime" label="创建时间" width="170" />
        <el-table-column label="操作" width="180" align="center">
          <template slot-scope="{ row }">
            <el-button
              v-if="row.status === 'waiting' || row.status === 'downloading'"
              type="text"
              size="small"
              @click="handlePauseDownloadTask(row)"
            >
              暂停
            </el-button>
            <el-button
              v-if="row.status === 'paused'"
              type="text"
              size="small"
              @click="handleResumeDownloadTask(row)"
            >
              恢复
            </el-button>
            <el-button
              v-if="row.status !== 'completed'"
              type="text"
              size="small"
              class="danger-text"
              @click="handleCancelDownloadTask(row)"
            >
              取消
            </el-button>
          </template>
        </el-table-column>
      </el-table>

      <div class="compact-pagination">
        <el-pagination
          background
          layout="total, sizes, prev, pager, next, jumper"
          :current-page.sync="downloadQueuePagination.page"
          :page-size.sync="downloadQueuePagination.pageSize"
          :page-sizes="[10, 20, 50, 100]"
          :total="downloadQueuePagination.total"
          @size-change="handleDownloadQueueSizeChange"
          @current-change="handleDownloadQueuePageChange"
        />
      </div>
    </el-dialog>

    <el-dialog
      title="上传队列"
      :visible.sync="uploadQueueDialogVisible"
      width="980px"
      @opened="syncUploadQueueTableHeight"
    >
      <div class="action-row queue-toolbar">
        <div class="soft-note">
          <i class="el-icon-time"></i>
          <span>查看设备文件回传到服务器的任务状态。</span>
        </div>
        <div class="action-group">
          <el-button icon="el-icon-refresh" @click="fetchUploadQueue">刷新</el-button>
          <el-button type="danger" plain @click="clearUploadHistory">清理已完成</el-button>
        </div>
      </div>

      <el-table
        :data="uploadQueueList"
        v-loading="uploadQueueLoading"
        border
        :max-height="uploadQueueTableMaxHeight"
        empty-text="暂无上传任务"
      >
        <el-table-column type="index" label="序号" width="60" align="center" />
        <el-table-column prop="deviceName" label="设备名称" min-width="120" />
        <el-table-column prop="fileName" label="设备文件" min-width="180" show-overflow-tooltip />
        <el-table-column prop="patternName" label="服务器花型" min-width="180" show-overflow-tooltip />
        <el-table-column prop="status" label="状态" width="110" align="center">
          <template slot-scope="{ row }">
            <span :class="['status-pill', getUploadStatusType(row.status)]">
              {{ getUploadStatusText(row.status) }}
            </span>
          </template>
        </el-table-column>
        <el-table-column prop="progress" label="进度" width="80" align="center" />
        <el-table-column prop="message" label="结果信息" min-width="180" show-overflow-tooltip />
        <el-table-column prop="createTime" label="创建时间" width="170" />
        <el-table-column label="操作" width="180" align="center">
          <template slot-scope="{ row }">
            <el-button
              v-if="row.status === 'waiting' || row.status === 'uploading'"
              type="text"
              size="small"
              @click="handlePauseUploadTask(row)"
            >
              暂停
            </el-button>
            <el-button
              v-if="row.status === 'paused'"
              type="text"
              size="small"
              @click="handleResumeUploadTask(row)"
            >
              恢复
            </el-button>
            <el-button
              v-if="row.status !== 'completed' && row.status !== 'canceled'"
              type="text"
              size="small"
              class="danger-text"
              @click="handleCancelUploadTask(row)"
            >
              取消
            </el-button>
          </template>
        </el-table-column>
      </el-table>

      <div class="compact-pagination">
        <el-pagination
          background
          layout="total, sizes, prev, pager, next, jumper"
          :current-page.sync="uploadQueuePagination.page"
          :page-size.sync="uploadQueuePagination.pageSize"
          :page-sizes="[10, 20, 50, 100]"
          :total="uploadQueuePagination.total"
          @size-change="handleUploadQueueSizeChange"
          @current-change="handleUploadQueuePageChange"
        />
      </div>
    </el-dialog>

    <el-dialog
      title="同名花型处理"
      :visible.sync="uploadConflictDialogVisible"
      width="620px"
      @closed="handleUploadConflictDialogClosed"
    >
      <div class="conflict-dialog-intro">
        检测到服务器中已存在同名花型，请选择处理方式。
      </div>
      <el-radio-group v-model="uploadConflictForm.mode" class="conflict-mode-group">
        <el-radio label="overwrite">覆盖同名花型</el-radio>
        <el-radio label="rename">重命名后新增</el-radio>
      </el-radio-group>
      <div v-if="uploadConflictForm.mode === 'rename'" class="conflict-rename-list">
        <div
          v-for="item in uploadConflictDuplicates"
          :key="item.fileId"
          class="conflict-rename-item"
        >
          <div class="conflict-rename-label">
            {{ item.fileName || item.patternName || item.fileId }}
          </div>
          <el-input
            v-model.trim="uploadConflictForm.renameNames[String(item.fileId)]"
            placeholder="请输入新的花型名称"
          />
        </div>
      </div>
      <div v-else class="conflict-overwrite-list">
        <div
          v-for="item in uploadConflictDuplicates"
          :key="item.fileId"
          class="conflict-overwrite-item"
        >
          {{ item.patternName }}
        </div>
      </div>
      <span slot="footer" class="dialog-footer">
        <el-button @click="cancelUploadConflictResolution">取消</el-button>
        <el-button type="primary" @click="confirmUploadConflictResolution">确定</el-button>
      </span>
    </el-dialog>
  </div>
</template>

<script>
const defaultSearchForm = () => ({
  keyword: '',
  patternType: '',
  orderNo: '',
  dateRange: []
})

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

const defaultDownloadForm = () => ({
  patternId: null,
  patternName: '',
  deviceIds: []
})

const defaultUploadConflictForm = () => ({
  mode: 'overwrite',
  renameNames: {}
})

const defaultDeviceFileQuery = () => ({
  deviceId: '',
  keyword: '',
  patternType: ''
})

const connectedDeviceStatuses = ['online', 'idle', 'working', 'alarm']
const patternTransferHttpTimeout = 30 * 60 * 1000

export default {
  name: 'Patterns',
  data() {
    return {
      loading: false,
      uploading: false,
      saving: false,
      deviceFileLoading: false,
      refreshingDeviceFiles: false,
      downloadingToDevice: false,
      uploadingFromDevice: false,
      downloadQueueLoading: false,
      uploadQueueLoading: false,
      serverTableHeight: 320,
      deviceFileTableHeight: 320,
      downloadQueueTableMaxHeight: 460,
      uploadQueueTableMaxHeight: 460,
      tableData: [],
      patternTypeOptions: [],
      deviceOptions: [],
      deviceFileList: [],
      deviceFileSelectedRows: [],
      downloadQueueList: [],
      uploadQueueList: [],
      searchForm: defaultSearchForm(),
      deviceFileQuery: defaultDeviceFileQuery(),
      pagination: {
        page: 1,
        pageSize: 20,
        total: 0
      },
      deviceFilePagination: {
        page: 1,
        pageSize: 10,
        total: 0
      },
      downloadQueuePagination: {
        page: 1,
        pageSize: 10,
        total: 0
      },
      uploadQueuePagination: {
        page: 1,
        pageSize: 10,
        total: 0
      },
      uploadDialogVisible: false,
      editDialogVisible: false,
      downloadDialogVisible: false,
      downloadQueueDialogVisible: false,
      uploadQueueDialogVisible: false,
      uploadConflictDialogVisible: false,
      uploadForm: defaultUploadForm(),
      editForm: defaultEditForm(),
      downloadForm: defaultDownloadForm(),
      uploadConflictForm: defaultUploadConflictForm(),
      uploadConflictDuplicates: [],
      uploadConflictResolver: null,
      uploadFileList: [],
      uploadRules: {
        name: [
          { required: true, message: '请输入花型名称', trigger: 'blur' }
        ],
        file: [
          {
            validator: (_, __, callback) => {
              if (!this.uploadFileList.length) {
                callback(new Error('请选择文件'))
                return
              }
              callback()
            },
            trigger: 'change'
          }
        ]
      },
      editRules: {
        name: [
          { required: true, message: '请输入花型名称', trigger: 'blur' }
        ]
      }
    }
  },
  mounted() {
    this.fetchPageData()
    window.addEventListener('resize', this.syncTableHeights)
  },
  beforeDestroy() {
    window.removeEventListener('resize', this.syncTableHeights)
  },
  methods: {
    async fetchPageData() {
      await Promise.all([
        this.fetchServerPatterns(),
        this.fetchPatternTypes(),
        this.fetchDeviceOptions()
      ])
      this.syncTableHeights()
    },
    async fetchServerPatterns() {
      this.loading = true
      try {
        const res = await this.$axios.get('/pattern/list', {
          params: {
            keyword: this.searchForm.keyword,
            patternType: this.searchForm.patternType,
            orderNo: this.searchForm.orderNo,
            startDate: this.searchForm.dateRange?.[0],
            endDate: this.searchForm.dateRange?.[1],
            page: this.pagination.page,
            pageSize: this.pagination.pageSize
          }
        })
        if (res.code === 0) {
          this.tableData = Array.isArray(res.data?.list) ? res.data.list : []
          this.pagination.total = Number(res.data?.total || 0)
        }
      } catch (error) {
        console.error('加载服务器花型列表失败', error)
      } finally {
        this.loading = false
      }
    },
    async fetchPatternTypes() {
      try {
        const res = await this.$axios.get('/pattern/types')
        if (res.code === 0) {
          this.patternTypeOptions = Array.isArray(res.data) ? res.data : []
        }
      } catch (error) {
        console.error('加载花型类型失败', error)
      }
    },
    async fetchDeviceOptions() {
      try {
        const res = await this.$axios.get('/device/list', {
          params: {
            page: 1,
            pageSize: 2000
          }
        })
        if (res.code !== 0) {
          return
        }
        const list = Array.isArray(res.data?.list) ? res.data.list : []
        const sorted = [...list].sort((a, b) => {
          const rankDiff = this.getDeviceOptionRank(a) - this.getDeviceOptionRank(b)
          if (rankDiff !== 0) return rankDiff
          return String(a.displayName || a.name || a.code || '').localeCompare(String(b.displayName || b.name || b.code || ''), 'zh-Hans-CN')
        })
        this.deviceOptions = sorted.map(item => ({
          id: item.id,
          name: this.formatDeviceOptionLabel(item),
          status: item.status
        }))
        if (!this.deviceFileQuery.deviceId && this.deviceOptions.length) {
          this.deviceFileQuery.deviceId = this.deviceOptions[0].id
          this.fetchDeviceFileList()
        }
      } catch (error) {
        console.error('加载设备选项失败', error)
      }
    },
    handleSearch() {
      this.pagination.page = 1
      this.fetchServerPatterns()
    },
    handleReset() {
      this.searchForm = defaultSearchForm()
      this.pagination.page = 1
      this.fetchServerPatterns()
    },
    handlePageChange(page) {
      this.pagination.page = page
      this.fetchServerPatterns()
    },
    handleSizeChange(size) {
      this.pagination.pageSize = size
      this.pagination.page = 1
      this.fetchServerPatterns()
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
        ...defaultDeviceFileQuery(),
        deviceId: this.deviceFileQuery.deviceId || ''
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
        const res = await this.$axios.get('/pattern/device-files', {
          params: {
            deviceId: this.deviceFileQuery.deviceId,
            keyword: this.deviceFileQuery.keyword,
            patternType: this.deviceFileQuery.patternType,
            page: this.deviceFilePagination.page,
            pageSize: this.deviceFilePagination.pageSize
          }
        })
        if (res.code === 0) {
          this.deviceFileList = Array.isArray(res.data?.list) ? res.data.list : []
          this.deviceFilePagination.total = Number(res.data?.total || 0)
        }
      } catch (error) {
        console.error('加载设备花型文件失败', error)
        this.$message.error(this.getRequestErrorMessage(error, '获取设备文件失败'))
      } finally {
        this.deviceFileLoading = false
      }
    },
    async refreshDeviceFiles() {
      if (!this.deviceFileQuery.deviceId) {
        this.$message.warning('请先选择设备')
        return
      }

      this.refreshingDeviceFiles = true
      try {
        const res = await this.$axios.post('/pattern/device-files/refresh', null, {
          params: {
            deviceId: this.deviceFileQuery.deviceId
          },
          timeout: 20 * 60 * 1000
        })
        if (res.code === 0) {
          this.$message.success(res.message || '设备花型列表已刷新')
          await this.fetchDeviceFileList()
        }
      } catch (error) {
        console.error('实时读取设备花型失败', error)
        this.$message.error(this.getRequestErrorMessage(error, '实时读取设备花型失败'))
      } finally {
        this.refreshingDeviceFiles = false
      }
    },
    async handleDeleteDeviceFile(row) {
      try {
        await this.$confirm('确定要删除设备中的该文件吗？', '警告', { type: 'warning' })
        const res = await this.$axios.delete(`/pattern/device-files/${row.id}`)
        if (res.code === 0) {
          this.$message.success('删除成功')
          await this.fetchDeviceFileList()
        }
      } catch (error) {
        if (error !== 'cancel') {
          console.error('删除设备文件失败', error)
          this.$message.error(this.getRequestErrorMessage(error, '删除设备文件失败'))
        }
      }
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
        let res = null
        try {
          res = await this.submitUploadFromDeviceRequest('ask')
        } catch (error) {
          const duplicates = Array.isArray(error?.response?.data?.data?.duplicates)
            ? error.response.data.data.duplicates
            : []
          if (error?.response?.status !== 409 || !duplicates.length) {
            throw error
          }

          const conflictDecision = await this.promptUploadConflictResolution(duplicates)
          if (!conflictDecision?.mode) {
            return
          }
          res = await this.submitUploadFromDeviceRequest(conflictDecision.mode, conflictDecision.renameNames || {})
        }

        if (res.code === 0) {
          this.$message.success(`回传完成：成功 ${res.data.success || 0} 条，失败 ${res.data.failed || 0} 条`)
          await Promise.all([
            this.fetchServerPatterns(),
            this.fetchPatternTypes(),
            this.fetchUploadQueue()
          ])
        }
      } catch (error) {
        console.error('回传设备文件失败', error)
        this.$message.error(this.getRequestErrorMessage(error, '回传设备文件失败'))
      } finally {
        this.uploadingFromDevice = false
      }
    },
    submitUploadFromDeviceRequest(conflictMode = 'ask', renameNames = {}) {
      return this.$axios.post('/pattern/device-files/upload', {
        deviceId: this.deviceFileQuery.deviceId,
        fileIds: this.deviceFileSelectedRows.map(item => item.id),
        conflictMode,
        renameNames
      }, {
        suppressErrorMessage: true,
        timeout: patternTransferHttpTimeout
      })
    },
    async promptUploadConflictResolution(duplicates = []) {
      const renameNames = {}
      for (const item of duplicates) {
        renameNames[String(item.fileId)] = `${item.patternName || item.fileName || '新花型'}(1)`
      }
      this.uploadConflictDuplicates = duplicates.map(item => ({ ...item }))
      this.uploadConflictForm = {
        mode: 'overwrite',
        renameNames
      }
      this.uploadConflictDialogVisible = true
      return new Promise(resolve => {
        this.uploadConflictResolver = resolve
      })
    },
    resolveUploadConflict(result) {
      const resolver = this.uploadConflictResolver
      this.uploadConflictResolver = null
      this.uploadConflictDialogVisible = false
      this.uploadConflictDuplicates = []
      this.uploadConflictForm = defaultUploadConflictForm()
      if (typeof resolver === 'function') {
        resolver(result)
      }
    },
    cancelUploadConflictResolution() {
      this.resolveUploadConflict({ mode: '', renameNames: {} })
    },
    confirmUploadConflictResolution() {
      if (this.uploadConflictForm.mode === 'rename') {
        const renameNames = {}
        for (const item of this.uploadConflictDuplicates) {
          const nextName = String(this.uploadConflictForm.renameNames[String(item.fileId)] || '').trim()
          if (!nextName) {
            this.$message.warning(`请为“${item.fileName || item.patternName || item.fileId}”输入新名称`)
            return
          }
          renameNames[String(item.fileId)] = nextName
        }
        this.resolveUploadConflict({ mode: 'rename', renameNames })
        return
      }
      this.resolveUploadConflict({ mode: 'overwrite', renameNames: {} })
    },
    handleUploadConflictDialogClosed() {
      if (this.uploadConflictResolver) {
        this.resolveUploadConflict({ mode: '', renameNames: {} })
      }
    },
    openUploadQueueDialog() {
      this.uploadQueueDialogVisible = true
      this.uploadQueuePagination.page = 1
      this.fetchUploadQueue()
      this.syncUploadQueueTableHeight()
    },
    resolveDefaultDownloadDeviceIds() {
      const candidates = []
      const currentDeviceId = Number(this.deviceFileQuery.deviceId || 0)
      if (currentDeviceId) {
        candidates.push(currentDeviceId)
      }
      const firstConnected = this.deviceOptions.find(item => this.isConnectedDeviceStatus(item.status))
      if (firstConnected) {
        candidates.push(firstConnected.id)
      }
      const uniqueIds = Array.from(new Set(candidates.filter(Boolean)))
      return uniqueIds.length ? [uniqueIds[0]] : []
    },
    openDownloadDialog(row) {
      this.downloadForm = {
        patternId: row.id,
        patternName: row.name || row.fileName || `花型${row.id}`,
        deviceIds: this.resolveDefaultDownloadDeviceIds()
      }
      this.downloadDialogVisible = true
    },
    resetDownloadDialog() {
      this.downloadForm = defaultDownloadForm()
    },
    async submitDownloadToDevice() {
      if (!this.downloadForm.patternId) {
        this.$message.warning('请选择要下发的花型')
        return
      }
      if (!this.downloadForm.deviceIds.length) {
        this.$message.warning('请至少选择一个目标设备')
        return
      }

      this.downloadingToDevice = true
      try {
        const res = await this.$axios.post('/pattern/download', {
          patternId: this.downloadForm.patternId,
          deviceIds: this.downloadForm.deviceIds
        }, {
          timeout: patternTransferHttpTimeout
        })
        if (res.code === 0) {
          this.$message.success(`已加入下发队列，共 ${this.downloadForm.deviceIds.length} 台设备`)
          this.downloadDialogVisible = false
          this.openDownloadQueueDialog()
        }
      } catch (error) {
        console.error('下发花型失败', error)
        this.$message.error(this.getRequestErrorMessage(error, '下发花型失败'))
      } finally {
        this.downloadingToDevice = false
      }
    },
    openDownloadQueueDialog() {
      this.downloadQueueDialogVisible = true
      this.downloadQueuePagination.page = 1
      this.fetchDownloadQueue()
      this.syncDownloadQueueTableHeight()
    },
    handleDownloadQueueSizeChange(size) {
      this.downloadQueuePagination.pageSize = size
      this.fetchDownloadQueue()
    },
    handleDownloadQueuePageChange(page) {
      this.downloadQueuePagination.page = page
      this.fetchDownloadQueue()
    },
    async fetchDownloadQueue() {
      if (!this.downloadQueueDialogVisible) {
        return
      }
      this.downloadQueueLoading = true
      try {
        const res = await this.$axios.get('/pattern/queue', {
          params: {
            page: this.downloadQueuePagination.page,
            pageSize: this.downloadQueuePagination.pageSize
          }
        })
        if (res.code === 0) {
          this.downloadQueueList = Array.isArray(res.data?.list) ? res.data.list : []
          this.downloadQueuePagination.total = Number(res.data?.total || 0)
          this.syncDownloadQueueTableHeight()
        }
      } catch (error) {
        console.error('加载下发队列失败', error)
        this.$message.error(this.getRequestErrorMessage(error, '获取下发队列失败'))
      } finally {
        this.downloadQueueLoading = false
      }
    },
    getDownloadStatusType(status) {
      const map = {
        waiting: 'warning',
        downloading: 'primary',
        paused: 'info',
        completed: 'success',
        failed: 'danger'
      }
      return map[status] || 'info'
    },
    getDownloadStatusText(status) {
      const map = {
        waiting: '等待中',
        downloading: '下发中',
        paused: '已暂停',
        completed: '已完成',
        failed: '失败'
      }
      return map[status] || status || '-'
    },
    async handlePauseDownloadTask(row) {
      try {
        const res = await this.$axios.post(`/pattern/queue/${row.id}/pause`)
        if (res.code === 0) {
          this.$message.success('任务已暂停')
          this.fetchDownloadQueue()
        }
      } catch (error) {
        console.error('暂停下发任务失败', error)
        this.$message.error(this.getRequestErrorMessage(error, '暂停下发任务失败'))
      }
    },
    async handleResumeDownloadTask(row) {
      try {
        const res = await this.$axios.post(`/pattern/queue/${row.id}/resume`)
        if (res.code === 0) {
          this.$message.success('任务已恢复')
          this.fetchDownloadQueue()
        }
      } catch (error) {
        console.error('恢复下发任务失败', error)
        this.$message.error(this.getRequestErrorMessage(error, '恢复下发任务失败'))
      }
    },
    async handleCancelDownloadTask(row) {
      try {
        const res = await this.$axios.delete(`/pattern/queue/${row.id}`)
        if (res.code === 0) {
          this.$message.success('任务已取消')
          this.fetchDownloadQueue()
        }
      } catch (error) {
        console.error('取消下发任务失败', error)
        this.$message.error(this.getRequestErrorMessage(error, '取消下发任务失败'))
      }
    },
    async clearDownloadHistory() {
      try {
        const res = await this.$axios.delete('/pattern/queue/completed')
        if (res.code === 0) {
          this.$message.success(`已清理 ${res.data?.affected || 0} 条历史任务`)
          this.fetchDownloadQueue()
        }
      } catch (error) {
        console.error('清理下发历史失败', error)
        this.$message.error(this.getRequestErrorMessage(error, '清理下发历史失败'))
      }
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
      if (!this.uploadQueueDialogVisible) {
        return
      }
      this.uploadQueueLoading = true
      try {
        const res = await this.$axios.get('/pattern/upload-queue', {
          params: {
            page: this.uploadQueuePagination.page,
            pageSize: this.uploadQueuePagination.pageSize
          }
        })
        if (res.code === 0) {
          this.uploadQueueList = Array.isArray(res.data?.list) ? res.data.list : []
          this.uploadQueuePagination.total = Number(res.data?.total || 0)
          this.syncUploadQueueTableHeight()
        }
      } catch (error) {
        console.error('加载上传队列失败', error)
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
      return map[status] || status || '-'
    },
    async handlePauseUploadTask(row) {
      try {
        const res = await this.$axios.post(`/pattern/upload-queue/${row.id}/pause`)
        if (res.code === 0) {
          this.$message.success('任务已暂停')
          this.fetchUploadQueue()
        }
      } catch (error) {
        console.error('暂停上传任务失败', error)
        this.$message.error('暂停失败')
      }
    },
    async handleResumeUploadTask(row) {
      try {
        const res = await this.$axios.post(`/pattern/upload-queue/${row.id}/resume`)
        if (res.code === 0) {
          this.$message.success('任务已恢复')
          this.fetchUploadQueue()
        }
      } catch (error) {
        console.error('恢复上传任务失败', error)
        this.$message.error('恢复失败')
      }
    },
    async handleCancelUploadTask(row) {
      try {
        const res = await this.$axios.delete(`/pattern/upload-queue/${row.id}`)
        if (res.code === 0) {
          this.$message.success('任务已取消')
          this.fetchUploadQueue()
        }
      } catch (error) {
        console.error('取消上传任务失败', error)
        this.$message.error('取消失败')
      }
    },
    async clearUploadHistory() {
      try {
        const res = await this.$axios.delete('/pattern/upload-queue/completed')
        if (res.code === 0) {
          this.$message.success(`已清理 ${res.data?.affected || 0} 条历史任务`)
          this.fetchUploadQueue()
        }
      } catch (error) {
        console.error('清理上传历史失败', error)
        this.$message.error('清理上传历史失败')
      }
    },
    formatPrice(value) {
      const num = Number(value || 0)
      return num.toFixed(3)
    },
    getRequestErrorMessage(error, fallback) {
      return error?.response?.data?.message || error?.message || fallback
    },
    isConnectedDeviceStatus(status) {
      return connectedDeviceStatuses.includes(String(status || '').trim())
    },
    getDeviceOptionRank(device) {
      if (!device) return 99
      if (this.isConnectedDeviceStatus(device.status)) {
        if (device.status === 'working') return 1
        if (device.status === 'alarm') return 2
        if (device.status === 'idle') return 3
        return 4
      }
      return 99
    },
    formatDeviceOptionLabel(device) {
      const name = device.displayName || device.name || device.code || `设备${device.id}`
      const status = this.isConnectedDeviceStatus(device.status) ? '在线' : '离线'
      const ip = device.ip ? ` · ${device.ip}` : ''
      return `${name} [${status}]${ip}`
    },
    openUploadDialog() {
      this.uploadDialogVisible = true
    },
    resetUploadDialog() {
      this.uploadForm = defaultUploadForm()
      this.uploadFileList = []
      this.$refs.uploadRef?.clearFiles()
      this.$refs.uploadFormRef?.clearValidate()
    },
    handleUploadFileChange(file, fileList) {
      this.uploadFileList = fileList.slice(-1)
      if (!this.uploadForm.name) {
        const sourceName = String(file?.name || '').trim()
        this.uploadForm.name = sourceName.replace(/\.[^.]+$/, '')
      }
      this.$refs.uploadFormRef?.validateField('file')
    },
    handleUploadFileRemove(file, fileList) {
      this.uploadFileList = fileList
      this.$refs.uploadFormRef?.validateField('file')
    },
    handleUploadExceed() {
      this.$message.warning('一次只能上传一个文件')
    },
    async submitUpload() {
      try {
        await this.$refs.uploadFormRef.validate()
        const currentFile = this.uploadFileList[0]?.raw
        if (!currentFile) {
          this.$message.warning('请选择文件')
          return
        }

        this.uploading = true
        const formData = new FormData()
        formData.append('file', currentFile)
        formData.append('name', this.uploadForm.name.trim())
        if (this.uploadForm.patternType) {
          formData.append('patternType', this.uploadForm.patternType)
        }
        formData.append('stitches', String(Number(this.uploadForm.stitches || 0)))
        formData.append('unitPrice', String(Number(this.uploadForm.unitPrice || 0).toFixed(3)))
        if (this.uploadForm.orderNo) {
          formData.append('orderNo', this.uploadForm.orderNo.trim())
        }

        const res = await this.$axios.post('/pattern/upload', formData, {
          headers: { 'Content-Type': 'multipart/form-data' }
        })
        if (res.code === 0) {
          this.$message.success('上传成功')
          this.uploadDialogVisible = false
          await Promise.all([this.fetchServerPatterns(), this.fetchPatternTypes()])
        }
      } catch (error) {
        console.error('上传花型失败', error)
      } finally {
        this.uploading = false
      }
    },
    openEditDialog(row) {
      this.editForm = {
        id: row.id,
        name: row.name || '',
        patternType: row.patternType || '',
        stitches: Number(row.stitches || 0),
        unitPrice: Number(row.unitPrice || 0),
        orderNo: row.orderNo || ''
      }
      this.editDialogVisible = true
    },
    resetEditDialog() {
      this.editForm = defaultEditForm()
      this.$refs.editFormRef?.clearValidate()
    },
    async submitEdit() {
      try {
        await this.$refs.editFormRef.validate()
        this.saving = true
        const res = await this.$axios.put(`/pattern/${this.editForm.id}`, {
          name: this.editForm.name.trim(),
          patternType: this.editForm.patternType || '',
          stitches: Number(this.editForm.stitches || 0),
          unitPrice: Number(this.editForm.unitPrice || 0),
          orderNo: this.editForm.orderNo.trim()
        })
        if (res.code === 0) {
          this.$message.success('保存成功')
          this.editDialogVisible = false
          await Promise.all([this.fetchServerPatterns(), this.fetchPatternTypes()])
        }
      } catch (error) {
        console.error('编辑花型失败', error)
      } finally {
        this.saving = false
      }
    },
    async confirmDelete(row) {
      try {
        await this.$confirm(
          `确定要删除花型“${row.name || row.fileName || row.id}”吗？`,
          '警告',
          { type: 'warning' }
        )
        const res = await this.$axios.delete(`/pattern/${row.id}`)
        if (res.code === 0) {
          this.$message.success('删除成功')
          await this.fetchServerPatterns()
        }
      } catch (error) {
        if (error !== 'cancel') {
          console.error('删除花型失败', error)
        }
      }
    },
    syncTableHeights() {
      this.$nextTick(() => {
        const syncHeight = (cardRefName, tableRefName, stateKey, minHeight = 240) => {
          const card = this.$refs[cardRefName] && this.$refs[cardRefName].$el
          const table = this.$refs[tableRefName] && this.$refs[tableRefName].$el
          if (!card || !table) return
          const body = card.querySelector('.el-card__body')
          if (!body) return
          let occupied = 0
          Array.from(body.children).forEach(child => {
            if (child === table) return
            occupied += child.offsetHeight
          })
          this[stateKey] = Math.max(minHeight, body.clientHeight - occupied - 16)
        }

        syncHeight('serverPatternCard', 'serverPatternTable', 'serverTableHeight', 260)
        syncHeight('deviceFileCard', 'deviceFileTable', 'deviceFileTableHeight', 220)
        this.syncDownloadQueueTableHeight()
        this.syncUploadQueueTableHeight()
      })
    },
    syncDownloadQueueTableHeight() {
      this.$nextTick(() => {
        if (!this.downloadQueueDialogVisible) return
        const viewportHeight = window.innerHeight || document.documentElement.clientHeight || 900
        this.downloadQueueTableMaxHeight = Math.max(240, viewportHeight - 320)
      })
    },
    syncUploadQueueTableHeight() {
      this.$nextTick(() => {
        if (!this.uploadQueueDialogVisible) return
        const viewportHeight = window.innerHeight || document.documentElement.clientHeight || 900
        this.uploadQueueTableMaxHeight = Math.max(240, viewportHeight - 320)
      })
    }
  }
}
</script>

<style lang="scss" scoped>
.pattern-card {
  display: flex;
  flex-direction: column;
  min-height: 0;
}

.device-file-toolbar {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
  margin-bottom: 10px;
}

.action-row--device {
  margin-bottom: 12px;
}

.queue-toolbar {
  margin-bottom: 12px;
}

.dialog-tip {
  margin-top: 8px;
  font-size: 12px;
  line-height: 1.5;
  color: #909399;
}

.conflict-dialog-intro {
  margin-bottom: 14px;
  color: #606266;
  line-height: 1.7;
}

.conflict-mode-group {
  display: flex;
  gap: 20px;
  margin-bottom: 16px;
}

.conflict-rename-list,
.conflict-overwrite-list {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.conflict-rename-item,
.conflict-overwrite-item {
  padding: 10px 12px;
  border-radius: 10px;
  background: #f7f8fa;
}

.conflict-rename-label {
  margin-bottom: 8px;
  color: #303133;
  font-size: 13px;
}

.danger-text {
  color: #f56c6c !important;
}
</style>
