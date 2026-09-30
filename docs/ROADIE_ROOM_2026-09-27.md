# Eddie's opening courtyard

Replaces the empty grid floor and generic GDI text placards with a furnished
3D metal courtyard. The game still supplies the actual menu art and typography.
The touch target below the display is now an inlaid play switch, retaining
the existing fingertip, aim/trigger and A-button behavior.

The set includes a basalt terrace, a skull/wing/guitar floor medallion, a horned
iron display arch, four amp stacks, gear cases, four braziers, torn red banners,
low railings, basalt columns, irregular distant spires and a fixed dusk sky.
The room has one static transform derived from its recenter pose. Floor texture
coordinates are room-local, independent of eye and navigation changes. The
ground never uses animated UVs. Only fire colour pulses over time.

Stone and medallion artwork are original assets made with the built-in ImageGen
tool. Files and exact prompts live in `assets/room/`. Textures are decoded as
sRGB and use generated mipmaps with anisotropic filtering. The scene is a
custom opening environment, not an imported retail gameplay level.

`roadie_room.h` constructs immutable geometry once at startup. Eddie, the room
and menu are rendered with the same per-eye camera and depth buffer. Stick
walking stays inside a 6.35 metre radius; bulky scenery sits outside that clear
area. The central display retains its approach constraint. Existing rig, input,
weapon, menu and gameplay rendering behavior is retained.

Validation:
- Release host build passes warnings-as-errors.
- `blvr_xr_host --rig-visual-test` passes shader compilation, rendering, metric
  floor calibration, weapon selection, two-hand support and both menu hit paths.
  Its old X/Y checks were updated to send release edges, matching the existing
  chord-safe control behavior.
- `blvr_xr_host --self-test` passes after the live simulator releases its shared
  mappings. An earlier attempt during the live run correctly refused the busy
  mailbox; it did not modify the active session.
- Final compositor stereo views cover front, floor, sides and back in
  `artifacts/stereo-diagnostic-20260927-120113/bot/roadie-*.png`.
- The final scenery build is captured in
  `artifacts/stereo-diagnostic-20260927-120349/bot/courtyard-*.png`, with sparse
  motion timestamps in `courtyard-motion.json`: 12 samples across 10.937 seconds
  of yaw, downward pitch and positional lean. Both eyes and the set are present
  throughout the reviewed sequence. A continued outward stick hold stops inside
  the railing; static floor crops in both eyes remain pixel-identical across the
  two boundary captures. Physical headset comfort remains a user check; these
  are simulator and renderer checks, not a continuous headset recording.

Host SHA-256: `da1b873fbb61bd131760e4b434056774b023a470ce9e894e006a4e867730984b`.
The terrain-correction proxy DLL is unchanged from the preceding task.
