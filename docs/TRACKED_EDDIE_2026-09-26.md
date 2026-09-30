# Tracked Eddie — September 26, 2026

## Current behavior

The first frame can present actual Eddie geometry around the headset, a floor,
and the game's live title/menu on a large 3D panel. The left stick moves toward
the panel. Touch Confirm with a free index finger, aim and trigger with empty
hands, or press A. The panel stop distance is 0.40 m, so the button is reachable.
Right-stick horizontal movement snap-turns once until the stick returns neutral;
vertical movement selects native menu items. X equips/stows the axe, Y the guitar;
pressing the selected weapon's button again stows it. Left stick click is the
command modifier. Right trigger uses the selected weapon. See
[the full control mapping](VR_CONTROLS.md).

The right hand owns the axe. The left hand owns the guitar at the middle of the
neck. The other hand can attach by squeezing near the haft/body and releases on
letting go or exceeding reach. Physical axe motion and right-hand string crossing
produce short retail attack requests. Head translation and room navigation do
not themselves create swings. The engine still owns attack eligibility, damage,
hit windows and progression. This is not yet collision driven by an arbitrary
controller sweep or individual string/note simulation.

The right index pad is swept across the local string plane. Both stroke
directions work; an idle right hand rejects movement caused only by the left
hand. During solos, left grip enables fretting: X selects note 2, Y selects
note 3, neither selects note 1, and a right-hand stroke plays the selected note.
Without left grip the A/X/Y alternative remains available. Radial selection
suppresses strumming; mode transitions discard old action pulses.

## Assets and animation

`scripts/import_eddie_assets.py` reads owned PC pack-v5 resources into a private
ignored cache. `scripts/build_eddie_rig.py` decodes the real 212-bone skeleton,
27 mesh subsets and eight diffuse textures. Header, skin ranges, weights, indices
and stream ends are validated. Head/hair triangles and wing materials are removed
from the opening mesh. No retail files are redistributed or checked in.

`scripts/capture_eddie_actions.py --pid <diagnostic-game-pid>` caches the retail
engine's evaluated LOCAL Qs poses and inventory-owned weapon transforms for idle,
axe, guitar and walk. It validates the explicit simulator receipt, exact actor
and component owners, and resumes the game in a finally block after every
coherent sample. The private provenance file records executable hash, bone names,
source session and each clip hash. No synthetic swing or strum trajectory is used.

The opening/game rig stabilizes the torso under the HMD and retargets authored
arm movement. Full-body attack crouch/roll cannot push Eddie's back into the eye.
For the guitar the retail right-palm trajectory is expressed relative to the
currently held guitar, then connected to the shoulder with IK. Physical action
previews keep the tracked hands authoritative. Source walk leg poses supply the
measured cycle from 0.3454375 to 1.4817519 seconds.

## Frames and grip corrections

All host matrices use row vectors, meters, proper rotations and local*parent FK.
The anatomical palm frame comes from named Wrist/Middle1/Index1/Pinky1 joints.
Palm +Y is the hand's back; left +X/right -X is thumbward, fingers point -Z.
Calibration v2 uses the OpenXR grip contract: -Z runs along a held tube toward
the index finger; +X is outward from the left palm and inward to the right.
The previous correction incorrectly treated identity grip as upright and changed
the palm rotation when a weapon was equipped. The anatomical calibration is now
constant. Forearm interpolation distributes only axial twist.

Weapon local +Y is the haft/neck and follows grip -Z. Weapon +Z is the blade /
string face and follows grip -Y. The socket owns that correction; selecting a
weapon cannot rotate the hand. The fretting contact is at local neck y=0.28.
Finger poses start from native animation, then bounded CCD fits distal pads to
the actual fretboard/back-of-neck and axe-cylinder surfaces. A secondary axe grip
fits its fingers to the same shaft after the two-hand solve.

The owned hand measured 0.2907 m wrist-to-tip and guitar 1.8413 m. Presentation
now uses 0.21 m hands, uniform 0.72243 hand/weapon scale (guitar about 1.33 m),
and a separate 1.72 m eye height / 0.895168 body scale so adult arm reach is
retained. These dimensions are shared between the opening and gameplay. LOCAL
headset Y is not a floor height: the opening floor is latched at headY-1.72 and
the menu panel is placed relative to that floor. Gameplay eyes use the matching
body height above the native root; vehicle/cinematic eyes remain authored.

## Native render integration

`blvr_rig_bridge.h` defines a pointer-free 64-entry history. Each entry contains
the producer epoch, frame ID, predicted display time, bone-name signature,
selected weapon, and bind/weapon-to-center-HMD transforms. The rig slot commits
before its pose publication. The x86 reader rejects any mismatch; it never uses
a newer hand pose with an older world image.

`PlayerViewRig_Begin` retains its exact player/skeleton/render-owner checks.
`PlayerViewRig_ApplyTracked` composes HMD-local rig matrices with the same centered
game camera used for the eye pair, then converts to the existing body packet's
local space. The existing shader skin palette draws the native meshes and native
lighting. It keeps retail wing bones, hides the head subtree, and edits only
inventory-owned weapon packets. Skin and weapon matrices restore byte-for-byte
after both eyes; no simulation, NPC or shared asset transforms are changed.

The native renderer can publish a valid 204-matrix prefix of the 212-bone
skeleton after a solo. It now remains tracked with bounded writes and exact
restoration. On mounting, the camera's vehicle entity is resolved through its
native CoMount rider handle to Player_A. The native RIGHT-hand steering animation,
including finger curls and stowed weapon sockets, remains intact while driving.
The left arm stays freely tracked for waving out the window. The host temporarily
stows its selected weapon while mounted, clears gesture playback, and restores
the selection on dismount. Grip release opens the free hand. A separate
native-hand history publishes both rendered wrists/fingertips; wrist UI joins
their exact source frame, pose frame, epoch and display time. Publisher and reader
retain their named mapping handles so another process can open the shared object.
Wrist/palm panels render only their readable front face. Boarding faces the vehicle;
actual vehicle yaw preserves the user's relative snap turn, independently of
chase-camera auto-centering.

Observed on this executable: Entity+0x64 is CoInventory (owner+0x10), whose axe
and guitar handles are at +0x20/+0x38. The global entity table is exe+0xb79d8c,
12 bytes per entry. Each weapon's Entity+0x38 owns CoRenderMesh. Its scene packet
is DynamicMeshSnapshot, vtable exe+0xab18b4, owner +0xe0, world matrix +0x70.
The actor's world scale is 1; stowed weapon scale is 0.8 and wielded scale is 1.

## Validation and limitations

Native input, stable pivot, navigation, shadow, quaternion/projection and render
restoration tests pass. Host self-test passes with no running game (its mailbox
fixture requires an unused descriptor). `--rig-visual-test` renders 25 views and
exercises selection, snap latching, idle no-attack, axe/guitar support attach and
release, physical swing/strum pulses, aimed Confirm and actual finger contact.

Final-eye simulator evidence is under
`artifacts/stereo-diagnostic-20260926-092840`: opening weapon captures and
`gameplay-guitar-live-00.png`, `gameplay-axe-live-00.png`, and
`native-strum-mid-action.png`. The last is a native-trigger take, not a physical
controller-sweep damage test. Live logs confirm matching 212-bone rig publication
and both exact owned weapon packets. Earlier `gameplay-guitar-tracked-*` captures
were taken during loading and are not gameplay evidence.

At 12:33:53 and 12:42:40 the simulator played Relic Raiser with four physical
right-hand strokes and left-hand frets. The native game reported success=1,
count=4 and raised the museum parts. Evidence is in
`artifacts/stereo-diagnostic-20260926-122845/bot/trace.jsonl` and `physical-*`
captures. Both-eye grip views are `guitar-corrected-grip.png` and
`guitar-rotate-*.png`. Pause UI and the desktop mirror remain stable through a
12.64-second, ten-sample head-motion sequence; 141 coherent UI samples contained
no blank publication. Desktop captures exclude the host's attached UI.

The later physical headset check **failed** grip rotation, oversized hands and
weapons, and the opening floor. Earlier simulator grip captures did not establish
physical alignment. Calibration v2 addresses those causes; new final-eye evidence
includes `stereo-diagnostic-20260926-130550/bot/local-zero-*` and
`artifacts/grip-repair-20260926/calibrated-rig-contact.png` (offline WARP render,
not a compositor capture). The 0 m LOCAL-origin fixture remains above the floor
after walking to Continue. New physical comfort/alignment still needs feedback.

Driving evidence is in `artifacts/stereo-diagnostic-20260926-133632/bot`:
`free-left-wave-00.png` through `09.png` are ten final-compositor stereo samples
over 16.61 seconds (13:42:02–13:42:18); their contact sheet was reviewed in full.
Both connected arms remain visible: the left waves, the right grips the wheel.
`left-wave-right-steering-final.png` additionally shows the right hand following
the rotated wheel while the left remains raised. The game was stationary; this
does not establish a full driving mission or physical tracking comfort. The
mounted rig test now separately checks that the left wrist changes while the
native right shoulder/wrist remain untouched. The rig visual fixture verifies
that mounting clears a held guitar/attack and dismounting restores it.

Headset comfort, every combat combo/upgrade, death and
all cinematics have not been accepted by a physical user. Actions still use
retail hit volumes. New weapon meshes beyond axe/guitar need their own bindings.

## Build

`scripts/build.ps1` now builds the x86 proxy and x64 host with Visual Studio 2022.
The Visual Studio 2026 compiler in the earlier host build directory began throwing
an internal C++ exception during code generation; the v143 host build succeeds.
Use `build/blvr-host-v143`, not the older `build/blvr-host-x64`, for this build.
Import/build/capture the private owned cache before enabling the opening rig;
initialization falls back to native presentation if the cache is absent.
