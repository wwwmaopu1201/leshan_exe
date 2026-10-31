# Windows Qt 安装包

使用 Docker 在 macOS（Apple Silicon）交叉编译 Windows x64 程序并由 NSIS 生成安装器：

```bash
docker build -t boer-qt-windows-builder:6.8.2 new-architecture/packaging/windows
docker run --rm --name boer-qt-windows-package \
  -v "$PWD/new-architecture:/src/new-architecture" \
  -v "$PWD/boer-lan-client:/src/boer-lan-client:ro" \
  -v "$PWD/boer-lan-server:/src/boer-lan-server:ro" \
  -v boer-qt-windows-build:/build \
  boer-qt-windows-builder:6.8.2 \
  bash /src/new-architecture/packaging/windows/build.sh
```

构建目录保存在 Docker volume，产物位于 `new-architecture/dist-windows-installers/`，包含两端安装包、payload（依赖收集目录）、client.json、server.json。
默认升级服务器已写入应用代码。将安装包及两个 JSON 上传到 `http://47.92.226.92:99/update/`，JSON 哈希由实际安装包生成。版本号取各工程的 CMake project VERSION，发布下一版时需提升版本。

安装器按当前用户安装到 LocalAppData/Programs/BohrQt（已有安装沿用原位置），创建桌面、开始菜单快捷方式与系统卸载入口。安装器向当前会话的旧进程发送关闭请求并等待退出；客户端 20 秒后仍不退出才结束该进程，服务端不强制结束，超时提示重试。再次安装使用相同位置，覆盖程序文件。卸载仅删除安装包携带的文件，保留额外配置及应用用户数据。

运行平台为 Windows 10/11 x64。包含 SQLite、ODBC 和 MySQL/MariaDB Qt 驱动及其运行依赖；MSSQL 仍需目标机器安装 Microsoft ODBC Driver。Qt 库动态链接，可在 payload 中替换。安装包未做代码签名，Windows 可能显示未知发布者。

交叉编译、PE 依赖检查、NSIS 编译不代替 Windows 实机验证。交付前后需在 Windows 验证安装、启动、登录、数据保存、覆盖升级、卸载与在线下载流程。服务端升级前退出并备份数据库。

## 0.1.1 安装界面

启动安装器时可选择简体中文或 English，后续安装页、自定义提示及系统卸载名称使用所选语言。选择保存在对应产品的 HKCU 注册表键中，卸载时读取同一语言。快捷方式使用固定的产品名，避免切换安装语言后产生重复快捷方式。

安装器图标、卸载图标、欢迎及完成页侧图和内页页眉图均取自对应产品现有应用 Logo。素材在 `branding/`；更新原图后可使用安装了 Pillow 的 Python 运行 `build-branding.py` 重新生成。

0.1.2 修复：Windows 无原生标题栏的圆角窗口，保留自绘拖动/缩放与窗口按钮；升级对话框居中且不能拖动；Windows 打开安装包后应用自动退出。安装器也兼容关闭尚无自动退出能力的旧客户端。

0.1.3：公共按钮过滤器统一取消鼠标获取的持久焦点、关闭普通按钮的隐式默认行为，保留显式默认确认按钮、Tab/空格导航及真实选中状态；点击反馈 180ms 后清除，快速连点按最后一次计时。新增 button_feedback 回归测试。

0.1.4：用统一下载面板替换标准 QProgressDialog，扩大尺寸并固定居中；支持软件内实时中英文切换（标题、提示、取消、完成及安装确认）。新增 update_progress 回归测试，覆盖两端、中英往返、移动锁定、父窗口移动、未知总大小、进度保持及取消信号。

0.1.5：Windows 不再额外创建独立标题行，窗口按钮并入原有顶栏/登录页头；新增界面动态头部检查，顶栏显示真实版本。更新流程旧窗口销毁后才创建下一窗口；安装器语言页、欢迎页及后续步骤在同一固定窗口切换，移除安装器系统标题栏。

## 0.1.6 范围更正

安装器与卸载器恢复标准 NSIS 窗口标题栏、关闭按钮、语言选择及向导，删除所有安装器标题栏移除和位置锁定钩子。无独立白色标题栏只应用于 Qt 程序主窗口/登录窗口；固定居中与步骤先关闭再打开只用于软件内更新弹窗。此前 0.1.5 安装器锁定窗口的实现已撤回。

2.1.11 起，英文品牌为 Bohr，EXE 和安装包文件名使用 bohr。内部 CMake target、环境变量、QSettings 和数据库路径、安装注册表键保留旧标识，保证升级兼容；安装器兼容关闭 boer/bohr 两种进程并移除旧 EXE。
