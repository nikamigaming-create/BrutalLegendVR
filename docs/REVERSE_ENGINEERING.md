# Brütal Legend VR (BLVR) — Reverse Engineering Reference

## Target Executable
- Executable: BrutalLegend.exe
- Machine: x86 (32-bit PE32, GUI)
- Image Base: 0x400000
- Code Range (.text): 0x401000 - 0xe3354b
- SHA256: 872dc676e8fd77ad3351dd9dfcc99e89353aae0ed9857272a65fd47f298fb0b1
- D3D9 Import: Direct3DCreate9 at 0xe34830

## Camera Subsystem (Double Fine Buddha Engine)

### Class Hierarchy
- Camera (base class, 0x487600 - 0x488550)
- CoCamera (derives from Camera, vtables 0xe8c29c, 0xe8c2e0)
- CoCameraController (abstract controller, vtables 0xe87454, 0xe874b4)
- CoGameCameraController (chase/hover controller, vtables 0xed4ffc, 0xed505c)
  - State: CoGameCameraController::State_Chase (0xed4ca4)
  - State: CoGameCameraController::State_Hover (0xed4c80)

### Camera Object Memory Layout
Offset | Type | Description
---|---|---
+0x20 | Camera* | Parent camera / scene node (null if root)
+0x40 | float[3] | Local Position (X, Y, Z)
+0x50 | float[4] | Local Orientation Quaternion (X, Y, Z, W)
+0x60 | float[3] | World Position (X, Y, Z) computed by UpdateWorldTransform
+0x70 | float[4] | World Orientation Quaternion (X, Y, Z, W)
+0x80 | uint8_t | isDirty flag (1 = transform modified, 0 = clean)
+0x90 | float | Field of View in degrees (default 45.0f, from 0xe82a5c)
+0x94 | int32_t | Viewport Width (default 1280)
+0x98 | int32_t | Viewport Height (default 720)
+0x9c | float | Near clipping plane (default 0.25f, from 0xe82a60)
+0xa0 | float | Far clipping plane (default 4096.0f, from 0xe82a64)

### Coordinate System
- Right: +X
- Up: +Y
- Forward: -Z (verified via 0x4884e0 loading -1.0f from 0xe7a790 as forward vector)
- Matches OpenXR coordinate frame directly!

### Core Functions
- 0x488410: void __thiscall Camera::UpdateWorldTransform()
  - Checks isDirty (+0x80).
  - Recursively updates parent if present (+0x20).
  - Computes world position and orientation from +0x40 and +0x50.
  - Writes world transform to +0x60..+0x7f.
  - Clears isDirty = 0.
  - Called immediately before rendering each pass/scene by the renderer (0x9cde1c..0xa9cd84).
  - Observation only in the current VR path. Writing HMD pose here caused a
    chase-camera feedback loop. XR pose is applied to the frozen render scene.

- 0x488390: void __cdecl Camera::SetFromVectors(Vector3* pPos, Quaternion* pRot, Camera* pCam)
  - Stores Pos to +0x40 and Quat to +0x50.
  - Sets isDirty = 1.

- 0x487b30: void __thiscall Camera::SetPositionAndOrientation(Vector3* pPos, Quaternion* pRot)
  - Stores Pos to +0x40 and Quat to +0x50.
  - Sets isDirty = 1.
  - Calls virtual method index 14.

- 0x580a20: CoPhysicsCharacter::UpdateCamera
  - In vtable 0xe9878c (CoPhysicsCharacter) at index 36 (0xe9881c).
  - Reads the `DFCharacterProxy` physics transform through 0xd66e40. This is
    not a skeleton head joint and must not be used as the anatomical eye anchor.
  - Calls 0x488390 at 0x580be9 to synchronize camera with character.

See [PLAYER_VIEW_REPAIR_2026-09-26.md](PLAYER_VIEW_REPAIR_2026-09-26.md) for the
player-owned skeleton, render snapshot, stable head pivot, and shadow contracts.

### Retail shadow reconstruction and detail distances (2026-09-27)

Actual D3D9 receiver shaders (private diagnostic disassembly
`artifacts/render-shaders/ps-4be9316fdcfff5cf.txt`) reconstruct view position with
`ray.xy = g_vCameraUnitScreen.xy + uv * g_vCameraUnitScreen.zw`, followed by
`float3(ray.xy, 1) * -clipW`. For the OpenXR projection P, the constant is
`{(1-P[8])/P[0], -(1+P[9])/P[5], -2/P[0], 2/P[5]}`. The previous scale-first,
positive-depth packing was wrong even though its self-consistent test passed.
The descriptor is VA 0xF7A8C0; its parameter index is at +4. Shader disassembly
is opt-in through BLVR_SHADER_DUMP_DIR and remains a local diagnostic.

The default config's detailCullingScale and meshLODScale are loaded at
0x405590 / 0x405617, copied to renderer-global (0x100A010) +0x214 / +0x228 at
0x402BCA / 0x402C06, and copied into scene +0x124 / +0x138 by 0x6D5050.
0x6F17A5 uses the detail multiplier for screen-size/distance fading;
0x6F1946 uses the mesh multiplier before the native LOD selection at 0x6F80B0.
The selected LOD is compared against the resource's MaxVisibleLOD at 0x6F19BA:
an overly distant LOD can suppress the mesh completely. Increasing the scale
increases projected detail and reduces effective distance, preserving native
thresholds and hysteresis. BLVR raises main gameplay scene minima to 2 and 3
at the camera-build return VA 0x6D3351, before collection. Auxiliary light
cameras and persistent retail config are not modified.

### Camera-relative terrain textures (2026-09-27)

The quad-tree terrain VS (`vs-225fa744229a9743` in the private shader dump)
forms camera-relative world vertices from ModelScale and ModelTranslate, then
projects those vertices through g_mCamWorldToAlbedo0..3 (c108..c123). Unlike
ordinary mesh UVs, these four projections are baked during scene collection.

Verified against retail executable SHA-256
`872dc676e8fd77ad3351dd9dfcc99e89353aae0ed9857272a65fd47f298fb0b1`:

- VA 0x73AEA0 is a stdcall snapshot builder with four arguments. It reads the
  translation-only camera-to-world matrix at scene +0x810 (origin at +0x840)
  and writes four layer matrices at packet +0x80/+0xC0/+0x100/+0x140. The layer
  count is byte +0x1D4; four material pointers begin at +0x68.
- VA 0x73BBB0 is a stdcall destination/source snapshot copy. Its origin metadata
  must follow the copy, rather than being taken from a later interpolated camera.
- VA 0x73BDC0 is the thiscall terrain preparation virtual with three stack
  arguments. Its helper 0x73BF20 computes ModelTranslate with renderer +0x100
  (negative current-eye origin) but uploads the four baked layer matrices as-is.
  The current positive eye origin is renderer +0x140.
- For row-vector matrices the correction is
  `T(currentEye - collectionEye) * bakedProjection`. Only the last matrix row
  changes. VA 0x65E010 delegates to 0x65DFA0, which copies the matrix into the
  renderer's parameter storage, so restoring the packet after preparation is safe.
- The blend projection is recomputed from renderer +0x110 and packet +0x180.
  Front/side projections are regenerated by 0x6F6550 from the same renderer
  matrix. Neither needs a second rebase.

`terrain_projection.cpp` uses three prologue-checked hooks and exact origin
metadata for each native snapshot. The per-eye change is scoped to stereo
rendering and restored after upload; authored terrain resources are not changed.
`BLVR_TERRAIN_REBASE=0` is an optional diagnostic negative fixture. Live terrain
packets in the September 27 10:18 simulator session exercised collection and eye
preparation without missing-origin reports.

### Camera-relative particle snapshots (2026-09-29)

The owned retail particle snapshot builder at VA 0x737CE0 copies system world
XYZ (+0xD4) to packet +0xC8, and writes world minus collection-camera XYZ to
packet +0xB8. Its thiscall preparation virtual at 0x737FD0 takes renderer,
scene and flags as three stack arguments (ret 0xC). The helper at 0x738010
adds packet local center (+0xA8) to that baked relative translation and copies
the result into the g_vParticleToCamWorld parameter. The particle VS applies
packed vertex center * system range + this parameter, then camera billboard
offsets and the current camera-relative projection.

`camera_relative_effects.cpp` verifies the retail prologue before hooking that
preparation virtual. During stereo rendering it uses stored absolute emitter
XYZ minus renderer +0x140 (the current positive eye origin), then restores the
three edited floats after native parameter upload. Particle simulation and
world-space lifetime remain native. Shader-equation tests include both eyes,
head motion and a stale-origin negative fixture; live hook execution is in the
private September 29 simulator receipts. This contract does not establish
ownership-based relocation of attack particles or correctness of every effect
shader.
