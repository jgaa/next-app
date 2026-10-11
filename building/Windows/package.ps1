[CmdletBinding()]
param([string]$BuildRoot = '')

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-Checked {
    param([string]$File, [string[]]$Arguments)
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$File failed with exit code $LASTEXITCODE" }
}

$source = (Resolve-Path "$PSScriptRoot\..\..").Path
if (!$BuildRoot) { $BuildRoot = Join-Path $source 'build\windows-vcpkg' }
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$build = Join-Path $BuildRoot 'app'
$stage = Join-Path $BuildRoot 'stage'
$assets = Join-Path $BuildRoot 'assets'
foreach ($tool in 'cmake', 'cpack', 'makensis') {
    Get-Command $tool -ErrorAction Stop | Out-Null
}
foreach ($required in "$build\VERSION.txt", "$stage\bin\nextapp.exe", "$stage\LICENSE.txt") {
    if (!(Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing $required. Run building\Windows\build.ps1 to create the deployment first."
    }
}
New-Item -ItemType Directory -Force -Path $assets | Out-Null
Push-Location $BuildRoot
try {
    Invoke-Checked cmake @("-DSOURCE_DIR=$source", "-DBUILD_DIR=$build", "-DSTAGE_DIR=$stage",
        "-DOUTPUT_DIR=$assets", '-P', "$PSScriptRoot\Package.cmake")
    try {
        Invoke-Checked cpack @('--config', "$BuildRoot\CPackConfig.cmake", '-G', 'NSIS')
    } catch {
        Get-ChildItem "$assets\_CPack_Packages" -Filter 'NSISOutput.log' -Recurse -File -ErrorAction SilentlyContinue |
            ForEach-Object {
                Write-Host "NSIS diagnostics: $($_.FullName)"
                Get-Content -LiteralPath $_.FullName -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_ }
            }
        throw
    }
    $version = (Get-Content "$build\VERSION.txt" -Raw).Trim()
    $installer = Join-Path $assets "NextApp-$version-windows-x64.exe"
    if (!(Test-Path -LiteralPath $installer -PathType Leaf)) { throw "Missing installer: $installer" }
    # Keep only the current deliverable and discard duplicated packaging payloads.
    Get-ChildItem $assets -File | Where-Object {
        $_.Name -like 'NextApp-*-windows-x64.*' -and $_.FullName -ne $installer
    } | Remove-Item -Force
    Remove-Item "$assets\_CPack_Packages" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item $stage -Recurse -Force
    Write-Host "Installer: $installer"
} finally { Pop-Location }
