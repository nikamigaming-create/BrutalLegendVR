[CmdletBinding()]
param([switch]$Clean)
$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $projectRoot

if ($Clean -and (Test-Path "build")) {
    $buildTarget = (Resolve-Path -LiteralPath (Join-Path $projectRoot 'build')).Path
    $expectedTarget = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build'))
    if ($buildTarget -ne $expectedTarget) { throw "Unexpected build cleanup path: $buildTarget" }
    Remove-Item -LiteralPath $buildTarget -Recurse -Force
}

cmake -B build -A Win32 -G "Visual Studio 17 2022"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

cmake --build build --config Release
if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }

cmake -S src/openxr_host -B build/blvr-host-v143 -A x64 -G "Visual Studio 17 2022" -DBLVR_XR_BUILD_ID=brutal-legend
if ($LASTEXITCODE -ne 0) { throw "OpenXR host configure failed" }
cmake --build build/blvr-host-v143 --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw "OpenXR host build failed" }

Write-Output "Build completed: build\Release\d3d9.dll and tools\blvr_xr_host.exe"
