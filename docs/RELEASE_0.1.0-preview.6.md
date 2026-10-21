# 0.1.0-preview.6 — OpenXR runtime selection

VR settings discover and label Virtual Desktop (VDXR), SteamVR and Meta
runtimes, with Browse available for custom installation paths.

Automatic selection respects XR_RUNTIME_JSON, then the registered 64-bit
OpenXR runtime. A saved BLVR selection overrides Automatic; an explicit
-RuntimeJson overrides both. Meta is no longer preferred merely because it
is installed. BLVR does not change the system runtime.

The launcher logs the runtime manifest and selection source. The native host
preflight logs the runtime name and provides connection-specific error guidance.
The game hook and gameplay behavior are unchanged from preview 5.

On October 20, 2026, the user tested the updated local launcher in Virtual
Desktop (VDXR) and reported it working. This is a user-reported smoke test;
no detailed checklist or capture was supplied. SteamVR physical-headset testing,
other controller profiles and the full runtime checklist remain pending.
See [runtime testing](RUNTIME_TESTING.md).

The installer and portable ZIP include the updated launcher and native host,
plus the existing support logging and export features. No retail game assets
are included. The support video remains available in preview 5.

Validation: all 31 Python launcher/settings/support and release-audit tests,
9 runtime-selection cases, 28 launcher lifecycle checks, the native host
self-test and controls validation passed. Lifecycle fixtures use their own
child processes and exclude unrelated running hosts from process assertions.

Physical headset acceptance beyond the reported VDXR test, full campaign,
complete battles, all lighting/shader effects, other factions and online play
remain unverified.
