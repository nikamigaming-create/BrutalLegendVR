[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Stage, [string]$Version='0.1.0-preview.6')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'scripts\common.ps1')
$proof=Join-Path $root 'artifacts\installer-diagnostics'
New-Item -ItemType Directory -Path $proof -Force | Out-Null
$Stage=(Resolve-Path -LiteralPath $Stage).Path
if (-not $Stage.StartsWith((Join-Path $root 'release-work\stage-') ,[StringComparison]::OrdinalIgnoreCase)) { throw 'Use an audited workspace runtime stage.' }
$app=Join-Path $proof 'app'
$logs=Join-Path $proof 'logs'
$marker=Join-Path $proof 'abort-installer-test'
$script=Get-Content -LiteralPath (Join-Path $root 'scripts\installer.iss') -Raw
# Use the real installer source and payload with isolated identity, shortcuts,
# destination and logs. Do not alter the user's installed BLVR or launch a game.
$script=$script.Replace('C096980B-6910-4A02-B262-EE0D0A71AF1E','237983F1-9B3B-4D61-B09D-045F79302AAA')
$script=$script.Replace('DefaultGroupName=Brütal Legend VR','DefaultGroupName=BLVR diagnostics fixture')
$script=$script.Replace('..\assets\',(Join-Path $root 'assets\')).Replace('..\LICENSE',(Join-Path $root 'LICENSE'))
$script=$script.Replace('{localappdata}\BrutalLegendVR\Logs',$logs)
$script+=@'

function InitializeSetup: Boolean;
begin
  Result := not FileExists(ExpandConstant('{src}\abort-installer-test'));
  if not Result then Log('Fixture abort before installing any files.');
end;
'@
$source=Join-Path $proof 'installer-test.iss'
[IO.File]::WriteAllText($source,$script,[Text.UTF8Encoding]::new($true))
$compiler=Join-Path $root 'release-work\inno\compiler\ISCC.exe'
& $compiler ('/DSourceDir='+$Stage) ('/DReleaseVersion='+$Version) ('/O'+$proof) '/Finstaller-test' $source *> (Join-Path $proof 'compile.log')
if ($LASTEXITCODE -ne 0) { throw ('Installer test compilation failed; see '+(Join-Path $proof 'compile.log')) }
$setup=Join-Path $proof 'installer-test.exe'
$checks=[Collections.Generic.List[string]]::new()
function Check($condition,[string]$label) { if (-not $condition) { throw "FAIL: $label" };$checks.Add($label) }
function Run-Installer([string]$path,[string]$arguments) {
    $process=Start-Process -FilePath $path -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(120000)) { $process.Kill();throw 'Installer test timed out.' }
    return $process.ExitCode
}
$originalLocalAppData=$env:LOCALAPPDATA
$env:LOCALAPPDATA=Join-Path $proof 'user-data'
try {
    Check ((Run-Installer $setup ('/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /DIR="'+$app+'"')) -eq 0) 'installer succeeds without explicit /LOG'
    $installerLog=Join-Path $logs 'installer-latest.log'
    Check ((Test-Path -LiteralPath $installerLog) -and (Get-Content -LiteralPath $installerLog -Raw) -match 'Saving installer support log') 'installer automatically preserves its log outside app'
    $exe=Join-Path $app 'tools\blvr_setup.exe'
    Check ((Invoke-BlvrProgram $exe ('--prepare --game-dir "'+(Join-Path $proof 'missing-game')+'"') 30) -ne 0) 'packaged windowless setup reports failed preparation'
    $appLogs=Join-Path $env:LOCALAPPDATA 'BrutalLegendVR\Logs'
    $failure=(Get-ChildItem -LiteralPath $appLogs -Filter 'launcher-*.log' | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }) -join "`n"
    Check ($failure -match 'Traceback' -and $failure -match 'Select the installed PC game folder') 'windowless setup retains full exception traceback'
    Copy-Item -LiteralPath $installerLog -Destination (Join-Path $appLogs 'installer-latest.log')
    $zip=Join-Path $proof 'BLVR-support.zip'
    Check ((Invoke-BlvrProgram $exe ('--export-support "'+$zip+'"') 30) -eq 0) 'packaged exporter works without opening launcher UI'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $bundle=[IO.Compression.ZipFile]::OpenRead($zip)
    try {
        Check ($null -ne $bundle.GetEntry('report.json') -and $null -ne $bundle.GetEntry('launcher/installer-latest.log')) 'packaged ZIP includes report and installer log'
    } finally { $bundle.Dispose() }
    [IO.File]::WriteAllText($marker,'abort before installation')
    Check ((Run-Installer $setup ('/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /DIR="'+$app+'"')) -ne 0) 'early setup abort exits unsuccessfully'
    $abortedLog=Get-Content -LiteralPath $installerLog -Raw
    Check ($abortedLog -match 'Fixture abort before installing any files' -and $abortedLog -match 'Saving installer support log') 'early abort still preserves installer log'
    Check (Test-Path -LiteralPath ($installerLog+'.previous')) 'previous install log survives the next attempt'
    Remove-Item -LiteralPath $marker
    $uninstall=Join-Path $app 'unins000.exe'
    Check ((Run-Installer $uninstall '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG') -eq 0) 'fixture uninstalls without touching retail game'
    $deadline=[DateTime]::UtcNow.AddSeconds(10)
    while (-not (Test-Path -LiteralPath (Join-Path $logs 'uninstaller-latest.log')) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
    Check (Test-Path -LiteralPath (Join-Path $logs 'uninstaller-latest.log')) 'uninstaller preserves its troubleshooting log'
    [IO.File]::WriteAllText((Join-Path $proof 'receipt.json'),(@{passed=$true;checks=$checks.ToArray();game_launched=$false} | ConvertTo-Json -Depth 4))
    Write-Host "PASS: $($checks.Count) packaged installer diagnostics checks"
} finally {
    $env:LOCALAPPDATA=$originalLocalAppData
    Remove-Item -LiteralPath $marker -ErrorAction SilentlyContinue
}
