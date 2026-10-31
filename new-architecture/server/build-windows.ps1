param(
    [string]$QtRoot = "",
    [string]$MySqlClientDll = ""
)

$ErrorActionPreference = "Stop"
$serverDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $serverDir "build-windows"
$distDir = Join-Path $serverDir "dist-windows"

$configure = @("-S", $serverDir, "-B", $buildDir, "-DCMAKE_BUILD_TYPE=Release")
if ($QtRoot) {
    $configure += "-DCMAKE_PREFIX_PATH=$QtRoot"
}
& cmake @configure
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

& cmake --build $buildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Qt server build failed." }

$candidatePaths = @(
    (Join-Path $buildDir "Release/bohr-qt-server.exe"),
    (Join-Path $buildDir "bohr-qt-server.exe")
)
$exe = $candidatePaths | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) { throw "Built bohr-qt-server.exe was not found." }

New-Item -ItemType Directory -Force -Path $distDir | Out-Null
Copy-Item $exe (Join-Path $distDir "bohr-qt-server.exe") -Force

$deploy = Get-Command windeployqt -ErrorAction SilentlyContinue
if (-not $deploy -and $QtRoot) {
    $candidate = Join-Path $QtRoot "bin/windeployqt.exe"
    if (Test-Path $candidate) { $deploy = $candidate }
}
if (-not $deploy) { throw "windeployqt was not found. Add Qt bin to PATH or pass -QtRoot." }
& $deploy --release --no-translations --dir $distDir (Join-Path $distDir "bohr-qt-server.exe")
if ($LASTEXITCODE -ne 0) { throw "Qt runtime deployment failed." }

$deployPath = if ($deploy -is [string]) { $deploy } else { $deploy.Source }
$qtPrefix = if ($QtRoot) { $QtRoot } else { Split-Path -Parent (Split-Path -Parent $deployPath) }
$driverDir = Join-Path $qtPrefix "plugins/sqldrivers"
$targetDrivers = Join-Path $distDir "sqldrivers"
New-Item -ItemType Directory -Force -Path $targetDrivers | Out-Null
foreach ($driver in @("qsqlmysql.dll", "qsqlodbc.dll", "qsqlite.dll")) {
    $source = Join-Path $driverDir $driver
    if (Test-Path $source) { Copy-Item $source $targetDrivers -Force }
}
foreach ($required in @("qsqlmysql.dll", "qsqlodbc.dll", "qsqlite.dll")) {
    if (-not (Test-Path (Join-Path $targetDrivers $required))) {
        throw "$required is missing. Install/build the matching Qt SQL driver before packaging."
    }
}

if (-not $MySqlClientDll) {
    foreach ($candidate in @((Join-Path $qtPrefix "bin/libmysql.dll"), (Join-Path $qtPrefix "bin/libmariadb.dll"))) {
        if (Test-Path $candidate) { $MySqlClientDll = $candidate; break }
    }
}
if (-not $MySqlClientDll -or -not (Test-Path $MySqlClientDll)) {
    throw "The MySQL/MariaDB client DLL is missing. Pass -MySqlClientDll with libmysql.dll or libmariadb.dll."
}
Copy-Item $MySqlClientDll $distDir -Force

Write-Host "Windows Qt server package: $distDir"
Write-Host "MSSQL connections also require Microsoft ODBC Driver 18 on the target machine."
