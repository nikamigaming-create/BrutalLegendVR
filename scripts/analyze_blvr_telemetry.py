#!/usr/bin/env python3
"""Summarize a BLVR telemetry JSONL capture and flag timing/origin faults."""

from __future__ import annotations

import argparse
import json
import math
from collections import Counter
from pathlib import Path
from typing import Any


def finite(value: Any) -> bool:
    return isinstance(value, (int, float)) and math.isfinite(float(value))


def events_with(events: list[dict[str, Any]], key: str) -> list[dict[str, Any]]:
    return sorted(
        [event for event in events if key in event and finite(event.get(key))],
        key=lambda event: float(event["tick_ms"]),
    )


def max_delta(events: list[dict[str, Any]], key: str) -> tuple[float, int]:
    ordered = events_with(events, key)
    if len(ordered) < 2:
        return 0.0, 0
    deltas = [float(b[key]) - float(a[key]) for a, b in zip(ordered, ordered[1:])]
    return max(deltas), sum(1 for delta in deltas if delta > 1.0)


def longest_run(events: list[dict[str, Any]], predicate) -> int:
    longest = 0
    current = 0
    for event in events:
        if predicate(event):
            current += 1
            longest = max(longest, current)
        else:
            current = 0
    return longest


def vec_distance(a: Any, b: Any) -> float | None:
    if not isinstance(a, list) or not isinstance(b, list) or len(a) != len(b):
        return None
    if not all(finite(value) for value in a + b):
        return None
    return math.sqrt(sum((float(x) - float(y)) ** 2 for x, y in zip(a, b)))


def analyze(path: Path) -> dict[str, Any]:
    events: list[dict[str, Any]] = []
    malformed = 0
    if path.exists():
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            if not line.strip():
                continue
            try:
                item = json.loads(line)
                if isinstance(item, dict):
                    events.append(item)
            except json.JSONDecodeError:
                malformed += 1

    by_kind: dict[str, list[dict[str, Any]]] = {}
    for event in events:
        by_kind.setdefault(str(event.get("kind", "unknown")), []).append(event)

    summary: dict[str, Any] = {
        "path": str(path),
        "events": len(events),
        "malformed_lines": malformed,
        "kinds": {kind: len(items) for kind, items in sorted(by_kind.items())},
        "warnings": [],
    }

    present = by_kind.get("present", [])
    xr = by_kind.get("xr_tick", [])
    camera = by_kind.get("camera", [])
    capture = by_kind.get("capture", [])

    present_gap, present_gap_count = max_delta(present, "present_count")
    pose_gap, pose_gap_count = max_delta(xr, "pose_count")
    summary["present"] = {
        "count": len(present),
        "max_counter_delta": present_gap,
        "counter_jumps": present_gap_count,
        "reentrant": sum(1 for event in present if event.get("reentrant")),
        "failed_hresult": sum(1 for event in present if int(event.get("present_hr", 0)) & 0x80000000),
    }
    summary["xr"] = {
        "count": len(xr),
        "max_pose_counter_delta": pose_gap,
        "pose_counter_jumps": pose_gap_count,
        "bridge_read_failures": sum(1 for event in xr if not event.get("bridge_read", 1)),
        "runtime_inactive": sum(1 for event in xr if not event.get("runtime_active")),
        "bridge_frames": len({event.get("bridge_frame") for event in xr if event.get("bridge_frame")}),
    }
    # Calculate the stateful mailbox-staleness run explicitly for a readable
    # report.
    xr_ordered = events_with(xr, "pose_count")
    last_bridge = object()
    same_bridge_run = 0
    max_same_bridge_run = 0
    bridge_frame_deltas: list[float] = []
    bridge_jumps: list[dict[str, Any]] = []
    previous_bridge_tick = None
    for event in xr_ordered:
        bridge_frame = event.get("bridge_frame")
        if bridge_frame == last_bridge:
            same_bridge_run += 1
        else:
            max_same_bridge_run = max(max_same_bridge_run, same_bridge_run)
            same_bridge_run = 0
        if isinstance(last_bridge, (int, float)) and isinstance(bridge_frame, (int, float)):
            delta = float(bridge_frame) - float(last_bridge)
            bridge_frame_deltas.append(delta)
            if delta > 5.0:
                bridge_jumps.append({
                    "delta": delta,
                    "from": last_bridge,
                    "to": bridge_frame,
                    "tick_ms": event.get("tick_ms"),
                    "pose_count": event.get("pose_count"),
                })
        last_bridge = bridge_frame
        previous_bridge_tick = event.get("tick_ms")
    summary["xr"]["same_bridge_frame_run"] = max_same_bridge_run
    summary["xr"]["max_bridge_frame_delta"] = max(bridge_frame_deltas, default=0.0)
    summary["xr"]["largest_bridge_jumps"] = sorted(
        bridge_jumps, key=lambda item: float(item["delta"]), reverse=True
    )[:5]
    summary["camera"] = {
        "count": len(camera),
        "active": sum(1 for event in camera if event.get("active")),
        "inactive": sum(1 for event in camera if not event.get("active")),
        "anomalies": dict(Counter(str(event.get("anomaly", "none")) for event in camera)),
        "max_root_delta": max((float(event.get("root_delta", 0.0)) for event in camera), default=0.0),
        "max_eye_delta": max((float(event.get("eye_delta", 0.0)) for event in camera), default=0.0),
    }
    movie_capture = [
        event for event in capture
        if event.get("recording") and event.get("stage") in ("frame", "reused_last_good")
    ]
    summary["capture"] = {
        "count": len(capture),
        "valid_frames": sum(1 for event in capture if event.get("source_valid")),
        "reused_last_good": sum(1 for event in capture if event.get("reused_last_good")),
        "copy_or_target_failures": sum(1 for event in capture if str(event.get("stage", "")).endswith("failed")),
        "invalid_no_last_good": sum(1 for event in capture if event.get("stage") == "invalid_no_last_good"),
        "invalid_skipped_recording": sum(1 for event in capture if event.get("stage") == "invalid_skip_recording"),
        "max_recorded": max((int(event.get("recorded", 0)) for event in capture), default=0),
        "max_first_lock_count": max((int(event.get("first_lock_count", 0)) for event in capture), default=0),
        "reuse_ratio": (sum(1 for event in capture if event.get("reused_last_good")) / len(capture)) if capture else 0.0,
        "longest_reuse_run": longest_run(capture, lambda event: bool(event.get("reused_last_good"))),
        "longest_valid_run": longest_run(capture, lambda event: bool(event.get("source_valid"))),
        "stages": dict(Counter(str(event.get("stage", "unknown")) for event in capture)),
        "dimensions": sorted({
            f"{event.get('width', 0)}x{event.get('height', 0)}@{event.get('format', 0)}"
            for event in capture if event.get("width")
        }),
    }
    summary["capture"]["movie"] = {
        "count": len(movie_capture),
        "valid_frames": sum(1 for event in movie_capture if event.get("source_valid")),
        "reused_last_good": sum(1 for event in movie_capture if event.get("reused_last_good")),
        "reuse_ratio": (sum(1 for event in movie_capture if event.get("reused_last_good")) / len(movie_capture)) if movie_capture else 0.0,
        "longest_reuse_run": longest_run(movie_capture, lambda event: bool(event.get("reused_last_good"))),
    }

    if present and present_gap > 3:
        summary["warnings"].append(f"Present counter jumped by {present_gap:.0f}")
    if xr and pose_gap > 3:
        summary["warnings"].append(f"XR pose counter jumped by {pose_gap:.0f}")
    if summary["xr"]["bridge_read_failures"]:
        summary["warnings"].append("BLVR pose mailbox read failed at least once")
    if summary["xr"]["runtime_inactive"]:
        summary["warnings"].append("XR runtime/controller stream went inactive")
    if summary["xr"]["max_bridge_frame_delta"] > 5:
        summary["warnings"].append(
            f"BLVR bridge frame id jumped by {summary['xr']['max_bridge_frame_delta']:.0f} between game samples"
        )
    if summary["camera"]["max_root_delta"] > 10 or summary["camera"]["max_eye_delta"] > 10:
        summary["warnings"].append("Camera/root or eye moved more than 10 world units")
    if summary["capture"]["movie"]["reused_last_good"]:
        summary["warnings"].append(
            "Capture reused the last-good surface during the recorded movie; continuity was synthetic "
            f"({summary['capture']['movie']['reuse_ratio']:.1%}, "
            f"max run {summary['capture']['movie']['longest_reuse_run']})"
        )
    if summary["capture"]["copy_or_target_failures"]:
        summary["warnings"].append("Render-target acquisition/copy failed")
    if summary["capture"]["invalid_skipped_recording"]:
        summary["warnings"].append(
            f"Skipped {summary['capture']['invalid_skipped_recording']} cleared source samples while recording"
        )
    if summary["capture"]["max_first_lock_count"] > 1:
        summary["warnings"].append("First-frame lock was observed repeatedly; capture state may be re-entering")
    if any(event.get("anomaly") not in (None, "none", "camera_inactive") for event in camera):
        summary["warnings"].append("Camera telemetry reported a non-normal anomaly")

    # Keep the tail small but actionable for a quick post-run read.
    summary["last_anomalies"] = [
        {
            "tick_ms": event.get("tick_ms"),
            "kind": event.get("kind"),
            "stage": event.get("stage"),
            "anomaly": event.get("anomaly"),
            "root_delta": event.get("root_delta"),
            "eye_delta": event.get("eye_delta"),
            "source_valid": event.get("source_valid"),
            "bridge_read": event.get("bridge_read"),
        }
        for event in events
        if event.get("anomaly") not in (None, "none", "camera_inactive")
        or event.get("stage") not in (None, "frame", "reused_last_good")
        or event.get("bridge_read") is False
    ][-20:]
    return summary


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", nargs="?", default="D:/code/blvr/artifacts/blvr_telemetry.jsonl")
    parser.add_argument("--out", default="D:/code/blvr/artifacts/blvr_telemetry_summary.json")
    args = parser.parse_args()
    summary = analyze(Path(args.path))
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0 if not summary["warnings"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
