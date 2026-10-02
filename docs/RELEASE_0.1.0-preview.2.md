# 0.1.0-preview.2

Experimental testing release, October 2, 2026:
[Nikami Brütal Legend VR 0.1.0-preview.2](https://github.com/nikamigaming-create/BrutalLegendVR/releases/tag/v0.1.0-preview.2).

This pass improves control clarity and how the first-person rig, attached UI
and scene collection respond to changing tracking. It retains runtime native
animation and locally imported owned model data; no motion recording is
required. It does not resolve every reported graphics issue.

## Changes

- **All 51 native actions + 20 VR controls remain remappable during play.**
  The editor adds search, readable controller names, action descriptions,
  current command/Earthshaker chords and clear pending/saved status. Switching
  rows or filtering preserves staged changes; Save controls applies them
  together after native validation. Broken setup settings no longer prevent
  opening the editor. Failed validation or a missing/hung validator leaves
  the active controls file unchanged and removes the temporary candidate.
- **Earthshaker still defaults to left grip + right grip together.** Equipment
  selection is retained; release before repeating. Solos, driving, pause and
  stage commands suppress the slam. The editor identifies unbound inputs or
  a remap that uses the same input for both halves of the chord.
- Movement, turning, solo selection, target switching and driving hints follow
  remapped stick roles. The tutorial-card footer refreshes on a layout change.
  Command-scoped menu directions no longer leak analog input without their
  chord. Both sticks use dominant-direction evaluation for digital bindings,
  so remapped diagonal RTS orders remain exclusive; analog navigation retains
  its full stick strength. Nonfinite analog values become neutral.
- A guarded native UI correction filters duplicate arrow-menu steps from
  matching mapped button/stick input. Opening carousel input and native
  gameplay history are preserved. Command-scoped UI direction bindings retain
  their configured input alongside the native stage-order aliases. Owned native
  fixtures cover full/partial axes, hold/repeat/release and live button/chord
  remaps. A fresh native AI Practice pause retake confirmed one right-stick press advances one menu item.
- Host pause, solo-menu and button-animation readers share the native digital
  scope rule. A command-bound pause/cancel no longer swallows ordinary button
  use, and command-bound or unbound attack/note buttons cannot manufacture a
  host animation without the corresponding native action. Native command mode
  still reserves combat/solo inputs for stage and secondary actions.
  Native vehicle analog preserves partial remapped trigger strength while
  honoring scope/unbound inputs. Physical-strum fallback selection follows
  scoped note bindings and retains the authored next note as its authority.
- Native front-end enum aliases now resolve through the VR action layout
  before keyboard/gamepad glyph formatting. Menu Select, Back and Watch
  Tutorial show their actual configured inputs. Watch Tutorial defaults to
  **X**, separately from Select **A**; the prior shared input could enter the
  tutorial when selecting a stage-battle mode.
  Opening-room horizontal turning remains on right stick; the native carousel
  follows left-stick horizontal. Opening menu stick selects vertically, while
  native UiLeft/UiRight remap horizontal choices separately.
- Native build-wheel feedback now distinguishes stage recruitment from solo
  UI. Hold command + A, select with the radial stick and confirm with right
  trigger; X cancels a queued unit. Left trigger requests research through
  the new remappable Build research alternate, while ordinary Use/interact
  remains on A. During the held wheel these inputs no longer fly, attack or
  issue orders. Confirmation hints use the actual inputs available in that
  context. Recruitment and stage-tier upgrades share the same confirmation
  input. Its shared **RECRUIT / UPGRADE** caption passed the RTS15 simulator
  retake in both eyes. Research retains its separate input.
  Inputs already required by the held build chord are excluded from
  recruitment, research and cancellation, preventing actions on wheel opening;
  conflicting hints show unbound. Native resources/abilities remain authoritative.
  Simulator recruitment has spawned four Headbangers; the latest native
  readback confirmed Stage 2 and Stage 3 progression (native levels 1 and 2).
  A Headbanger research purchase, with an authored cost of 200 fans, joined the exact research key
  and advanced the native purchased-upgrade count from 19 to 20. Its expected
  MaxHealth effect from 250 to 375 remains unproved. X cancels a queued unit
  with a refund. Complete battle acceptance remains pending.
- **Tracking loss uses the current native arm and equipment pose.** It preserves
  equipment selection, releases generated attack/confirmation/solo pulses and
  disables the affected UI pointer. Reacquisition does not manufacture a
  swing or strum. Invalid head tracking stops republishing an older rig.
  Version, layout and transform validation reject incompatible or malformed
  data before a partial render edit can be applied.
- **Gameplay UI requires matching rendered geometry for both eyes.** Wrist,
  palm and guitar panels are withheld together when an exact attachment is
  missing, instead of using a different tracked solve in one eye. Geometry
  from a native fallback arm can still anchor a panel, while its untracked
  fingertip cannot interact. The opening room retains its own tracked UI.
- Central native popups move 20 cm inward and 30 cm farther from their exact
  rendered palm plane. This preserves the full alpha crop and source aspect
  while framing the complete native pause row in both simulator eyes at a
  neutral pose. Build, solo and forearm surfaces retain their previous mounts.
  Labels remain small in the 640-pixel-per-eye review image. The native pause
  menu hides after 450 idle Flash frames and wakes on native button or stick
  input. A fresh 10.5-second, 315-frame stereo take retained the entire row in both eyes through head and palm movement. Physical readability remains unverified.
- Unreachable support-hand attachment restores the unconstrained hand and
  primary weapon. A collapsed or unavailable native guitar attachment no
  longer interrupts otherwise valid tracking and live leg retargeting.
- Scene collection now expands depth as well as angular bounds for its camera
  setback. This avoids discarding geometry visible to either eye near the
  collection far plane, including asymmetric/canted eyes and pitched or rolled
  head poses. The actual eye render projection is unchanged.
- CPU/GPU performance CSV and diagnostic telemetry logging are opt-in with
  **BLVR_PERF=1** and **BLVR_TELEMETRY=1** respectively (or **true**), so ordinary
  play does not accumulate those files. Each stops at 32 MiB; telemetry permits
  a smaller budget. The input diagnostic log shares the telemetry opt-in,
  resets per launch and stops at 32 MiB. Video recording requires explicit opt-in even in simulator
  sessions.

## Validation status

| Check | Candidate status |
| --- | --- |
| Controls editor | Six tests pass: all 71 rows/descriptions, actual hidden Tk edit/search events, native-compatible parser ordering, atomic save and stale settings recovery |
| Native input and prompt fixtures | Final paired rebuild passes all 51 native actions, control configuration, held-build press/hold/release, cancellation, tracking loss, live research remapping, required-chord conflict guards and analog/strum scope fixtures. The real retail formatter suite passes 25 cases with 23 remaps, including the observed recruitment/upgrade caption and lock-on dodge template |
| Native rig and collection fixtures | Current native fixtures pass, including ownership, effect lineage, live fallback, reacquisition, malformed/versioned data, exact restoration and binocular near/far coverage |
| Host protocol and owned-rig checks | Final paired host build, stopped host self-test and owned-rig visual fixtures pass, including both-eye UI rejection, unreachable support rollback and remapped pause, attack, solo and note-animation scope cases |
| Fresh runtime ZIP/setup | The bundled standalone Setup imports the owned model with no animation recordings. Publication gates check the runtime allowlist, PE architectures, ZIP integrity and every packaged file checksum |
| Native stage battle simulator evidence | Native menu A/B/X and right-trigger recruitment/X cancellation labels confirmed; local Ironheade AI Practice, tracked flight, four spawned Headbangers, Stage 2/3 progression and cancellation with refunds observed. A Headbanger research purchase with an authored cost of 200 fans changed the exact native purchased-upgrade map from 19 to 20 entries; the isolated debit and health effect remain unproved. Native Follow/Defend/Move/Charge timers matched the input sequence, and the same squad moved. The shared recruitment/upgrade caption was confirmed in both eyes. Complete battle and final readability acceptance remain pending |
| Walker descent final-eye check | Real same-frame SBS compositor retained visible world geometry through yaw ±35°, pitch ±30° and 15 cm forward lean in a bounded simulator take. Physical headset behavior remains unverified |
| Stationary-platform flame check | Flame visuals stayed attached through lean ±12 cm, yaw ±20° and pitch +15° in the simulator. Light pools, shadows, height dependence and physical headset behavior remain unverified |
| Native pause popup | New paired build/self-test and owned-rig fixtures pass. The complete pause row appears in both eyes at a neutral pose; labels are small at 640 pixels per eye. The fresh 315-frame take keeps the complete row in both eyes through head and palm movement; the earlier idle-logo take is not motion-menu acceptance evidence |
| Diagnostic disk bounds | Telemetry concurrency/oversized-row checks and the 32 MiB hard cap pass. Profiling files also have a 32 MiB cap; recording requires explicit opt-in, including simulator sessions |
| Current rendered UI contact and timed solo retake | Existing-map QA joined the native rendered guitar plane and fingertip to both submitted eyes. Fingertip selection entered native note mode; native slot 10 completed with its authored Y/X/A sequence and reported success. Physical solo strokes remain unproved in this retake |
| Physical headset acceptance | Unverified |
| Complete live RTS stage battle | Unverified |

The packaged runtime uses these SHA-256 identities:

| Binary | SHA-256 |
| --- | --- |
| OpenXR host | `3d6db8cc53ee32f87e3ac9518edf8bfd490438a6d3aa4ef4480e9de42b48ac64` |
| Native hook DLL | `72d3448708d64c860f51ec010eb3cd9b01e62b3f959fb266f523966c87a0d01c` |

The Walker descent and stationary-flame captures used the earlier host
`a41ea8c6` / hook `fff4f493` build. They are bounded evidence for those binaries;
the newer pause-popup build has not repeated those checks. The fresh pause
motion capture used the same host with hook `f61bdf71`; the packaged hook adds
periodic flushing of opt-in telemetry without changing those render/input paths.

Proof recording uses short clips with a 500 MiB total disk cap. The optional
simulator tap records the actual final compositor after all layers; SBS mode
retains both eyes from one source frame. Source dimensions, downscaling,
producer identity, elapsed/encoded duration and source hashes are recorded.
This improves evidence collection and does not establish physical acceptance.

The earlier [preview 1 validation](RELEASE_0.1.0-preview.1.md) remains historical
evidence for those binaries. Its simulator capture and performance figures
are not acceptance evidence for this candidate. Synthetic fixtures can verify
contracts and failures; they do not establish final headset appearance or
comfort.

## Known limitations

- Native attack emitters have not been accepted at tracked weapons. A bounded
  weapon-ribbon endpoint correction is opt-in and remains disabled by default
  while its live acceptance is pending. Exact particle/effect attachment remains unresolved. Prior
  camera-relative preparation fixes cover specific rendering paths; all
  lights, candles, materials and shaders are not certified.
- The Walker descent passed the bounded simulator head-motion check above.
  Wider poses, physical headset behavior and other rides remain unverified.
- RTS inputs are implemented, with first-person flight/commanding intended.
  Native AI Practice, tracked flight and held build-wheel selection have been
  observed in the simulator, with four native Headbangers spawned through recruitment,
  Stage 2/3 progression and cancellation refunds confirmed. A native Headbanger
  research purchase is confirmed; its expected health effect remains unproved.
  Build-wheel final readability remains under investigation. Order acceptance
  and squad movement do not yet prove Defend
  settle-and-hold, arrival at a Move beacon or Charge damage to a hostile.
  No complete live stage battle has been accepted. Evidence covers local
  Ironheade AI Practice; other faction rigs, online play and co-op are
  unverified. Late-game/campaign support remains experimental.
- Physical headset comfort, tracking feel, final UI readability and long
  sessions require player testing. Other executable builds and controller
  profiles are unverified.
- The complete native pause row stays framed in both simulator eyes during
  the fresh bounded head/palm motion take. Labels remain small at 640 pixels
  per eye; physical readability remains unverified. Head or palm motion alone
  does not reset the game's 450-frame idle timer.
- Native timed solo glyphs retain the game's A/X/Y artwork; the live legend
  names your remapped inputs. Its final in-headset readability still needs
  a live timed-solo check.
- Already formatted native messages can retain their previous control text
  until the game recreates them.
- Gameplay panels can disappear while exact rendered attachment data is
  unavailable. The host logs `native UI geometry missing`; it does not
  substitute an unmatched hand position.
- Update the hook and host together from one complete package. Keep the
  extracted folder and its private imported owned-model data.
