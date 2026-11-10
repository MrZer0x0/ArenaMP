# Build pinned OpenSSL 1.1.1w shared DLLs for the existing Qt 5.15.2 Windows SDK.
# The independent Actions cache avoids compiling or fetching these on subsequent builds.
[CmdletBinding()]
param([string]$Destination = '.cache/qt5-openssl-1.1.1w')
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Destination = [IO.Path]::GetFullPath($Destination)
$expected = @('libssl-1_1-x64.dll', 'libcrypto-1_1-x64.dll', 'LICENSE')
if (@($expected | Where-Object { -not (Test-Path (Join-Path $Destination $_)) }).Count -eq 0) {
    Write-Host 'Using cached Qt 5 TLS/OpenSSL 1.1.1w DLLs.'
    exit 0
}

$work = Join-Path $env:RUNNER_TEMP 'arenamp-qt5-tls-build'
if (-not $env:RUNNER_TEMP) { $work = Join-Path $env:TEMP 'arenamp-qt5-tls-build' }
New-Item -ItemType Directory -Force $work | Out-Null
$archive = Join-Path $work 'openssl-1.1.1w.tar.gz'
$source = Join-Path $work 'openssl-1.1.1w'
$sha256 = 'cf3098950cb4d853ad95c0841f1f9c6d3dc102dccfcacd521d93925208b76ac8'
$urls = @(
    'https://www.openssl-library.org/source/old/1.1.1/openssl-1.1.1w.tar.gz',
    'https://mirror.openssl-library.org/source/old/1.1.1/openssl-1.1.1w.tar.gz',
    'https://github.com/openssl/openssl/releases/download/OpenSSL_1_1_1w/openssl-1.1.1w.tar.gz'
)
$downloaded = $false
foreach ($url in $urls) {
    if ($downloaded) { break }
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        try {
            Write-Host "Fetching OpenSSL 1.1.1w from $url (attempt $attempt)"
            Invoke-WebRequest -Uri $url -OutFile $archive -MaximumRedirection 10 -TimeoutSec 90
            $actual = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($actual -ne $sha256) { throw "Unexpected source checksum: $actual" }
            $downloaded = $true
            break
        } catch {
            Write-Warning $_.Exception.Message
            Remove-Item $archive -Force -ErrorAction SilentlyContinue
            if ($attempt -lt 3) { Start-Sleep -Seconds (3 * $attempt) }
        }
    }
}
if (-not $downloaded) { throw 'Cannot download verified OpenSSL 1.1.1w source from any mirror. TLS runtime cache was not present.' }
if (Test-Path $source) { Remove-Item $source -Recurse -Force }
& tar -xzf $archive -C $work
if ($LASTEXITCODE -ne 0 -or -not (Test-Path (Join-Path $source 'Configure'))) {
    throw 'OpenSSL archive extraction failed'
}
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'MSVC 2022 not found' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "Missing MSVC environment: $vcvars" }
$batch = Join-Path $work 'build-tls.cmd'
@"
@echo off
call "$vcvars"
if errorlevel 1 exit /b 1
cd /d "$source"
perl Configure VC-WIN64A no-asm no-tests shared
if errorlevel 1 exit /b 1
nmake
if errorlevel 1 exit /b 1
"@ | Set-Content -Path $batch -Encoding ASCII
& cmd.exe /c $batch
if ($LASTEXITCODE -ne 0) { throw "OpenSSL 1.1.1w Windows build failed with code $LASTEXITCODE" }
foreach ($name in $expected) {
    if (-not (Test-Path (Join-Path $source $name))) { throw "Missing built OpenSSL output: $name" }
}
New-Item -ItemType Directory -Force $Destination | Out-Null
foreach ($name in $expected) { Copy-Item (Join-Path $source $name) (Join-Path $Destination $name) -Force }
Write-Host 'OpenSSL 1.1.1w DLLs built and ready to cache.'
