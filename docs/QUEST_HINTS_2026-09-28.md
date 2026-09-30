# Quest controls in native hints

Floating gameplay messages still displayed desktop/controller glyphs even
though the existing text hooks were installed. Their Flash translator resolves
localized line codes inline, bypassing the hooked text setters and localization
function. The patch also intercepts the shared native alias formatter before
it turns input aliases into glyphs. Native string assignment retains the game's
buffer ownership, and the original formatter handles remaining authored markup.

All replacement labels come from the input binding table. Earthshaker becomes
`X + Y together`; menu confirmation becomes `A`; block becomes `B`. Weapon
sequences retain weapon order, group repeated hits, and identify the selection
button. Inline command hints include the full left-grip/left-stick-click chord.
Mounted secondary-fire instructions use the vehicle binding. Unknown and
non-control aliases keep the original retail behavior.

The release DLL is deployed to the installed game. Restart with `Play VR.cmd`
to load it. Input bindings themselves are unchanged by this patch.

## Validation

- Built the x86 release DLL and native formatter test.
- Executed the owned retail UTF-16 formatter with the hook: 252 cases passed,
  including local game strings, Unicode, unchanged text, command chords and
  weapon combinations. Temporary native allocations balanced in every case.
- Semantic assertions reject the former trigger-only combo, repeated equipment
  toggles, menu-to-axe translation and wrong vehicle secondary trigger.
- Native input regression passed all 51 actions, analog driving, combinations,
  radial selection, press/release history, scripted locks and stale-input loss.
- Fresh simulator log confirms all six prompt hooks enabled. The final deployed
  DLL hash matches the build. Test-created game/host processes were stopped and
  save snapshots compared; no save file needed restoration.

Final-eye captures showed startup/cinema and pause UI, without an active control
hint. They do **not** establish hint wrapping, sustained hint visibility or
physical Quest readability. Those visual checks remain unproven; no campaign-wide
coverage or physical-headset acceptance is claimed.

Local evidence is under `artifacts/quest-hints-20260928`, including native test
results, final build hashes, the bounded probe and its actual captures/logs.
The native behavioral dossier is under
`D:\Dev\Tools\Ghidrust\workspace\evidence\brutal_legend\quest_hints`.
