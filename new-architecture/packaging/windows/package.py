"""Collect the transitive PE imports and create a per-user NSIS installer."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

product, build_arg, qt_arg, output_arg = sys.argv[1:]
source = Path('/src/new-architecture')
build, qt, output = map(Path, (build_arg, qt_arg, output_arg))
version = re.search(r'project\([^\n]+ VERSION ([\d.]+)', (source/product/'CMakeLists.txt').read_text())[1]
payload = output / f'{product}-payload'
payload.mkdir(parents=True, exist_ok=True)
exe = f'bohr-qt-{product}.exe'
# Reused payload directories can contain an executable from the previous brand.
(payload/f'boer-qt-{product}.exe').unlink(missing_ok=True)
shutil.copy2(build/exe, payload/exe)
for category in ('platforms', 'styles', 'imageformats', 'iconengines', 'tls', 'networkinformation'):
    directory = qt/'plugins'/category
    if directory.exists():
        (payload/category).mkdir(exist_ok=True)
        for item in directory.glob('*.dll'):
            if not (item.stem.endswith('d') and item.with_name(item.stem[:-1]+'.dll').exists()):
                shutil.copy2(item, payload/category/item.name)
if product == 'server':
    (payload/'sqldrivers').mkdir(exist_ok=True)
    for name in ('qsqlite.dll', 'qsqlodbc.dll', 'qsqlmysql.dll'):
        item = qt/'plugins/sqldrivers'/name
        if item.exists():
            shutil.copy2(item, payload/'sqldrivers'/name)
        else:
            raise RuntimeError(f'Required database plugin missing: {item}')
    for plugin in Path('/opt/mariadb/lib/mariadb/plugin').glob('*.dll'):
        shutil.copy2(plugin, payload/plugin.name)
    shutil.copy2(source/'server/config.example.yaml', payload/'config.example.yaml')
# Search Qt and compiler runtime directories; never substitute host Linux libraries.
search_dirs = [qt/'bin', Path('/usr/x86_64-w64-mingw32/lib')]
search_dirs += list(Path('/usr/lib/gcc/x86_64-w64-mingw32').glob('*-posix'))
index = {p.name.lower(): p for directory in search_dirs for p in directory.glob('*.dll')}
# Windows system DLLs are supplied by the OS; all other imports must be bundled.
system = set('odbc32 odbccp32 advapi32 authz bcrypt cfgmgr32 dhcpcsvc d3d12 d3dcompiler_47 dxva2 hid msimg32 pdh shcore wevtapi win32u winspool.drv winusb comctl32 comdlg32 crypt32 d2d1 d3d11 d3d9 dbghelp dnsapi dwmapi dwrite dxgi gdi32 imm32 iphlpapi kernel32 mpr msvcrt mswsock ncrypt netapi32 normaliz ntdll ole32 oleaut32 opengl32 powrprof propsys psapi rpcrt4 secur32 setupapi shell32 shlwapi synchronization ucrtbase urlmon user32 userenv usp10 uxtheme version winhttp wininet winmm winspool wintrust wldap32 ws2_32 wtsapi32'.split())
checked = set()
queue = list(payload.rglob('*.dll')) + [payload/exe]
while queue:
    item = queue.pop()
    if item in checked:
        continue
    checked.add(item)
    imports = subprocess.check_output(['x86_64-w64-mingw32-objdump', '-p', str(item)], text=True)
    for name in re.findall(r'DLL Name:\s*(\S+)', imports):
        lower = name.lower()
        if lower.removesuffix('.dll') in system or lower.startswith(('api-ms-win-', 'ext-ms-win-')):
            continue
        if lower not in index:
            raise RuntimeError(f'Unresolved dependency {name} of {item.name}')
        target = payload/index[lower].name
        if not target.exists():
            shutil.copy2(index[lower], target)
            queue.append(target)
(payload/'qt.conf').write_text('[Paths]\nPlugins=.\n', encoding='utf-8')
licenses = payload/'licenses'
licenses.mkdir(exist_ok=True)
for name in ('LGPL-3', 'GPL-3'):
    shutil.copy2(Path('/usr/share/common-licenses')/name, licenses/name)
for package in ('qt6-base-dev', 'libqt6svg6'):
    notice = Path('/usr/share/doc')/package/'copyright'
    if notice.exists():
        shutil.copy2(notice, licenses/(package+'-copyright.txt'))
if product == 'server':
    for notice in Path('/build/mysql-driver/mariadb-src').glob('*COPYING*'):
        if notice.is_file():
            shutil.copy2(notice, licenses/('MariaDB-'+notice.name))
# Preserve existing update-config.json on upgrade by using the compiled-in defaults.
readme = 'Windows 10/11 x64。安装前退出旧版。用户设置和服务端数据位于原有用户数据目录，安装和卸载不删除这些数据。\n此构建为交叉编译版本，尚需 Windows 实机验收。\n'
(payload/'安装说明.txt').write_text(readme, encoding='utf-8-sig')
# Delete only shipped files, leaving any extra configuration or data untouched.
uninstall = build/'uninstall-files.nsh'
lines = ['Delete "$INSTDIR\\'+str(p.relative_to(payload)).replace('/', '\\')+'"' for p in payload.rglob('*') if p.is_file()]
lines += ['RMDir "$INSTDIR\\'+str(p.relative_to(payload)).replace('/', '\\')+'"' for p in sorted(payload.rglob('*'), key=lambda p: len(p.parts), reverse=True) if p.is_dir()]
uninstall.write_text('\n'.join(lines)+'\n')
installer = output/f'bohr-qt-{product}-{version}-setup.exe'
# Close old versions too: their updater may not yet quit after launching Setup.
# Use WM_CLOSE first so normal Qt shutdown can flush data. Only the client gets
# a forced fallback; the server must finish shutdown before installation proceeds.
shutdown = f"""$ErrorActionPreference='Stop';
$session=[System.Diagnostics.Process]::GetCurrentProcess().SessionId;
$items=@(Get-Process -Name 'boer-qt-{product}','bohr-qt-{product}' -ErrorAction SilentlyContinue | Where-Object {{$_.SessionId -eq $session}});
foreach($p in $items) {{
 try {{
 if($p.HasExited) {{continue}};
 [void]$p.CloseMainWindow();
 if(-not $p.WaitForExit(20000)) {{
  {'Stop-Process -Id $p.Id -Force; if(-not $p.WaitForExit(10000)) {exit 1}' if product == 'client' else 'exit 1'}
 }};
 }} catch {{ if(Get-Process -Id $p.Id -ErrorAction SilentlyContinue) {{exit 1}} }};
}};
exit 0;
"""
close_script = build/'close-process.ps1'
close_script.write_text(shutdown, encoding='utf-8-sig')
subprocess.run(['makensis', '-V2', f'-DCLOSE_PROCESS_SCRIPT={close_script}', f'-DPRODUCT={product}', f'-DAPP_EXE={exe}', f'-DAPP_VERSION={version}', f'-DAPP_LABEL={"客户端" if product == "client" else "服务端"}', f'-DAPP_LABEL_EN={product.title()}', f'-DBRANDING_DIR={source/"packaging/windows/branding"}', f'-DAPP_ICON={source/product/"assets"/("client-icon.ico" if product == "client" else "app-icon.ico")}', f'-DPAYLOAD={payload}', f'-DOUTPUT_FILE={installer}', f'-DUNINSTALL_FILES={uninstall}', str(source/'packaging/windows/installer.nsi')], check=True)
manifest = {'product': product, 'releases': {'windows-x86_64': {'version': version, 'notes': '1. 英文品牌名称统一更正为 Bohr。\n2. 程序和安装包文件名统一使用 bohr，升级兼容旧版 boer 进程与原有配置。\n3. 保留花型临时直传及原有功能。', 'url': f'http://47.92.226.92:99/update/{installer.name}', 'sha256': hashlib.sha256(installer.read_bytes()).hexdigest()}}}
if product == 'server':
    manifest['releases']['windows-x86_64']['notes'] = f'{version} 英文品牌及安装包名称更正为 Bohr，保留现有数据库位置和旧版本升级兼容。'
(output/f'{product}.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(f'Installer: {installer}', flush=True)
