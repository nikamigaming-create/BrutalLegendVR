# Load the modules belonging to this shell. A launcher started from PowerShell
# 7 can otherwise inherit module paths that Windows PowerShell 5 cannot load.
foreach ($blvrModule in @('Microsoft.PowerShell.Management','Microsoft.PowerShell.Utility')) {
    Import-Module ($PSHOME+'\Modules\'+$blvrModule+'\'+$blvrModule+'.psd1') -ErrorAction Stop
}

function Read-BlvrSettings([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    try {
        $value=Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
        if ($null -eq $value -or $value -is [Array] -or $value -is [string] -or $value -is [ValueType]) { throw 'Settings must be an object.' }
        return $value
    } catch { Write-Warning 'Settings could not be read. Open Brütal Legend VR to select your game and save settings again.'; return $null }
}

function Set-BlvrPreferences($Settings) {
    $preferences=@{
        render_resolution=@('BLVR_RENDER_WIDTH',1536,720,2048)
        frame_limit_fps=@('BLVR_FRAME_LIMIT_FPS',90,45,144)
        guitar_height_cm=@('BLVR_GUITAR_HEIGHT_CM',53,25,85)
        guitar_distance_cm=@('BLVR_GUITAR_DISTANCE_CM',38,20,70)
        guitar_angle_degrees=@('BLVR_GUITAR_ANGLE',35,0,70)
        guitar_face_degrees=@('BLVR_GUITAR_FACE_ANGLE',50,0,80)
        guitar_volume=@('BLVR_GUITAR_VOLUME',65,0,100)
    }
    foreach ($key in $preferences.Keys) {
        $rule=$preferences[$key]; $number=[int]$rule[1]; $candidate=0
        if ($Settings -and [int]::TryParse([string]$Settings.$key,[ref]$candidate) -and $candidate -ge $rule[2] -and $candidate -le $rule[3]) { $number=$candidate }
        [Environment]::SetEnvironmentVariable($rule[0],[string]$number,'Process')
    }
    $env:BLVR_RENDER_HEIGHT=$env:BLVR_RENDER_WIDTH
    $env:BLVR_EDGE_AA=if ($Settings -and $Settings.edge_aa -eq $false) { '0' } else { '1' }
    $env:BLVR_TELEMETRY=if ($Settings -and $Settings.telemetry -eq $true) { '1' } else { '0' }
    $env:BLVR_PERF=if ($Settings -and $Settings.performance_logging -eq $true) { '1' } else { '0' }
}

function Invoke-BlvrProgram([string]$Path,[string]$Arguments,[int]$TimeoutSeconds=30) {
    if ($TimeoutSeconds -lt 1 -or $TimeoutSeconds -gt 600) { throw 'Invalid program timeout.' }
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=$Path;$start.Arguments=$Arguments;$start.UseShellExecute=$false
    $start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $start.WorkingDirectory=Split-Path -Parent $Path
    $process=[Diagnostics.Process]::new();$process.StartInfo=$start
    $watch=[Diagnostics.Stopwatch]::StartNew();$launched=$false
    try {
        if (-not $process.Start()) { throw "Could not start $Path. Repair the installation." }
        $launched=$true
        $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds*1000)) {
            $process.Kill();$process.WaitForExit(5000) | Out-Null
            throw "$(Split-Path -Leaf $Path) timed out after $TimeoutSeconds seconds. Check the headset connection and try again."
        }
        # A child can exit while a descendant keeps its redirected pipes open.
        # Reading those pipes must obey the same deadline as the program.
        $remaining=[int][Math]::Max(1,$TimeoutSeconds*1000-$watch.ElapsedMilliseconds)
        if (-not [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($stdout,$stderr),$remaining)) {
            throw "$(Split-Path -Leaf $Path) output timed out. Open the app logs for details."
        }
        $output=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
        if ($output) { Write-Host $output.Trim() }
        return $process.ExitCode
    } finally {
        if ($launched -and -not $process.HasExited) { $process.Kill();$process.WaitForExit(5000) | Out-Null }
        $process.Dispose()
    }
}

function Read-BlvrStartupLog([string]$Path,[ref]$Offset) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    $stream=$null
    try {
        $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
        if ($stream.Length -lt $Offset.Value) { $Offset.Value=0 }
        $stream.Position=$Offset.Value
        $count=[int][Math]::Min(65536,$stream.Length-$Offset.Value)
        if ($count -le 0) { return '' }
        $buffer=New-Object byte[] $count;$read=$stream.Read($buffer,0,$count)
        $Offset.Value+=$read
        return [Text.Encoding]::UTF8.GetString($buffer,0,$read)
    } catch [IO.IOException] { return '' }
    finally { if ($stream) { $stream.Dispose() } }
}
