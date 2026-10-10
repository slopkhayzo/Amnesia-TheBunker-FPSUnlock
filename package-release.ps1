# Packages build\BunkerHighFPS.asi into two flat release archives under dist\:
#   AmnesiaTheBunker.HighFPSFix-<version>-plugin-only.zip
#   AmnesiaTheBunker.HighFPSFix-<version>-with-Ultimate-ASI-Loader.zip
# The version is PLUGIN_VERSION_STR in src\BunkerHighFPS.cpp. Run build.ps1 first.

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$asi = Join-Path $root 'build\BunkerHighFPS.asi'
$ini = Join-Path $root 'BunkerHighFPS.ini'
$readme = Join-Path $root 'packaging\README.txt'
$loaderDir = Join-Path $root 'research\tools\ultimate-asi-loader-v9.7.4'
$loader = Join-Path $loaderDir 'version.dll'
$loaderLicense = Join-Path $loaderDir 'LICENSE.txt'
# Ultimate ASI Loader x64 v9.7.4, the file the fix was tested with.
$loaderSha256 = '031A3E5576D91DCE1E438D36B9A3D462C7334AB4791990A8FF1E3DDC0E132DAF'
$miscLine = 'For more game fixes, visit https://slop-blog.enkhayzomachines.net/fixes :)'

function Get-Sha256([string]$path) { (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash }

function Get-PeMachine([string]$path) {
    $bytes = [IO.File]::ReadAllBytes($path)
    [BitConverter]::ToUInt16($bytes, [BitConverter]::ToInt32($bytes, 0x3C) + 4)
}

foreach ($input in $asi, $ini, $readme, $loader, $loaderLicense) {
    if (-not (Test-Path -LiteralPath $input)) { throw "Missing packaging input: $input" }
}

$source = Get-Content -Raw (Join-Path $root 'src\BunkerHighFPS.cpp')
if ($source -notmatch '#define PLUGIN_VERSION_STR "([0-9]+\.[0-9]+\.[0-9]+)"') { throw 'PLUGIN_VERSION_STR not found' }
$version = $Matches[1]

if ((Get-PeMachine $asi) -ne 0x8664) { throw 'BunkerHighFPS.asi is not x64' }
if ((Get-PeMachine $loader) -ne 0x8664) { throw 'version.dll is not x64' }
if ((Get-Sha256 $loader) -ne $loaderSha256) { throw 'version.dll is not the pinned Ultimate ASI Loader build' }
# The version string is compiled into the .asi; a stale build must not be packaged under a new number.
$asiText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($asi))
if (-not $asiText.Contains("BunkerHighFPS $version (ASI plugin)")) { throw "build\BunkerHighFPS.asi is not version $version; run build.ps1" }

$readmeText = (Get-Content -Raw $readme).Replace('@VERSION@', $version)
if ($readmeText -notmatch '(?m)^Misc\r?\n-------\r?\n' -or -not $readmeText.Contains($miscLine)) {
    throw 'Release README must retain the Misc section and fixes URL'
}

$dist = Join-Path $root 'dist'
$packages = @(
    @{ Name = "AmnesiaTheBunker.HighFPSFix-$version-plugin-only"; Loader = $false },
    @{ Name = "AmnesiaTheBunker.HighFPSFix-$version-with-Ultimate-ASI-Loader"; Loader = $true }
)

foreach ($package in $packages) {
    $stage = Join-Path $dist "release\$($package.Name)"
    $zip = Join-Path $dist "$($package.Name).zip"
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    New-Item -ItemType Directory -Force $stage | Out-Null

    Copy-Item -LiteralPath $asi (Join-Path $stage 'BunkerHighFPS.asi')
    Copy-Item -LiteralPath $ini (Join-Path $stage 'BunkerHighFPS.ini')
    [IO.File]::WriteAllText((Join-Path $stage 'README.txt'), $readmeText, (New-Object Text.UTF8Encoding $false))
    $files = @('BunkerHighFPS.asi', 'BunkerHighFPS.ini', 'README.txt')
    if ($package.Loader) {
        Copy-Item -LiteralPath $loader (Join-Path $stage 'version.dll')
        Copy-Item -LiteralPath $loaderLicense (Join-Path $stage 'ULTIMATE-ASI-LOADER-LICENSE.txt')
        $files += 'version.dll', 'ULTIMATE-ASI-LOADER-LICENSE.txt'
    }

    $sums = $files | ForEach-Object { "$(Get-Sha256 (Join-Path $stage $_))  $_" }
    [IO.File]::WriteAllLines((Join-Path $stage 'SHA256SUMS.txt'), $sums, (New-Object Text.ASCIIEncoding))
    $files += 'SHA256SUMS.txt'

    Compress-Archive -Path ($files | ForEach-Object { Join-Path $stage $_ }) -DestinationPath $zip -CompressionLevel Optimal

    # Validate the archive: flat, exactly the expected files, allowed extensions only.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $entries = @($archive.Entries | ForEach-Object { $_.FullName })
    } finally {
        $archive.Dispose()
    }
    if (Compare-Object ($entries | Sort-Object) ($files | Sort-Object)) { throw "Unexpected contents in $zip" }
    foreach ($entry in $entries) {
        if ($entry -match '[\\/]') { throw "Archive is not flat: $entry" }
        if ($entry -notmatch '\.(asi|ini|dll|txt)$') { throw "Disallowed file in archive: $entry" }
    }
    Write-Host "$zip"
    Write-Host "  SHA-256 $(Get-Sha256 $zip)"
}
