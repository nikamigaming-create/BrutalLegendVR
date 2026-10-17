@echo off
cd /d "%~dp0"
if exist "%~dp0tools\blvr_setup.exe" (
    start "" /wait "%~dp0tools\blvr_setup.exe" --export-support "%TEMP%\BLVR-support.zip"
) else (
    python scripts\blvr_setup.py --export-support "%TEMP%\BLVR-support.zip"
)
if errorlevel 1 (
    echo Support export failed. See %%LOCALAPPDATA%%\BrutalLegendVR\Logs for details.
    pause
    exit /b 1
)
explorer.exe /select,"%TEMP%\BLVR-support.zip"
