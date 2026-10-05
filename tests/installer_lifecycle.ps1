[CmdletBinding()]
param([string]$Setup,[Parameter(Mandatory=$true)][string]$GameFixture)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'scripts\common.ps1')
$GameFixture=(Resolve-Path -LiteralPath $GameFixture).Path
if (-not $GameFixture.StartsWith((Join-Path $root 'artifacts\installer-source')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Installer tests require an explicitly isolated workspace game fixture.' }
if (-not $Setup) { $Setup=Join-Path $root 'release\BrutalLegendVR-0.1.0-preview.3-Setup.exe' }
$key='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{C096980B-6910-4A02-B262-EE0D0A71AF1E}_is1'
if (Test-Path -LiteralPath $key) { throw 'An installed application already owns this AppId. Use an isolated Windows user for installer lifecycle tests.' }
$proof=Join-Path $root 'artifacts\installer-lifecycle'
New-Item -ItemType Directory -Path $proof -Force | Out-Null
$fixture=Join-Path $proof ([guid]::NewGuid().ToString('N'))
$app=Join-Path $fixture 'app';$game=Join-Path $fixture 'game'
$checks=[Collections.Generic.List[string]]::new()
function Check($condition,[string]$label) { if (-not $condition) { throw "FAIL: $label" };$checks.Add($label) }
function Hash([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
function Run-Installer([string]$path,[string]$arguments) {
    $process=Start-Process -FilePath $path -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(180000)) { $process.Kill();throw 'Installer test timed out.' }
    return $process.ExitCode
}
try {
    New-Item -ItemType Directory -Path $fixture,$game | Out-Null
    Check ((Run-Installer $Setup ('/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /DIR="'+$app+'" /LOG="'+(Join-Path $proof 'cold-install.log')+'"')) -eq 0) 'cold installer completes'
    Check ((Get-ItemProperty -LiteralPath $key).InstallLocation.TrimEnd('\') -eq $app) 'installed app has its Windows uninstall entry'
    Check (Test-Path -LiteralPath (Join-Path $app 'tools\_internal\python311.dll')) 'installed app includes its own Python runtime'
    $setupExe=Join-Path $app 'tools\blvr_setup.exe'
    Check ((Invoke-BlvrProgram $setupExe ('--prepare --game-dir "'+$GameFixture+'"') 180) -eq 0) 'cold installed app imports owned models without system Python'
    Check ((Invoke-BlvrProgram $setupExe '--check-assets' 30) -eq 0) 'installed cache checker validates generated models and textures'
    # A private copy of the supported executable is used only for deployment;
    # this fixture never launches the retail game.
    Copy-Item -LiteralPath (Join-Path $GameFixture 'BrutalLegend.exe') -Destination (Join-Path $game 'BrutalLegend.exe')
    [IO.File]::WriteAllText((Join-Path $game 'd3d9.dll'),'original third party D3D9 hook')
    [IO.File]::WriteAllText((Join-Path $game 'openxr_loader.dll'),'original loader')
    $originalHook=Hash (Join-Path $game 'd3d9.dll');$originalLoader=Hash (Join-Path $game 'openxr_loader.dll')
    $settings=Join-Path $app 'settings.json'
    [IO.File]::WriteAllText($settings,(@{game_dir=$game;guitar_volume=31;frame_limit_fps=72} | ConvertTo-Json))
    Add-Content -LiteralPath (Join-Path $app 'controls.ini') -Value '# preserved custom controls'
    [IO.File]::WriteAllText((Join-Path $app 'guitar-placement.txt'),'private saved body position')
    & (Join-Path $app 'scripts\deploy.ps1') -GameDir $game
    $preserved=@{}
    foreach ($relative in 'settings.json','controls.ini','guitar-placement.txt','artifacts\eddie-rig\eddie.rigcache') { $preserved[$relative]=Hash (Join-Path $app $relative) }
    Check ((Run-Installer $Setup ('/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /DIR="'+$app+'" /LOG="'+(Join-Path $proof 'upgrade.log')+'"')) -eq 0) 'installer upgrade completes'
    foreach ($relative in $preserved.Keys) { Check ((Hash (Join-Path $app $relative)) -eq $preserved[$relative]) ('upgrade preserves '+$relative) }
    & (Join-Path $app 'scripts\deploy.ps1') -GameDir $game
    [IO.File]::WriteAllText((Join-Path $game 'd3d9.dll'),'modified after VR installation')
    $uninstaller=Join-Path $app 'unins000.exe'
    $blocked=Run-Installer $uninstaller ('/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG="'+(Join-Path $proof 'blocked-uninstall.log')+'"')
    Check ((Test-Path -LiteralPath $setupExe) -and (Test-Path -LiteralPath (Join-Path $game 'blvr-install.json'))) 'unsafe uninstall preserves app and game recovery manifest'
    Check ((Hash (Join-Path $game 'openxr_loader.dll')) -eq (Hash (Join-Path $app 'bin\openxr_loader.dll'))) 'failed uninstall restores no files partially'
    Copy-Item -LiteralPath (Join-Path $app 'bin\d3d9.dll') -Destination (Join-Path $game 'd3d9.dll') -Force
    Check ((Run-Installer $uninstaller ('/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG="'+(Join-Path $proof 'uninstall.log')+'"')) -eq 0) 'safe uninstall completes'
    Check ((Hash (Join-Path $game 'd3d9.dll')) -eq $originalHook -and (Hash (Join-Path $game 'openxr_loader.dll')) -eq $originalLoader) 'uninstaller restores both original game hooks'
    Check (-not (Test-Path -LiteralPath (Join-Path $game 'blvr-install.json'))) 'safe uninstall removes game install manifest'
    Check (-not (Test-Path -LiteralPath $setupExe) -and -not (Test-Path -LiteralPath $key)) 'safe uninstall removes app and Windows registration'
    $receipt=@{passed=$true;checks=$checks.ToArray();installer_sha256=(Hash $Setup);host_sha256=(Hash (Join-Path $root 'tools\blvr_xr_host.exe'));blocked_uninstall_exit=$blocked;retail_game_launched=$false}
    [IO.File]::WriteAllText((Join-Path $proof 'receipt.json'),($receipt | ConvertTo-Json -Depth 4))
    Write-Host "PASS: $($checks.Count) real installer lifecycle checks"
} finally {
    # Keep failed fixtures and their uninstall entry for diagnosis/recovery.
    if (-not (Test-Path -LiteralPath $key) -and (Test-Path -LiteralPath $fixture)) {
        if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($fixture)) -ne [IO.Path]::GetFullPath($proof)) { throw 'Refusing invalid fixture cleanup.' }
        Remove-Item -LiteralPath $fixture -Recurse -Force
    }
}
