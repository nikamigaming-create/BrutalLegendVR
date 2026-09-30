"""Run BLVR against the Meta XR Simulator through the BLVR x64 XR host.

Brutal Legend remains the only game process. Elliott owns the 64-bit Meta
OpenXR session and publishes a pointer-free pose/input mailbox consumed by the
32-bit BLVR DLL; the Meta operator drives that session without desktop input.
"""

import base64
import json
import math
import os
import pathlib
import queue
import subprocess
import sys
import threading
import time


ROOT = pathlib.Path(__file__).resolve().parents[1]
GAME_DIR = pathlib.Path(r"D:\SteamLibrary\steamapps\common\BrutalLegend")
GAME_EXE = GAME_DIR / "BrutalLegend.exe"
GAME_LOG = GAME_DIR / "blvr.log"
ARTIFACTS = ROOT / "artifacts"
VIDEO = ARTIFACTS / "blvr-sim-camera-proof.mp4"
META_RUNTIME = pathlib.Path(r"C:\Program Files\MetaXRSimulator\v205.0\meta_openxr_simulator.json")
META_EXE = pathlib.Path(r"C:\Program Files\MetaXRSimulator\v205.0\MetaXRSimulator.exe")
OPERATOR = pathlib.Path(
    r"D:\code\gta-iv\out-openxr\external\meta-xr-operator-standalone-205.1"
    r"\extracted\meta-xr-operator-standalone-public\windows\meta-xr-operator-mcp-proxy.exe"
)
BLVR_HOST = ROOT / "tools" / "blvr_xr_host.exe"
BLVR_HOST_LOG = BLVR_HOST.with_suffix(".log")


class MetaOperator:
    def __init__(self, executable: pathlib.Path, env=None):
        self.process = subprocess.Popen(
            [str(executable)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True, encoding="utf-8",
            env=env,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        self.messages = queue.Queue()
        self.sequence = 0
        self.lock = threading.Lock()
        # The simulator may have another OpenXR client active (for example a
        # separate MGS session).  All pose/input/capture helpers below add
        # this handle explicitly once BLVR host session is discovered.
        self.session_handle = None
        threading.Thread(target=self._read, daemon=True).start()
        self.request("initialize", {
            "protocolVersion": "2024-11-05", "capabilities": {},
            "clientInfo": {"name": "blvr-meta-proof", "version": "1.0"},
        })
        self.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
        tools = self.request("tools/list", {}).get("tools", [])
        self.tools = [item["name"] for item in tools]

    def _read(self):
        for line in self.process.stdout:
            try:
                self.messages.put(json.loads(line))
            except json.JSONDecodeError:
                continue
        self.messages.put({"error": {"message": "Meta operator exited"}})

    def send(self, message):
        self.process.stdin.write(json.dumps(message, separators=(",", ":")) + "\n")
        self.process.stdin.flush()

    def request(self, method, params):
        with self.lock:
            self.sequence += 1
            request_id = self.sequence
            self.send({"jsonrpc": "2.0", "id": request_id, "method": method,
                       "params": params})
            deadline = time.monotonic() + 15.0
            while time.monotonic() < deadline:
                response = self.messages.get(timeout=max(0.01, deadline - time.monotonic()))
                if "error" in response:
                    raise RuntimeError(response["error"])
                if response.get("id") == request_id:
                    return response.get("result", {})
            raise TimeoutError(method)

    def name(self, suffix):
        matches = [name for name in self.tools if name.endswith(suffix)]
        if len(matches) != 1:
            raise RuntimeError(f"Expected one Meta tool ending {suffix!r}, got {matches}")
        return matches[0]

    def call(self, suffix, arguments):
        result = self.request("tools/call", {
            "name": self.name(suffix), "arguments": arguments,
        })
        if result.get("isError"):
            raise RuntimeError(result)
        return result

    def close(self):
        if self.process.poll() is not None:
            return
        try:
            self.process.stdin.close()
            self.process.wait(timeout=3)
        except (BrokenPipeError, subprocess.TimeoutExpired):
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()


def quaternion(yaw=0.0, pitch=0.0, roll=0.0):
    cy, sy = math.cos(yaw / 2.0), math.sin(yaw / 2.0)
    cp, sp = math.cos(pitch / 2.0), math.sin(pitch / 2.0)
    cr, sr = math.cos(roll / 2.0), math.sin(roll / 2.0)
    return [
        cy * sp * cr + sy * cp * sr,
        sy * cp * cr - cy * sp * sr,
        cy * cp * sr - sy * sp * cr,
        cy * cp * cr + sy * sp * sr,
    ]


def set_head(operator, position, orientation, duration=0.35):
    args = {
        "base_space": "local", "position": position,
        "orientation": orientation, "duration_seconds": duration,
    }
    if operator.session_handle is not None:
        args["session_handle"] = operator.session_handle
    operator.call("openxr_set_head_pose", args)


def set_hand(operator, hand, position, orientation=None):
    args = {"hand": hand, "base_space": "local", "pose_type": "grip",
            "position": position}
    if orientation is not None:
        args["orientation"] = orientation
    if operator.session_handle is not None:
        args["session_handle"] = operator.session_handle
    operator.call("openxr_set_controller_pose", args)


def button(operator, hand, component, value=1.0, hold=0.15):
    args = {
        "hand": hand, "component": component, "value": value,
        "auto_release": True, "hold_duration": hold,
    }
    if operator.session_handle is not None:
        args["session_handle"] = operator.session_handle
    operator.call("openxr_set_controller_input", args)


def sustained_button(operator, hand, component, seconds=1.5):
    """Keep an action high across several host syncs, then release it."""
    hold(operator, hand, component, 1.0)
    time.sleep(seconds)
    hold(operator, hand, component, 0.0)


def hold(operator, hand, component, value):
    args = {
        "hand": hand, "component": component, "value": value,
    }
    if operator.session_handle is not None:
        args["session_handle"] = operator.session_handle
    operator.call("openxr_set_controller_input", args)


def showcase(operator):
    """Drive a full, continuous 6DoF showcase through Meta's input API."""
    hold(operator, "left", "Grip", 1)
    hold(operator, "right", "Grip", 1)
    set_hand(operator, "left", [-0.25, -0.22, -0.42])
    set_hand(operator, "right", [0.25, -0.22, -0.42])

    # 0.0s - 2.5s: Turn head left to look at the stone pillar and archway
    print("[SHOWCASE] 0-2.5s: Turn head left to inspect courtyard pillars", flush=True)
    set_head(operator, [-0.08, 0.02, -0.04], quaternion(yaw=-0.50, pitch=0.08), 1.0)
    set_hand(operator, "left", [-0.28, -0.18, -0.36], quaternion(yaw=-0.25))
    time.sleep(2.5)

    # 2.5s - 5.5s: Turn head right across the courtyard towards the right altar
    print("[SHOWCASE] 2.5-5.5s: Turn head right towards right gate", flush=True)
    set_head(operator, [0.08, 0.01, -0.04], quaternion(yaw=0.55, pitch=-0.05), 1.2)
    set_hand(operator, "right", [0.28, -0.18, -0.34], quaternion(yaw=0.30))
    time.sleep(3.0)

    # 5.5s - 8.5s: Look down at Eddie's hands and the Broadaxe weapon!
    print("[SHOWCASE] 5.5-8.5s: Look down at hands and chest", flush=True)
    set_head(operator, [0.00, 0.04, -0.02], quaternion(yaw=0.0, pitch=-0.75), 1.0)
    set_hand(operator, "left", [-0.22, -0.26, -0.35], quaternion(pitch=-0.20))
    set_hand(operator, "right", [0.22, -0.24, -0.32], quaternion(pitch=-0.25))
    time.sleep(3.0)

    # 8.5s - 12.0s: Weapon swings (A button / Broadaxe chop + right trigger)
    print("[SHOWCASE] 8.5-12.0s: Combat attacks with Broadaxe", flush=True)
    button(operator, "right", "A", 1.0, 0.25)
    set_hand(operator, "right", [0.15, -0.15, -0.30], quaternion(yaw=0.20, pitch=-0.40))
    time.sleep(1.0)
    button(operator, "right", "Trigger", 1.0, 0.30)
    button(operator, "right", "A", 1.0, 0.25)
    set_hand(operator, "right", [0.25, -0.28, -0.35], quaternion(yaw=-0.10))
    time.sleep(1.2)
    button(operator, "right", "B", 1.0, 0.25)
    time.sleep(1.3)

    # 12.0s - 16.5s: Locomotion! Walk forward across the courtyard
    print("[SHOWCASE] 12.0-16.5s: Locomotion forward towards gate", flush=True)
    set_head(operator, [0.00, 0.00, 0.00], quaternion(yaw=0.0, pitch=-0.10), 0.8)
    args_fwd = {
        "hand": "left", "component": "Thumbstick", "sub_component": "Y",
        "value": 0.85, "auto_release": True, "hold_duration": 4.0,
    }
    if operator.session_handle is not None:
        args_fwd["session_handle"] = operator.session_handle
    operator.call("openxr_set_controller_input", args_fwd)
    time.sleep(2.0)

    # 16.5s - 19.5s: Head turn while walking + swing
    print("[SHOWCASE] 16.5-19.5s: Head turn while moving + strike", flush=True)
    set_head(operator, [0.06, -0.02, -0.05], quaternion(yaw=0.35, pitch=-0.15), 0.8)
    button(operator, "right", "A", 1.0, 0.25)
    time.sleep(2.5)

    hold(operator, "left", "Grip", 0)
    hold(operator, "right", "Grip", 0)
    print("[SHOWCASE] Showcase completed across full 20s recording window", flush=True)


def read_new_log(position):
    if not GAME_LOG.exists():
        return position, []
    with GAME_LOG.open("r", encoding="utf-8", errors="ignore") as stream:
        stream.seek(position)
        data = stream.read()
        return stream.tell(), [line.strip() for line in data.splitlines() if line.strip()]


def wait_for_text(path, text, timeout=20.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            try:
                if text in path.read_text(encoding="utf-8", errors="ignore"):
                    return True
            except OSError:
                pass
        time.sleep(0.2)
    return False


def ensure_simulator():
    running = subprocess.run(
        ["tasklist", "/fi", "imagename eq MetaXRSimulator.exe", "/fo", "csv", "/nh"],
        capture_output=True, text=True, check=False,
    ).stdout
    if "MetaXRSimulator.exe" in running:
        return None
    return subprocess.Popen(
        [str(META_EXE)], cwd=str(META_EXE.parent),
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )


def _json_content(result):
    for item in result.get("content", []):
        if item.get("type") == "text":
            try:
                return json.loads(item.get("text", ""))
            except json.JSONDecodeError:
                continue
    return {}


def session_info(operator):
    return _json_content(operator.call("openxr_get_session_info", {
        "include_history": True,
        "reason": "identify the Elliott OpenXR session",
        "sentiment": "neutral",
    }))


def session_handles(info):
    handles = []
    for record in info.get("history", []):
        try:
            handles.append((int(record["session"]["handle"]), record["state"]["name"]))
        except (KeyError, TypeError, ValueError):
            pass
    try:
        handles.append((int(info["session"]["handle"]), info["state"]["name"]))
    except (KeyError, TypeError, ValueError):
        pass
    return handles


def capture_composited_eye(operator, filename="blvr-meta-composited-eye.png"):
    args = {
        "eye": "left",
        "reason": "BLVR Meta XR Simulator left-eye gameplay proof",
        "sentiment": "positive",
    }
    if operator.session_handle is not None:
        args["session_handle"] = operator.session_handle
    result = operator.call("openxr_capture_composited_image", args)
    images = [item for item in result.get("content", [])
              if item.get("type") == "image" and item.get("mimeType") == "image/png"]
    if len(images) != 1:
        raise RuntimeError("Meta compositor did not return one PNG")
    output = ARTIFACTS / filename
    output.write_bytes(base64.b64decode(images[0]["data"], validate=True))
    return output


def main():
    required = [GAME_EXE, META_RUNTIME, OPERATOR, BLVR_HOST]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        print("Missing BLVR/Meta requirements: " + "; ".join(missing))
        return 1
    ARTIFACTS.mkdir(parents=True, exist_ok=True)

    simulator = ensure_simulator()
    if simulator:
        print(f"Started Meta XR Simulator PID={simulator.pid}", flush=True)
        time.sleep(2.0)

    for path in (VIDEO, ARTIFACTS / "blvr-meta-composited-eye.png",
                 ARTIFACTS / "blvr-meta-composited-eye-final.png"):
        try:
            path.unlink()
        except FileNotFoundError:
            pass

    env = os.environ.copy()
    for key in ("BLVR_FNVXR_BRIDGE", "BLVR_SIM", "BLVR_SIM_DATA_DIR", "BLVR_XR_BRIDGE"):
        env.pop(key, None)
    env.update({
        "XR_RUNTIME_JSON": str(META_RUNTIME),
        "XR_API_LAYER_PATH": str(OPERATOR.parent),
        "XR_ENABLE_API_LAYERS": "XR_APILAYER_METAX_operator",
        "OPENXR_SIMULATOR_HEADLESS": "0",
        "BLVR_XR_BRIDGE": "1",
        "BLVR_AUTOMATION": "1",
        "BLVR_FIRST_PERSON": "1",
        "BLVR_ARM_RIG": "1",
        "BLVR_NATIVE_BODY_HEAD": "0",
        "BLVR_KEEP_NATIVE_CAMERA": "1",
        # Keep the retail scene/culling origin stable while the view camera
        # follows the tracked head.  Without this the world camera itself is
        # rewritten to the HMD pose and the floor appears to swim during the
        # showcase.
        "BLVR_PRESERVE_SCENE_POSITION": "1",
        "BLVR_ARM_WORLD_ATTACH": os.environ.get("BLVR_ARM_WORLD_ATTACH", "0"),
        "BLVR_FORCE_TWO_SIDED": os.environ.get("BLVR_FORCE_TWO_SIDED", "1"),
        # Native first-person materials can carry the actor's transparency
        # flag.  The showcase must keep the surviving arm/weapon passes opaque.
        "BLVR_FORCE_OPAQUE": os.environ.get("BLVR_FORCE_OPAQUE", "1"),
        "BLVR_HIDE_NATIVE_HEAD": os.environ.get("BLVR_HIDE_NATIVE_HEAD", "1"),
        "BLVR_HEAD_ONLY_ARMS": os.environ.get("BLVR_HEAD_ONLY_ARMS", "1"),
        "BLVR_HEAD_ONLY_ARMS_MASK": os.environ.get("BLVR_HEAD_ONLY_ARMS_MASK", "0x01"),
        "BLVR_CAPTURE_ACTIVE_TARGET": os.environ.get("BLVR_CAPTURE_ACTIVE_TARGET", "0"),
        "BLVR_RECORD_DELAY_PRESENTS": os.environ.get("BLVR_RECORD_DELAY_PRESENTS", "60"),
        "BLVR_CAPTURE_BEFORE_PRESENT": "1",
        "BLVR_FIRST_PERSON_OVERLAY": "0",
        "BLVR_BODY_WORLD_X_OFFSET": "",
        "BLVR_BODY_WORLD_Y_OFFSET": "",
        "BLVR_BODY_WORLD_Z_OFFSET": "",
        "BLVR_BODY_OBJECT_ANCHOR": "0",
        "BLVR_TRACE_CONSTANTS": "1",
        "BLVR_HEAD_PITCH_OFFSET": "0.0",
        "BLVR_HEAD_FORWARD": os.environ.get("BLVR_HEAD_FORWARD", "0.12"),
        "BLVR_HEAD_HEIGHT": os.environ.get("BLVR_HEAD_HEIGHT", "2.15"),
        "BLVR_WINDOWED": "1",
        "BLVR_NOFOCUS_PAUSE": "1",
        "BLVR_KEEP_RUNNING": "0",
        "BLVR_RECORD": "1",
        "BLVR_W2P_OVERRIDE": "1",
        "BLVR_CONTEXT_EYE": "0",
        "BLVR_MATRIX_UPLOAD_OVERRIDE": "0",
    })

    try:
        GAME_LOG.write_text("", encoding="utf-8")
    except OSError:
        pass
    host = None
    game = None
    operator = None
    baseline_session_handles = set()
    log_position = 0
    gameplay_seen = False
    started = time.monotonic()
    try:
        # The Meta operator service is shared with other OpenXR clients on
        # this machine.  Snapshot its existing handles before BLVR starts,
        # then bind every later pose/input/capture call to BLVR host new
        # session instead of whichever app happens to be foregrounded.
        operator = MetaOperator(OPERATOR, env=env)
        try:
            baseline_session_handles = {
                handle for handle, _state in session_handles(session_info(operator))
            }
        except Exception as exc:
            print(f"Meta operator session snapshot unavailable: {exc}", flush=True)
        print(
            "Meta operator baseline session handles="
            + ",".join(str(handle) for handle in sorted(baseline_session_handles))
            if baseline_session_handles else "Meta operator baseline session handles=none",
            flush=True,
        )

        # Elliott is the proven x64 OpenXR owner. Start it before the x86 game
        # so the mailbox exists when d3d9.dll initializes.
        try:
            BLVR_HOST_LOG.unlink()
        except FileNotFoundError:
            pass
        host = subprocess.Popen(
            [str(BLVR_HOST), "--game", "--timeout-ms", "120000"],
            cwd=str(BLVR_HOST.parent), env=env,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        if not wait_for_text(BLVR_HOST_LOG, "PoseBridge: READY", 20.0):
            print("BLVR x64 OpenXR host did not publish its pose mailbox", flush=True)
            return 1
        print(f"BLVR x64 OpenXR host ready PID={host.pid}", flush=True)

        game_env = env.copy()
        # The Meta operator API layer is x64.  BLVR consumes BLVR host mailbox
        # and must not try to load that layer from the x86 retail process.
        game_env.pop("XR_API_LAYER_PATH", None)
        game_env.pop("XR_ENABLE_API_LAYERS", None)
        game = subprocess.Popen([str(GAME_EXE)], cwd=str(GAME_DIR), env=game_env)
        print(f"Launched BrutalLegend PID={game.pid}; BLVR consuming BLVR 6DoF", flush=True)

        # Wait for the distinct BLVR session to appear in the operator's
        # session history.  Passing this handle is what makes the result a
        # real BLVR/Meta left-eye capture even while another XR app stays
        # connected to the shared operator service.
        deadline = time.monotonic() + 30.0
        elliott_session = None
        while time.monotonic() < deadline:
            try:
                info = session_info(operator)
                candidates = [
                    handle for handle, state in session_handles(info)
                    if handle not in baseline_session_handles
                    and state in {"XR_SESSION_STATE_FOCUSED", "XR_SESSION_STATE_VISIBLE",
                                  "XR_SESSION_STATE_SYNCHRONIZED", "XR_SESSION_STATE_READY"}
                ]
                if candidates:
                    elliott_session = candidates[-1]
                    break
            except Exception as exc:
                print(f"Waiting for BLVR Meta session: {exc}", flush=True)
                time.sleep(0.25)
        if elliott_session is None:
            print("Could not identify BLVR host OpenXR session handle", flush=True)
            return 1
        operator.session_handle = elliott_session
        print(f"Bound Meta operator to BLVR session handle={elliott_session}", flush=True)

        # BLVR host Touch action set binds the retail menu action to the left
        # controller's Menu component (the right System path is not suggested
        # by that host action set).
        wait_for_text(GAME_LOG, "BLVR bridge mapping opened", 15.0)
        # Do not spend the menu edge while the retail process is still
        # constructing its front end.  The OpenXR action is real controller
        # input, but the game can miss it before its first controller poll.
        # Wait for the native automation thread and a few Present calls, then
        # use the menu action followed by the Touch A accept action.  This is
        # still entirely inside the Meta/Elliott input path; no desktop input
        # injection is involved.
        wait_for_text(GAME_LOG, "AutoAdvanceThread: Started", 20.0)
        wait_for_text(GAME_LOG, "D3D9 Present #", 20.0)
        time.sleep(2.0)
        print("BLVR front end ready; sending the spaced Menu -> A controller sequence", flush=True)
        sustained_button(operator, "left", "Menu")
        print("Sent Meta left Menu edge", flush=True)
        time.sleep(3.5)
        sustained_button(operator, "right", "A")
        print("Sent Meta right A edge", flush=True)
        gameplay_deadline = time.monotonic() + 50.0
        while time.monotonic() < gameplay_deadline:
            time.sleep(0.5)
            log_position, lines = read_new_log(log_position)
            for line in lines:
                print("[BLVR] " + line, flush=True)
                if "Triggering VideoCapture" in line or "CameraActive=1" in line:
                    gameplay_seen = True
            if gameplay_seen:
                time.sleep(0.5)
                capture_composited_eye(operator)
                showcase(operator)
                capture_composited_eye(operator, "blvr-meta-composited-eye-final.png")
                break
            if game.poll() is not None:
                print(f"BrutalLegend exited before gameplay (code={game.returncode})", flush=True)
                break

        if not gameplay_seen:
            print("BLVR did not publish an active gameplay camera", flush=True)
            return 1

        # VideoCapture is bounded by the game to a short proof take.
        video_deadline = time.monotonic() + 50.0
        while time.monotonic() < video_deadline:
            if VIDEO.exists() and VIDEO.stat().st_size > 100000:
                text = GAME_LOG.read_text(encoding="utf-8", errors="ignore")
                if "Final video with audio finalized" in text or "Video finalized!" in text:
                    print(f"Gameplay video ready: {VIDEO} ({VIDEO.stat().st_size} bytes)", flush=True)
                    return 0
            time.sleep(0.5)
        print("Gameplay camera ran, but bounded video did not finalize", flush=True)
        return 1
    finally:
        if operator:
            operator.close()
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
        if simulator and simulator.poll() is None:
            simulator.terminate()
            try:
                simulator.wait(timeout=4)
            except subprocess.TimeoutExpired:
                simulator.kill()


if __name__ == "__main__":
    sys.exit(main())
