[CmdletBinding()]
param([string]$GameDir, [string]$RuntimeJson, [switch]$CheckOnly)
$ErrorActionPreference = 'Stop'
. ($PSScriptRoot+'\common.ps1')
$projectRoot = Split-Path -Parent $PSScriptRoot
$hostExe = Join-Path $projectRoot 'tools\blvr_xr_host.exe'
$setupExe = Join-Path $projectRoot 'tools\blvr_setup.exe'
$settingsPath = Join-Path $projectRoot 'settings.json'
$launchLock=$null;$launchMutex=$null;$mutexOwned=$false;$hostProcess=$null;$game=$null;$started=$false
try {
    $launchMutex=[Threading.Mutex]::new($false,'Local\BrutalLegendVR_Launcher_v1')
    try { $mutexOwned=$launchMutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $mutexOwned=$true }
    if (-not $mutexOwned) { throw 'Another launch or installation check is already running. Wait for it to finish.' }
    try { $launchLock=[IO.File]::Open((Join-Path $projectRoot 'launcher.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None) }
    catch { throw 'Another launch or installation check is already running. Wait for it to finish.' }
if (Get-Process -Name BrutalLegend -ErrorAction SilentlyContinue) { throw 'Brütal Legend is already running. Exit it normally before launching or checking VR.' }
$settings=Read-BlvrSettings $settingsPath
$env:BLVR_GUITAR_PLACEMENT_FILE=Join-Path $projectRoot 'guitar-placement.txt'
if (-not $GameDir -and $settings) { $GameDir=$settings.game_dir }
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
Set-BlvrPreferences $settings
foreach ($required in @('tools\blvr_xr_host.exe','tools\blvr_setup.exe','assets\room\iron-medallion.png','assets\room\basalt.png')) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $required) -PathType Leaf)) { throw "Incomplete installation: $required. Run the installer again to repair it." }
}
if (Test-Path -LiteralPath (Join-Path $projectRoot 'bin')) {
    foreach ($required in @('bin\d3d9.dll','bin\openxr_loader.dll','tools\_internal\python311.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $required) -PathType Leaf)) { throw "Incomplete installation: $required. Run the installer again to repair it." }
    }
}
if (-not $CheckOnly) {
    foreach ($oldHost in (Get-Process -Name blvr_xr_host -ErrorAction SilentlyContinue)) {
        if ($oldHost.Path -ne $hostExe) { throw 'Another BLVR host is running from a different installation. Close it before starting this copy.' }
        $oldHost | Stop-Process; $oldHost | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
    }
}
if ((Invoke-BlvrProgram $hostExe '--check-controls' 10) -ne 0) { throw 'Invalid controls.ini. Open the app Controls tab and restore a working layout.' }
if ((Invoke-BlvrProgram $hostExe '--check-runtime' 30) -ne 0) { throw 'OpenXR could not find a connected headset. Start Quest Link / Air Link, wake your headset, and choose its runtime in VR settings. See the message above.' }
$rig=Join-Path $projectRoot 'artifacts\eddie-rig\eddie.rigcache'
if ($CheckOnly) {
    if (-not (Test-Path -LiteralPath $rig -PathType Leaf)) { throw "Owned model missing: $rig. Run Setup VR.cmd first." }
    $hook=Join-Path $projectRoot 'bin\d3d9.dll'
    if (-not (Test-Path -LiteralPath $hook)) { $hook=Join-Path $projectRoot 'build\Release\d3d9.dll' }
    if (-not (Test-Path -LiteralPath $hook)) { throw 'VR hook is missing.' }
    if (-not (Test-Path -LiteralPath $setupExe) -or (Invoke-BlvrProgram $setupExe '--check-assets' 30) -ne 0) { throw 'Owned models or textures are missing or damaged. Choose Prepare / repair game in the app.' }
    Write-Host 'PASS: game version, headset runtime, host, controls, owned model and artwork. No game was launched.'
    return
}
if (Get-Process -Name BrutalLegend -ErrorAction SilentlyContinue) { throw 'Exit the game normally before starting VR.' }
$cacheInvalid=(-not (Test-Path -LiteralPath $rig))
if (-not $cacheInvalid -and (Test-Path -LiteralPath $setupExe)) { $cacheInvalid=(Invoke-BlvrProgram $setupExe '--check-assets' 30) -ne 0 }
if ($cacheInvalid -or -not $settings -or $settings.game_dir -ne $GameDir) {
    if (-not (Test-Path -LiteralPath $setupExe)) { throw 'Prepare your owned model with Setup VR.cmd first.' }
    Write-Host 'Preparing your model from the installation. No animation recording is needed.'
    if ((Invoke-BlvrProgram $setupExe ('--prepare --game-dir "'+$GameDir+'"') 180) -ne 0) { throw 'Model preparation failed. Open the app and choose Prepare / repair game. See tools\blvr_setup-error.log for details.' }
}
& (Join-Path $PSScriptRoot 'deploy.ps1') -GameDir $GameDir
$env:BLVR_XR_BRIDGE='1'; $env:BLVR_SIM='0'; $env:BLVR_AUTOMATION='0'; $env:BLVR_RECORD='0'
$env:BLVR_WINDOWED='1'; $env:BLVR_NOFOCUS_PAUSE='1'; $env:BLVR_FIRST_PERSON='1'; $env:BLVR_KEEP_RUNNING='1'
$logFile=Join-Path $projectRoot 'tools\blvr_xr_host.log'
$logOffset=if (Test-Path -LiteralPath $logFile) { (Get-Item -LiteralPath $logFile).Length } else { 0 }
$hostProcess=Start-Process -FilePath $hostExe -ArgumentList '--game' -WorkingDirectory (Split-Path $hostExe) -WindowStyle Hidden -PassThru
$ready=$false
$startupLog=''
for ($i=0; $i -lt 480; $i++) {
    Start-Sleep -Milliseconds 250
    if ($hostProcess.HasExited) { throw "OpenXR startup failed. See $logFile" }
    if (Test-Path -LiteralPath $logFile) {
        $startupLog+=Read-BlvrStartupLog $logFile ([ref]$logOffset)
        if ($startupLog.Length -gt 262144) { $startupLog=$startupLog.Substring($startupLog.Length-262144) }
        if ($startupLog -match 'EddieLobby: unavailable') {
            Stop-Process -Id $hostProcess.Id -ErrorAction SilentlyContinue
            throw "VR rig could not load. Open Setup VR.cmd to prepare your model and check $logFile."
        }
        if ($startupLog -match 'PoseBridge: READY') { $ready=$true; break }
    }
}
if (-not $ready) { Stop-Process -Id $hostProcess.Id -ErrorAction SilentlyContinue; throw 'OpenXR host did not become ready. Check the headset connection and host log.' }
try { $game=Start-Process -FilePath $gameExe -WorkingDirectory $GameDir -PassThru }
catch { Stop-Process -Id $hostProcess.Id -ErrorAction SilentlyContinue; throw }
Start-Sleep -Seconds 3
if ($game.HasExited -or $hostProcess.HasExited) { throw "Game or VR host exited during startup. Check the host log and $GameDir\blvr.log." }
$started=$true
Write-Host "Brütal Legend VR started (game PID $($game.Id)). Put on your connected headset."
} finally {
    if ($game -and -not $started -and -not $game.HasExited) { $game.Kill();$game.WaitForExit(5000) | Out-Null }
    if ($hostProcess -and -not $started -and -not $hostProcess.HasExited) { $hostProcess.Kill();$hostProcess.WaitForExit(5000) | Out-Null }
    if ($launchLock) { $launchLock.Dispose() }
    if ($launchMutex) { if ($mutexOwned) { $launchMutex.ReleaseMutex() };$launchMutex.Dispose() }
}
