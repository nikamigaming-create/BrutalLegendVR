# PC OpenXR runtime acceptance

Launcher selection now supports Meta, Virtual Desktop (VDXR), and SteamVR
without preferring Meta merely because it is installed. This is launch
preparation. On October 20, 2026, the user reported that the updated local
launcher worked in Virtual Desktop (VDXR). No detailed per-step results or
capture were supplied. The full checklist below remains pending, and SteamVR
has not been confirmed on a physical headset.

Automatic uses the inherited XR_RUNTIME_JSON, then the registered 64-bit
ActiveRuntime. A saved BLVR runtime overrides Automatic; -RuntimeJson overrides
both. Invalid selections fail visibly rather than silently switching providers.
The launcher log records the chosen manifest and selection source.

## Quest 3 test procedure

For each route below, connect the headset first, choose the corresponding
64-bit runtime in BLVR VR settings, save, and run the installation check:

| Connection | BLVR runtime |
| --- | --- |
| Quest Link / Air Link | Meta (`oculus_openxr_64.json`) |
| Virtual Desktop with VDXR | `virtualdesktop-openxr.json` |
| Virtual Desktop through SteamVR | `steamxr_win64.json` |
| Steam Link | `steamxr_win64.json` |

VDXR is normally in `C:\Program Files\Virtual Desktop Streamer\OpenXR`.
Use Browse for a custom installation. Do not select its `-32` manifest:
BLVR's external OpenXR host is 64-bit.

1. Check that the preflight log identifies the intended runtime and passes.
2. Launch with Play VR. Confirm the opening room appears in both eyes,
   head motion is correct, and both Touch controllers track.
3. Enter gameplay. Check movement, snap turn, recenter, buttons, triggers,
   grips, axe/guitar interaction, and wrist/solo menus.
4. Check audio, pause/resume, headset sleep/wake, clean exit, and relaunch.
5. Disconnect the headset and repeat the check. Confirm the error identifies
   the runtime; reconnect and confirm recovery.
6. Repeat Automatic with VDXR and SteamVR registered while Meta remains
   installed. Clear any saved BLVR override and inherited XR_RUNTIME_JSON first.
7. Export a support ZIP for each failed route with the connection method,
   Streamer/SteamVR version, and exact failing step. Old game logs may precede
   a preflight failure; use timestamps to distinguish them.

VDXR documents D3D11, projection/quad layers and Oculus Touch support:
https://github.com/mbucchia/VirtualDesktop-OpenXR/wiki/Developers
These capabilities are compatible in principle with BLVR's host, but do not
replace the live tests above. Other controller profiles remain unverified.
