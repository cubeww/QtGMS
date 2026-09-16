#Requires -Version 5.1
<#
.SYNOPSIS
Builds and packages the Windows XP x86 Release of QtGMS as a ZIP archive.
.DESCRIPTION
Uses the configured build/windows-xp toolchain. For a fresh checkout, set
QTGMS_QT_DIR and QTGMS_MINGW_DIR as required by the windows-xp CMake preset.
Requires CMake (including CPack), Ninja, Qt 5.6.3 and MinGW 4.9.2 on the build PC.
The resulting ZIP includes the editor, Qt plugins, compiler runtimes, action
libraries and game runtime. No SDK or archive utility is needed to unpack it.
.EXAMPLE
.\package.ps1
.EXAMPLE
.\package.ps1 -OutputDirectory C:\Packages -Jobs 6
#>
[CmdletBinding()]
param(
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [ValidateRange(1, 256)]
    [int]$Jobs = [Environment]::ProcessorCount
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $PSScriptRoot 'build/windows-xp'
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $PSScriptRoot 'build/packages'
}

function Invoke-BuildTool {
    param([string]$Executable, [string[]]$Arguments)
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Executable failed with exit code $LASTEXITCODE."
    }
}

$cmakePath = (Get-Command cmake.exe -CommandType Application -ErrorAction Stop).Source
$cpackPath = Join-Path (Split-Path -Parent $cmakePath) 'cpack.exe'
if (-not (Test-Path -LiteralPath $cpackPath -PathType Leaf)) {
    throw "CPack was not found beside CMake: $cpackPath"
}
$BuildDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDirectory)
$OutputDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)

Push-Location -LiteralPath $PSScriptRoot
try {
    if (Test-Path -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt') -PathType Leaf) {
        Invoke-BuildTool $cmakePath @('-S', $PSScriptRoot, '-B', $BuildDirectory, '-DCMAKE_BUILD_TYPE=Release')
    } else {
        if (-not $env:QTGMS_QT_DIR -or -not $env:QTGMS_MINGW_DIR) {
            throw 'Set QTGMS_QT_DIR and QTGMS_MINGW_DIR before packaging a fresh checkout.'
        }
        Invoke-BuildTool $cmakePath @('--preset', 'windows-xp', '-B', $BuildDirectory)
    }
    Invoke-BuildTool $cmakePath @('--build', $BuildDirectory, '--config', 'Release', '--target', 'QtGMS', '--parallel', "$Jobs")
    Invoke-BuildTool $cpackPath @('--config', (Join-Path $BuildDirectory 'CPackConfig.cmake'), '-C', 'Release', '-G', 'ZIP', '-B', $OutputDirectory)
    Write-Host "Package directory: $OutputDirectory"
} finally {
    Pop-Location
}
