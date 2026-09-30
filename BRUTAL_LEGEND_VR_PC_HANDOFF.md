# Brütal Legend VR --- PC Handoff

## Goal

Build a **first-person PCVR conversion of Brütal Legend**. Preserve the
original game, combat rules, AI, damage, progression, vehicles, stage
battles, audio, and content. Add OpenXR rendering/input and
progressively replace third-person presentation with VR-native camera,
hands, weapons, driving, UI, and commands.

**Primary target:** Quest 3 via PCVR/OpenXR.\
**Initial rule:** prove camera + stereo before spending time on hands,
IK, UI, or gameplay polish.

------------------------------------------------------------------------

## Known Starting Point

-   Windows PC version.
-   32-bit x86.
-   Direct3D 9-era renderer.
-   Double Fine Buddha engine.
-   Game data includes Lua/script resources and package formats that
    existing community tooling can inspect.
-   Treat the retail executable as a reverse-engineering target; do
    **not** assume internal symbols/API names.
-   Prefer signatures/patterns and documented discoveries over brittle
    hard-coded offsets.

------------------------------------------------------------------------

## Definition of Success

### P0 --- Recon

Produce a technical inventory of the installed game: - executable
architecture/imports/modules - D3D9 device creation/render path -
package/script layout - Lua entry points/globals visible from shipped
scripts - likely camera/view/projection code - player/Eddie transform
candidates - frame/update/render boundaries - findings recorded in
`reverse/notes.md`

### P1 --- Head Tracking Proof

Inject/load our x86 VR module, initialize OpenXR, and make **HMD
rotation control the game camera**.

Success: \> I turn my physical head and the in-game camera follows with
low latency.

Then add HMD XYZ translation for 6DoF and place the view approximately
at Eddie's head.

### P2 --- Real Stereo

Render independent left/right eye views with OpenXR projections and
submit them to the compositor.

Requirements: - simulation updates once per game frame - world renders
twice - correct IPD/eye offsets - correct HMD pose - no obvious double
simulation - investigate culling, shadows, particles, post FX,
billboards, and depth behavior

**Preferred approach:** locate a high-level Buddha
`RenderScene(camera)`-equivalent and invoke the world render for each
eye.

**Fallback:** D3D9 interception and identified view/projection shader
constants.

### P3 --- Playable FPS VR

-   first-person Eddie
-   hide head/geometry that clips into HMD
-   existing controller gameplay remains usable
-   cinematics shown on a comfortable virtual theater screen
-   existing HUD temporarily projected to a VR-safe surface if necessary

At this point the campaign should be testable before motion controls.

### P4 --- Motion Controls

Map OpenXR controllers to: - tracked hands - Separator axe
presentation - Clementine presentation/aiming - interaction ray -
existing gameplay actions

Do **not** rewrite combat initially.

Use:
`VR gesture/input -> existing Brütal Legend action -> original game resolves damage/AI/effects`

### P5 --- VR-Native Systems

-   physical axe swing classification
-   Clementine VR interaction
-   Solo selection/activation
-   Deuce first-person cockpit
-   independent HMD look while driving
-   Stage Battle pointing/raycast orders
-   flight controls/comfort options
-   optional body/arm IK
-   world-space/wrist UI and HUD reduction

------------------------------------------------------------------------

## Architecture

``` text
BrutalLegend.exe
  |
  +-- Buddha / original game
  +-- D3D9
  |
  +-- BrutalLegendVR x86 module
      |
      +-- bootstrap / loader
      +-- signature scanner
      +-- D3D9 hooks
      +-- Buddha camera/render hooks
      +-- OpenXR session + swapchains
      +-- pose/input system
      +-- VR camera
      +-- game/player bridge
      +-- hands/weapons
      +-- vehicle VR
      +-- UI/cutscene handling
      +-- diagnostics
```

Suggested repository:

``` text
BrutalLegendVR/
  CMakeLists.txt
  README.md
  docs/
    STATUS.md
    REVERSE_ENGINEERING.md
  src/
    bootstrap/
    hooks/
    reverse/
    openxr/
    vr/
    game/
    diagnostics/
  reverse/
    notes.md
    signatures/
    ghidra/
  tools/
    script_scan/
    shader_dump/
```

------------------------------------------------------------------------

# FIRST SESSION: DO THIS, NOT THE WHOLE MOD

## Mission

Get from an untouched installed game to a **repeatable HMD-camera
proof**.

### Step 1 --- Inventory

Inspect the actual installed files. Record hashes/version information.
Do not rely on Internet assumptions when the local binary can answer the
question.

### Step 2 --- Build x86 Bootstrap

Create the smallest reliable way to load our code into
`BrutalLegend.exe`.

Requirements: - Debug logging to a file - crash-safe initialization -
identify D3D9 device - detect Present/Reset - clean
shutdown/reinitialization handling

### Step 3 --- OpenXR

Initialize OpenXR without changing rendering yet.

Log: - runtime - system/HMD - supported view configuration - recommended
eye sizes - poses - controller availability

### Step 4 --- Camera RE

Use static + runtime analysis to locate the game's active gameplay
camera.

Useful evidence: - FOV changes - view matrix writes - projection
construction - camera position/orientation - references immediately
before world rendering - shader constants carrying view/projection
matrices

Build a debug override such as: - FOV override - camera yaw offset -
camera XYZ offset

Do not move on until modifying these values visibly and predictably
changes the gameplay camera.

### Step 5 --- Connect HMD

Convert OpenXR coordinates into Buddha's coordinate system and feed HMD
orientation into the discovered camera path.

Then add positional tracking.

Keep Eddie/game movement independent from HMD local movement.

Conceptually:

``` text
VR camera world pose =
player/reference-space transform
* HMD local pose
```

### Step 6 --- First Deliverable

Stop and document when this works:

> Launch game -\> enter gameplay -\> headset pose is read -\> physical
> head rotation/translation drives the in-game camera.

Capture logs and document every discovered address/function/signature
with confidence level and evidence.

------------------------------------------------------------------------

# Reverse-Engineering Rules

1.  **Never invent engine APIs.** Names like `RenderScene`,
    `Eddie::Attack`, or `Camera` are conceptual until proven.
2.  Annotate recovered functions by behavior first:
    `FUN_render_world_candidate`, etc.
3.  Prefer signature scanning to fixed addresses.
4.  Separate game-version-specific discoveries from generic VR code.
5.  Keep original simulation authoritative.
6.  Do not patch dozens of things simultaneously. Every hook gets a test
    proving why it exists.
7.  Add runtime toggles for experimental hooks.
8.  Log enough information that a crash can be traced without guessing.
9.  Preserve a clean path to launch the unmodified game.
10. Commit after every proven milestone.

------------------------------------------------------------------------

# Critical Questions to Answer

-   Where is the active gameplay camera created/updated?
-   Where are view and projection matrices generated?
-   Is there one high-level world-render function we can safely call
    twice?
-   Can rendering be repeated without advancing simulation?
-   What uses camera position for culling/LOD?
-   Which passes are world, UI, shadows, postprocessing, particles, and
    video?
-   How is Eddie represented and where is his world transform?
-   Can Eddie's head mesh/bone be selectively hidden?
-   What gameplay actions are reachable through Lua versus native code?
-   Where are vehicle camera states selected?
-   How are Stage Battle commands represented?
-   How are cutscenes distinguished from gameplay?

------------------------------------------------------------------------

# Performance Strategy

Brütal Legend is old enough that brute-force stereo may be practical,
but measure rather than assume.

Instrument CPU/GPU timing for: - simulation - world pass - shadows -
particles - post FX - UI - each eye

Optimize only after correct stereo exists.

------------------------------------------------------------------------

# Codex / Agent Operating Instructions

You are working directly in the installed Brütal Legend PC directory and
the `BrutalLegendVR` repository.

**Work autonomously. Do not stop merely because a symbol, structure,
offset, or API is unknown. Investigate it.**

Use local binary inspection, Ghidra/static analysis where available,
runtime logging, D3D9 instrumentation, package/script inspection,
controlled experiments, and source code you create.

Do not attempt to implement the entire VR conversion in one pass.

Your immediate priority order is:

1.  inventory the real installed build
2.  establish reliable x86 code loading
3.  hook/observe D3D9 safely
4.  initialize OpenXR
5.  locate and prove control of the gameplay camera
6.  connect HMD rotation
7.  connect HMD position
8.  document discoveries
9.  only then begin stereo rendering

Whenever uncertain between several candidate implementations, build the
smallest experiment that distinguishes them.

Maintain: - `docs/STATUS.md` --- what works, what is broken, exact next
action - `docs/REVERSE_ENGINEERING.md` --- recovered
functions/structures/signatures and evidence - `reverse/notes.md` ---
raw investigation notes - normal source control commits at stable
checkpoints

Do not report success because code compiles. Success requires observable
behavior in the running game.

## First finish line

**Brütal Legend launches normally, OpenXR initializes, and the gameplay
camera follows the Quest 3 HMD in rotational and positional 6DoF.**

Once that is demonstrated, stop feature expansion, commit everything,
update the documentation, and report exactly what was discovered about
the camera/render path and what is required for dual-eye rendering.

------------------------------------------------------------------------

## Later Finish Line

A complete conversion should eventually provide:

**HMD:** full 6DoF first-person view\
**Rendering:** true stereo OpenXR\
**Eddie:** VR-safe first-person body\
**Separator:** tracked melee presentation\
**Clementine:** tracked ranged/guitar presentation\
**Combat:** original game logic preserved\
**Deuce:** cockpit VR\
**Stage Battles:** point/raycast command interface\
**Flight:** VR-compatible controls + comfort options\
**UI:** world-space/VR-safe\
**Cutscenes:** theater mode initially\
**Input:** controller fallback always available

The guiding principle is:

> **Make Brütal Legend believe it is still Brütal Legend. Replace the
> camera, rendering presentation, and input layer around it rather than
> rebuilding the game.**

ROLL.
