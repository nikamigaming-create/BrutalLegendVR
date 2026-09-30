# September 27 performance and image-quality pass

The deployed source image is now **1536 × 1536 per eye**, up from 1280 × 720.
The host follows the runtime's recommended projection size, capped at 2048,
and applies edge antialiasing to the world image. Native menus stay unfiltered.
`BLVR_RENDER_WIDTH`, `BLVR_RENDER_HEIGHT`, `BLVR_PROJECTION_LIMIT`, and
`BLVR_EDGE_AA=0` provide explicit overrides.

The main CPU fix throttles the terrain-origin cache sweep to once per second.
Previously, exceeding 16,384 entries caused a full scan on every terrain-build
call. Origin retention and the terrain texture anchoring fix are preserved.
Frame transport now uses contiguous copies and bounded diagnostic sampling.

Measured on RTX 4070 SUPER, Elliott OpenXR Simulator, sustained opening gameplay,
last 40 seconds before the showcase recorder was enabled:

| Measurement | Result |
| --- | ---: |
| Fresh game frames | 58.05 FPS |
| Mean frame time | 17.23 ms |
| 95th / 99th percentile frame | 25.12 / 29.12 ms |
| Stereo section, inclusive | 8.86 ms |
| Eye render CPU, left / right | 0.55 / 0.62 ms |
| Eye readback and conversion, both | 5.97 ms |
| Stereo mailbox publication | 1.66 ms |
| Native UI capture | 0.91 ms |
| Tracked rig | 0.025 ms |
| Terrain build / amortized prune | 0.071 / 0.015 ms |
| Eye GPU timestamp span, left / right | 0.96 / 1.01 ms |

Counters overlap: do not add inclusive stereo, capture, readback and conversion
columns together. GPU numbers are timestamp elapsed spans, not utilization.
Thread CPU samples use coarse Windows accounting and are unsuitable as per-frame
percentiles. The raw CPU/GPU CSV files are in
`artifacts/stereo-diagnostic-20260927-155819/`; the summary is
`artifacts/perf-final-game.json`.

An earlier 1280 × 720 capture ran at 31.3 FPS with a larger draw count and another
game competing for the GPU. This is useful diagnostic context, not a controlled
percentage speedup. Readback, conversion and CPU transport remain the dominant
VR overhead. A fresh physical-headset/Link measurement and a sustained 90 FPS
result are still outstanding; this simulator measurement does not establish them.

Native input hints now share the action-binding registry with input evaluation.
The native input test changes Block to A and checks that both the input and hint
change together. Known action tokens, command controls and physical solo prompts
are covered; unknown authored tokens are preserved. Solo strums choose the next
authored note without changing the game's rhythm windows. The timing path passes
native input fixtures, but a complete live Relic Raiser success is still unverified.
