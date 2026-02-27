<template>
  <div class="page-container">
    <div class="manual-layout">
      <!-- 左侧目录 -->
      <div class="manual-sidebar">
        <h3>目录</h3>
        <el-menu
          :default-active="activeSection"
          class="manual-menu"
          @select="handleSelect"
        >
          <el-menu-item index="overview">
            <i class="el-icon-s-home"></i>
            <span>软件概述</span>
          </el-menu-item>
          <el-menu-item index="install">
            <i class="el-icon-download"></i>
            <span>安装指南</span>
          </el-menu-item>
          <el-menu-item index="login">
            <i class="el-icon-user"></i>
            <span>登录使用</span>
          </el-menu-item>
          <el-menu-item index="device">
            <i class="el-icon-monitor"></i>
            <span>设备管理</span>
          </el-menu-item>
          <el-menu-item index="pattern">
            <i class="el-icon-folder"></i>
            <span>花型管理</span>
          </el-menu-item>
          <el-menu-item index="statistics">
            <i class="el-icon-s-data"></i>
            <span>数据统计</span>
          </el-menu-item>
          <el-menu-item index="faq">
            <i class="el-icon-question"></i>
            <span>常见问题</span>
          </el-menu-item>
        </el-menu>
      </div>

      <!-- 右侧内容 -->
      <div class="manual-content">
        <div class="content-header">
          <h2>{{ $t('support.userManual') }}</h2>
          <div class="version">{{ $t('support.version') }}: v1.0.0</div>
        </div>

        <div class="content-body">
          <!-- 软件概述 -->
          <section id="overview" class="manual-section">
            <h3>1. 软件概述</h3>
            <p>
              博尔局域网管理软件是一款专为工业缝纫设备设计的局域网管理客户端。
              通过本软件，您可以轻松管理工厂内的所有缝纫设备，包括设备监控、花型下发、
              数据统计等功能。
            </p>
            <h4>主要功能:</h4>
            <ul>
              <li>设备实时监控和远程控制</li>
              <li>花型文件管理和批量下发</li>
              <li>生产数据统计和报表导出</li>
              <li>员工工资自动计算</li>
              <li>报警信息实时推送</li>
            </ul>
          </section>

          <!-- 安装指南 -->
          <section id="install" class="manual-section">
            <h3>2. 安装指南</h3>
            <h4>系统要求:</h4>
            <ul>
              <li>操作系统: Windows 10/11 64位</li>
              <li>内存: 4GB以上</li>
              <li>硬盘: 500MB可用空间</li>
              <li>网络: 局域网连接</li>
            </ul>
            <h4>安装步骤:</h4>
            <ol>
              <li>下载安装包 boer-lan-client-setup.exe</li>
              <li>双击运行安装程序</li>
              <li>按照提示完成安装</li>
              <li>安装完成后，桌面会生成快捷方式</li>
            </ol>
          </section>

          <!-- 登录使用 -->
          <section id="login" class="manual-section">
            <h3>3. 登录使用</h3>
            <p>首次使用需要配置服务器连接信息:</p>
            <ol>
              <li>输入服务器IP地址（向管理员获取）</li>
              <li>输入端口号（默认8088）</li>
              <li>输入用户名和密码</li>
              <li>点击登录按钮</li>
            </ol>
            <div class="tip-box">
              <i class="el-icon-info"></i>
              <span>提示: 勾选"记住密码"可在下次登录时自动填充。</span>
            </div>
          </section>

          <!-- 设备管理 -->
          <section id="device" class="manual-section">
            <h3>4. 设备管理</h3>
            <h4>添加设备:</h4>
            <ol>
              <li>进入"设备管理 > 设备列表"</li>
              <li>点击"添加设备"按钮</li>
              <li>填写设备信息（编号、名称、IP地址等）</li>
              <li>选择所属分组</li>
              <li>点击确定保存</li>
            </ol>
            <h4>设备分组:</h4>
            <p>您可以按车间或其他方式对设备进行分组管理，方便批量操作。</p>
          </section>

          <!-- 花型管理 -->
          <section id="pattern" class="manual-section">
            <h3>5. 花型管理</h3>
            <h4>上传花型:</h4>
            <ol>
              <li>进入"花型管理 > 花型列表"</li>
              <li>点击"上传文件"按钮</li>
              <li>选择花型文件（支持.dst, .dsb等格式）</li>
              <li>等待上传完成</li>
            </ol>
            <h4>下发花型:</h4>
            <ol>
              <li>选择要下发的花型文件</li>
              <li>点击"下发"按钮</li>
              <li>选择目标设备</li>
              <li>确认下发</li>
            </ol>
          </section>

          <!-- 数据统计 -->
          <section id="statistics" class="manual-section">
            <h3>6. 数据统计</h3>
            <p>系统提供多维度的数据统计功能:</p>
            <ul>
              <li><strong>工资统计:</strong> 按员工、设备、日期查看工资数据</li>
              <li><strong>加工概况:</strong> 查看设备加工件数、用线量等</li>
              <li><strong>时长统计:</strong> 统计设备运行、空闲、报警时长</li>
              <li><strong>报警统计:</strong> 分析报警类型和频率</li>
            </ul>
          </section>

          <!-- 常见问题 -->
          <section id="faq" class="manual-section">
            <h3>7. 常见问题</h3>
            <div class="faq-item">
              <div class="faq-q">Q: 无法连接服务器怎么办？</div>
              <div class="faq-a">A: 请检查网络连接是否正常，确认服务器IP和端口是否正确。</div>
            </div>
            <div class="faq-item">
              <div class="faq-q">Q: 设备显示离线怎么处理？</div>
              <div class="faq-a">A: 检查设备是否开机，网线是否连接正常，设备IP配置是否正确。</div>
            </div>
            <div class="faq-item">
              <div class="faq-q">Q: 花型下发失败怎么办？</div>
              <div class="faq-a">A: 确认设备在线，检查花型文件格式是否正确，必要时重新上传。</div>
            </div>
          </section>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
export default {
  name: 'Manual',
  data() {
    return {
      activeSection: 'overview'
    }
  },
  methods: {
    handleSelect(index) {
      this.activeSection = index
      const element = document.getElementById(index)
      if (element) {
        element.scrollIntoView({ behavior: 'smooth', block: 'start' })
      }
    }
  }
}
</script>

<style lang="scss" scoped>
.manual-layout {
  display: flex;
  gap: 20px;
  min-height: calc(100vh - 140px);
}

.manual-sidebar {
  width: 220px;
  background: #fff;
  border-radius: 8px;
  padding: 20px;
  position: sticky;
  top: 20px;
  height: fit-content;

  h3 {
    margin: 0 0 15px;
    padding-bottom: 10px;
    border-bottom: 1px solid #eee;
    color: #303133;
  }

  .manual-menu {
    border: none;
  }
}

.manual-content {
  flex: 1;
  background: #fff;
  border-radius: 8px;
  padding: 30px;

  .content-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 30px;
    padding-bottom: 20px;
    border-bottom: 1px solid #eee;

    h2 {
      margin: 0;
      color: #303133;
    }

    .version {
      color: #909399;
      font-size: 13px;
    }
  }
}

.manual-section {
  margin-bottom: 40px;

  h3 {
    color: #303133;
    margin-bottom: 15px;
    padding-bottom: 10px;
    border-bottom: 2px solid #409EFF;
  }

  h4 {
    color: #606266;
    margin: 20px 0 10px;
  }

  p {
    color: #606266;
    line-height: 1.8;
  }

  ul, ol {
    color: #606266;
    line-height: 2;
    padding-left: 20px;
  }
}

.tip-box {
  margin-top: 15px;
  padding: 15px;
  background: #ecf5ff;
  border-radius: 4px;
  color: #409EFF;
  display: flex;
  align-items: center;

  i {
    margin-right: 10px;
    font-size: 18px;
  }
}

.faq-item {
  margin-bottom: 20px;

  .faq-q {
    font-weight: 600;
    color: #303133;
    margin-bottom: 8px;
  }

  .faq-a {
    color: #606266;
    padding-left: 20px;
    line-height: 1.8;
  }
}
</style>
