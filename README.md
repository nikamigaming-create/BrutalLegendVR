# Brütal Legend VR

A Windows OpenXR mod by Nikami for the Steam PC version of Brütal Legend.
First-person stereo gameplay, tracked Eddie hands and equipment, physical axe
swings and guitar strokes, forearm menus, and guitar-mounted solo UI.

**[Download 0.1.0-preview.4](https://github.com/nikamigaming-create/BrutalLegendVR/releases/tag/v0.1.0-preview.4), an experimental testing release.** This launcher patch fixes the
OpenXR version mismatch between preflight and gameplay. Automatic runtime
selection now prefers Meta Quest Link / Air Link when installed; an explicitly
selected runtime still takes precedence. The installer, saved VR settings and
chest-mounted Touch guitar from preview 3 are included. Its guitar and RTS
simulator recordings remain available in the [preview 3 release](https://github.com/nikamigaming-create/BrutalLegendVR/releases/tag/v0.1.0-preview.3).
Physical headset acceptance, complete battles, all lighting/shader effects,
other factions, online play and the full campaign remain unverified. See the
[release validation notes](docs/RELEASE_0.1.0-preview.4.md).

## Install and play

Use **BrutalLegendVR-0.1.0-preview.4-Setup.exe** for a per-user installation
with Start menu shortcuts and an integrated Play, settings and controls app.
The portable ZIP includes the same runtime and its dependencies; Python is
not required to play.

1. Install and launch your legally owned Steam copy of Brütal Legend once.
2. Run the Windows installer, or extract the entire portable release ZIP into a writable folder outside the game.
3. Open **Brütal Legend VR**, select **BrutalLegend.exe**, and choose **Prepare / repair game**. In the portable package, use **Setup VR.cmd**.
4. Connect your headset through its PC OpenXR runtime. Quest Link / Air Link
   with Touch controllers is the current controller profile.
5. Exit any running desktop game, then choose **Play VR**. Use **VR settings** to select a runtime and adjust graphics, guitar placement and volume.

The first setup imports Eddie's model, skeleton and textures locally from your
installation. The release contains no retail game assets. Animation uses the
game's current pose at runtime; no idle, walk or attack recording is required.
For the portable ZIP, keep the extracted folder: the launcher and OpenXR host
run from it.

The launcher checks the supported executable before installing the hook.
Existing DLLs are backed up. Deployment stages the whole update and rolls back
if it cannot complete. **Uninstall VR.cmd** restores those files when
their installed hashes still match; your saves and imported model remain.

Supported executable SHA-256:

```text
872dc676e8fd77ad3351dd9dfcc99e89353aae0ed9857272a65fd47f298fb0b1
```

## Controls

| Action | Default |
| --- | --- |
| Walk / snap turn | Left stick / right stick |
| Interact / accept | A |
| Native menu Select / Back / Watch Tutorial | A / B / X |
| Opening native carousel / vertical options | Left stick left/right / right stick up/down |
| Back / evade / block | B |
| Equip or stow axe / guitar | X / Y, on release |
| Attack | Right trigger with a selected weapon, physical axe swing or guitar stroke |
| Hold and slide guitar neck | Lighter left grip near the neck; slide the left controller along it |
| Reposition guitar on body | Firm left grip while attached; move and rotate, then release to save placement |
| Guitar fingers while holding neck | Left trigger: index; X: middle; Y: ring. Right-hand strokes play notes |
| Target | Left trigger |
| Earthshaker | **Left grip + right grip together**; release before repeating |
| Solo selection | Right stick click; right stick + A / right trigger, or touch the wedge |
| Timed solo notes | A / X / Y, or right-hand strokes on the beat |
| Drive | Right grip + hand turn, or left stick; right trigger gas, left trigger brake |
| Stage commands | Hold left grip + left stick click; right stick orders, hold A for build wheel, Y fly |
| Held build wheel | Keep command + A held; right stick selects, right trigger recruits/upgrades the stage, left trigger requests unit research when available, X cancels a queued unit |
| Pause / recenter | Left Menu / left Menu + right stick click |

The guitar stays at estimated chest height when selected. Head pitch and roll
let you look down at it; large turns gradually reorient the chest estimate.
Lighter left grip attaches the fretting hand. A firm grip moves and rotates the
whole guitar; release leaves it on your body and saves that placement for the
next launch. VR settings can reset the saved placement.
Finger buttons retain the equipped guitar while the neck is held. Touch
controllers estimate finger poses from triggers, grip and button contact;
independent bare-finger motion is unavailable with these controllers.

In the opening 3D room, right-stick left/right turns the room. Use **left-stick
left/right** to change the native carousel. To try the observed local stage
battle path, select **Multiplayer**, press A, use B to skip the stage tutorial
if it appears, then select **AI Practice** with A. In its lobby, **left Menu**
activates Start. Follow the current native labels if you have remapped controls.

Run **Remap Controls.cmd** to edit all **51 native game actions + 20 VR
controls**. Search by action or input, select a row, and choose its input.
Edits stay pending while you browse; **Save controls** applies them together.
The editor shows what the selected action does and your current command and
Earthshaker chords. Changes reload during play within 250 ms. Release held
buttons after saving. Invalid edits keep the last working layout. You can
also edit **controls.ini** directly.

Both the game hook and host read the same layout. Native text hints use that
layout; the guitar's timed note display includes a live control legend because
its original A/X/Y artwork stays baked into the game UI. Native contexts and
gameplay locks still determine when an action is available. Existing native
messages may keep their previous labels until the game refreshes them.
Overlapping bindings deliberately activate overlapping actions.
Use ordinary bindings for combat and solos; command mode reserves their inputs
for stage commands and secondary actions. Unbinding a button leaves physical
swings, strums and fingertip contact available.
The held build wheel excludes confirmation/cancellation/research inputs that
reuse any button required to hold its chord. **Build research alternate** defaults to left trigger and works
only in that wheel; ordinary Use/interact stays on A. The separate Use action
also works for research when mapped to a distinct input.

If a controller loses tracking, its arm and equipment use the current native
game pose, and generated attack and UI input pulses release. Wrist and guitar
panels appear only when their rendered geometry matches both submitted eyes;
they are withheld while that matching data is unavailable.

See [complete controls](docs/VR_CONTROLS.md) and
[preview validation and known issues](docs/RELEASE_0.1.0-preview.4.md).

![Default Touch controls](assets/ui/default-controls.png)

Defaults are shown above; all 71 controls can be remapped live. Keep the full
command chord and A held while choosing and using the build wheel.

## Troubleshooting

Use the VR launcher to start both processes. If the headset remains blank,
check the connection, wake both controllers, and read
`tools/blvr_xr_host.log` plus the game's `blvr.log`.
For a preflight without launching the game:

```powershell
.\scripts\launch_vr.ps1 -CheckOnly
```

Select another installed 64-bit OpenXR runtime for a launch with
`-RuntimeJson "C:\path\to\runtime.json"`. Automatic selection prefers the
installed Meta Quest Link / Air Link runtime, then the registered headset
runtime. A saved runtime selection overrides that automatic choice.
Other controller profiles are not yet verified.

If wrist or guitar panels disappear, check tracking and the host log for
`native UI geometry missing`. Preview 2 withholds these panels when their
attachment cannot be matched to both game images. Keep the hook and host from
the same extracted package when updating.

Performance CSV and telemetry logging are opt-in. Set **BLVR_PERF=1** (or
**true**) before launching for CPU/GPU timings, or **BLVR_TELEMETRY=1** (or
**true**) for diagnostic telemetry; ordinary play leaves both disabled.
Telemetry, the per-launch input diagnostic log and each profiler file stop at
32 MiB. Input diagnostics share the telemetry opt-in. Video recording also requires
an explicit opt-in, including simulator sessions.

The native pause row is fully framed in both simulator eyes at a neutral palm
pose after the popup-placement fix. Its labels are small in the 640-pixel-per-eye
review image. The game hides the row after 450 idle Flash frames; a native
button or stick input wakes it. Head or palm motion alone keeps it idle.
A fresh 10.5-second stereo take kept the full row visible through head and palm movement. Physical readability remains unverified.

## Build from source

Install Visual Studio 2022 with Desktop C++ and a Windows SDK, CMake 3.20+,
and Python 3.11+. Clone with submodules:

```powershell
git clone --recursive https://github.com/nikamigaming-create/BrutalLegendVR.git
cd BrutalLegendVR
.\scripts\build.ps1
python -m pip install -r requirements-release.txt
python scripts/blvr_setup.py
```

The hook is x86; the host and setup utility are x64. MinHook is vendored and
OpenXR SDK sources are pinned as a submodule. To package a runtime ZIP, install
the Python dependencies above and run `scripts/package_release.ps1`. Its
optional `-DxvkDll` argument accepts the official DXVK 3.0.2 x86 D3D9 DLL;
the tested release includes that backend.

Original mod code and generated artwork use the MIT license. Dependencies
retain their own licenses; see [third-party notices](THIRD_PARTY_NOTICES.md).
Brütal Legend and its retail content belong to their respective owners.
This is an unofficial mod.
