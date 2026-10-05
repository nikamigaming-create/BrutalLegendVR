# 0.1.0-preview.3

Experimental testing release, October 4, 2026:
[Brütal Legend VR 0.1.0-preview.3](https://github.com/nikamigaming-create/BrutalLegendVR/releases/tag/v0.1.0-preview.3).
Windows 10/11 x64, the tested Steam executable, PC OpenXR and Touch controllers
are the current installation and input profile. A legally owned game is required.

## Installation and launcher

- **Windows Setup.exe installer:** per-user installation, original generated
  guitar workshop artwork, Start menu shortcuts and an integrated Play, setup,
  settings and controls application. The portable ZIP contains the same runtime.
  Both include their own Python dependencies; system Python is unnecessary.
- **Saved VR settings:** runtime selection, render resolution, frame cap, edge
  antialiasing, diagnostic logging and guitar height, distance, neck angle,
  upward string-face angle and volume. Settings are validated and saved
  atomically; display and guitar changes apply on the next launch.
- **Startup recovery:** preflight and child-process deadlines, rejection of
  duplicate launches, fresh startup-log reads and cleanup of children started
  by a failed launch. Invalid owned model caches are detected before gameplay.
- **Installation recovery:** hook deployment stages files and rolls back a
  failed update. Upgrades preserve settings, controls, imported models and
  guitar placement. Uninstall refuses to remove the app when changed game
  hooks prevent safe restoration of the original files.

Install the game and launch it once, run Setup.exe, select **BrutalLegend.exe**,
then choose **Prepare / repair game**. Connect your PC OpenXR headset, wake
both Touch controllers, close the desktop game and choose **Play VR**.
Portable users should extract the entire ZIP and start with **Setup VR.cmd**.
Owned models and textures are imported locally; the runtime packages contain
no retail game assets or saves. SHA-256 files accompany both downloads.

## Touch-controller guitar

The selected guitar floats at estimated chest height, with its neck to the
left and its strings facing upward. The chest estimate comes from headset
position and yaw; no body tracker is required.

1. Hold a lighter left grip near the neck to attach the fretting hand and slide
   it along the frets.
2. Squeeze firmly while attached to move and rotate the entire guitar. Release
   to leave it in that body position. Placement survives launches and upgrades;
   VR settings can reset it.
3. While attached, left trigger curls the index finger, X the middle finger and
   Y the ring finger. These inputs retain the equipped guitar.
4. Stroke the strings with the right hand to play original synthesized notes.
   Finger changes can also sound after a recent stroke.

Touch inputs estimate finger poses. Independent bare-finger tracking is not
part of this controller profile. During native solos, the game's timing stays
active: attached fingers select notes 1/2/3, and releasing the neck restores
authored-note assistance. Timed solo UI after arbitrary guitar placement still
requires acceptance.

## Validation and footage

- **25 launcher lifecycle checks passed**, using private dependency stubs and
  real child processes: duplicate launch rejection, deadlines, stale/locked
  logs, failed preflight, readiness failure and early game exit.
- **16 real installer lifecycle checks passed**, including cold owned-model
  import with bundled Python, upgrade preservation, blocked unsafe uninstall
  and restoration of original hooks. Writes used isolated game fixtures.
- **16 transactional deployment checks, 25 Python tests and 15 rig bridge
  checks passed.** Native input, prompt, controls, build-wheel ownership,
  player rig, shadow/terrain anchoring, binocular culling, telemetry budgets
  and host self/visual checks passed.
- **Guitar recording:** 11.5 seconds, 345 distinct final-compositor frames in
  both eyes. Joined pose/rig checks cover frets 1/5/9, finger selections 1/2/3,
  translation and rotation with firm grip, unchanged placement on release and
  following the estimated chest. The audio uses aligned render-endpoint
  loopback, includes other sound on that endpoint and has one startup
  discontinuity; no microphone was recorded.
- **RTS recording:** 66.133 seconds, 1,984 encoded frames and 1,983 distinct
  final-compositor frames in both eyes. The native Ironheade AI Practice path
  against Drowning Doom on The Amplified Cliffs completed a stage upgrade
  (native level 0 to 1) and recruited eight Headbangers in two squads. Native
  readbacks verify actor ownership and movement after Follow. The recording
  shows the held build wheel, submitted Defend/beacon/Move/Charge inputs,
  flight, ascent and tracked head movement. It does not establish a complete
  battle outcome or show every recruited unit in the camera view.

Both recordings passed freshness and real-time cadence guards and use host
SHA-256 `b660dc9a4cee91632df79e323778b498d71217fea5bb9c45627c5ed87fc7530e`
and hook SHA-256
`be5d8dce0da40d3e1478e54378c41bf1bd7391e770d80642447bf05abedca76f`.
The published MP4s are simulator evidence. Private logs, raw telemetry, saves
and imported retail data are excluded from both runtime packages and source.

## Remaining acceptance

Physical Quest/Touch play, prolonged tracking loss, comfort, all native
shader/effect families, arbitrary-placement solo UI, other factions, online
RTS, complete battle victory and the full campaign remain unverified.
The read-only GPU observer saw one native EVENT query on the sampled path;
it does not establish coverage of other occlusion paths. See
[preview 2 graphics and effect limitations](RELEASE_0.1.0-preview.2.md).
This release does not establish universal reliability or online multiplayer
support.
