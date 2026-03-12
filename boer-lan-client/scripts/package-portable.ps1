$ErrorActionPreference = 'Stop'

$releaseDir = (Resolve-Path (Join-Path $PSScriptRoot '..\src-tauri\target\release')).Path
$portableRoot = Join-Path $releaseDir 'portable'
$appDir = Join-Path $portableRoot 'Boer-LAN-Manager'
$archivePath = Join-Path $portableRoot 'Boer-LAN-Manager-windows-portable.zip'

$appExe = @(
  (Join-Path $releaseDir 'boer-lan-client.exe'),
  (Join-Path $releaseDir 'Boer-LAN-Manager.exe')
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $appExe) {
  throw "Tauri executable not found in $releaseDir"
}

if (Test-Path $appDir) {
  Remove-Item $appDir -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $appDir | Out-Null
Copy-Item $appExe (Join-Path $appDir 'Boer-LAN-Manager.exe') -Force

@"
博尔局域网管理软件便携版

1. 解压后直接运行 Boer-LAN-Manager.exe
2. 如系统缺少 WebView2，请先安装 WebView2 Runtime
"@ | Set-Content -Path (Join-Path $appDir '启动说明.txt') -Encoding UTF8

if (Test-Path $archivePath) {
  Remove-Item $archivePath -Force
}

Compress-Archive -Path (Join-Path $appDir '*') -DestinationPath $archivePath -Force
Write-Host "Portable package created: $archivePath"
