# Brütal Legend VR (BLVR) — Project Status

## Current build: September 29, 2026

Public preview [0.1.0-preview.1](https://github.com/nikamigaming-create/BrutalLegendVR/releases/tag/v0.1.0-preview.1) includes both-grip Earthshaker,
all-action live remapping and same-frame native animation retargeting. Setup
imports the owned model and textures; no recorded action clips are required.
The controls editor, native fixtures and host self-tests are covered by the
[preview release notes](RELEASE_0.1.0-preview.1.md). Physical headset acceptance,
the opening mountain ride, precise attack-effect attachment, all shaders and
live RTS stage battles remain unverified. Historical simulator evidence is in
[the September 29 investigation](CONTROLS_GRAPHICS_2026-09-29.md).

## September 28 hint pass

The Quest hint fix covers native Flash alias expansion used by floating
gameplay messages, which bypassed the previous localization/text hooks.
Earthshaker, weapon sequences, menu confirmation and full command chords now
use the actual Touch bindings. The deployed DLL passes 252 real retail
formatter cases and the 51-action native input test. The simulator confirms
startup and hook installation; it did not reach a hint in the bounded capture,
so final hint layout and physical-headset readability remain unproven.
See [Quest hint notes](QUEST_HINTS_2026-09-28.md).

The latest performance pass increases source detail to 1536 × 1536 per eye,
adds world-edge antialiasing, removes repeated terrain-cache scans and reduces
frame-copy overhead. The final simulator window measured 58.05 fresh game FPS
(17.23 ms mean; 25.12 ms p95). See [the frame-cost report](PERFORMANCE_2026-09-27.md)
for the stage breakdown and remaining physical-headset measurement.
Native hints and input now share the action-binding registry; a remapping fixture
checks both together. Natural solo strokes latch the next authored note while
preserving the retail timing judgment.

The opening rig room is now a complete metal courtyard: textured basalt,
a custom skull-and-guitar floor inlay, iron stage architecture, amp stacks,
braziers, banners, railings and distant rock formations. Its generic heading,
instruction cards and weapon captions are removed. The retail menu supplies
its own artwork and typography; a brass play switch retains touch/aim/A access.
Room geometry and mipmapped materials share one fixed recentered origin and
the current stereo view. Stick walking is bounded inside the furnished terrace.
The rebuilt host passes its self-test and the imported-rig visual/interaction
test. See [room notes](ROADIE_ROOM_2026-09-27.md) for the assets and captures.

The latest follow-up rebases quad-tree terrain's baked texture projections from
the native collection camera to each VR eye. This addresses selective terrain
texture sliding after the user reported substantial improvement in the headset.
The deployed fix passes terrain/shadow tests and executes on live native terrain
packets without missing-origin reports. A 13.375-second sparse stereo sequence
checks the simulator path; the specific outdoor plain still needs a headset retry.

The preceding follow-up corrects the actual retail shadow shader's screen-ray
packing and negative-depth convention, samples a complete tracking frame just
before stereo rendering, and extends native mesh detail and culling distances.
The earlier matrix-only shadow checks missed the shader contract error.
Both-eye simulator captures show the corrected cast shadows through head-pose
changes and visible actors at roughly 34 m. The user's latest physical report
describes the build as substantially better, with the residual ground texture
motion addressed above and minor face fragments visible when looking down.

Polish adds stationary cutscene cinema, stable root-anchored eyes, right-hand
wheel steering with stick takeover, contextual VR prompts, controller aiming,
RTS command conflict fixes and headset-based binocular culling bounds. The
updated build is deployed; use Play VR.cmd for the physical headset.
See [polish notes](POLISH_ACCEPTANCE_2026-09-27.md) and
[controls](VR_CONTROLS.md). Full campaign and live RTS completion are not claimed.

## First-person foundation

Target is the owned x86 Buddha/D3D9 executable, SHA-256
`872dc676e8fd77ad3351dd9dfcc99e89353aae0ed9857272a65fd47f298fb0b1`.
The proxy uses BLVR's x64 OpenXR host and versioned pose/frame mappings.

Implemented and covered by focused tests:

- Native controller input before the game's edge/history calculation; deliberate
  pause only, stale-input rejection, and background desktop input.
- Independent XR heading, full headset quaternion, 45-degree latched snap turns
  about the current HMD, and head-relative movement.
- Player-owned skeleton binding and render-only head/hair suppression, with
  wrist and weapon transforms preserved and exact restoration after both eyes.
- Anatomical eye-height pivot that excludes combat animation bob and body tilt.
- Per-eye shadow receiver matrices and asymmetric projection reconstruction.

The physical check subsequently exposed grip axes, oversized rig and a LOCAL
floor-origin bug. Calibration v2 now keeps a fixed anatomical hand frame across
weapon selection, aligns the held tube to OpenXR grip -Z, uses 21 cm hands with
separate adult arm reach, and anchors the opening floor below the headset.
Opening/gameplay sizing no longer changes. These fixes have simulator and
render tests; their physical feel has not yet been accepted.

Tracked Eddie is now available in the opening 3D menu and the native gameplay
render packets. The rig uses the owned 212-bone skeleton, native meshes and
retail-evaluated animation clips, with connected arm IK, corrected palm axes,
forearm axial twist and fitted weapon grips. X/Y select axe/guitar; right trigger
or a physical swing/strum feeds the existing retail action. Optional support-hand
grip works for both weapons. Walk up to the opening panel and touch Confirm,
or use A. Left stick walks; right stick snap-turns (vertical selects menu items).

Live simulator captures show the corrected rig in both gameplay eyes. The native
renderer receives the rig from the exact pose frame/epoch used for that eye pair;
all render edits restore after the pair. Combat uses the game's existing attack
animations, hit windows and damage, not arbitrary controller-swept collision.
Physical headset comfort and full combat progression are not yet user-verified.

The gameplay bot follows Ophelia, returns to the museum tablature, touches the
headstock solo wheel and completes Relic Raiser with physical right-hand
strokes. Left-hand X/Y frets choose the note while squeezing the grip. Valid
204/212-bone native palettes stay tracked after the solo. The car transition
retains Eddie's rig through the exact CoMount rider and aligns the view with
the vehicle. Driving keeps the native right-hand wheel grip while right grip is held;
release frees the right arm. The left arm stays tracked. Weapons temporarily stow so the free hand
opens, and wrist displays join the actual rendered hands through source-frame
history. A 16.61-second, ten-sample both-eye wave sequence and a separate steering
capture confirm the requested hand behavior in the simulator. Screen backs are
culled so mirrored text does not show through the raised hand.
Throttle, reverse and steering were exercised in the simulator.

Gameplay menus are stereo quads attached to forearms/palm or the guitar.
The desktop mirrors the completed first-person left eye. A 12.64-second
pause/head-motion sequence had ten consistent first-person desktop captures
and no blank UI in 141 coherent samples. The solo note track has a fixed
readable crop excluding the native full-screen vignette.

These are simulator results. Physical headset feel, long-session stability,
all cinematics and the full campaign remain unverified.

Details and evidence status:
[player view repair](PLAYER_VIEW_REPAIR_2026-09-26.md),
[native input repair](PAUSE_FIX_2026-09-26.md).
See also [tracked Eddie implementation](TRACKED_EDDIE_2026-09-26.md).
Use [VR controls](VR_CONTROLS.md) for the complete controller mapping.
