<template>
  <div class="page-container">
    <div class="manual-layout">
      <aside class="manual-sidebar">
        <div class="sidebar-head">
          <h3>操作目录</h3>
          <p>快速跳转到对应章节</p>
        </div>

        <el-menu :default-active="activeSection" class="manual-menu" @select="handleSelect">
          <el-menu-item v-for="item in sections" :key="item.id" :index="item.id">
            <span class="menu-badge"><i :class="item.icon"></i></span>
            <span>{{ item.label }}</span>
          </el-menu-item>
        </el-menu>
      </aside>

      <section class="manual-content">
        <div class="content-header">
          <div>
            <h2>{{ $t('support.userManual') }}</h2>
            <p>帮助安装、登录、设备管理和日常排查快速上手。</p>
          </div>
          <div class="header-actions">
            <div class="version">{{ $t('support.version') }}: v1.0.9</div>
            <el-button size="small" type="primary" icon="el-icon-download" @click="downloadManual">
              下载说明
            </el-button>
          </div>
        </div>

        <div class="content-body">
          <section id="overview" class="manual-section">
            <h3>1. 软件概述</h3>
            <p>
              博尔局域网管理软件是一款面向工业绣花设备的局域网客户端，
              用于设备状态监控、花型文件管理、统计分析和日常维护。
            </p>
            <div class="manual-points">
              <div class="point-card">
                <i class="el-icon-monitor"></i>
                <span>设备实时监控和远程查看</span>
              </div>
              <div class="point-card">
                <i class="el-icon-folder-opened"></i>
                <span>花型文件上传、下发和回传</span>
              </div>
              <div class="point-card">
                <i class="el-icon-data-analysis"></i>
                <span>工资、效率、时长、报警多维统计</span>
              </div>
            </div>
          </section>

          <section id="install" class="manual-section">
            <h3>2. 安装指南</h3>
            <ul>
              <li>操作系统：Windows 10/11 64 位</li>
              <li>建议内存：4GB 以上</li>
              <li>安装空间：500MB 以上</li>
              <li>网络要求：与服务器和设备处于同一局域网</li>
            </ul>
            <ol>
              <li>下载安装包 `boer-lan-client-setup.exe`。</li>
              <li>双击启动安装程序并完成安装。</li>
              <li>首次运行前，确认服务端地址和端口。</li>
            </ol>
          </section>

          <section id="login" class="manual-section">
            <h3>3. 登录使用</h3>
            <ol>
              <li>输入服务器 IP 地址。</li>
              <li>输入管理端口，默认 `8088`。</li>
              <li>填写账号和密码，点击登录。</li>
              <li>需要时可勾选“记住密码”。</li>
            </ol>
            <div class="tip-box">
              <i class="el-icon-info"></i>
              <span>如果登录失败，请先确认客户端与服务端网络是否互通。</span>
            </div>
          </section>

          <section id="device" class="manual-section">
            <h3>4. 设备管理</h3>
            <ol>
              <li>进入“设备管理”，在左侧设备树中查看分组与设备。</li>
              <li>可直接在设备树中新增、重命名、移动或删除分组。</li>
              <li>双击设备行可快速编辑设备信息。</li>
              <li>未分组设备会高亮提示，便于快速整理。</li>
            </ol>
          </section>

          <section id="pattern" class="manual-section">
            <h3>5. 花型管理</h3>
            <ol>
              <li>上传花型文件时，可填写花型类型、针数、工价和订单号。</li>
              <li>选择目标设备后可批量下发花型。</li>
              <li>设备端回传的花型文件可在“设备花型文件”模块统一处理。</li>
            </ol>
          </section>

          <section id="statistics" class="manual-section">
            <h3>6. 数据统计</h3>
            <ul>
              <li>工资统计：查看员工工资、加工件数和趋势。</li>
              <li>加工概况：查看设备产量、用线量和效率分布。</li>
              <li>时长统计：统计运行、空闲、报警时长。</li>
              <li>报警统计：分析报警类型与时间趋势。</li>
            </ul>
          </section>

          <section id="faq" class="manual-section">
            <h3>7. 常见问题</h3>
            <div class="faq-item">
              <div class="faq-q">Q：无法连接服务器怎么办？</div>
              <div class="faq-a">A：请检查网络连接，确认服务器 IP 和管理端口是否正确。</div>
            </div>
            <div class="faq-item">
              <div class="faq-q">Q：设备显示离线怎么处理？</div>
              <div class="faq-a">A：确认设备是否开机、网线是否正常、设备地址是否配置正确。</div>
            </div>
            <div class="faq-item">
              <div class="faq-q">Q：花型下发失败怎么办？</div>
              <div class="faq-a">A：检查设备是否在线，确认花型格式正确，并重试下发。</div>
            </div>
          </section>
        </div>
      </section>
    </div>
  </div>
</template>

<script>
export default {
  name: 'Manual',
  data() {
    return {
      activeSection: 'overview',
      sections: [
        { id: 'overview', label: '软件概述', icon: 'el-icon-reading' },
        { id: 'install', label: '安装指南', icon: 'el-icon-download' },
        { id: 'login', label: '登录使用', icon: 'el-icon-user' },
        { id: 'device', label: '设备管理', icon: 'el-icon-monitor' },
        { id: 'pattern', label: '花型管理', icon: 'el-icon-folder-opened' },
        { id: 'statistics', label: '数据统计', icon: 'el-icon-data-analysis' },
        { id: 'faq', label: '常见问题', icon: 'el-icon-question' }
      ]
    }
  },
  methods: {
    handleSelect(index) {
      this.activeSection = index
      const element = document.getElementById(index)
      if (element) {
        element.scrollIntoView({ behavior: 'smooth', block: 'start' })
      }
    },
    downloadManual() {
      const url = `${process.env.BASE_URL}manuals/client-user-manual.pdf`
      const link = document.createElement('a')
      link.href = url
      link.download = '局域网客户端操作说明-v1.0.9.pdf'
      document.body.appendChild(link)
      link.click()
      document.body.removeChild(link)
      this.$message.success('操作说明 PDF 已下载')
    }
  }
}
</script>

<style lang="scss" scoped>
.manual-layout {
  display: flex;
  gap: 20px;
  min-height: calc(100vh - 132px);
}

.manual-sidebar {
  width: 250px;
  background: #fff;
  border: 1px solid rgba(221, 229, 240, 0.92);
  border-radius: 22px;
  padding: 20px;
  box-shadow: 0 18px 36px rgba(59, 87, 132, 0.08);
  position: sticky;
  top: 20px;
  height: fit-content;
  align-self: flex-start;
}

.sidebar-head {
  margin-bottom: 18px;

  h3 {
    margin-bottom: 6px;
    color: #243654;
  }

  p {
    color: #8594aa;
    font-size: 12px;
  }
}

.manual-menu {
  border: none;
}

.menu-badge {
  width: 28px;
  height: 28px;
  border-radius: 12px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  background: #f3f7ff;
  color: #2f6df6;
  margin-right: 10px;
}

.manual-content {
  flex: 1;
  min-width: 0;
  background: #fff;
  border: 1px solid rgba(221, 229, 240, 0.92);
  border-radius: 22px;
  padding: 26px 28px;
  box-shadow: 0 18px 36px rgba(59, 87, 132, 0.08);
}

.content-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 20px;
  padding-bottom: 18px;
  margin-bottom: 24px;
  border-bottom: 1px solid #e3ebf5;

  h2 {
    margin-bottom: 8px;
    color: #22324d;
  }

  p {
    color: #8190a6;
  }
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 12px;
}

.version {
  color: #8b99ad;
  font-size: 12px;
}

.content-body {
  min-height: calc(100vh - 260px);
}

.manual-section {
  margin-bottom: 38px;

  h3 {
    margin-bottom: 16px;
    color: #243654;
  }

  p,
  li {
    color: #657994;
    line-height: 1.9;
  }

  ul,
  ol {
    padding-left: 18px;
  }
}

.manual-points {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 14px;
  margin-top: 18px;
}

.point-card {
  padding: 16px;
  border-radius: 18px;
  background: #f7faff;
  display: flex;
  align-items: center;
  gap: 12px;

  i {
    width: 40px;
    height: 40px;
    border-radius: 14px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    background: rgba(47, 109, 246, 0.12);
    color: #2f6df6;
  }
}

.tip-box {
  margin-top: 16px;
  padding: 16px 18px;
  border-radius: 16px;
  background: #edf4ff;
  color: #2f6df6;
  display: flex;
  align-items: center;
  gap: 10px;
}

.faq-item {
  padding: 16px 18px;
  border-radius: 18px;
  background: #f7faff;
  margin-bottom: 12px;
}

.faq-q {
  font-weight: 700;
  color: #243654;
  margin-bottom: 8px;
}

.faq-a {
  color: #657994;
  line-height: 1.8;
}

::v-deep .manual-menu .el-menu-item {
  height: 46px;
  line-height: 46px;
  border-radius: 14px;
  margin-bottom: 8px;
}

::v-deep .manual-menu .el-menu-item.is-active {
  background: rgba(47, 109, 246, 0.12);
  color: #2f6df6;
}

@media (max-width: 980px) {
  .manual-layout {
    flex-direction: column;
  }

  .manual-sidebar {
    width: 100%;
    position: static;
  }

  .manual-points {
    grid-template-columns: 1fr;
  }

  .content-header {
    flex-direction: column;
    align-items: flex-start;
  }
}
</style>
