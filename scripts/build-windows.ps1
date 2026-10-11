# NovaForge — build nova.exe and the Windows x64 installer.
#   powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1
#   ... -SkipBuild            only rebuild the installer from an existing build\Release\nova.exe
#   ... -Exe path\to\nova.exe package a nova.exe built some other way (e.g. MinGW)
# Needs: CMake + Visual Studio 2019/2022 (C++ workload) and Inno Setup 6.7+ (winget install JRSoftware.InnoSetup)
param([switch]$SkipBuild, [string]$Exe = "")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$version = (Select-String -Path src\config.hpp -Pattern 'VERSION = "([0-9.]+)"').Matches[0].Groups[1].Value
Write-Host "NovaForge $version" -ForegroundColor DarkYellow

if (-not $Exe) {
    $Exe = Join-Path $root "build\Release\nova.exe"
    if (-not $SkipBuild) {
        cmake -B build -A x64 -DCMAKE_BUILD_TYPE=Release
        if ($LASTEXITCODE) { throw "cmake configure failed" }
        cmake --build build --config Release
        if ($LASTEXITCODE) { throw "cmake build failed" }
    }
}
if (-not (Test-Path $Exe)) { throw "nova.exe not found: $Exe" }
& $Exe --version

$iscc = @(
    (Get-Command iscc.exe -ErrorAction SilentlyContinue).Source,
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6 not found. Install it: winget install JRSoftware.InnoSetup" }

& $iscc "/DAppVersion=$version" "/DNovaExe=$((Resolve-Path $Exe).Path)" installer\windows\novaforge.iss
if ($LASTEXITCODE) { throw "ISCC failed" }
$setup = Join-Path $root "dist\NovaForge-Setup-$version-x64.exe"
(Get-FileHash $setup -Algorithm SHA256).Hash.ToLower() + "  " + (Split-Path -Leaf $setup) | Set-Content "$setup.sha256" -Encoding ascii
Write-Host "Installer: $setup" -ForegroundColor Green
