[CmdletBinding()]
param([string]$GameDir)
$ErrorActionPreference='Stop'
. ($PSScriptRoot+'\common.ps1')
$root=Split-Path -Parent $PSScriptRoot
if (-not $GameDir) { $GameDir=(Get-Content -LiteralPath (Join-Path $root 'settings.json') -Raw | ConvertFrom-Json).game_dir }
$GameDir=(Resolve-Path -LiteralPath $GameDir).Path
if (Get-Process -Name BrutalLegend -ErrorAction SilentlyContinue) { throw 'Exit the game normally before uninstalling BLVR.' }
$manifestPath=Join-Path $GameDir 'blvr-install.json'
if (-not (Test-Path -LiteralPath $manifestPath)) { Write-Host 'No BLVR installation manifest found.'; return }
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.product -ne 'BrutalLegendVR') { throw 'Unexpected installation manifest.' }
$incomplete=$false
foreach ($entry in $manifest.files) {
    if ($entry.name -notin @('d3d9.dll','openxr_loader.dll','dxvk_d3d9.dll','steam_appid.txt')) { throw 'Unexpected file in installation manifest.' }
    $target=[IO.Path]::GetFullPath((Join-Path $GameDir $entry.name))
    if ([IO.Path]::GetDirectoryName($target) -ne $GameDir) { throw 'Invalid uninstall target.' }
    if (Test-Path -LiteralPath $target) {
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $entry.installed_sha256) {
            Write-Warning "$($entry.name) changed after installation; leaving it in place."; $incomplete=$true; continue
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
