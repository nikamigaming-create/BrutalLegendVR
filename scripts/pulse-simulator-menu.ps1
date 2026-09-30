param(
    [Parameter(Mandatory = $true)][string]$DataDirectory,
    [ValidateSet("left", "right")][string]$Hand = "right"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$inputScript = "D:\code\fnvvr\scripts\invoke-openxr-simulator-input.ps1"
if (-not (Test-Path -LiteralPath $inputScript -PathType Leaf)) {
    throw "FNVVR simulator input driver is missing: $inputScript"
}

# Right-controller Menu is mapped by the existing FNVVR sidecar to XInput
# Start.  This is per-run simulator IPC; it does not touch a window, focus,
# keyboard, mouse, or the simulator UI.
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $inputScript `
    -DataDirectory $DataDirectory -Hand $Hand -Menu pressed -WaitMilliseconds 5000
Start-Sleep -Milliseconds 250
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $inputScript `
    -DataDirectory $DataDirectory -Hand $Hand -Menu released -WaitMilliseconds 5000
