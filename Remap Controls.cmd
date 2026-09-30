@echo off
cd /d "%~dp0"
if exist "%~dp0tools\blvr_setup.exe" (
    start "" "%~dp0tools\blvr_setup.exe" --controls
) else (
    python scripts\blvr_setup.py --controls
)
