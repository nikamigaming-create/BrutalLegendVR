# Play Brütal Legend in VR

1. Run the Windows installer, or extract the complete portable ZIP into a writable folder outside the game.
2. Open **Brütal Legend VR** (portable: **Setup VR.cmd**), select your installed **BrutalLegend.exe**, and choose
   **Prepare / repair game**. Setup imports the owned model and textures locally.
3. Connect your headset through its PC OpenXR runtime. Quest Link / Air Link
   and Touch controllers are the current profile.
4. Exit the desktop game, then choose **Play VR** (portable: **Play VR.cmd**).
5. Wake both controllers. Select **Continue** with right-stick up/down and
   **A**, or touch the opening panel's Confirm switch.

The launcher installs the supported hook, starts the separate OpenXR host,
waits for tracking readiness and starts the game. Use it for VR sessions.
Animations read the running game's current pose; no motion recording is needed.

## Controls

| Action | Default |
| --- | --- |
| Walk / snap turn | Left stick / right stick |
| Accept / interact | A |
| Back / evade | B |
| Equip or stow axe / guitar | X / Y, on release |
| Use weapon | Right trigger, physical axe swing or guitar stroke |
| Guitar neck | Lighter left grip slides on the neck; firm grip moves and rotates the guitar; release leaves it on your body |
| Guitar fingers | While attached, left trigger curls index, X middle and Y ring; right hand strokes the strings |
| Earthshaker | Both grips together; release before repeating |
| Drive | Right grip and hand turn, or left stick; right trigger gas, left trigger brake |
| Stage commands | Left grip + left stick click, then right stick / A / Y |
| Pause / recenter | Left Menu / left Menu + right stick click |

Open **Remap Controls.cmd** to change the 51 native actions and additional VR
controls. Save reloads the layout during play. Release held buttons after
saving. See [complete controls](VR_CONTROLS.md) for solos and stage commands.

## Startup checks

The app's **VR settings** tab saves your runtime, render resolution, frame
limit, antialiasing, logging and guitar height/distance/neck angle/string-face angle/volume. Changed
display and guitar settings apply on the next launch. **Check installation**
reports failures before starting a game. Preferences and control remaps survive
installer upgrades.

If the headset remains blank, check its PC connection and wake the controllers.
Read `tools/blvr_xr_host.log` in the extracted release and `blvr.log` beside the
game. Run this from the release folder for a preflight without launching:

```powershell
.\scripts\launch_vr.ps1 -CheckOnly
```

The automatic launcher choice prefers the installed Meta Quest Link / Air Link
OpenXR runtime. When Meta is absent, it uses the registered headset runtime.
Choose a runtime in VR settings or use `-RuntimeJson "C:\path\to\runtime.json"`
to override that choice for BLVR.

This is a preview. Local Ironheade AI Practice recruitment, stage progression,
orders and flight have simulator evidence. Physical headset comfort, the
opening mountain ride, precise particle attachment and a complete RTS battle
still need player verification.
See [release validation and limitations](RELEASE_0.1.0-preview.4.md).
