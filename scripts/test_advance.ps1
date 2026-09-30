Add-Type -AssemblyName System.Windows.Forms
$env:BLVR_SIM = "1"
$gameDir = "D:\SteamLibrary\steamapps\common\BrutalLegend"
$proc = Start-Process -FilePath (Join-Path $gameDir "BrutalLegend.exe") -WorkingDirectory $gameDir -PassThru

for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Seconds 1
    $p = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
    if (-not $p) { break }
    if ($i -ge 5 -and $i % 3 -eq 0) {
        # Press Enter / Space to advance past logos
        [System.Windows.Forms.SendKeys]::SendWait("{ENTER}")
        [System.Windows.Forms.SendKeys]::SendWait(" ")
    }
}

Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
Get-Content (Join-Path $gameDir "blvr.log") | Select-Object -Last 40
