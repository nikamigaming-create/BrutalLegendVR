"""Run the BLVR first-person rig through the isolated Elliott OpenXR runtime.

This is the clean fallback when Meta XR Operator port 8720 is occupied by
another OpenXR task.  Elliott's runtime accepts the same 6DoF/controller
commands through its documented file-IPC mailbox, so the game still consumes
real OpenXR poses and actions; no desktop input is involved.
"""

import json
import os
import pathlib
import subprocess
import time


ROOT = pathlib.Path(__file__).resolve().parents[1]
GAME_DIR = pathlib.Path(r"D:\SteamLibrary\steamapps\common\BrutalLegend")
GAME_EXE = GAME_DIR / "BrutalLegend.exe"
GAME_LOG = GAME_DIR / "blvr.log"
ARTIFACTS = ROOT / "artifacts"
# BLVR's native capture writer uses this stable output name.  The Elliott
# preview recorder is a separate artifact and must not be used as the game
# proof source.
RAW_VIDEO = ARTIFACTS / "blvr-sim-camera-proof.mp4"
SBS_VIDEO = ARTIFACTS / "blvr-elliott-composed-sbs.mp4"
LEFT_VIDEO = ARTIFACTS / "blvr-elliott-left-eye.mp4"
LEFT_FRAME = ARTIFACTS / "blvr-elliott-left-eye.png"
MARKER = ARTIFACTS / "blvr-elliott-gameplay-ready.marker"
HOST = ROOT / "tools" / "blvr_xr_host.exe"
HOST_LOG = HOST.with_suffix(".log")
ELLIOTT_MANIFEST = pathlib.Path(
    r"D:\code\gta-iv\out-openxr\external\OpenXR-Simulator\bin\openxr_simulator.json"
)
ELLIOTT_DATA = pathlib.Path(os.environ.get("LOCALAPPDATA", pathlib.Path.home())) / "OpenXR-Simulator"
CTRL_FILE = ELLIOTT_DATA / "controller_pose_command.json"
SWEEP_FILE = ELLIOTT_DATA / "pose_sweep_command.json"
HEAD_POSE_FILE = ELLIOTT_DATA / "head_pose_command.json"
PROFILE_FILE = ELLIOTT_DATA / "headset_profile_command.json"
ACK_FILE = ELLIOTT_DATA / "command_ack.json"
RECORDER = pathlib.Path(r"D:\code\gta-iv\scripts\record-elliott-vr-preview.ps1")


def clear_file(path):
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def clear_ipc():
    for path in (CTRL_FILE, SWEEP_FILE, HEAD_POSE_FILE, PROFILE_FILE, ACK_FILE,
                 CTRL_FILE.with_name(CTRL_FILE.name + ".tmp"),
                 SWEEP_FILE.with_name(SWEEP_FILE.name + ".tmp"),
                 HEAD_POSE_FILE.with_name(HEAD_POSE_FILE.name + ".tmp"),
                 PROFILE_FILE.with_name(PROFILE_FILE.name + ".tmp"),
                 ACK_FILE.with_name(ACK_FILE.name + ".tmp")):
        clear_file(path)


def write_atomic(path, payload):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(payload, separators=(",", ":")), encoding="ascii")
    os.replace(temporary, path)


def wait_for_ack(command, sequence=0, timeout=3.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if ACK_FILE.exists():
            try:
                ack = json.loads(ACK_FILE.read_text(encoding="utf-8"))
                same_command = ack.get("command") == command
                same_sequence = command != "controller_state" or int(ack.get("sequence", -1)) == sequence
                if same_command and same_sequence:
                    if not ack.get("success", False):
                        raise RuntimeError(f"Elliott rejected {command}: {ack}")
                    return
            except (OSError, json.JSONDecodeError, ValueError):
                pass
        time.sleep(0.05)
    raise TimeoutError(f"Timed out waiting for Elliott {command} acknowledgment")


def send_profile():
    clear_file(ACK_FILE)
    write_atomic(PROFILE_FILE, {"name": "quest3"})
    wait_for_ack("headset_profile")


def send_sweep(enabled):
    clear_file(ACK_FILE)
    write_atomic(SWEEP_FILE, {
        "enabled": enabled,
        # The showcase needs an unmistakable head-tracking pass: look down
        # to the hands, sweep the room, and roll. Elliott consumes these as
        # native view-space poses; this is not a screen-space effect.
        "yaw_amp_deg": 45.0 if enabled else 0.0,
        "pitch_amp_deg": 25.0 if enabled else 0.0,
        "roll_amp_deg": 12.0 if enabled else 0.0,
        "freq_hz": 0.12,
    })
    wait_for_ack("pose_sweep")


def send_head_pose(*, yaw=0.0, pitch=0.0, roll=0.0, x=0.0, y=1.7, z=0.0):
    clear_file(ACK_FILE)
    write_atomic(HEAD_POSE_FILE, {
        "x": x, "y": y, "z": z,
        "yaw": yaw, "pitch": pitch, "roll": roll,
    })
    wait_for_ack("head_pose")


def send_controller(sequence, hand, *, neutral=False, lease_ms=1000, **fields):
    clear_file(ACK_FILE)
    payload = {
        "hand": hand,
        "sequence": sequence,
        "lease_ms": lease_ms,
        "neutral": neutral,
    }
    payload.update(fields)
    write_atomic(CTRL_FILE, payload)
    wait_for_ack("controller_state", sequence)


def wait_for_text(path, text, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            try:
                if text in path.read_text(encoding="utf-8", errors="ignore"):
                    return True
            except OSError:
                pass
        time.sleep(0.1)
    return False


def read_log(position):
    if not GAME_LOG.exists():
        return position, []
    with GAME_LOG.open("r", encoding="utf-8", errors="ignore") as stream:
        stream.seek(position)
        text = stream.read()
        return stream.tell(), [line.strip() for line in text.splitlines() if line.strip()]


def crop_left_eye():
    result = subprocess.run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-i", str(SBS_VIDEO),
        "-vf", "crop=iw/2:ih:0:0",
        "-c:v", "libx264", "-preset", "fast", "-crf", "18",
        "-an", "-movflags", "+faststart", str(LEFT_VIDEO),
    ], check=False)
    if result.returncode != 0 or not LEFT_VIDEO.exists():
        raise RuntimeError("Could not create the composed left-eye clip")
    result = subprocess.run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-ss", "6", "-i", str(LEFT_VIDEO), "-frames:v", "1", str(LEFT_FRAME),
    ], check=False)
    if result.returncode != 0 or not LEFT_FRAME.exists():
        raise RuntimeError("Could not extract the left-eye proof frame")


def make_single_eye_from_raw():
    """Use the active game target for mono fallback; it has no loading quad."""
    result = subprocess.run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-i", str(RAW_VIDEO),
        "-c:v", "libx264", "-preset", "fast", "-crf", "18",
        "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart",
        str(LEFT_VIDEO),
    ], check=False)
    if result.returncode != 0 or not LEFT_VIDEO.exists():
        raise RuntimeError("Could not create the active-target single-eye clip")
    result = subprocess.run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-ss", "6", "-i", str(LEFT_VIDEO), "-frames:v", "1", str(LEFT_FRAME),
    ], check=False)
    if result.returncode != 0 or not LEFT_FRAME.exists():
        raise RuntimeError("Could not extract the active-target single-eye frame")


def video_is_valid(path):
    if not path.exists() or path.stat().st_size <= 100000:
        return False
    result = subprocess.run([
        "ffprobe", "-hide_banner", "-v", "error",
        "-show_entries", "format=duration", "-of", "default=nw=1:nk=1",
        str(path),
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    return result.returncode == 0


def has_true_stereo_host_proof():
    """Require two distinct CPU-mailbox eyes before calling a capture stereo."""
    try:
        log_text = HOST_LOG.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return False
    for line in log_text.splitlines():
        if "GameBridge: CPU mailbox acquired" not in line:
            continue
        if (
            "eyes=2" in line
            and "sameTick=1" in line
            and "pixelDistinct=1" in line
            and "presentation=world-" in line
            and "mono" not in line
        ):
            return True
    return False


def main():
    required = [GAME_EXE, HOST, ELLIOTT_MANIFEST, RECORDER]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        print("Missing Elliott proof requirements: " + "; ".join(missing), flush=True)
        return 1
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    for path in (RAW_VIDEO, SBS_VIDEO, LEFT_VIDEO, LEFT_FRAME, MARKER):
        clear_file(path)
    clear_ipc()

    env = os.environ.copy()
    for key in ("BLVR_FNVXR_BRIDGE", "BLVR_SIM", "BLVR_SIM_DATA_DIR",
                "XR_API_LAYER_PATH", "XR_ENABLE_API_LAYERS"):
        env.pop(key, None)
    env.update({
        "XR_RUNTIME_JSON": str(ELLIOTT_MANIFEST),
        "OPENXR_SIMULATOR_HEADLESS": "1",
        "BLVR_XR_BRIDGE": "1",
        "BLVR_AUTOMATION": os.environ.get("BLVR_AUTOMATION", "1"),
        "BLVR_FIRST_PERSON": "1",
        "BLVR_ARM_RIG": "1",
        "BLVR_NATIVE_BODY_HEAD": os.environ.get("BLVR_NATIVE_BODY_HEAD", "0"),
        # The default keeps the retail culling anchor, but both switches are
        # exposed so the same Elliott pass can prove the actual eye camera
        # without editing the DLL between runs.
        "BLVR_KEEP_NATIVE_CAMERA": os.environ.get("BLVR_KEEP_NATIVE_CAMERA", "1"),
        "BLVR_PRESERVE_SCENE_POSITION": os.environ.get("BLVR_PRESERVE_SCENE_POSITION", "1"),
        "BLVR_CONTEXT_EYE": os.environ.get("BLVR_CONTEXT_EYE", "0"),
        "BLVR_W2P_OVERRIDE": os.environ.get("BLVR_W2P_OVERRIDE", "0"),
        "BLVR_ARM_WORLD_ATTACH": os.environ.get("BLVR_ARM_WORLD_ATTACH", "0"),
        "BLVR_FORCE_TWO_SIDED": os.environ.get("BLVR_FORCE_TWO_SIDED", "1"),
        "BLVR_FORCE_OPAQUE": os.environ.get("BLVR_FORCE_OPAQUE", "1"),
        "BLVR_CAPTURE_ACTIVE_TARGET": os.environ.get("BLVR_CAPTURE_ACTIVE_TARGET", "0"),
        "BLVR_CAPTURE_REUSE_LAST_GOOD": os.environ.get("BLVR_CAPTURE_REUSE_LAST_GOOD", "0"),
        # Bone-level collapse owns head/torso visibility for this pass; keep
        # the mixed skinned draw submissions alive so the arm surfaces can
        # actually render.
        "BLVR_HIDE_NATIVE_HEAD": os.environ.get("BLVR_HIDE_NATIVE_HEAD", "0"),
        # Keep the native mixed skinned object, but collapse only its
        # head/torso bones so hands and attached equipment remain live.
        "BLVR_HEAD_ONLY_ARMS": os.environ.get("BLVR_HEAD_ONLY_ARMS", "1"),
        "BLVR_HEAD_ONLY_ARMS_MASK": os.environ.get("BLVR_HEAD_ONLY_ARMS_MASK", "0x01"),
        "BLVR_FIRST_PERSON_OVERLAY": "0",
        "BLVR_WORLD_MATRIX_OVERRIDE": os.environ.get("BLVR_WORLD_MATRIX_OVERRIDE", "0"),
        "BLVR_BODY_DRAW_SKIP_START": os.environ.get("BLVR_BODY_DRAW_SKIP_START", ""),
        "BLVR_BODY_DRAW_SKIP_END": os.environ.get("BLVR_BODY_DRAW_SKIP_END", ""),
        "BLVR_BODY_DRAW_SKIP_SWEEP": os.environ.get("BLVR_BODY_DRAW_SKIP_SWEEP", "0"),
        "BLVR_BODY_WORLD_X_OFFSET": os.environ.get("BLVR_BODY_WORLD_X_OFFSET", ""),
        "BLVR_BODY_WORLD_Y_OFFSET": os.environ.get("BLVR_BODY_WORLD_Y_OFFSET", ""),
        "BLVR_BODY_WORLD_Z_OFFSET": os.environ.get("BLVR_BODY_WORLD_Z_OFFSET", ""),
        "BLVR_BODY_OBJECT_ANCHOR": os.environ.get("BLVR_BODY_OBJECT_ANCHOR", "0"),
        "BLVR_ARM_BODY_Y_OFFSET": os.environ.get("BLVR_ARM_BODY_Y_OFFSET", "0.0"),
        "BLVR_ARM_BODY_X_OFFSET": os.environ.get("BLVR_ARM_BODY_X_OFFSET", "0.0"),
        "BLVR_ARM_HEAD_X_OFFSET": os.environ.get("BLVR_ARM_HEAD_X_OFFSET", "0.0"),
        "BLVR_HEAD_PITCH_OFFSET": "0.0",
        "BLVR_HEAD_FORWARD": os.environ.get("BLVR_HEAD_FORWARD", "0.12"),
        "BLVR_HEAD_HEIGHT": os.environ.get("BLVR_HEAD_HEIGHT", "1.95"),
        "BLVR_WINDOWED": "1",
        "BLVR_NOFOCUS_PAUSE": "1",
        "BLVR_KEEP_RUNNING": "0",
        "BLVR_RECORD": "1",
        # The retail front end reports a camera before its loading surface has
        # handed off to the gameplay world. Arm capture only after that handoff
        # has had time to settle, so the showcase starts on a rendered world
        # frame instead of recording the loading/clear target.
        "BLVR_RECORD_DELAY_PRESENTS": os.environ.get("BLVR_RECORD_DELAY_PRESENTS", "600"),
    })

    try:
        GAME_LOG.write_text("", encoding="utf-8")
    except OSError:
        pass
    try:
        HOST_LOG.unlink()
    except FileNotFoundError:
        pass

    host = None
    game = None
    recorder = None
    allow_mono_left_eye = os.environ.get("BLVR_ALLOW_MONO_LEFT_EYE", "0") == "1"
    try:
        host = subprocess.Popen(
            [str(HOST), "--game", "--timeout-ms", "120000"],
            cwd=str(HOST.parent), env=env,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        if not wait_for_text(HOST_LOG, "session FOCUSED", 30.0):
            print("Elliott session did not reach FOCUSED", flush=True)
            return 1
        print(f"BLVR OpenXR session focused PID={host.pid}", flush=True)
        send_profile()
        send_controller(1, 2, neutral=True, lease_ms=1200)

        game = subprocess.Popen(
            [str(GAME_EXE)], cwd=str(GAME_DIR), env=env,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        print(f"Launched BrutalLegend PID={game.pid}; BLVR bridge owns 6DoF", flush=True)

        # Start the host-owned composed SBS recorder. It waits on our marker,
        # never activates the game or simulator window, and is scoped to this
        # host PID.
        recorder = subprocess.Popen([
            "pwsh", "-NoProfile", "-File", str(RECORDER),
            "-WalkMarkerPath", str(MARKER),
            "-OutputPath", str(SBS_VIDEO),
            "-LauncherPid", str(os.getpid()),
            "-DurationSeconds", "12",
            "-FrameRate", "60",
        ], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))

        if not wait_for_text(GAME_LOG, "BLVR bridge mapping opened", 25.0):
            print("BLVR did not open its pose mailbox", flush=True)
            return 1
        automation_enabled = env["BLVR_AUTOMATION"] == "1"
        if automation_enabled and not wait_for_text(GAME_LOG, "AutoAdvanceThread: Started", 25.0):
            print("BLVR automation thread did not start", flush=True)
            return 1

        # The native front-end routine is normally sufficient.  Elliott menu
        # edges remain available for runtimes that need an explicit XR input
        # handoff, but can be disabled when they interfere with the retail
        # title's asynchronous movie/menu transition.
        use_controller_menu = os.environ.get("BLVR_CONTROLLER_MENU", "1") == "1"
        if use_controller_menu:
            time.sleep(2.0)

            def menu_edge(sequence):
                send_controller(sequence, 0, lease_ms=1600, menu=True,
                                stickX=0.0, stickY=0.0, trigger=0.0, squeeze=0.0,
                                primary=False, secondary=False, thumbClick=False)
                time.sleep(1.8)
                send_controller(sequence + 1, 0, neutral=True, lease_ms=700)

            menu_edge(2)
            time.sleep(5.0)
            menu_edge(4)
            time.sleep(5.0)
            send_controller(6, 1, lease_ms=1600, primary=True,
                            stickX=0.0, stickY=0.0, trigger=0.0, squeeze=0.0,
                            secondary=False, menu=False, thumbClick=False)
            time.sleep(1.8)
            send_controller(7, 1, neutral=True, lease_ms=700)

        log_position = 0
        gameplay_seen = False
        deadline = time.monotonic() + 120.0
        next_sequence = 10
        next_retry = time.monotonic() + 6.0
        while time.monotonic() < deadline:
            time.sleep(0.25)
            log_position, lines = read_log(log_position)
            for line in lines:
                if any(token in line for token in (
                        "CameraActive=1", "Triggering VideoCapture", "VideoCapture: First frame")):
                    print("[BLVR] " + line, flush=True)
                if "Triggering VideoCapture" in line:
                    gameplay_seen = True
            if gameplay_seen:
                break
            if game.poll() is not None:
                print(f"BrutalLegend exited before gameplay (code={game.returncode})", flush=True)
                return 1
            if time.monotonic() >= next_retry:
                # The retail front end occasionally drops the first A edge
                # while its movie/menu surface is still settling. Re-issue a
                # real controller edge through Elliott; this is not desktop
                # input or a synthetic render overlay.
                # Keep the title handoff alive with real Elliott controller
                # edges.  Some front-end movie states drop one edge; retrying
                # the menu/A pair is bounded to the pre-game loop and stops as
                # soon as the native gameplay camera publishes.
                send_controller(next_sequence, 0, lease_ms=900, menu=True,
                                stickX=0.0, stickY=0.0, trigger=0.0, squeeze=0.0,
                                primary=False, secondary=False, thumbClick=False)
                send_controller(next_sequence + 1, 1, lease_ms=1200, primary=True,
                                stickX=0.0, stickY=0.0, trigger=0.0, squeeze=0.0,
                                secondary=False, menu=False, thumbClick=False)
                send_controller(next_sequence + 2, 1, neutral=True, lease_ms=700)
                next_sequence += 3
                next_retry = time.monotonic() + 4.0
        if not gameplay_seen:
            print("BLVR did not publish an active gameplay camera", flush=True)
            return 1

        # Deliberate native head poses: hold the gaze down on both hands,
        # then pan/roll each side before returning to level. These are
        # Elliott runtime poses, not a HUD or a fake arm overlay.
        send_sweep(False)
        send_head_pose(pitch=-0.95)
        send_controller(next_sequence, 0, lease_ms=9000,
                        posX=-0.30, posY=-0.20, posZ=-0.38,
                        yaw=-0.16, pitch=-0.12, squeeze=0.85,
                        trigger=0.0, stickX=0.0, stickY=0.0,
                        primary=False, secondary=False, menu=False, thumbClick=False)
        next_sequence += 1
        send_controller(next_sequence, 1, lease_ms=9000,
                        posX=0.27, posY=-0.18, posZ=-0.36,
                        yaw=0.16, pitch=-0.12, squeeze=0.85,
                        trigger=0.0, stickX=0.0, stickY=0.0,
                        primary=False, secondary=False, menu=False, thumbClick=False)
        next_sequence += 1
        MARKER.write_text("Elliott left/right grip poses + head sweep active\n", encoding="utf-8")

        time.sleep(3.5)
        send_head_pose(yaw=-0.70, pitch=-0.55, roll=0.12)
        time.sleep(2.0)
        send_head_pose(yaw=0.70, pitch=-0.55, roll=-0.12)
        time.sleep(2.0)
        send_head_pose(yaw=0.0, pitch=0.0, roll=0.0)
        time.sleep(1.0)
        # Weapon-use beat: right trigger/A, then a second controller pose.
        send_controller(next_sequence, 1, lease_ms=900,
                        posX=0.18, posY=-0.26, posZ=-0.32,
                        yaw=0.30, pitch=-0.10, squeeze=0.90,
                        trigger=1.0, stickX=0.0, stickY=0.0,
                        primary=True, secondary=False, menu=False, thumbClick=False)
        next_sequence += 1
        time.sleep(0.8)
        send_controller(next_sequence, 1, lease_ms=6000,
                        posX=0.18, posY=-0.26, posZ=-0.32,
                        yaw=0.30, pitch=-0.10, squeeze=0.90,
                        trigger=0.0, stickX=0.0, stickY=0.0,
                        primary=False, secondary=True, menu=False, thumbClick=False)
        next_sequence += 1

        # Short real locomotion interval, followed by a longer controller-only
        # approach. The route is deliberately expressed as XR stick poses so
        # the retail movement code owns Eddie/root/collision updates; the
        # camera is not translated by the capture script.
        send_controller(next_sequence, 0, lease_ms=3500,
                        posX=-0.30, posY=-0.20, posZ=-0.38,
                        yaw=-0.16, pitch=-0.12, squeeze=0.85,
                        trigger=0.0, stickX=0.0, stickY=0.45,
                        primary=False, secondary=False, menu=False, thumbClick=False)
        next_sequence += 1
        time.sleep(2.5)
        send_controller(next_sequence, 0, lease_ms=4500,
                        posX=-0.30, posY=-0.20, posZ=-0.38,
                        yaw=-0.16, pitch=-0.12, squeeze=0.85,
                        trigger=0.0, stickX=0.18, stickY=0.62,
                        primary=False, secondary=False, menu=False, thumbClick=False)
        next_sequence += 1
        # The translation is a real simulator head pose, not a post-process
        # camera slide. It gives the single-eye take a short 6DoF approach
        # toward the gate while the retail root remains collision-authoritative.
        send_head_pose(yaw=0.18, pitch=-0.18, roll=0.0, z=-0.55)
        time.sleep(3.2)
        send_controller(next_sequence, 0, lease_ms=4500,
                        posX=-0.30, posY=-0.20, posZ=-0.38,
                        yaw=-0.16, pitch=-0.12, squeeze=0.85,
                        trigger=0.0, stickX=-0.12, stickY=0.72,
                        primary=False, secondary=False, menu=False, thumbClick=False)
        next_sequence += 1
        send_head_pose(yaw=-0.08, pitch=-0.32, roll=0.0, z=-1.15)
        time.sleep(3.2)
        send_controller(next_sequence, 0, lease_ms=3500,
                        posX=-0.30, posY=-0.20, posZ=-0.38,
                        yaw=-0.16, pitch=-0.12, squeeze=0.85,
                        trigger=0.0, stickX=0.0, stickY=0.55,
                        primary=False, secondary=False, menu=False, thumbClick=False)
        next_sequence += 1
        send_head_pose(yaw=0.0, pitch=-0.22, roll=0.0, z=-1.65)
        time.sleep(2.2)
        send_controller(next_sequence, 0, neutral=True, lease_ms=1000)
        send_head_pose(yaw=0.0, pitch=0.0, roll=0.0, z=0.0)

        video_deadline = time.monotonic() + 45.0
        while time.monotonic() < video_deadline:
            if RAW_VIDEO.exists() and RAW_VIDEO.stat().st_size > 100000:
                text = GAME_LOG.read_text(encoding="utf-8", errors="ignore")
                if ("Final video with audio finalized" in text or "Video finalized!" in text) and video_is_valid(RAW_VIDEO):
                    print(f"Raw gameplay video ready: {RAW_VIDEO}", flush=True)
                    break
            time.sleep(0.5)
        else:
            print("Raw gameplay video did not finalize", flush=True)
            return 1

        if recorder is not None:
            recorder_output, _ = recorder.communicate(timeout=50)
            print(recorder_output[-4000:], flush=True)
            if recorder.returncode != 0 or not SBS_VIDEO.exists():
                print("Elliott composed SBS recorder failed", flush=True)
                return 1
        if not has_true_stereo_host_proof():
            if allow_mono_left_eye:
                make_single_eye_from_raw()
                print(
                    "Single-eye preview ready (source is mono; this is not "
                    f"stereo proof): {LEFT_VIDEO}",
                    flush=True,
                )
                print(f"Single-eye frame: {LEFT_FRAME}", flush=True)
                return 0
            print(
                "Stereo proof rejected: Elliott host reported one mono eye "
                "or identical eye pixels; no SBS artifact is being presented "
                "as true stereo.",
                flush=True,
            )
            return 1
        crop_left_eye()
        print(f"Left-eye proof ready: {LEFT_VIDEO}", flush=True)
        print(f"Left-eye frame: {LEFT_FRAME}", flush=True)
        return 0
    finally:
        clear_ipc()
        if recorder is not None and recorder.poll() is None:
            recorder.terminate()
            try:
                recorder.wait(timeout=4)
            except subprocess.TimeoutExpired:
                recorder.kill()
        if game is not None and game.poll() is None:
            game.terminate()
            try:
                game.wait(timeout=4)
            except subprocess.TimeoutExpired:
                game.kill()
        if host is not None and host.poll() is None:
            host.terminate()
            try:
                host.wait(timeout=4)
            except subprocess.TimeoutExpired:
                host.kill()


if __name__ == "__main__":
    raise SystemExit(main())
