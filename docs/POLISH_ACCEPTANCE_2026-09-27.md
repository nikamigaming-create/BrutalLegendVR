# September 27 VR polish

The user requested a playable polish pass and then prioritized trying it in the
headset. This file records the work; it is not an approval or release gate.

Follow-up for selective ground-texture motion (10:23):
- The user found the updated headset rendering substantially better, but some
  flat terrain textures still moved with the head while columns remained stable.
- Retail quad-tree terrain builds four diffuse texture-projection matrices from
  its collection camera. The terrain vertices subsequently use the current eye
  origin, leaving those baked matrices in the old chase-camera space. BLVR now
  records the exact collection origin, carries it through native packet copies,
  and rebases only those four matrices for each eye. The original packet is
  restored immediately after native constant upload. Blend and front/side
  projections already use the current renderer origin and are left intact.
- Release build and terrain/shadow regression tests pass. The terrain test
  covers both eyes, translation and height changes at distant world coordinates;
  its stale-origin negative fixture fails the same invariant.
- Live native terrain collection and eye preparation are confirmed, including
  different source/eye origins, with no missing-origin reports. Nineteen final
  compositor stereo samples over 13.375 real seconds exercise yaw, pitch,
  25 cm lateral lean, 15 cm forward/back motion and 10 cm height changes.
  Both eyes retain the world throughout those samples. These are sparse samples,
  not a continuous video; the arena scene does not establish a visual pass for
  the exact outdoor plain reported by the user. Its physical retest is pending.

Terrain build: `a6c09c81f2bbf58aaa07fe817873d746fe75dba69deb7edf67e0ad417593deee`.
Motion samples and timestamps:
`artifacts/stereo-diagnostic-20260927-101838/bot/terrain-motion*`.

Follow-up after the physical headset report (10:02):
- The earlier shadow test was insufficient: it repeated the implementation's
  incorrect assumption about the retail shader's reconstruction constants.
  Disassembling the actual pixel shaders established that `g_vCameraUnitScreen`
  is `{offset.xy, scale.xy}` and reconstructs with negative view Z. Correcting
  both packing and sign fixes the per-eye shadow/lighting lookup on the floor.
  The regression test now uses that observed shader contract and rejects the
  previous implementation explicitly.
- The complete host tracking snapshot is now read immediately before the stereo
  render, removing the extra frame of positional pose age from the previous
  Present. Both eyes, shadow reconstruction and the rig share that snapshot.
- Native main-scene detail culling is raised from 1 to 2 and mesh LOD scale from
  1.5 to 3 before visibility collection. Native distance selection and authored
  MaxVisibleLOD remain in use; this extends their ranges without forcing LOD 0.
- Live packets confirmed both scales. Final compositor captures cover yaw,
  pitch, 25 cm lean and return, with matching cast-shadow placement in the two
  eyes. Druid actors are visible at approximately 34 m. Ophelia's specific
  encounter and physical headset motion still require the user's retry.

Follow-up build: `dbebf435879a533529d8d4f69b613d953eb3207efe8da2ff11739c427b59c5d2`.
Captures: `artifacts/stereo-diagnostic-20260927-100104/bot/final-*.png`.
Release build and shadow, tracked-basis and navigation tests passed. These
checks do not establish physical headset comfort or full-campaign acceptance.

Implemented:
- Stationary cinema screen for authored cutscenes, including a return to stereo.
- Stable on-foot root pivot without animated eye-offset orbit, bob or body roll.
- Controller-directed aim, target plus attack, X/Y release selection and X+Y combo.
- Right-grip wheel steering, left-stick takeover, native seated grip while held,
  and freely tracked right arm after release.
- Verified mounted ownership, including boss cameras that target the rider.
- Binocular headset visibility bounds with eight degrees of margin; the actual
  eye projection remains the OpenXR field of view. Main render snapshots are
  identified by interpolation ownership as well as native UI publication.
- Generated metal tutorial frame with crisp live Touch text and native prompt
  aliases. The card explains the stage-command chord when relevant.
- All 51 native input actions reachable, including stage build, flight, altitude,
  beacon, charge, defend, follow, move, co-op and playlist actions. Diagonal
  command input sends one order.
- Immediate desktop mirror presentation and a 90 FPS upper bound for VR launches.

The simulator drove the vehicle using the right hand and confirmed stick
priority. Native input, rig, shadow and host tests pass. Audio reaches the active,
unmuted Oculus virtual headphones with nonzero game-session peaks.

The final simulator startup measured 57.7 complete stereo pairs per second,
up from approximately 30 before fixing render-snapshot ownership. Its forward
driving capture shows the vehicle and arms in both eyes. Expanded culling changes
only visibility volumes, preserving native camera-distance fades and render
matrices. Capture: `artifacts/stereo-diagnostic-20260927-092257/bot/final-forward.png`.
The simulator processes were stopped for the user's headset session.

The bot has opening combat, navigation, solo, driving and command strategies.
It has not completed the campaign or a live RTS stage battle. Physical headset
comfort, all mission-specific sequences and headset listening remain unverified.
