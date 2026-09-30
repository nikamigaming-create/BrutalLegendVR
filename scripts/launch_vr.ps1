[CmdletBinding()]
param([string]$GameDir, [string]$RuntimeJson, [switch]$CheckOnly)
$ErrorActionPreference = 'Stop'
. ($PSScriptRoot+'\common.ps1')
$projectRoot = Split-Path -Parent $PSScriptRoot
$hostExe = Join-Path $projectRoot 'tools\blvr_xr_host.exe'
$setupExe = Join-Path $projectRoot 'tools\blvr_setup.exe'
$settingsPath = Join-Path $projectRoot 'settings.json'
if (Test-Path -LiteralPath $settingsPath) { $settings=Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json }
if (-not $GameDir -and $settings) { $GameDir=$settings.game_dir }
if (-not $GameDir -and -not $CheckOnly) {
    if (-not (Test-Path -LiteralPath $setupExe)) { throw 'Run Setup VR.cmd, or supply -GameDir with your installed game folder.' }
    Write-Host 'Select your installed game in the setup window and choose Prepare game.'
    $setup=Start-Process -FilePath $setupExe -WindowStyle Hidden -PassThru
    $setup | Wait-Process
    if (Test-Path -LiteralPath $settingsPath) { $settings=Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json; $GameDir=$settings.game_dir }
}
if (-not $GameDir) { throw 'Select your installation using Setup VR.cmd first.' }
$GameDir=(Resolve-Path -LiteralPath $GameDir).Path
$gameExe=Join-Path $GameDir 'BrutalLegend.exe'
if ((Get-FileHash -LiteralPath $gameExe -Algorithm SHA256).Hash -ne '872DC676E8FD77AD3351DD9DFCC99E89353AAE0ED9857272A65FD47F298FB0B1') { throw 'Unsupported game executable. This preview supports the tested Steam PC build.' }
if (-not $RuntimeJson -and $settings -and $settings.runtime_json) { $RuntimeJson=$settings.runtime_json }
if (-not $RuntimeJson) {
    $active=(Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Khronos\OpenXR\1' -Name ActiveRuntime -ErrorAction SilentlyContinue).ActiveRuntime
    if ($active -and $active -notmatch 'simulator|elliott') { $RuntimeJson=$active }
    else {
        $meta=Join-Path $env:ProgramFiles 'Oculus\Support\oculus-runtime\oculus_openxr_64.json'
        if (Test-Path -LiteralPath $meta) { $RuntimeJson=$meta }
    }
}
if ($RuntimeJson) {
    if (-not (Test-Path -LiteralPath $RuntimeJson -PathType Leaf)) { throw "OpenXR runtime file not found: $RuntimeJson" }
    $env:XR_RUNTIME_JSON=$RuntimeJson
    Write-Host "OpenXR runtime: $RuntimeJson"
} else { Write-Host 'Using the system OpenXR runtime.' }
$env:BLVR_CONTROLS_FILE=Join-Path $projectRoot 'controls.ini'
if (-not (Test-Path -LiteralPath $hostExe)) { throw 'BLVR host is missing. Extract the complete release ZIP.' }
& $hostExe --check-controls
if ($LASTEXITCODE -ne 0) { throw 'Invalid controls.ini. Open Remap Controls.cmd and restore a working layout.' }
$rig=Join-Path $projectRoot 'artifacts\eddie-rig\eddie.rigcache'
if ($CheckOnly) {
    foreach ($file in @($rig,(Join-Path $projectRoot 'assets\room\iron-medallion.png'),(Join-Path $projectRoot 'assets\room\basalt.png'))) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required file missing: $file. Run Setup VR.cmd first." }
    }
    $hook=Join-Path $projectRoot 'bin\d3d9.dll'
    if (-not (Test-Path -LiteralPath $hook)) { $hook=Join-Path $projectRoot 'build\Release\d3d9.dll' }
    if (-not (Test-Path -LiteralPath $hook)) { throw 'VR hook is missing.' }
    Write-Host 'PASS: game version, host, controls, owned model and room assets. No game was launched.'
    return
}
if (Get-Process -Name BrutalLegend -ErrorAction SilentlyContinue) { throw 'Exit the game normally before starting VR.' }
if (-not (Test-Path -LiteralPath $rig)) {
    if (-not (Test-Path -LiteralPath $setupExe)) { throw 'Prepare your owned model with Setup VR.cmd first.' }
    Write-Host 'Preparing your model from the installation. No animation recording is needed.'
    $prepared=Start-Process -FilePath $setupExe -ArgumentList @('--prepare','--game-dir',('"'+$GameDir+'"')) -WindowStyle Hidden -PassThru -Wait
    if ($prepared.ExitCode -ne 0) { throw 'Model preparation failed. Open Setup VR.cmd for details.' }
}
$oldHosts=Get-Process -Name blvr_xr_host -ErrorAction SilentlyContinue
if ($oldHosts) { $oldHosts | Stop-Process -Force; $oldHosts | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue }
& (Join-Path $PSScriptRoot 'deploy.ps1') -GameDir $GameDir
$env:BLVR_XR_BRIDGE='1'; $env:BLVR_SIM='0'; $env:BLVR_AUTOMATION='0'; $env:BLVR_RECORD='0'
$env:BLVR_WINDOWED='1'; $env:BLVR_NOFOCUS_PAUSE='1'; $env:BLVR_FIRST_PERSON='1'; $env:BLVR_KEEP_RUNNING='1'; $env:BLVR_FRAME_LIMIT_FPS='90'
$logFile=Join-Path $projectRoot 'tools\blvr_xr_host.log'
$logOffset=if (Test-Path -LiteralPath $logFile) { (Get-Content -LiteralPath $logFile -Raw).Length } else { 0 }
$hostProcess=Start-Process -FilePath $hostExe -ArgumentList '--game' -WorkingDirectory (Split-Path $hostExe) -WindowStyle Hidden -PassThru
$ready=$false
for ($i=0; $i -lt 1200; $i++) {
    Start-Sleep -Milliseconds 250
    if ($hostProcess.HasExited) { throw "OpenXR startup failed. See $logFile" }
    if (Test-Path -LiteralPath $logFile) {
        $content=Get-Content -LiteralPath $logFile -Raw -ErrorAction SilentlyContinue
        if ($content.Length -lt $logOffset) { $logOffset=0 }
        $new=if ($content.Length -gt $logOffset) { $content.Substring($logOffset) } else { '' }
        if ($new -match 'PoseBridge: READY') { $ready=$true; break }
    }
}
if (-not $ready) { Stop-Process -Id $hostProcess.Id -ErrorAction SilentlyContinue; throw 'OpenXR host did not become ready. Check the headset connection and host log.' }
try { $game=Start-Process -FilePath $gameExe -WorkingDirectory $GameDir -PassThru }
catch { Stop-Process -Id $hostProcess.Id -ErrorAction SilentlyContinue; throw }
Write-Host "Brütal Legend VR started (game PID $($game.Id)). Put on your connected headset."
