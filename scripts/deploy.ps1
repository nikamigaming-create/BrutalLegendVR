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
    foreach ($entry in $old.files) { $entries[$entry.name]=$entry }
}
foreach ($name in $sources.Keys) {
    $destination = Join-Path $GameDir $name
    $hash = (Get-FileHash -LiteralPath $sources[$name] -Algorithm SHA256).Hash
    $entry = $entries[$name]
    if (Test-Path -LiteralPath $destination) {
        $existing = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        if ((-not $entry -or $entry.installed_sha256 -ne $existing) -and $existing -ne $hash) {
            $backup = "$name.blvr-backup-$($existing.Substring(0,12))"
            Copy-Item -LiteralPath $destination -Destination (Join-Path $GameDir $backup) -Force
            $entry = [pscustomobject]@{name=$name; original=$backup; original_sha256=$existing; installed_sha256=$hash}
        }
    }
    if (-not $entry) { $entry=[pscustomobject]@{name=$name; original=$null; original_sha256=$null; installed_sha256=$hash} }
    $entry.installed_sha256=$hash
    Copy-Item -LiteralPath $sources[$name] -Destination $destination -Force
    $entries[$name]=$entry
    @{product='BrutalLegendVR'; files=@($entries.Values)} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    Write-Host "Installed $name"
}
$appIdFile = Join-Path $GameDir 'steam_appid.txt'
if (-not (Test-Path -LiteralPath $appIdFile)) {
    Set-Content -LiteralPath $appIdFile -Value '225260' -Encoding ascii
    $entries['steam_appid.txt']=[pscustomobject]@{name='steam_appid.txt'; original=$null; original_sha256=$null; installed_sha256=(Get-FileHash -LiteralPath $appIdFile -Algorithm SHA256).Hash}
    @{product='BrutalLegendVR'; files=@($entries.Values)} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
}
