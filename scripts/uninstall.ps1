[CmdletBinding()]
param([string]$GameDir)
$ErrorActionPreference='Stop'
. ($PSScriptRoot+'\common.ps1')
$root=Split-Path -Parent $PSScriptRoot
if (-not $GameDir) { $GameDir=(Read-BlvrSettings (Join-Path $root 'settings.json')).game_dir }
if (-not $GameDir) { Write-Host 'No prepared game installation to restore.'; return }
$GameDir=(Resolve-Path -LiteralPath $GameDir).Path
if (Get-Process -Name BrutalLegend -ErrorAction SilentlyContinue) { throw 'Exit the game normally before uninstalling BLVR.' }
$manifestPath=Join-Path $GameDir 'blvr-install.json'
if (-not (Test-Path -LiteralPath $manifestPath)) { Write-Host 'No BLVR installation manifest found.'; return }
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.product -ne 'BrutalLegendVR') { throw 'Unexpected installation manifest.' }
$incomplete=$false
# Validate every backup and changed target before restoring any game file.
foreach ($entry in $manifest.files) {
    if ($entry.name -notin @('d3d9.dll','openxr_loader.dll','dxvk_d3d9.dll','steam_appid.txt')) { throw 'Unexpected file in installation manifest.' }
    $target=Join-Path $GameDir $entry.name
    if ($entry.original) {
        if ([IO.Path]::GetFileName($entry.original) -ne $entry.original -or $entry.original -notlike "$($entry.name).blvr-backup-*") { throw 'Invalid backup path.' }
        if ((Get-FileHash -LiteralPath (Join-Path $GameDir $entry.original) -Algorithm SHA256).Hash -ne $entry.original_sha256) { throw "Backup changed: $($entry.original)" }
    }
    if (Test-Path -LiteralPath $target) {
        $actual=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
        if ($actual -ne $entry.installed_sha256 -and $actual -ne $entry.original_sha256) { throw "$($entry.name) changed after installation; restore or move that file before uninstalling VR." }
        $lock=[IO.File]::Open($target,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
        $lock.Dispose()
    }
}
foreach ($entry in $manifest.files) {
    if ($entry.name -notin @('d3d9.dll','openxr_loader.dll','dxvk_d3d9.dll','steam_appid.txt')) { throw 'Unexpected file in installation manifest.' }
    $target=[IO.Path]::GetFullPath((Join-Path $GameDir $entry.name))
    if ([IO.Path]::GetDirectoryName($target) -ne $GameDir) { throw 'Invalid uninstall target.' }
    if (Test-Path -LiteralPath $target) {
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $entry.installed_sha256) {
            # A previous interrupted uninstall may already have restored this original.
            if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -eq $entry.original_sha256) { continue }
            throw "$($entry.name) changed during uninstall. Installation manifest was preserved."
        }
    }
    if ($entry.original) {
        if ([IO.Path]::GetFileName($entry.original) -ne $entry.original -or $entry.original -notlike "$($entry.name).blvr-backup-*") { throw 'Invalid backup path.' }
        $backup=Join-Path $GameDir $entry.original
        if ((Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash -ne $entry.original_sha256) { throw "Backup changed: $backup" }
        Copy-Item -LiteralPath $backup -Destination $target -Force
        Write-Host "Restored previous $($entry.name)"
    } elseif (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force; Write-Host "Removed $($entry.name)" }
}
if (-not $incomplete) { Remove-Item -LiteralPath $manifestPath -Force }
Write-Host 'Local imported models and game saves were preserved.'
