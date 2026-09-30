$env:BLVR_SIM = "1"
$gameDir = "D:\SteamLibrary\steamapps\common\BrutalLegend"
$logFile = Join-Path $gameDir "blvr.log"
$videoFile = "D:\code\blvr\artifacts\blvr-sim-camera-proof.mp4"

if (Test-Path $videoFile) {
    Remove-Item $videoFile -Force -ErrorAction SilentlyContinue
}

Write-Host "Launching Brutal Legend with BLVR_SIM=1..."
$proc = Start-Process -FilePath (Join-Path $gameDir "BrutalLegend.exe") -WorkingDirectory $gameDir -PassThru

$maxWaitSec = 40
$startTime = Get-Date

for ($s = 0; $s -lt $maxWaitSec; $s++) {
    Start-Sleep -Seconds 1
    $p = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
    if (-not $p) {
        Write-Host "Game process exited."
        break
    }

    if (Test-Path $logFile) {
        $lastLines = Get-Content $logFile -Tail 5 -ErrorAction SilentlyContinue
        $finishLine = $lastLines | Where-Object { $_ -like "*Video finalized*" -or $_ -like "*Video successfully finalized*" }
        if ($finishLine) {
            Write-Host "Detected video finalized in log! Waiting 2s for file flush..."
            Start-Sleep -Seconds 2
            break
        }
    }
}

Write-Host "Stopping game process..."
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

Write-Host "=== BLVR Log Tail ==="
if (Test-Path $logFile) {
    Get-Content $logFile -Tail 35
}

Write-Host "=== Video Artifact Check ==="
if (Test-Path $videoFile) {
    $item = Get-Item $videoFile
    Write-Host "SUCCESS! Video artifact created: $($item.FullName) (Size: $($item.Length) bytes)"
} else {
    Write-Host "Video artifact not found yet."
}
