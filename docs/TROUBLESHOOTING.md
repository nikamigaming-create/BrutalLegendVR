# Troubleshooting a tester's installation

The **Setup EXE** is the Windows installer. It installs the **Brütal Legend VR
launcher**, shortcuts and dependencies. The launcher's **Prepare / repair game**
imports the owned model; **Play VR** deploys the game hook and starts the VR host
and game. These are separate steps, and each can fail independently.

## Collect a report (preview 5 and later)

Watch the [1:51 instructional video](https://github.com/nikamigaming-create/BrutalLegendVR/releases/download/v0.1.0-preview.5/BLVR-support-guide.mp4)
([captions](https://github.com/nikamigaming-create/BrutalLegendVR/releases/download/v0.1.0-preview.5/BLVR-support-guide.srt))
for launching, creating a support ZIP, and sending it with a useful report.
Preview 4 and earlier need the older-release instructions below.

1. Open **Brütal Legend VR** and select the game installation.
2. Connect the headset through Quest Link / Air Link, then click **Check installation**.
3. If needed, open **VR settings**, enable **Detailed troubleshooting logs**,
   click **Save VR settings**, and reproduce the problem with **Play VR**.
   Enable **Performance logging** as well for stutter or frame rate problems.
4. Immediately click **Export support ZIP…** on **Play & setup**, choose where
   to save it, review it and send it with the issue description.
5. Turn off detailed/performance logging after collecting the report.

Basic launcher and installer logs are always saved; no logging switch is needed
to diagnose a failed installation or initial launch. **Open logs folder** and the
Start menu's **Troubleshooting logs** shortcut open
`%LOCALAPPDATA%\BrutalLegendVR\Logs`. If that location cannot be written, the
launcher tries `%TEMP%\BrutalLegendVR\Logs`.

If the app window will not open, run **Collect Support Logs.cmd** from its
installation or portable folder. It exports `%TEMP%\BLVR-support.zip` and
selects it in Explorer. This works without opening the Tk interface. The same
operation is available from PowerShell:

```powershell
.\tools\blvr_setup.exe --export-support "$env:TEMP\BLVR-support.zip"
```

The ZIP includes recent launcher/preflight and installer logs, the host log,
game hook/input logs when the game can be found, opt-in diagnostic log tails,
settings, controls, Windows version, graphics driver information, runtime
candidates, and hashes of the relevant executables/DLLs. `report.json` records
missing/unreadable files, modification times and truncation. Each copied file
is capped at 2 MiB; the combined copied content is capped at 24 MiB. Missing
logs are useful evidence: the failing step may never have reached that process.
The export copies only known filenames/patterns. It excludes game assets, saves,
recordings and an environment dump. Nothing is uploaded automatically. Paths in
logs and settings can contain the Windows username; review the ZIP before sharing.

## If the Windows installer fails

Preview 5 saves `installer-latest.log` in the logs folder when setup exits,
including cancellation or a failed install. One previous log is retained as
`installer-latest.log.previous`. The original Inno Setup log also remains in
`%TEMP%` as `Setup Log YYYY-MM-DD #NNN.txt` unless an explicit `/LOG` path was used.
Automatic installer logging uses Inno Setup's
[SetupLogging](https://jrsoftware.org/ishelp/topic_setup_setuplogging.htm) directive.

For **preview 4 and earlier**, rerun the downloaded installer with an explicit
log destination. Open PowerShell in the download folder and run:

```powershell
& '.\BrutalLegendVR-0.1.0-preview.4-Setup.exe' "/LOG=$env:TEMP\BLVR-installer.log"
```

Substitute the actual downloaded filename. After closing setup, open
`%TEMP%` in Explorer and send **BLVR-installer.log**. If Windows prevents the
EXE from starting at all, no installer log can be created; send the exact
Windows error/security message and check Windows Security's Protection history
for the filename.

Older launchers have no support ZIP export. Collect their
`tools\blvr_xr_host.log` and the game's `blvr.log`, plus the exact launcher error.
Their **Open logs folder** button opens `tools`.

## What to ask the tester

- Which release filename did they download, and did they use the installer or ZIP?
- What headset and PC connection/runtime are they using (Link, Air Link, etc.)?
- Does the normal Steam game start successfully outside VR?
- Which step fails: Windows installation, model preparation, installation check,
  host startup, game startup, or gameplay? What exact message appears?
- What did they expect and what happened? Include the time of reproduction,
  the support ZIP, and a screenshot of the error if available.

Read `launcher/installer-latest.log` for Windows installation problems. Read
the newest `launcher/launcher-*.log` and `launcher/preflight-*.log` for game
selection, preparation, deployment and runtime checks. Read
`host/blvr_xr_host.log` for OpenXR startup, and `game/blvr.log` for the injected
game hook. Compare file hashes and timestamps in `report.json` to identify
mixed versions or old logs. Runtime checks verify discovery; they do not prove
that headset rendering or gameplay is working.
