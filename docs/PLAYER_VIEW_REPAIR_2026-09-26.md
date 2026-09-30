# Player view and stereo repair — 2026-09-26

Executable SHA-256: `872dc676e8fd77ad3351dd9dfcc99e89353aae0ed9857272a65fd47f298fb0b1`.
Addresses here are preferred VAs for base `0x400000`.

## Camera and input authority

Writing HMD orientation into the native camera while deriving the next VR
heading from that camera created rotation feedback. `VrNavigation` owns an
independent heading initialized once. Chase auto-centering cannot rotate it.
Turning uses a neutral latch, 45-degree increments and an HMD pivot.
Head-relative movement is projected into the retail camera's input basis.
The native simulation camera keeps its orbit radius and vertical offset; its
horizontal bearing follows HMD yaw for action aiming. XR rendering uses the
independent heading and full tracked orientation.

Simulator run `artifacts/stereo-diagnostic-20260926-072339` recorded one
90-to-135-degree change during a 2.7-second held turn, 12.60 native units of
locomotion and about 0.00014 units maximum stationary-pose camera/root residual.
It predates the anatomical rig; it is not final physical acceptance. The earlier
`072018` run captured stale images after its game exited and is invalid.

## Shadow receivers

Native `0x716040` builds four view-to-shadow matrices before XR eye setup, at
scene offsets `0x1280 + cascade*0x290`. Rebase each with
`eyeWorld * nativeView * nativeShadow`. Descriptor index `0xf7adb4` and dirty
propagation match native `0x716384..0x7163c8`. Caster maps remain native.

This alone did not fix asymmetric-eye wall shadows. Native `0x6f369e` uploads
a symmetric UV-to-view ray vector through descriptor `0xf7a8c4`. The eye value
must include the principal point:
`(2/P00, -2/P11, (P20-1)/P00, (P21+1)/P11)`.
Projection, inverse, ray vector and receiver matrices now refer to one eye.
Tests include world-point invariance, asymmetric reconstruction and negative
fixtures for the old matrices/rays. `073529` final simulator images show improved
wall/ground agreement using the Quest 3 profile. Physical acceptance is pending.

## Player-owned anatomical view

The gameplay controller's handle at `0x1250` resolves through the 12-byte entity
table at global `0xf79d8c` (native `0x47ba70`). Entity `+0x24` is `CoSkeleton`;
`+0x38` is `CoRenderMesh`. Component owner `+0x10` must equal that entity, and
vtables must match. Nearby NPC palettes are never candidates.

`CoSkeleton+0x24` points to an animation instance; its `+4` is the skeleton
resource. Resource `+0x14` is bone count, `+0x18` name records, `+0x1c` signed
parent indices, and `+0x48` local reference Qs transforms. Name offsets address
the string table at `+8`. The observed 212-bone Eddie skeleton includes Head 29,
eyes 36/39, Neck1 17, wrists 49/52, props 57/58, AxeAttach 12 and GuitarAttach 13.
Code resolves names rather than hardcoding those indices.

`DynamicMeshSnapshot` vtable `0xeb18b4` has exact `CoRenderMesh` ownership at
`0xe0`, world matrix at `0x70`, and skin header at `0x134`. That header contains
3x4 skin matrices and a 16-bit count at `+4`. Native `0x7334e0` copies the skin
matrices into the render snapshot; `0x738b40/0x738c20` publish them for draws.

`PlayerViewRig_Begin` resolves the reference eye midpoint from this skeleton.
The retail midpoint is approximately (-0.0011, 1.9214, 0.0699) in model space.
Its world anchor uses the render snapshot's root, uniform scale, and flattened
body yaw. Combat skin translations and body pitch/roll do not move or tilt this
pivot. The initial animated-eye implementation visibly bobbed during combat
and was replaced. HMD orientation and physical room-scale movement own the
view, including pitch and roll; there is no additional synthetic neck arc.
Only the named
Head subtree (face and hair included) collapses to Neck1 in the eye snapshot.
Wrist, shoulder and weapon attachment transforms remain byte-identical.
Matrices are restored after both eyes; simulation and shadow snapshots remain
unchanged. The old `0xd66e40/0xd66ed0` observation is a `DFCharacterProxy`
physics path, not a head joint, and does not drive this rig.

The rig test checks a separately specified anatomical eye midpoint, unrelated-owner
rejection, wrist/weapon preservation, head/descendant suppression and byte-exact
restoration. Negative cases add large combat translations and body roll; neither
may move the head pivot. Live `074720` captures show head removal with the native body
retained. Weapon actions exposed a native aiming mismatch, prompting the camera
bearing correction above. These intermediate captures are not completed weapon
or physical VR acceptance.

The native inventory also resolves actual owned `A01_AvatarAxe` (slot +0x20)
and `A01_AvatarGuitar` (+0x38), using the same entity handle table. Their
`CoRenderMesh` components are distinct from Eddie's. A holstered weapon or a
world pickup is not accepted as proof of a visible in-hand attack.
