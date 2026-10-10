param(
    [switch]$Install,
    [string]$GameDir = 'D:\Program Files\Games\GOG\Amnesia The Bunker'
)

# Builds build\BunkerHighFPS.asi (64-bit, static runtime) with the Visual Studio toolchain.
# -Install also copies BunkerHighFPS.asi and BunkerHighFPS.ini into the game folder (-GameDir).

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Force $build | Out-Null

# Newest Visual Studio (or Build Tools) with the C++ x64 toolchain.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found; install Visual Studio with the C++ workload' }
$vs = & $vswhere -latest -prerelease -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio installation with the C++ x64 toolchain found' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'

$command = "`"$vcvars`" x64 >nul && cl /nologo /std:c++20 /O2 /MT /W4 /EHsc /LD " +
    "/D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /Fo`"$build\\`" /Fe`"$build\BunkerHighFPS.asi`" " +
    "`"$root\src\BunkerHighFPS.cpp`" /link /NOLOGO /NOIMPLIB /NOEXP"
cmd /c $command
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }
Write-Host "Built $build\BunkerHighFPS.asi"

if ($Install) {
    if (-not (Test-Path (Join-Path $GameDir 'AmnesiaTheBunker.exe'))) { throw "AmnesiaTheBunker.exe not found in $GameDir" }
    Copy-Item (Join-Path $build 'BunkerHighFPS.asi') $GameDir -Force
    Copy-Item (Join-Path $root 'BunkerHighFPS.ini') $GameDir -Force
    Write-Host "Installed into $GameDir"
}
