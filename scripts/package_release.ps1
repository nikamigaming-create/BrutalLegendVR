[CmdletBinding()]
param(
    [string]$Version='0.1.0-preview.4',
    [string]$Python='python',
    [string]$DxvkDll,
    [switch]$SkipSetupBuild,
    [string]$InnoCompiler
)
$ErrorActionPreference='Stop'
. ($PSScriptRoot+'\common.ps1')
$root=Split-Path -Parent $PSScriptRoot
if ($Version -notmatch '^[0-9A-Za-z._-]+$') { throw 'Invalid release version.' }
Push-Location $root
try {
    # One reviewed manifest owns both staging and the final archive gate.
    $manifestJson = & $Python 'scripts\audit_release.py' --version $Version --print-manifest
    if ($LASTEXITCODE -ne 0) { throw 'Release manifest validation failed.' }
    $manifest = ($manifestJson -join "`n") | ConvertFrom-Json
    $releaseFiles = @($manifest.rootFiles) + @($manifest.assetFiles) + @($manifest.scriptFiles) + @($manifest.docFiles) + @($manifest.licenseFiles)
    foreach ($relative in $releaseFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $root $relative) -PathType Leaf)) { throw "Missing reviewed release file: $relative" }
    }
    if (-not $SkipSetupBuild) {
        & $Python -m PyInstaller --noconfirm --windowed --onedir --name blvr_setup --icon (Join-Path $root 'assets\installer\blvr.ico') --distpath release-work/setup-dist --workpath release-work/setup-build --specpath release-work scripts/blvr_setup.py
        if ($LASTEXITCODE -ne 0) { throw 'Setup packaging failed.' }
    }
    $setupRoot = Join-Path $root 'release-work\setup-dist\blvr_setup'
    if (-not (Test-Path -LiteralPath (Join-Path $setupRoot 'blvr_setup.exe') -PathType Leaf) -or
        -not (Test-Path -LiteralPath (Join-Path $setupRoot '_internal') -PathType Container)) { throw 'Setup dependency collection is incomplete.' }
    foreach ($entry in (Get-ChildItem -LiteralPath $setupRoot -Force)) {
        if ($entry.Name -notin @('blvr_setup.exe','_internal')) { throw "Unexpected setup root entry: $($entry.Name)" }
    }
    $stage=Join-Path $root ("release-work\stage-"+$Version+"-"+[guid]::NewGuid().ToString('N').Substring(0,8))
    $bin=New-Item -ItemType Directory -Path (Join-Path $stage 'bin') -Force
    $tools=New-Item -ItemType Directory -Path (Join-Path $stage 'tools') -Force
    Copy-Item -LiteralPath 'build\Release\d3d9.dll' -Destination $bin.FullName
    Copy-Item -LiteralPath 'build\openxr-sdk\src\loader\Release\openxr_loader.dll' -Destination $bin.FullName
    if ($DxvkDll) {
        if ((Get-FileHash -LiteralPath $DxvkDll -Algorithm SHA256).Hash -ne '44A2E749694128710CCE3A545C954BD9D986DD6A8EA61BB08D931C2EF0190488') { throw 'Use the unmodified official DXVK 3.0.2 x86 D3D9 DLL.' }
        Copy-Item -LiteralPath $DxvkDll -Destination (Join-Path $bin.FullName 'dxvk_d3d9.dll')
    }
    Copy-Item -LiteralPath 'tools\blvr_xr_host.exe' -Destination $tools.FullName
    Copy-Item -LiteralPath (Join-Path $setupRoot 'blvr_setup.exe') -Destination $tools.FullName
    Copy-Item -LiteralPath (Join-Path $setupRoot '_internal') -Destination $tools.FullName -Recurse
    foreach ($relative in $releaseFiles) {
        $destination = Join-Path $stage $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $root $relative) -Destination $destination
    }
    New-Item -ItemType Directory -Path (Join-Path $root 'release') -Force | Out-Null
    & $Python 'scripts\audit_release.py' --stage $stage --version $Version --output (Join-Path $root 'release')
    if ($LASTEXITCODE -ne 0) { throw 'Release audit failed.' }
    if (-not $InnoCompiler) {
        $InnoCompiler=Join-Path $root 'release-work\inno\compiler\ISCC.exe'
        if (-not (Test-Path -LiteralPath $InnoCompiler)) { $InnoCompiler=Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe' }
    }
    if (-not (Test-Path -LiteralPath $InnoCompiler -PathType Leaf)) { throw 'Inno Setup 6.6+ is required to build the Windows installer; supply -InnoCompiler.' }
    & $InnoCompiler ("/DSourceDir="+$stage) ("/DReleaseVersion="+$Version) (Join-Path $root 'scripts\installer.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Windows installer build failed.' }
    $installer=Join-Path $root ("release\BrutalLegendVR-"+$Version+"-Setup.exe")
    $hash=(Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLower()
    [IO.File]::WriteAllText($installer+'.sha256', $hash+'  '+[IO.Path]::GetFileName($installer)+"`n")
    Write-Host "Release staged at $stage"
} finally { Pop-Location }
