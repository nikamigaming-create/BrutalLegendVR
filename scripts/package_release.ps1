[CmdletBinding()]
param(
    [string]$Version='0.1.0-preview.1',
    [string]$Python='python',
    [string]$DxvkDll,
    [switch]$SkipSetupBuild
)
$ErrorActionPreference='Stop'
. ($PSScriptRoot+'\common.ps1')
$root=Split-Path -Parent $PSScriptRoot
if ($Version -notmatch '^[0-9A-Za-z._-]+$') { throw 'Invalid release version.' }
Push-Location $root
try {
    if (-not $SkipSetupBuild) {
        & $Python -m PyInstaller --noconfirm --windowed --onedir --name blvr_setup --distpath release-work/setup-dist --workpath release-work/setup-build --specpath release-work scripts/blvr_setup.py
        if ($LASTEXITCODE -ne 0) { throw 'Setup packaging failed.' }
    }
    $stage=Join-Path $root ("release-work\stage-"+$Version+"-"+[guid]::NewGuid().ToString('N').Substring(0,8))
    $bin=New-Item -ItemType Directory -Path (Join-Path $stage 'bin') -Force
    $tools=New-Item -ItemType Directory -Path (Join-Path $stage 'tools') -Force
    $scripts=New-Item -ItemType Directory -Path (Join-Path $stage 'scripts') -Force
    $docs=New-Item -ItemType Directory -Path (Join-Path $stage 'docs') -Force
    Copy-Item -LiteralPath 'build\Release\d3d9.dll' -Destination $bin.FullName
    Copy-Item -LiteralPath 'build\openxr-sdk\src\loader\Release\openxr_loader.dll' -Destination $bin.FullName
    if ($DxvkDll) {
        if ((Get-FileHash -LiteralPath $DxvkDll -Algorithm SHA256).Hash -ne '44A2E749694128710CCE3A545C954BD9D986DD6A8EA61BB08D931C2EF0190488') { throw 'Use the unmodified official DXVK 3.0.2 x86 D3D9 DLL.' }
        Copy-Item -LiteralPath $DxvkDll -Destination (Join-Path $bin.FullName 'dxvk_d3d9.dll')
    }
    Copy-Item -LiteralPath 'tools\blvr_xr_host.exe' -Destination $tools.FullName
    Get-ChildItem -LiteralPath 'release-work\setup-dist\blvr_setup' | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $tools.FullName -Recurse
    }
    foreach ($name in @('launch_vr.ps1','deploy.ps1','uninstall.ps1','common.ps1')) {
        Copy-Item -LiteralPath (Join-Path 'scripts' $name) -Destination $scripts.FullName
    }
    foreach ($name in @('Play VR.cmd','Setup VR.cmd','Remap Controls.cmd','Uninstall VR.cmd','controls.ini','README.md','LICENSE','THIRD_PARTY_NOTICES.md')) {
        Copy-Item -LiteralPath $name -Destination $stage
    }
    foreach ($name in @('VR_CONTROLS.md',('RELEASE_'+$Version+'.md'))) {
        Copy-Item -LiteralPath (Join-Path 'docs' $name) -Destination $docs.FullName
    }
    Copy-Item -LiteralPath 'assets' -Destination $stage -Recurse
    Copy-Item -LiteralPath 'licenses' -Destination $stage -Recurse
    New-Item -ItemType Directory -Path (Join-Path $root 'release') -Force | Out-Null
    & $Python 'scripts\audit_release.py' --stage $stage --version $Version --output (Join-Path $root 'release')
    if ($LASTEXITCODE -ne 0) { throw 'Release audit failed.' }
    Write-Host "Release staged at $stage"
} finally { Pop-Location }
