param([string]$QtRoot = "")

$ErrorActionPreference = "Stop"
$clientDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $clientDir "build-windows"
$distDir = Join-Path $clientDir "dist-windows"
$configure = @("-S", $clientDir, "-B", $buildDir, "-DCMAKE_BUILD_TYPE=Release")
if ($QtRoot) { $configure += "-DCMAKE_PREFIX_PATH=$QtRoot" }
& cmake @configure
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }
& cmake --build $buildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Qt client build failed." }

$candidatePaths = @(
    (Join-Path $buildDir "Release/bohr-qt-client.exe"),
    (Join-Path $buildDir "bohr-qt-client.exe")
)
$exe = $candidatePaths | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) { throw "Built bohr-qt-client.exe was not found." }
New-Item -ItemType Directory -Force -Path $distDir | Out-Null
Copy-Item $exe (Join-Path $distDir "bohr-qt-client.exe") -Force
$deploy = Get-Command windeployqt -ErrorAction SilentlyContinue
if (-not $deploy -and $QtRoot) {
    $candidate = Join-Path $QtRoot "bin/windeployqt.exe"
    if (Test-Path $candidate) { $deploy = $candidate }
}
if (-not $deploy) { throw "windeployqt was not found." }
& $deploy --release --no-translations --dir $distDir (Join-Path $distDir "bohr-qt-client.exe")
if ($LASTEXITCODE -ne 0) { throw "Qt runtime deployment failed." }
Write-Host "Windows Qt client package: $distDir"
