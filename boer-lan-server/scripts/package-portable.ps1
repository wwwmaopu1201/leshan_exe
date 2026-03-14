$ErrorActionPreference = 'Stop'

$appVersion = 'V1.0.1'
$appExeName = "Boer-LAN-Server-$appVersion.exe"

$releaseDir = (Resolve-Path (Join-Path $PSScriptRoot '..\src-tauri\target\release')).Path
$portableRoot = Join-Path $releaseDir 'portable'
$appDir = Join-Path $portableRoot 'Boer-LAN-Server'
$archivePath = Join-Path $portableRoot 'Boer-LAN-Server-windows-portable.zip'

$appExe = @(
  (Join-Path $releaseDir 'boer-lan-server.exe'),
  (Join-Path $releaseDir 'Boer-LAN-Server.exe')
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $appExe) {
  throw "Tauri executable not found in $releaseDir"
}

$backendExe = Join-Path $releaseDir 'backend-server.exe'
if (-not (Test-Path $backendExe)) {
  throw "Backend executable not found: $backendExe"
}

if (Test-Path $appDir) {
  Remove-Item $appDir -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $appDir | Out-Null
Copy-Item $appExe (Join-Path $appDir $appExeName) -Force
Copy-Item $backendExe (Join-Path $appDir 'backend-server.exe') -Force

@"
博尔局域网服务器便携版

1. 解压后直接运行 $appExeName
2. 数据目录会在首次启动后自动创建
3. 服务端配置使用程序内置默认值
4. 如系统缺少 WebView2，请先安装 WebView2 Runtime
"@ | Set-Content -Path (Join-Path $appDir '启动说明.txt') -Encoding UTF8

if (Test-Path $archivePath) {
  Remove-Item $archivePath -Force
}

Compress-Archive -Path (Join-Path $appDir '*') -DestinationPath $archivePath -Force
Write-Host "Portable package created: $archivePath"
