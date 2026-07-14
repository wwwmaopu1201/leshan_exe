param(
  [Parameter(Mandatory = $true)]
  [string]$Destination
)

$ErrorActionPreference = "Stop"

$chromiumVersion = "109.0.5414.120-1.1"
$archiveName = "ungoogled-chromium_${chromiumVersion}_windows_x64.zip"
$archiveUrl = "https://github.com/ungoogled-software/ungoogled-chromium-windows/releases/download/${chromiumVersion}/${archiveName}"
$expectedSha256 = "655b53273ef0b35fa2db5d224af8039131e254296e51a8618f26088ae5e57cba"
$runtimeRootName = "ungoogled-chromium_${chromiumVersion}_windows"
$workDir = Join-Path $env:RUNNER_TEMP "boerlan-chromium-${chromiumVersion}"
$archivePath = Join-Path $workDir $archiveName
$extractDir = Join-Path $workDir "extracted"
$runtimeSource = Join-Path $extractDir $runtimeRootName

Remove-Item -Recurse -Force $workDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

Write-Host "Downloading Chromium $chromiumVersion..."
Invoke-WebRequest -Uri $archiveUrl -OutFile $archivePath

$actualSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $archivePath).Hash.ToLowerInvariant()
if ($actualSha256 -ne $expectedSha256) {
  throw "Chromium archive SHA-256 mismatch. Expected $expectedSha256, got $actualSha256."
}

Expand-Archive -LiteralPath $archivePath -DestinationPath $extractDir -Force
if (-not (Test-Path -LiteralPath (Join-Path $runtimeSource "chrome.exe"))) {
  throw "Chromium archive does not contain the expected chrome.exe."
}

Remove-Item -Recurse -Force $Destination -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
Copy-Item -Recurse -Force -LiteralPath $runtimeSource -Destination $Destination

$productVersion = (Get-Item -LiteralPath (Join-Path $Destination "chrome.exe")).VersionInfo.ProductVersion
if (-not $productVersion.StartsWith("109.0.5414.120")) {
  throw "Unexpected bundled Chromium version: $productVersion"
}

@"
Bundled browser runtime: ungoogled-chromium $chromiumVersion (x64)
Upstream release: https://github.com/ungoogled-software/ungoogled-chromium-windows/releases/tag/$chromiumVersion
Archive SHA-256: $expectedSha256

This frozen Chromium 109 runtime is bundled only for Windows 7 compatibility.
Windows 7 and Chromium 109 no longer receive security updates; use the Windows 10 package where possible.
"@ | Set-Content -Encoding UTF8 (Join-Path $Destination "RUNTIME-INFO.txt")

Write-Host "Bundled Chromium $productVersion at $Destination"
