import os
import subprocess
import sys
import time


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FNV_ROOT = r"D:\code\fnvvr"
FNV_HOST = os.path.join(FNV_ROOT, "build-product-x64", "Release", "fnvxr_openxr_pose_host.exe")
RUNTIME_JSON = r"C:\Program Files\MetaXRSimulator\v205.0\meta_openxr_simulator.json"
SIMULATOR_EXE = r"C:\Program Files\MetaXRSimulator\v205.0\MetaXRSimulator.exe"
GAME_LOG = r"D:\SteamLibrary\steamapps\common\BrutalLegend\blvr.log"
HOST_LOG = os.path.join(ROOT, "artifacts", "fnvxr_meta_host.log")
SIM_LOG = os.path.join(ROOT, "artifacts", "fnvxr_meta_simulator.log")
SIM_DATA = os.path.join(ROOT, "artifacts", "fnvxr_meta_simulator_data")
PROOF_RUNNER = os.path.join(ROOT, "scripts", "run_proof_capture.py")


def fail(message):
    print(f"ERROR: {message}")
    return 1


def wait_for_ready(host_process, timeout=20.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(HOST_LOG):
            with open(HOST_LOG, "r", encoding="utf-8", errors="ignore") as stream:
                if "fnvxrHostBridgeReady xrSessionCreated=1 sharedMappingsReady=1" in stream.read():
                    return True
        if host_process.poll() is not None:
            return False
        time.sleep(0.1)
    return False


def main():
    required = [FNV_HOST, RUNTIME_JSON, PROOF_RUNNER]
    missing = [path for path in required if not os.path.isfile(path)]
    if missing:
        return fail("missing required real OpenXR bridge input: " + "; ".join(missing))
    if not os.path.isfile(SIMULATOR_EXE):
        return fail(f"MetaXRSimulator.exe is missing: {SIMULATOR_EXE}")

    os.makedirs(os.path.dirname(HOST_LOG), exist_ok=True)
    os.makedirs(SIM_DATA, exist_ok=True)
    for path in (HOST_LOG, SIM_LOG):
        try:
            os.remove(path)
        except FileNotFoundError:
            pass

    # Keep the simulator external to the game, but make the proof self-starting
    # when no simulator process exists.  This is process-handle orchestration,
    # not UI automation; the host selects it with the process-local manifest.
    simulator_processes = subprocess.run(
        ["tasklist", "/fi", "imagename eq MetaXRSimulator.exe", "/fo", "csv", "/nh"],
        capture_output=True,
        text=True,
        check=False,
    ).stdout
    simulator = None
    if "MetaXRSimulator.exe" not in simulator_processes:
        simulator = subprocess.Popen(
            [SIMULATOR_EXE],
            cwd=os.path.dirname(SIMULATOR_EXE),
            env=os.environ.copy(),
            stdout=open(SIM_LOG, "a", encoding="utf-8"),
            stderr=subprocess.STDOUT,
        )
        print(f"Started MetaXRSimulator PID={simulator.pid}")
        time.sleep(2.0)

    host_env = os.environ.copy()
    host_env.update(
        {
            "XR_RUNTIME_JSON": RUNTIME_JSON,
            "OPENXR_SIMULATOR_HEADLESS": "0",
            "OPENXR_SIMULATOR_DATA_DIR": SIM_DATA,
            "OPENXR_SIMULATOR_LOG_PATH": SIM_LOG,
            # Use the same sidecar profile as the proven FNVVR controller
            # route.  This authorizes simulator controller actions without
            # giving the host ownership of the Windows desktop.
            "FNVXR_RUN_PROFILE": "openxr-sidecar",
            "FNVXR_RUN_ID": "blvr-meta-openxr-proof",
            "FNVXR_RUN_LOG_DIR": os.path.dirname(HOST_LOG),
            "FNVXR_WINDOWS_FOREGROUND_INPUT_FORBIDDEN": "1",
            "FNVXR_XINPUT_PHYSICAL_MENU_BUTTONS_ENABLE": "1",
            "FNVXR_DISABLE_STEREO_WORLD": "1",
            "FNVXR_ENABLE_LEGACY_IMAGE_DIAGNOSTICS": "1",
            "FNVXR_SHOW_GAME_PLANE": "1",
            "FNVXR_SHOW_GAME_PLANE_IN_GAME": "1",
            "FNVXR_GAME_PLANE_SOURCE": "shared",
            "FNVXR_GAME_PLANE_WINDOW_FALLBACK_ON_BLACK": "0",
            "FNVXR_SHOW_BODY_RIG": "0",
            "FNVXR_SHOW_WORLD_PROPS": "0",
            "FNVXR_SHOW_LEFT_AIM_RAY": "0",
            "FNVXR_SHOW_RIGHT_AIM_RAY": "0",
        }
    )

    host_log = open(HOST_LOG, "w", encoding="utf-8")
    keep_running = os.environ.get("BLVR_KEEP_RUNNING", "1") == "1"
    host = subprocess.Popen(
        [FNV_HOST, "100000000" if keep_running else "18000"],
        cwd=os.path.dirname(FNV_HOST),
        env=host_env,
        stdout=host_log,
        stderr=subprocess.STDOUT,
    )
    print(f"Started FNVVR x64 OpenXR host PID={host.pid}")
    completed = False
    try:
        if not wait_for_ready(host):
            return fail("FNVVR host did not create a real Meta OpenXR session")

        game_env = os.environ.copy()
        game_env.pop("BLVR_SIM", None)
        game_env["BLVR_FNVXR_BRIDGE"] = "1"
        game_env["BLVR_AUTOMATION"] = "1"
        game_env["BLVR_SIM_DATA_DIR"] = SIM_DATA
        game_env["BLVR_FIRST_PERSON"] = os.environ.get("BLVR_FIRST_PERSON", "1")
        game_env.setdefault("BLVR_ARM_RIG", "1")
        game_env.setdefault("BLVR_NATIVE_BODY_HEAD", "0")
        game_env["BLVR_ARM_WORLD_ATTACH"] = "0"
        game_env["BLVR_FORCE_TWO_SIDED"] = os.environ.get("BLVR_FORCE_TWO_SIDED", "1")
        game_env["BLVR_BODY_OBJECT_ANCHOR"] = "0"
        game_env["BLVR_HEAD_PITCH_OFFSET"] = os.environ.get("BLVR_HEAD_PITCH_OFFSET", "0.0")
        game_env["BLVR_HEAD_FORWARD"] = os.environ.get("BLVR_HEAD_FORWARD", "0.12")
        game_env["BLVR_HEAD_HEIGHT"] = os.environ.get("BLVR_HEAD_HEIGHT", "2.15")
        game_env.pop("BLVR_HEAD_ZOOM", None)
        game_env["BLVR_WINDOWED"] = "1"
        game_env["BLVR_NOFOCUS_PAUSE"] = "1"
        game_env["BLVR_KEEP_RUNNING"] = "1" if keep_running else "0"
        result = subprocess.run(
            [sys.executable, PROOF_RUNNER],
            cwd=ROOT,
            env=game_env,
            check=False,
        )
        completed = result.returncode == 0
        if completed and keep_running:
            print(f"Live simulator left running, host PID={host.pid}")
        return result.returncode
    finally:
        if host.poll() is None and not (completed and keep_running):
            host.terminate()
            try:
                host.wait(timeout=5)
            except subprocess.TimeoutExpired:
                host.kill()
        host_log.close()
        if simulator is not None and simulator.poll() is None:
            simulator.terminate()
            try:
                simulator.wait(timeout=5)
            except subprocess.TimeoutExpired:
                simulator.kill()


if __name__ == "__main__":
    sys.exit(main())
