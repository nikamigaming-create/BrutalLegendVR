# 0.1.0-preview.4

Experimental launcher patch, October 4, 2026:
[Brütal Legend VR 0.1.0-preview.4](https://github.com/nikamigaming-create/BrutalLegendVR/releases/tag/v0.1.0-preview.4).

Preview 3 could pass its headset check and then fail before launching the game
with `XR_ERROR_API_VERSION_UNSUPPORTED (-4)`. The check requested OpenXR 1.0,
while gameplay requested the 1.1 version of the compilation headers. The host
uses OpenXR 1.0 and `XR_KHR_D3D11_enable`; both paths now request that same
application version. Logs distinguish the compiled SDK version from the API
version requested from the runtime.

Automatic runtime selection now prefers the installed **Meta Quest Link /
Air Link OpenXR runtime**. This avoids starting SteamVR merely because it is
registered as the system runtime. A runtime saved in VR settings, or supplied
with `-RuntimeJson`, still overrides the automatic choice. BLVR changes its
own launch environment; it does not replace the system runtime registration.
When Meta is absent, the registered headset runtime remains the fallback.
The settings list puts Meta first and explains this choice.

When the host exits during startup, the launcher reads its fresh log and
includes the actual fatal reason in the displayed error. It continues to
clean up processes started by a failed launch.

## Install or update

Close Brütal Legend and the VR launcher, then run
**BrutalLegendVR-0.1.0-preview.4-Setup.exe** over the existing installation.
Saved preferences, controls and imported models remain. Portable users should
extract the entire ZIP and keep their local settings, controls and model cache.

If you previously selected SteamVR explicitly, choose the Meta
`oculus_openxr_64.json` runtime in **VR settings** and save, or clear that
selection to use the new automatic preference. Start Quest Link / Air Link,
connect the headset and Touch controllers, then choose **Play VR**.
Python is included. The installer and ZIP include the same verified DXVK
backend, original artwork, settings and guitar implementation as preview 3.
Retail game assets and saves are imported locally and excluded from downloads.

## Validation

The old installed host reproduced the reported failure: SteamVR preflight
passed and gameplay instance creation failed with result -4. The patched
installed host created a D3D11 session, initialized Touch actions and submitted
an OpenXR frame through the same SteamVR runtime.

Meta runtime preflight also passed. A subsequent local session used **Oculus
OpenXR**, published tracked poses, acquired native same-tick stereo game
frames and continued submitting them until a clean runtime shutdown.
This verifies the reported startup and runtime selection path; comfort,
complete campaign coverage and every controller interaction remain separate
acceptance work.

Tested and packaged host SHA-256:
`80c7b378ea03b13155e4f7ee55f371b3c6b0d1ac9a53567e906fc9b82d767226`.

The [preview 3 validation notes](RELEASE_0.1.0-preview.3.md) retain the installer,
guitar and RTS evidence and graphics limitations. Its published recordings use
the earlier host identified in those notes; they are not new preview 4 footage.
All previously documented shader/effect, occlusion, full-battle and online
multiplayer acceptance limits remain.
