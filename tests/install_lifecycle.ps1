[CmdletBinding()]
param([string]$GameExe='D:\SteamLibrary\steamapps\common\BrutalLegend\BrutalLegend.exe')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$proofRoot=Join-Path $root 'artifacts\install-lifecycle'
New-Item -ItemType Directory -Path $proofRoot -Force | Out-Null
$fixture=[IO.Path]::GetFullPath((Join-Path $proofRoot ([guid]::NewGuid().ToString('N'))))
if ([IO.Path]::GetDirectoryName($fixture) -ne [IO.Path]::GetFullPath($proofRoot)) { throw 'Invalid fixture path.' }
$checks=[Collections.Generic.List[string]]::new()
function Assert-True($condition,[string]$label) {
    if (-not $condition) { throw "FAIL: $label" }
    $checks.Add($label)
}
function Expect-Failure([scriptblock]$action,[string]$label) {
    $failed=$false
    try { & $action } catch { $failed=$true }
    Assert-True $failed $label
}
function Hash([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
try {
    $game=Join-Path $fixture 'game';$app=Join-Path $fixture 'app'
    New-Item -ItemType Directory -Path $game,(Join-Path $app 'scripts'),(Join-Path $app 'bin') | Out-Null
    foreach ($name in 'common.ps1','deploy.ps1','uninstall.ps1') { Copy-Item -LiteralPath (Join-Path $root ('scripts\'+$name)) -Destination (Join-Path $app ('scripts\'+$name)) }
    Copy-Item -LiteralPath $GameExe -Destination (Join-Path $game 'BrutalLegend.exe')
    $hook=Join-Path $game 'd3d9.dll';$loader=Join-Path $game 'openxr_loader.dll';$manifest=Join-Path $game 'blvr-install.json'
    [IO.File]::WriteAllText($hook,'foreign-hook-original');[IO.File]::WriteAllText($loader,'foreign-loader-original')
    [IO.File]::WriteAllText((Join-Path $game 'steam_appid.txt'),'existing-custom-appid')
    $originalHook=Hash $hook;$originalLoader=Hash $loader
    [IO.File]::WriteAllText((Join-Path $app 'bin\d3d9.dll'),'test-hook-v1')
    [IO.File]::WriteAllText((Join-Path $app 'bin\openxr_loader.dll'),'test-loader-v1')
    $deploy=Join-Path $app 'scripts\deploy.ps1';$uninstall=Join-Path $app 'scripts\uninstall.ps1'
    & $deploy -GameDir $game
    Assert-True ((Hash $hook) -eq (Hash (Join-Path $app 'bin\d3d9.dll'))) 'Cold install writes the hook'
    Assert-True ((Get-Content -LiteralPath (Join-Path $game 'steam_appid.txt') -Raw) -eq 'existing-custom-appid') 'Existing Steam app ID is preserved'
    $oldHook=Hash $hook;$oldLoader=Hash $loader;$oldManifest=Hash $manifest
    [IO.File]::WriteAllText((Join-Path $app 'bin\d3d9.dll'),'test-hook-v2')
    [IO.File]::WriteAllText((Join-Path $app 'bin\openxr_loader.dll'),'test-loader-v2')
    $lock=[IO.File]::Open($loader,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
    try { Expect-Failure { & $deploy -GameDir $game } 'Locked loader refuses the update' } finally { $lock.Dispose() }
    Assert-True ((Hash $hook) -eq $oldHook -and (Hash $loader) -eq $oldLoader -and (Hash $manifest) -eq $oldManifest) 'Preflight failure preserves every installed byte'
    # Readers remain allowed; replacement of the manifest is denied after DLLs commit.
    $lock=[IO.File]::Open($manifest,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    try { Expect-Failure { & $deploy -GameDir $game } 'Manifest commit failure triggers rollback' } finally { $lock.Dispose() }
    Assert-True ((Hash $hook) -eq $oldHook -and (Hash $loader) -eq $oldLoader -and (Hash $manifest) -eq $oldManifest) 'Late commit failure rolls back all DLLs'
    & $deploy -GameDir $game
    Assert-True ((Hash $hook) -eq (Hash (Join-Path $app 'bin\d3d9.dll'))) 'Upgrade installs the new hook'
    $entries=(Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json).files
    Assert-True (($entries | Where-Object name -eq 'd3d9.dll').original_sha256 -eq $originalHook) 'Upgrade preserves the first original backup'
    $backup=Join-Path $game (($entries | Where-Object name -eq 'openxr_loader.dll').original)
    $backupBytes=[IO.File]::ReadAllBytes($backup);[IO.File]::WriteAllText($backup,'damaged-backup')
    $unchanged=Hash $manifest
    Expect-Failure { & $deploy -GameDir $game } 'Damaged backup blocks upgrade'
    Expect-Failure { & $uninstall -GameDir $game } 'Damaged backup blocks uninstall'
    Assert-True ((Hash $manifest) -eq $unchanged) 'Failed backup validation preserves the manifest'
    [IO.File]::WriteAllBytes($backup,$backupBytes)
    $installedBytes=[IO.File]::ReadAllBytes($loader);[IO.File]::WriteAllText($loader,'changed-after-install')
    $currentHook=Hash $hook
    Expect-Failure { & $uninstall -GameDir $game } 'Modified game DLL blocks uninstall'
    Assert-True ((Hash $hook) -eq $currentHook -and (Test-Path -LiteralPath $manifest)) 'Uninstall preflight preserves the other DLL and manifest'
    [IO.File]::WriteAllBytes($loader,$installedBytes)
    & $uninstall -GameDir $game
    Assert-True ((Hash $hook) -eq $originalHook -and (Hash $loader) -eq $originalLoader) 'Uninstall restores both original DLLs'
    Assert-True (-not (Test-Path -LiteralPath $manifest)) 'Successful restoration removes the manifest'
    Assert-True (@(Get-ChildItem -LiteralPath $game -Directory -Filter '.blvr-transaction-*').Count -eq 0) 'Completed and failed transactions leave no staging directories'
    [IO.File]::WriteAllText((Join-Path $proofRoot 'receipt.json'),(@{passed=$true;checks=$checks.ToArray();game_exe_sha256=(Hash $GameExe)} | ConvertTo-Json -Depth 4))
    Write-Host "PASS: $($checks.Count) installation lifecycle checks"
} finally {
    if (Test-Path -LiteralPath $fixture) {
        if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($fixture)) -ne [IO.Path]::GetFullPath($proofRoot)) { throw 'Refusing invalid fixture cleanup.' }
        Remove-Item -LiteralPath $fixture -Recurse -Force
    }
}
