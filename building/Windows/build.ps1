[CmdletBinding()]
param(
    [string]$VcpkgRoot = 'C:\src\vcpkg',
    [string]$CacheRoot = 'C:\src\nextapp-windows-cache',
    [string]$BuildRoot = '',
    [ValidateRange(1, 1024)][int]$Jobs = [Environment]::ProcessorCount
)

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
$CacheRoot = [IO.Path]::GetFullPath($CacheRoot)
$build = Join-Path $BuildRoot 'app'
$stage = Join-Path $BuildRoot 'stage'
$assets = Join-Path $BuildRoot 'assets'
$installed = Join-Path $CacheRoot 'installed'
$binaryCache = Join-Path $CacheRoot 'archives'
$triplet = 'x64-nextapp-release'
$lock = $null

New-Item -ItemType Directory -Force -Path $CacheRoot, $binaryCache, $build | Out-Null
try {
    # Serialize users of the shared checkout AND installed tree, across Jenkins workspaces.
    # Fail clearly instead of waiting indefinitely behind another build.
    $lock = [IO.File]::Open((Join-Path $VcpkgRoot 'nextapp-build.lock'),
        [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or !$vs) { throw 'Visual Studio C++ x64 build tools are required.' }
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    # Pass cmd.exe its raw command line. Windows PowerShell's native argument
    # quoting adds another quote layer around this /c command when VS has spaces
    # in its path. /s removes only the outer pair, preserving the quoted path.
    $vcStartInfo = New-Object System.Diagnostics.ProcessStartInfo
    $vcStartInfo.FileName = $env:ComSpec
    $vcStartInfo.Arguments = '/d /u /s /v:off /c ""{0}" >nul && set"' -f $vcvars
    $vcStartInfo.UseShellExecute = $false
    $vcStartInfo.RedirectStandardOutput = $true
    $vcStartInfo.StandardOutputEncoding = [Text.Encoding]::Unicode
    $vcProcess = New-Object System.Diagnostics.Process
    $vcProcess.StartInfo = $vcStartInfo
    try {
        if (!$vcProcess.Start()) { throw 'Could not start the MSVC environment setup.' }
        $vcEnvironment = $vcProcess.StandardOutput.ReadToEnd()
        $vcProcess.WaitForExit()
        if ($vcProcess.ExitCode -ne 0) {
            throw "Could not initialize the MSVC x64 environment (exit code $($vcProcess.ExitCode))."
        }
    } finally { $vcProcess.Dispose() }
    foreach ($line in ($vcEnvironment -split '\r?\n')) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }
    foreach ($tool in 'git', 'cmake', 'cpack', 'ninja', 'makensis', 'dumpbin') {
        Get-Command $tool -ErrorAction Stop | Out-Null
    }
    $cmakeVersion = & cmake --version
    if ($cmakeVersion[0] -notmatch '(\d+\.\d+\.\d+)' -or [version]$Matches[1] -lt [version]'3.30.0') {
        throw 'CMake 3.30 or newer is required for the Windows build scripts.'
    }

    # Refresh the tool and registry; the manifest baseline still pins package versions.
    Invoke-Checked git @('-C', $VcpkgRoot, 'pull', '--ff-only', '--quiet')
    Invoke-Checked (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') @('-disableMetrics')
    Invoke-Checked git @('-C', $source, 'submodule', 'update', '--init', '--depth', '1', '--',
        'dependencies/logfault', 'dependencies/qcoro', 'dependencies/expected')

    $env:VCPKG_ROOT = $VcpkgRoot
    $env:VCPKG_BINARY_SOURCES = "clear;files,$binaryCache,readwrite"
    $env:VCPKG_MAX_CONCURRENCY = "$Jobs"
    $env:VCPKG_DISABLE_METRICS = '1'
    $env:CMAKE_GENERATOR = 'Ninja'
    Remove-Item Env:CMAKE_GENERATOR_PLATFORM -ErrorAction SilentlyContinue
    Remove-Item Env:CMAKE_GENERATOR_TOOLSET -ErrorAction SilentlyContinue

    Invoke-Checked (Join-Path $VcpkgRoot 'vcpkg.exe') @('install',
        "--x-manifest-root=$PSScriptRoot", "--x-install-root=$installed",
        "--overlay-triplets=$PSScriptRoot\triplets", "--overlay-ports=$PSScriptRoot\ports",
        "--triplet=$triplet", "--host-triplet=$triplet", '--clean-after-build')

    $prefix = Join-Path $installed $triplet
    $env:PATH = "$prefix\bin;$prefix\tools\Qt6\bin;$env:PATH"
    Invoke-Checked cmake @('-S', $source, '-B', $build, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot/scripts/buildsystems/vcpkg.cmake",
        "-DVCPKG_MANIFEST_DIR=$PSScriptRoot", "-DVCPKG_INSTALLED_DIR=$installed",
        "-DVCPKG_TARGET_TRIPLET=$triplet", "-DVCPKG_HOST_TRIPLET=$triplet",
        "-DVCPKG_OVERLAY_TRIPLETS=$PSScriptRoot/triplets", "-DVCPKG_OVERLAY_PORTS=$PSScriptRoot/ports",
        '-DVCPKG_MANIFEST_INSTALL=OFF', '-DVCPKG_APPLOCAL_DEPS=ON',
        '-DNEXTAPP_WITH_UI=ON', '-DNEXTAPP_WITH_AI=ON', '-DNEXTAPP_WITH_MCP=ON',
        '-DNEXTAPP_WITH_BACKEND=OFF', '-DNEXTAPP_WITH_SIGNUP=OFF',
        '-DNEXTAPP_WITH_TESTS=OFF', '-DNEXTAPP_WITH_TESTS_UI=OFF', '-DBUILD_TESTING=OFF',
        '-DQCORO_WITH_QTTEST=OFF', '-DQCORO_WITH_QTNETWORK=OFF',
        '-DUSE_STATIC_QT=OFF', '-DBUILD_SHARED_LIBS=OFF')
    Invoke-Checked cmake @('--build', $build, '--target', 'appNextAppUi', '--parallel', "$Jobs")

    # Always start deployment clean: removed plugins must not leak into later installers.
    foreach ($dir in $stage, $assets) {
        if (Test-Path $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
        New-Item -ItemType Directory -Path $dir | Out-Null
    }
    $bin = New-Item -ItemType Directory -Path "$stage\bin"
    Copy-Item "$build\bin\nextapp.exe" $bin.FullName
    Copy-Item "$source\src\NextAppUi\LICENSE" "$stage\LICENSE.txt"
    $deploy = Join-Path $prefix 'tools\Qt6\bin\windeployqt.exe'
    Invoke-Checked $deploy @('--release', '--no-translations', '--no-compiler-runtime',
        '--no-opengl-sw', '--qmldir', "$source\src\NextAppUi",
        '--dir', $bin.FullName, "$bin\nextapp.exe")

    # App-local MSVC runtime avoids a second installer or a prerequisite download.
    $crt = @(Get-ChildItem "$env:VCToolsRedistDir\x64\Microsoft.VC*.CRT" -Directory)
    if ($crt.Count -ne 1) { throw 'Expected exactly one x64 MSVC CRT redistributable directory.' }
    Copy-Item "$($crt[0].FullName)\*.dll" $bin.FullName
    Invoke-Checked cmake @("-DSTAGE_DIR=$stage", "-DBUILD_DIR=$build",
        "-DDEPENDENCY_BIN=$prefix/bin", '-P', "$PSScriptRoot\ResolveRuntime.cmake")
    if (!(Test-Path "$bin\platforms\qwindows.dll") -or !(Test-Path "$bin\sqldrivers\qsqlite.dll")) {
        throw 'Deployment is missing the Windows platform or SQLite plugin.'
    }
    $unwanted = @(Get-ChildItem $stage -Recurse -File | Where-Object {
        $_.Extension -in '.lib', '.a', '.h', '.hpp', '.pdb', '.obj'
    })
    if ($unwanted.Count) { throw "Non-runtime files in deployment: $($unwanted.FullName -join ', ')" }

    & "$PSScriptRoot\package.ps1" -BuildRoot $BuildRoot
} finally {
    if ($lock) { $lock.Dispose() }
}
