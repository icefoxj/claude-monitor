#Requires -Version 7.0
<#
.SYNOPSIS
    Builds and runs the monitor-core host tests on Windows.

.DESCRIPTION
    Uses MSVC when Visual Studio or the Build Tools are installed (found with
    vswhere, activated through vcvars64.bat), otherwise a g++ on PATH that
    supports C++17. No ESP-IDF needed. Exit code 0 = all checks passed.
#>
$ErrorActionPreference = 'Stop'

$root  = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force $build | Out-Null

$inc     = Join-Path $root 'components\monitor-core\include'
$sources = @(
    (Join-Path $root 'tests\host\test_core.cpp'),
    (Join-Path $root 'components\monitor-core\src\protocol.cpp'),
    (Join-Path $root 'components\monitor-core\src\tilt.cpp')
)
$exe = Join-Path $build 'test_core.exe'

$vcvars  = $null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vs) { $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat' }
}

if ($vcvars -and (Test-Path $vcvars)) {
    $srcArgs = ($sources | ForEach-Object { "`"$_`"" }) -join ' '
    $cmdFile = Join-Path $build 'build.cmd'
    @"
@echo off
call "$vcvars" >nul 2>nul
cl /nologo /std:c++17 /EHsc /W4 /I "$inc" $srcArgs /Fo"$build\\" /Fe"$exe"
"@ | Set-Content -Path $cmdFile -Encoding ascii
    Write-Host "Building with MSVC ($vs)"
    cmd /c "`"$cmdFile`""
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
} else {
    Write-Host "Building with g++"
    g++ -std=c++17 -Wall -Wextra -Werror -I $inc $sources -o $exe
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}

& $exe
exit $LASTEXITCODE
