# 0.1.0-preview.1

The first public Nikami Brütal Legend VR preview includes corrected Touch
controls, live remapping, and runtime native animation retargeting.

## Changes

- Earthshaker defaults to both grips, with release required before repetition.
  It preserves weapon selection and is suppressed during menus, driving and commands.
- All 51 native actions plus equipment, chord inputs, stick selection, wheel
  grab, support grips, solo fretting, alternate confirmation and recenter are
  editable through Remap Controls.cmd or controls.ini.
- Valid saved layouts reload in both processes within 250 ms. Invalid edits
  retain the previous layout. A rig solved with a different layout is rejected.
- Native hint formatting follows the input registry. A guitar-mounted live
  legend explains the original timed-solo A/X/Y glyphs after remapping.
- First-person button attacks use the game's current render-owned pose during
  the same stereo pair. Walking legs also use the live pose. Physical swings
  and strokes keep the tracked hands; the eye anchor remains stable.
- Wrist and guitar UI use the rendered hand/weapon transforms joined by source
  frame, pose frame, display time and session epoch, including during attacks.
- The host no longer requires recorded idle/walk/axe/guitar action files.
  Setup imports the owned model, skeleton and textures without launching the game.
- Particle camera-relative preparation rebases to each eye. Mounted scene
  collection and stereo rendering use matching seated eye anchors.
- Command-mode right trigger sends Ascend without also confirming a solo.
- Portable launch/setup, executable checks, DLL backups and hash-aware uninstall.

## Validation

Native executable fixtures cover all 51 action IDs, command mappings,
analog throttle/brake, button edges/releases, gameplay locks, tracking loss,
hint formatting, control reload, and invalid-file rejection.
Synthetic native rig fixtures change the source pose between consecutive
render frames and verify live arm retargeting, stable eye anchors, layout
mismatch rejection, shorter palettes, and exact restoration of original skin.
The x64 host shader/protocol and owned-model grip/interaction tests pass.
Fresh owned-model setup completes without any animation files.

The freshly extracted runtime ZIP passes its file checksums, standalone
first-run setup, Windows PowerShell 5 launch preflight, host self-test and
owned-rig visual/interaction checks. Setup produces no action recordings.
Installer fixtures verify repeat installation, original DLL restoration and
preservation of files changed after installation.

The packaged binaries also completed a bounded simulator final-compositor
run: 33.267 seconds, 998 output frames, 995 distinct compositor frames and
25 both-eye screenshots. The reviewed sequence includes seated yaw/pitch and
12 cm lateral lean, dismount, repeated axe/guitar button attacks, native effects,
three both-grip Earthshakers and the guitar-mounted solo wheel. World geometry
remains visible in these tested poses. The rendered-UI source join executes;
stale frame/pose/epoch/display-time and missing-hand fixtures reject the join.
All nine backed-up save/configuration files remain unchanged.

Hook SHA-256: `e5d7f32ffa60a6aef68996477ec0c7a3fe6eabe0d06acaf08510dd4231b201fe`.
Host SHA-256: `d604633c791309fde12d59dd943d10fd385e36c574f219904a1514df279418b7`.
The capture is private retail-game evidence. Continuous footage covers the
left eye; the separate screenshots cover both eyes. Audio capture failed.
This evidence does not establish every shader, emitter or physical headset
behavior. Timed-solo notes were not reached in the bounded run.

## Known limitations

- Physical headset comfort, final readability and long sessions need player testing.
- The exact scripted opening mountain ride has not been reproduced and accepted.
- Exact native attack-particle attachment to tracked weapons is not certified.
  The renderer correction fixes the observed camera-relative preparation path;
  it is not evidence that every shader, light or emitter is correct.
- RTS/stage-command input fixtures pass. A live stage battle has not been tested.
- The whole campaign, boss sequences, other builds and other controller profiles
  are not validated. Unsupported executable hashes are refused before installation.
- Native timed note glyphs remain the game's A/X/Y artwork; use the live legend
  for your current buttons or physical strokes.
- The timed-solo legend is covered by host checks; its final in-headset
  readability still needs a live timed-solo check.
- Existing native messages may retain text until the game refreshes them.
- The mod uses a separate x64 host and a locally converted owned model. Keep
  the extracted release folder and its private imported data.
