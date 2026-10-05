[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$GameDir)
$ErrorActionPreference = 'Stop'
. ($PSScriptRoot+'\common.ps1')
$projectRoot = Split-Path -Parent $PSScriptRoot
$GameDir = (Resolve-Path -LiteralPath $GameDir).Path
if (Get-Process -Name BrutalLegend -ErrorAction SilentlyContinue) { throw 'Exit the game normally before installing BLVR.' }
$gameExe = Join-Path $GameDir 'BrutalLegend.exe'
$supported = '872DC676E8FD77AD3351DD9DFCC99E89353AAE0ED9857272A65FD47F298FB0B1'
if ((Get-FileHash -LiteralPath $gameExe -Algorithm SHA256).Hash -ne $supported) { throw 'Unsupported game executable. No files were installed.' }
$hook = Join-Path $projectRoot 'bin\d3d9.dll'
if (-not (Test-Path -LiteralPath $hook)) { $hook = Join-Path $projectRoot 'build\Release\d3d9.dll' }
$loader = Join-Path $projectRoot 'bin\openxr_loader.dll'
if (-not (Test-Path -LiteralPath $loader)) { $loader = Join-Path $projectRoot 'build\openxr-sdk\src\loader\Release\openxr_loader.dll' }
if (-not (Test-Path -LiteralPath $loader)) { $loader = Join-Path $projectRoot 'third_party\openxr_loader.dll' }
$sources = @{'d3d9.dll'=$hook; 'openxr_loader.dll'=$loader}
$backend = Join-Path $projectRoot 'bin\dxvk_d3d9.dll'
if (Test-Path -LiteralPath $backend) { $sources['dxvk_d3d9.dll']=$backend }
foreach ($source in $sources.Values) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing release file: $source" }
}
$manifestPath = Join-Path $GameDir 'blvr-install.json'
$entries = @{}
if (Test-Path -LiteralPath $manifestPath) {
    $old = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($old.product -ne 'BrutalLegendVR') { throw 'Unexpected installation manifest.' }
    foreach ($entry in $old.files) {
        if ($entry.name -notin @('d3d9.dll','openxr_loader.dll','dxvk_d3d9.dll','steam_appid.txt')) { throw 'Unexpected installation manifest file.' }
        if ($entries.ContainsKey($entry.name) -or $entry.installed_sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'Invalid installation manifest entry.' }
        if ($entry.original -and ([IO.Path]::GetFileName($entry.original) -ne $entry.original -or $entry.original -notlike "$($entry.name).blvr-backup-*")) { throw 'Invalid installation backup path.' }
        if ($entry.original -and ((Get-FileHash -LiteralPath (Join-Path $GameDir $entry.original) -Algorithm SHA256).Hash -ne $entry.original_sha256)) { throw "Backup changed: $($entry.original). Repair this backup before updating VR." }
        $entries[$entry.name]=$entry
    }
}
$transaction = [IO.Path]::GetFullPath((Join-Path $GameDir ('.blvr-transaction-'+[guid]::NewGuid().ToString('N'))))
if ([IO.Path]::GetDirectoryName($transaction) -ne $GameDir) { throw 'Invalid installation transaction path.' }
New-Item -ItemType Directory -Path $transaction | Out-Null
$written = [Collections.Generic.List[string]]::new()
$createdBackups = [Collections.Generic.List[string]]::new()
$rollbackErrors=@()
try {
    if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'steam_appid.txt'))) {
        $appIdSource=Join-Path $transaction 'appid-source'
        [IO.File]::WriteAllText($appIdSource,'225260', [Text.Encoding]::ASCII)
        $sources['steam_appid.txt']=$appIdSource
    }
    # Stage every byte and check every target before changing any installed file.
    foreach ($name in @($sources.Keys | Sort-Object)) {
        $destination=Join-Path $GameDir $name
        $staged=Join-Path $transaction ($name+'.new')
        Copy-Item -LiteralPath $sources[$name] -Destination $staged
        $hash=(Get-FileHash -LiteralPath $staged -Algorithm SHA256).Hash
        $entry=$entries[$name]
        if (Test-Path -LiteralPath $destination) {
            $lock=[IO.File]::Open($destination,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
            $lock.Dispose()
            Copy-Item -LiteralPath $destination -Destination (Join-Path $transaction ($name+'.old'))
            $existing=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
            if ((-not $entry -or $entry.installed_sha256 -ne $existing) -and $existing -ne $hash) {
                $backup="$name.blvr-backup-$($existing.Substring(0,12))"
                $backupPath=Join-Path $GameDir $backup
                if (-not (Test-Path -LiteralPath $backupPath)) {
                    Copy-Item -LiteralPath $destination -Destination $backupPath
                    $createdBackups.Add($backupPath)
                } elseif ((Get-FileHash -LiteralPath $backupPath -Algorithm SHA256).Hash -ne $existing) { throw "Backup checksum mismatch: $backup" }
                $entry=[pscustomobject]@{name=$name; original=$backup; original_sha256=$existing; installed_sha256=$hash}
            }
        }
        if (-not $entry) { $entry=[pscustomobject]@{name=$name; original=$null; original_sha256=$null; installed_sha256=$hash} }
        $entry.installed_sha256=$hash
        $entries[$name]=$entry
    }
    $newManifest=Join-Path $transaction 'manifest.new'
    [IO.File]::WriteAllText($newManifest,(@{product='BrutalLegendVR'; files=@($entries.Values)} | ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
    foreach ($name in @($sources.Keys | Sort-Object)) {
        $destination=Join-Path $GameDir $name
        $staged=Join-Path $transaction ($name+'.new')
        if (Test-Path -LiteralPath $destination) { [IO.File]::Replace($staged,$destination,[NullString]::Value) }
        else { [IO.File]::Move($staged,$destination) }
        $written.Add($name)
    }
    if (Test-Path -LiteralPath $manifestPath) { [IO.File]::Replace($newManifest,$manifestPath,[NullString]::Value) }
    else { [IO.File]::Move($newManifest,$manifestPath) }
    foreach ($name in $written) { Write-Host "Installed $name" }
} catch {
    $failure=$_
    $rollbackErrors=@()
    foreach ($name in $written) {
        try {
            $destination=Join-Path $GameDir $name
            $previous=Join-Path $transaction ($name+'.old')
            if (Test-Path -LiteralPath $previous) { [IO.File]::Replace($previous,$destination,[NullString]::Value) }
            else { Remove-Item -LiteralPath $destination -Force }
        } catch { $rollbackErrors += $_.Exception.Message }
    }
    if ($rollbackErrors.Count) { throw "Update failed; recovery files remain in $transaction. $($rollbackErrors -join ' ')" }
    foreach ($backupPath in $createdBackups) { Remove-Item -LiteralPath $backupPath -Force }
    throw $failure
} finally {
    if (-not $rollbackErrors.Count) {
        # The absolute transaction path was checked against the selected game directory above.
        Remove-Item -LiteralPath $transaction -Recurse -Force
    }
}
