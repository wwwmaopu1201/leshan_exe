"""Bundle verified client/server installers and their matching update manifests."""
import hashlib
import json
from pathlib import Path
import zipfile

output = Path(__file__).resolve().parents[2]/'dist-windows-installers'
verification = json.loads((output/'verification.json').read_text())
files = []
versions = set()
for product in ('client', 'server'):
    release = json.loads((output/f'{product}.json').read_text())['releases']['windows-x86_64']
    installer = output/release['url'].rsplit('/', 1)[-1]
    digest = hashlib.sha256(installer.read_bytes()).hexdigest()
    assert digest == release['sha256'] == verification[product]['sha256']
    assert verification[product]['brandingVerified'] and verification[product]['languageSelectorVerified']
    versions.add(release['version'])
    files.extend([installer.name, f'{product}.json'])
assert len(versions) == 1
version = versions.pop()
readme = f'''Qt Windows x64 安装包 {version} / Windows x64 Installers

支持 Windows 10/11 x64。安装时可选择简体中文 / English。
客户端与服务端的安装及卸载界面使用各自程序 Logo。
安装器自动关闭旧客户端；服务端等待正常退出，升级前请备份数据库。
双语安装向导、桌面快捷方式与卸载入口均已配置。
默认在线更新目录：http://47.92.226.92:99/update/
本版英文品牌及程序文件名统一更正为 Bohr / bohr。升级兼容旧版 boer 进程，保留原有配置和数据库。
花型临时直传功能需配套 Qt 客户端和 Qt 服务端。
上传时选择“直接下发到机器（不留存）”，选择目标设备；服务端仅内存中转，不保存花型文件、花型库记录和下发日志。
取消不删除已写入机器的花型。请用现场机器验证接收、写入和重连。

发布：先上传两个 EXE，再上传 client.json 和 server.json 到上述目录。
JSON 包含本次安装包的实际 SHA-256，修改包后必须重新生成哈希。
低于当前版本的用户可检测到新版，安装同版本不会重复提示升级。

已完成交叉编译、DLL 依赖收集、安装包解压、逐文件哈希校验、Logo 素材及语言选择组件验证。
尚需 Windows 实机验证安装、启动、语言显示和覆盖升级。安装包未签名。
MSSQL 需要目标机器安装 Microsoft ODBC Driver。

Requires Windows 10/11 x64. Select Simplified Chinese or English when starting Setup.
Save your work and close the old version before installing. Back up the server database.
Upload both EXE files first, followed by client.json and server.json, to the update directory.
Installers are unsigned and still require installation and runtime testing on Windows.
'''
(output/'发布说明.txt').write_text(readme, encoding='utf-8-sig')
files.extend(['发布说明.txt', 'verification.json'])
bundle = output/f'bohr-qt-windows-{version}-release.zip'
with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_STORED) as archive:
    for filename in files:
        archive.write(output/filename, filename)
print(bundle)
