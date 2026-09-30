# Controls and graphics investigation — September 29, 2026

This is the historical investigation before runtime remapping and live native
animation retargeting. The user subsequently authorized publication. See the
[preview release notes](RELEASE_0.1.0-preview.1.md) for the current implementation
and verification. The recorded action clips described below are superseded.
The user's headset report remains the acceptance reference: lights shift with
head motion, some scripted mountain views lose geometry, and button attacks
do not produce coherent first-person motion.

## Changes in this build

Earthshaker now uses **left grip + right grip**, above 0.65 on both grips.
Releasing and squeezing again creates another native press edge. Holding the
chord does not repeatedly press the attacks or change the selected weapon.
Solos, driving, pause and the stage-command chord suppress it. The input
evaluator and native hint formatter share the same chord definition.

RTS right-trigger Ascend no longer also sends RadialAccept (solo confirmation).
The native regression fixture failed with that leak and passes with command
context applied to both acceptance alternatives. Ordinary trigger acceptance
and the four stage orders remain reachable.

Right-trigger attacks now retarget the existing owned axe/guitar arm clips.
Another press during recovery restarts the visible action. The torso and eye
anchor remain stable; physical swings and strums keep tracked hands. A held
support hand follows its weapon during authored motion. This restores visible
button responses, but cached clips do not establish exact correspondence with
every live retail attack, eligibility decision, combo or emitted effect.

Native particle snapshots bake an emitter translation relative to the scene
collection camera. Their preparation now derives that translation from the
stored absolute emitter position and the current rendering eye, then restores
the snapshot after the native helper copies its constants. This fixes a
specific head-dependent drift path without moving world-space effects to the
viewer or changing simulation, lifetime or damage.

Scene collection now reads the player-owned live eye hierarchy for mounted
views. Stereo rendering already used seated eye joints from its render
snapshot. Both paths use standing metric height on foot and seated anatomical
eyes while mounted. This removes the standing-versus-seated policy mismatch;
the exact opening mountain ride remains unproven.

## Validation

- The installed executable's actual input updater passes all 51 actions, both-
  grip edges/releases and context suppression. Focused RTS checks cover all
  four order edges, held-direction suppression, build, flight, altitude and
  release of the command chord before menu confirmation.
- The final host self-test passes shaders, frame protocol, exact pose cache,
  stereo mailbox and menu routing without touching the OpenXR runtime.
- The real retail Flash formatter passes seven fixtures, including six
  remaps. Existing authored solo glyph frames use A/X/Y; those frames were
  not replaced with invented Flash frames.
- Particle shader-equation fixtures pass both eyes, head translation and a
  deliberately stale-camera negative fixture.
- Rig tests pass collection/render standing and seated anchors, malformed
  hierarchy and pose rejection, native ownership, unchanged skin during
  collection and exact restoration after rendering.
- The owned rig visual/interaction test passes button motion, repeated presses,
  return to tracking and weapon/support contact.
- A fresh simulator run executes the particle hook and captures 28.233 seconds
  of final composition: 847 output frames, 846 distinct compositor frames.
  Continuous footage is the left eye; 23 separate screenshots contain both
  eyes. It includes seated head yaw/pitch and 12 cm lateral lean, dismount,
  repeated axe/guitar attacks and three Earthshakers. World geometry remains
  visible in the tested poses. Native axe trails, guitar lightning and
  Earthshaker effects are present. Audio recording failed, so no audio claim
  is made.

Private receipts and captures are under
`artifacts/graphics-controls-20260929/live1`. A second session verifies that
the native solo wheel opens and its remapped direction text is present. The
user clarified that the guitar's calibration was fine: the awkward holding
pose belonged to the simulator operator. No guitar size, grip or direction
layout change was made on that basis. Diagnostic processes are stopped and
the user's save files are preserved.

The live captures precede the final RTS-only input correction. Their DLL hash
is recorded in each session receipt; they validate the same graphics and rig
code, but do not count as a live test of the final RTS fix.

The final installed x86 DLL has SHA-256
`f753a5c739f2e78561aed19322006f1796976f85a12cb69494dd25070756eb87`.
Its installed copy matches the built DLL. The final native input and prompt
tests pass after the RTS correction, the x64 host self-test passes, and all
nine backed-up top-level save/configuration files remain unchanged.

## Remaining acceptance

| Claim | Result |
| --- | --- |
| Two-grip Earthshaker input and visible native effect | Native and simulator checks pass; headset gesture unproven |
| Visible button attack response | Rig and simulator checks pass |
| Exact first-person weapon/attack-effect correspondence | Unproven; cached attack retargeting is not the live animation state |
| Camera-relative particle origin correction | Native hook executes; shader equation passes; physical drift acceptance pending |
| Tested Deuce seated head motion retains geometry | Simulator checks pass |
| Opening mountain scripted ride at unrestricted head poses | Unproven |
| All lighting/shader paths and all particle types | Unproven; do not generalize the corrected path |
| RTS unit selection, targeting, build, flight and battle UI | Native input checks pass; live stage battle unproven |
| Physical headset graphics readiness | Unproven |

RTS controls: hold **left grip + left stick click**. Right-stick up charges,
down defends, right moves and left follows. A opens build, X opens the map,
Y toggles flight, left/right triggers descend/ascend and right grip boosts.
Release the chord for ordinary menu navigation and A/B confirmation/cancel.
The retail game determines which contextual actions are available.
