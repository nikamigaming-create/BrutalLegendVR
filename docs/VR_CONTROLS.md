# BLVR Touch controls

Run **Remap Controls.cmd** to edit all 51 native actions and the VR controls,
or edit **controls.ini**. Saved changes reload during play within 250 ms;
invalid edits retain the last working layout. Release held buttons after
saving. Chords, equipment, wheel/support grips, recenter and all four stick
roles can be reassigned. The tables below show defaults. Native gameplay
contexts and scripted locks still apply.

Timed solo glyphs keep the game's original A/X/Y artwork. A live legend on the
guitar identifies your configured inputs for those notes. Native text hints
use the same registry as input; existing messages refresh with the game.


By default, the left hand holds the guitar neck. The right hand picks across the strings
and holds the axe. X equips/stows the axe; Y equips/stows the guitar.
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
| Back / evade / block / vehicle handbrake | B, depending on context |
| Dismiss tutorial card | B |
| Use selected weapon | Right trigger, physical right-hand axe swing, or right-hand guitar stroke |
| Target / vehicle brake and reverse | Left trigger |
| Earthshaker combo | Squeeze left grip + right grip together; release and squeeze again to repeat. Weapon selection stays unchanged |
| Sprint / vehicle nitro | Left stick click |
| Support a weapon with two hands | Squeeze the other grip near the axe haft or guitar body; release to let go |
| Open solo selection | Right stick click; wheel appears on the guitar headstock |
| Choose a solo | Touch its wedge with the right index finger, or right stick plus A/right trigger |
| Play solo with buttons | A = note 1, X = note 2, Y = note 3 |
| Play solo physically | Strum across the guitar with the right hand on each beat; the next authored note is selected automatically |
| Pause / journal | Left Menu |
| Recenter | Left Menu + right stick click |
| Drive | Right grip + hand turn, or left stick; right trigger accelerates, left trigger brakes/reverses |
| Native menu direction | Left stick; opening room uses right-stick vertical for selection |

Repeated physical solo notes require separate strokes. Moving only the fretting
hand does not play a note. Either grip can remain held during a timed solo;
the picking hand stays free. Strokes up to about 23 cm from the string plane
are accepted. The game's rhythm windows and success rules still apply. When
native note information is unavailable, the manual frets remain: no X/Y = note 1,
X = note 2, Y = note 3; left grip prevents a fret button from firing before the stroke.

Hold **left grip + left stick click** for commands. Right stick sends charge/up,
defend/down, move/right and follow/left. A opens build; X opens map; Y toggles
flight; triggers descend/ascend; right grip boosts. Native context also routes
right-stick directions to playlist show/toggle/next/previous, right stick click to
rewind, B to beacon/alternate camera, A/B to menu shoulders, and triggers to
menu pages/co-op attacks. The game chooses the applicable contextual action.

Gameplay status and menus attach to the forearms or above the left palm. Turn
the palm up to read it. Solo selection and timed notes attach to the guitar
headstock; bring the instrument into view. UI is rendered into both eyes while
the 3D world remains present.

Weapon selection commits when X or Y is released. Earthshaker uses both grips,
independently of those selection buttons, and is suppressed during solos,
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

Stage battle commands use the dominant right-stick direction, so a diagonal
issues one order. Release the command chord to use A/B normally in a build
menu; the left stick navigates and X cancels a queued unit.
The command right trigger sends Ascend without also confirming a solo.

Button attacks request the game's current rendered arm pose, including another
press during recovery. Walking uses native poses too; no motion recording is required. Physical swings and strums keep the tracked hands.
Native input tests cover the RTS mappings; live stage-battle behavior is still
unverified. See [the preview release validation](RELEASE_0.1.0-preview.1.md).
