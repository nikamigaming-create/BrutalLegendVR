# BLVR Touch controls

Run **Remap Controls.cmd** to edit all **51 native actions + 20 VR controls**,
or edit **controls.ini**. Saved changes reload during play within 250 ms;
invalid edits retain the last working layout. Release held buttons after
saving. Chords, equipment, wheel/support grips, recenter and all four stick
roles can be reassigned. The tables below show defaults. Native gameplay
contexts and scripted locks still apply.

Command-scoped actions require your configured command chord. Use ordinary
bindings for combat and solos: command mode reserves those inputs for stage
commands and other secondary actions. Setting an action to **unbound** removes
its button input; physical weapon swings, strums and fingertip contact remain
separate interactions.

Search by action name or input to find a control, then select its row. The
editor explains the selected action and displays controller names such as
**X · left controller**. Choosing an input stages that row immediately;
switching rows or filtering keeps pending edits. **Save controls** validates
the complete layout and applies all staged changes together. Restore defaults
also stays pending until saved. The editor displays your current command and
Earthshaker chords, including if an Earthshaker input is unbound.

Timed solo glyphs keep the game's original A/X/Y artwork. A live legend on the
guitar identifies your configured inputs for those notes. Native text hints
use the same registry as input, including native menu enum aliases. Existing
messages may retain their previous
labels until the game recreates them. Movement, turning, solo selection,
target switching and driving hints follow remapped stick roles.

The guitar floats at estimated chest height with its strings facing outward and
neck to the left. A lighter left-grip squeeze near the neck attaches the
fretting hand and lets it slide. Squeeze firmly to move and rotate the whole
guitar, then release to leave it at that spot on your body. The placement is
saved for your next launch. Left trigger curls the index finger; X curls the
middle finger and Y curls the ring finger. Touch controllers animate these
fingers; this mode uses no bare-hand tracking. Release left grip to detach.
The right hand picks across the strings and holds the axe. X equips/stows the
axe and Y equips/stows the guitar when the neck is free. While fretting, X/Y
operate the fingers and keep the guitar equipped. Guitar height, distance,
neck angle, upward string-face angle and volume can be adjusted in VR settings.
Use **Reset guitar placement** to return to these settings. Saving a changed
height, distance or angle also resets the manually placed guitar.
While driving, hold right grip to grab the wheel, then turn your hand around
a wheel arc or rotate your wrist. Release to move the right arm freely. The
left arm remains tracked. The left hand opens when its grip is released; it never snaps to
the wheel. Weapons are stowed while mounted and the previous selection returns
on dismount. Left stick overrides hand steering immediately. Right trigger accelerates and left trigger
brakes/reverses.

| Action | Control |
| --- | --- |
| Walk | Left stick, relative to where you look |
| Turn | Right stick left/right, 45 degrees once per deflection |
| Interact / accept / enter or exit vehicle | A, when the game permits it |
| Native menu Select / Back / Watch Tutorial | A / B / X; tutorial is separate from selecting |
| Back / evade / block / vehicle handbrake | B, depending on context |
| Dismiss tutorial card | B |
| Use selected weapon | Right trigger, physical right-hand axe swing, or right-hand guitar stroke |
| Recruit / stage upgrade / research / cancel | While holding command + A in the build wheel: right stick selects, right trigger recruits/upgrades the stage, left trigger requests unit research when available, X cancels a queued unit |
| Target / vehicle brake and reverse | Left trigger |
| Earthshaker combo | Squeeze both grips together; release and squeeze again to repeat. Suppressed while the guitar neck is held |
| Sprint / vehicle nitro | Left stick click |
| Support a weapon with two hands | Squeeze the other grip near the axe haft; left grip attaches to the guitar neck |
| Open solo selection | Right stick click; wheel appears on the guitar headstock |
| Choose a solo | Touch its wedge with the right index finger, or right stick plus A/right trigger |
| Play solo with buttons | A = note 1, X = note 2, Y = note 3 |
| Play solo physically | Strum with the right hand on each beat; while fretting, left trigger / X / Y select note 1 / 2 / 3; with the neck free, the next authored note is selected automatically |
| Jam outside a solo | Pick across the strings, slide the held neck, or change the fretting fingers shortly after a stroke |
| Pause / journal | Left Menu |
| Recenter | Left Menu + right stick click |
| Drive | Right grip + hand turn, or left stick; right trigger accelerates, left trigger brakes/reverses |
| Native menu direction | Left stick; opening carousel uses left-stick left/right and vertical options also use right-stick up/down |

In the opening 3D room, **right-stick left/right turns the room**. Use
**left-stick left/right to change the native main-menu carousel**; a room turn
does not select another page. The **Opening menu stick** remap changes vertical
selection. Horizontal native choices separately follow **UiLeft / UiRight**.
A selects, B returns and X activates Watch Tutorial where the menu shows it.

The observed local RTS entry path is **Multiplayer → A → B to skip the stage
tutorial if shown → AI Practice → A**. At the AI Practice lobby, press **left
Menu** for Start. Wait for each native page before making the next selection.
These are the default inputs; the menu labels follow your saved remaps.

Repeated physical solo notes require separate strokes. Moving only the fretting
hand does not create a new strum. After a jam stroke, sliding or pressing a
different fretting finger can sound another note for 0.8 seconds. Either grip can remain held during a timed solo;
the picking hand stays free. Strokes up to about 23 cm from the string plane
are accepted. The game's rhythm windows and success rules still apply. When
the neck is held, the index / middle / ring fingers select native note 1 / 2 / 3.
The game's rhythm windows and success rules remain active.

Hold **left grip + left stick click** for commands. Right stick sends charge/up,
defend/down, move/right and follow/left. Hold A to keep the build wheel open;
X opens map; Y toggles
flight; triggers descend/ascend; right grip boosts. Native context also routes
right-stick directions to playlist show/toggle/next/previous, right stick click to
rewind, B to beacon/alternate camera, A/B to menu shoulders, and triggers to
menu pages/co-op attacks. The game chooses the applicable contextual action.

Gameplay status and menus attach to the forearms or above the left palm. Turn
the palm up to read it. Solo selection and timed notes attach to the guitar
headstock; bring the instrument into view. Gameplay panels require matching
rendered attachment data for both eyes. When either eye lacks it, panels are
withheld together rather than composed at a stale tracked-hand position.
The 3D world remains present. The opening room uses its own tracked rig.
Central native popup menus extend 20 cm inward and 30 cm farther from the
rendered left-palm plane, preserving their complete source crop and aspect.
This frames the full pause row in both simulator eyes at a neutral pose.
The game hides the row after 450 idle Flash frames. A native button or stick
input wakes it; head or palm motion alone does not. B, Menu/Start or Back
from the idle page returns to the game. A fresh 10.5-second stereo take keeps
the complete row visible during head and palm movement. Its small labels still
need physical readability checks; other menus have not all been accepted.

If a grip loses tracking, the affected arm and equipment follow the current
native game pose. Equipment selection is retained, generated attack and UI
pulses release, and that untracked hand cannot point at UI. Reacquiring the
controller does not count as a physical swing or strum. Invalid head tracking
also stops old rig and interaction data from being republished.

Weapon selection commits when X or Y is released. Earthshaker uses both grips,
independently of those selection buttons, and is suppressed while fretting, during solos,
driving, pause and stage commands. Hold left trigger and use right trigger to target
and attack together. Aim comes from the right controller; looking around does
not aim or fire. With targeting held, right stick switches native targets.

Scripted camera cutscenes play on a stationary cinema screen. Normal play
returns to the tracked first-person view. Native tutorial cards use the
generated metal frame on a stationary screen, with live text remapped to these controls; other native
notifications retain their content with known input aliases replaced.

Floating gameplay hints and localized Flash messages also use Quest controls.
Earthshaker reads **left grip + right grip together**; block reads **B**; interaction and menu
confirmation read **A**. Weapon attacks name the selection button and right
trigger. Repeated combo hits keep the same weapon selected instead of asking
you to toggle it for every hit. Command hints spell out **left grip + left
stick click** plus the action button. These labels use the same binding table
as input. See [the hint fix and validation](QUEST_HINTS_2026-09-28.md).

Stage battle commands use the dominant stick direction, so a diagonal issues
one order even if those directions are remapped to the left stick. Use the
configured radial stick to choose an item in the build wheel while holding the build
input. By default, hold left grip + left stick click + A to keep that wheel
open; releasing the build input closes it. While it is open, right trigger
requests recruitment and X requests cancellation of a queued unit. Those
inputs follow **Radial accept**, **Solo accept alternate** and **Cancel build
item** in the editor. The wheel owns those buttons while held: confirmation
does not ascend, attack or issue a stage order.

Research upgrades use **Build research alternate**, which defaults to left
trigger and works only in the held native build wheel. Ordinary Use/interact
stays on A. The separate **Use** action can also request research when mapped
to a distinct input. Confirmation, cancellation and research exclude the held
build button and any required command-grip/click inputs. Those inputs are
already held when the wheel appears and cannot supply a fresh press. Conflicting
native hints show **unbound**. A build binding without command scope can still
use otherwise free command modifiers. The command requirement applies to actions configured to require
it. Native abilities, available resources and researched technology remain
authoritative. Native recruitment has queued Headbangers in the simulator;
four units have spawned, Stage 2 and Stage 3 progression completed and X
cancellation refunds the queued unit's cost. A Headbanger research purchase
has an authored cost of 200 fans and appeared in the native purchased-upgrade
map. The isolated fan debit and its expected
MaxHealth effect and complete battle acceptance still need verification.
Outside the build wheel, command right trigger sends Ascend without also
confirming a solo.
Unit recruitment and stage-tier upgrades use the same confirmation input.
The native wheel's shared caption reads **right trigger TO RECRUIT / UPGRADE**
with default bindings. The RTS15 simulator retake confirmed that caption in
both eyes; 25 real retail formatter cases also passed. Left-trigger unit
research remains separate; its native purchase is confirmed for Headbangers,
with the resulting health effect still unverified.

Button attacks request the game's current rendered arm pose, including another
press during recovery. Walking uses native poses too; no motion recording is required. Physical swings and strums keep the tracked hands.
Native input tests cover the RTS mappings. Simulator testing has reached native
AI Practice, tracked flight and build-wheel selection. The build wheel's
native recruitment, unit spawning, Stage 2/3 progression, a Headbanger research
purchase and cancellation with refunds are confirmed. The research's health
effect and final readability still need live verification. Flying and commanding in
first-person VR remain the intended stage battle behavior; a complete battle
has not yet been accepted. This evidence covers local Ironheade AI Practice;
other factions, online play and co-op remain unverified.
See [the preview release validation](RELEASE_0.1.0-preview.2.md).
