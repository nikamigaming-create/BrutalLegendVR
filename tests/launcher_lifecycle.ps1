[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'scripts\common.ps1')
$proof=Join-Path $root 'artifacts\launcher-lifecycle'
New-Item -ItemType Directory -Path $proof -Force | Out-Null
$fixture=Join-Path $proof ([guid]::NewGuid().ToString('N'))
$checks=[Collections.Generic.List[string]]::new()
$shell=Join-Path $PSHOME 'powershell.exe'
function Check($condition,[string]$label) {
    if (-not $condition) { throw "FAIL: $label" };$checks.Add($label)
}
function Encoded([string]$code) { '-NoProfile -ExecutionPolicy Bypass -EncodedCommand '+[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($code)) }
function Run-Launch([string]$mode,[bool]$checkOnly=$false) {
    $env:BLVR_TEST_MODE=$mode
    $arguments=Encoded ('& '''+(Join-Path $fixture 'scripts\launch_vr.ps1')+''' -GameDir '''+(Join-Path $fixture 'game')+''' -RuntimeJson '''+(Join-Path $fixture 'runtime.json')+''' '+$(if($checkOnly){'-CheckOnly'}))
    return Invoke-BlvrProgram $shell $arguments 20
}
function Stop-Fixture {
    foreach ($process in (Get-Process -Name BrutalLegend,blvr_xr_host -ErrorAction SilentlyContinue)) {
        if ($process.Path -and $process.Path.StartsWith($fixture+'\',[StringComparison]::OrdinalIgnoreCase)) {
            $process.Kill();$process.WaitForExit(5000) | Out-Null
        }
    }
}
try {
    New-Item -ItemType Directory -Path $fixture,(Join-Path $fixture 'scripts'),(Join-Path $fixture 'tools'),(Join-Path $fixture 'game'),(Join-Path $fixture 'artifacts\eddie-rig'),(Join-Path $fixture 'assets\room') | Out-Null
    foreach ($name in 'common.ps1','launch_vr.ps1') { Copy-Item -LiteralPath (Join-Path $root ('scripts\'+$name)) -Destination (Join-Path $fixture ('scripts\'+$name)) }
    # Native game hash and deployment are replaced only inside this private
    # fixture. Children are real processes; no retail executable is launched.
    Add-Content -LiteralPath (Join-Path $fixture 'scripts\common.ps1') -Value @'
function Get-FileHash([string]$LiteralPath,[string]$Algorithm) {
    if ((Split-Path -Leaf $LiteralPath) -eq 'BrutalLegend.exe') {
        return [pscustomobject]@{Hash=$(if($env:BLVR_TEST_MODE -eq 'unsupported'){'BAD'}else{'872DC676E8FD77AD3351DD9DFCC99E89353AAE0ED9857272A65FD47F298FB0B1'})}
    }
    Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath $LiteralPath -Algorithm $Algorithm
}
'@
    [IO.File]::WriteAllText((Join-Path $fixture 'scripts\deploy.ps1'),'param($GameDir)'+"`r`n"+'[IO.File]::WriteAllText((Join-Path $PSScriptRoot ''deploy.called''),''fixture'')')
    $source=@'
using System; using System.IO; using System.Threading; using System.Diagnostics;
class FixtureProgram {
    public static int Main(string[] args) {
        string path=Process.GetCurrentProcess().MainModule.FileName;
        string dir=Path.GetDirectoryName(path),mode=Environment.GetEnvironmentVariable("BLVR_TEST_MODE");
        string command=args.Length==0?"":args[0];
        if(command=="--check-controls") return mode=="controlsFail"?2:0;
        if(command=="--check-runtime") return mode=="runtimeFail"?3:0;
        if(command=="--check-assets") return mode=="cacheFail"?4:0;
        if(command=="--prepare") {File.WriteAllText(Path.Combine(dir,"prepare.called"),"fixture");return 4;}
        if(command=="--game") {
            File.WriteAllText(Path.Combine(dir,"host.pid"),Process.GetCurrentProcess().Id.ToString());
            if(mode=="stale") {Thread.Sleep(1500);return 1;}
            File.AppendAllText(Path.Combine(dir,"blvr_xr_host.log"),mode=="rigFail"?"EddieLobby: unavailable\n":"PoseBridge: READY\n");
            Thread.Sleep(60000);return 0;
        }
        if(mode=="gameExit") return 1;
        File.WriteAllText(Path.Combine(dir,"game.pid"),Process.GetCurrentProcess().Id.ToString());
        Thread.Sleep(60000);return 0;
    }
}
'@
    Add-Type -TypeDefinition $source -OutputAssembly (Join-Path $fixture 'tools\blvr_xr_host.exe') -OutputType ConsoleApplication
    Copy-Item -LiteralPath (Join-Path $fixture 'tools\blvr_xr_host.exe') -Destination (Join-Path $fixture 'tools\blvr_setup.exe')
    Copy-Item -LiteralPath (Join-Path $fixture 'tools\blvr_xr_host.exe') -Destination (Join-Path $fixture 'game\BrutalLegend.exe')
    foreach ($name in 'iron-medallion.png','basalt.png') { [IO.File]::WriteAllText((Join-Path $fixture ('assets\room\'+$name)),'fixture') }
    [IO.File]::WriteAllText((Join-Path $fixture 'artifacts\eddie-rig\eddie.rigcache'),'fixture')
    [IO.File]::WriteAllText((Join-Path $fixture 'runtime.json'),'{}')
    [IO.File]::WriteAllText((Join-Path $fixture 'settings.json'),(@{game_dir=(Join-Path $fixture 'game')} | ConvertTo-Json))
    $output=Encoded '[Console]::Out.WriteLine("output");[Console]::Error.WriteLine("error");exit 7'
    Check ((Invoke-BlvrProgram $shell $output 5) -eq 7) 'bounded child preserves nonzero exit status'
    $pidFile=Join-Path $fixture 'timeout.pid'
    $wait=Encoded ('[IO.File]::WriteAllText('''+$pidFile+''',[string]$PID);Start-Sleep -Seconds 30')
    $watch=[Diagnostics.Stopwatch]::StartNew();$failed=$false
    try { Invoke-BlvrProgram $shell $wait 1 | Out-Null } catch { $failed=$true }
    Check ($failed -and $watch.Elapsed.TotalSeconds -lt 8) 'hung preflight ends within its deadline'
    Check (-not (Get-Process -Id ([int](Get-Content -LiteralPath $pidFile)) -ErrorAction SilentlyContinue)) 'timed-out child is terminated'
    $log=Join-Path $fixture 'tools\blvr_xr_host.log'
    [IO.File]::WriteAllText($log,"PoseBridge: READY`n");$offset=(Get-Item -LiteralPath $log).Length
    Check ((Read-BlvrStartupLog $log ([ref]$offset)) -eq '') 'old READY is never read as a new startup'
    [IO.File]::AppendAllText($log,'fresh log');$locked=[IO.File]::Open($log,'Open','ReadWrite','None')
    try { $before=$offset;Check ((Read-BlvrStartupLog $log ([ref]$offset)) -eq '' -and $offset -eq $before) 'locked log leaves the read cursor intact' } finally { $locked.Dispose() }
    Check ((Read-BlvrStartupLog $log ([ref]$offset)) -eq 'fresh log') 'unlocked log retries unread bytes'
    $lock=[IO.File]::Open((Join-Path $fixture 'launcher.lock'),'OpenOrCreate','ReadWrite','None')
    try { Check ((Run-Launch 'ready') -ne 0) 'duplicate installation launch is rejected' } finally { $lock.Dispose() }
    $mutex=[Threading.Mutex]::new($true,'Local\BrutalLegendVR_Launcher_v1')
    try { Check ((Run-Launch 'ready') -ne 0) 'concurrent launch from another installation is rejected' } finally { $mutex.ReleaseMutex();$mutex.Dispose() }
    foreach ($mode in 'unsupported','controlsFail','runtimeFail','cacheFail','stale','rigFail','gameExit') {
        Stop-Fixture
        [IO.File]::WriteAllText($log,"PoseBridge: READY`n")
        Check ((Run-Launch $mode) -ne 0) ($mode+' fails safely')
        Check (-not (Get-Process -Name BrutalLegend,blvr_xr_host -ErrorAction SilentlyContinue)) ($mode+' leaves no fixture child running')
    }
    Check ((Run-Launch 'ready') -eq 0) 'successful startup reports success only after both children survive'
    Check ((@(Get-Process -Name BrutalLegend,blvr_xr_host -ErrorAction SilentlyContinue)).Count -eq 2) 'successful launch leaves its game and host running'
    Check ((Run-Launch 'ready') -ne 0) 'already-running game prevents a second launch'
    Stop-Fixture
    [IO.File]::WriteAllText((Join-Path $proof 'receipt.json'),(@{passed=$true;checks=$checks.ToArray();native_dependencies='private stub executables; owned real child lifetimes; no game launched'} | ConvertTo-Json -Depth 4))
    Write-Host "PASS: $($checks.Count) launcher lifecycle checks"
} finally {
    Stop-Fixture
    Remove-Item Env:\BLVR_TEST_MODE -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $fixture) {
        if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($fixture)) -ne [IO.Path]::GetFullPath($proof)) { throw 'Refusing invalid fixture cleanup.' }
        Remove-Item -LiteralPath $fixture -Recurse -Force
    }
}
