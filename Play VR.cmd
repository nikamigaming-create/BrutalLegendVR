@echo off
setlocal
title Brutal Legend VR
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\launch_vr.ps1" %*
set "launchExitCode=%errorlevel%"
if not "%launchExitCode%"=="0" (
    echo.
    echo BLVR could not start. Review the message above.
    echo Host log: "%~dp0tools\blvr_xr_host.log"
    pause
)
exit /b %launchExitCode%
