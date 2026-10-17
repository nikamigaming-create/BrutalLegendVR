# 0.1.0-preview.5 — support diagnostics

An experimental testing release with automatic launcher and installer logs,
support ZIP export, and a [1:51 narrated launch/logging guide](https://github.com/nikamigaming-create/BrutalLegendVR/releases/download/v0.1.0-preview.5/BLVR-support-guide.mp4)
([captions](https://github.com/nikamigaming-create/BrutalLegendVR/releases/download/v0.1.0-preview.5/BLVR-support-guide.srt)).
The native game hook and OpenXR host are unchanged from preview 4.

- Launcher operations retain progress, child output, exit codes and exception
  tracebacks in a writable per-user logs folder, including GUI failures.
- Direct PowerShell launches also retain preflight transcripts.
- **Export support ZIP…** collects bounded known logs, settings, runtime
  candidates, Windows/graphics-driver details and executable/DLL identities.
- **Collect Support Logs.cmd** exports without requiring the launcher window.
- The Windows installer logs automatically and preserves its log after success,
  cancellation or failure. The installer and Start menu expose the logs location.
- **Detailed troubleshooting logs** and performance logging apply on the next
  launch and write to the per-user diagnostics folder.
- Settings scroll at higher Windows display scaling, with Save kept visible.
  Error dialogs show the actionable message; full details remain in the log.

Validation: 21 launcher/settings/support Python checks, 10 release-audit checks,
28 process-lifecycle checks, and 11 packaged installer-diagnostics checks passed.
The revised support guide uses production launcher widgets filmed with example
paths and a lower male neural voice with authored breath pauses. Personal
desktop captures and actual tester support ZIPs are excluded from the render
inputs. All 3,325 frames decode at 1920×1080 / 30 fps; the key controls remain
clear in the encoded video, and speech transcription preserves the instructions.
Source assets and rebuild instructions are in `assets/tutorial/`.

See [tester troubleshooting instructions](TROUBLESHOOTING.md). Logging and
export checks do not extend the physical-headset or gameplay acceptance claims
in the [preview 4 notes](RELEASE_0.1.0-preview.4.md).
