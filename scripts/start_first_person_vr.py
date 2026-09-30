"""Start phase-one BLVR with Elliott Tate's OpenXR Simulator, without automatic input."""
import argparse
import ctypes
import json
import os
import pathlib
import shutil
import subprocess
import time

import run_blvr_elliott_proof as elliott


def process_alive(pid):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.restype = ctypes.c_void_p
    kernel.GetExitCodeProcess.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.OpenProcess(0x1000, False, pid)
    if not handle:
        return False
    code = ctypes.c_ulong()
    ok = kernel.GetExitCodeProcess(handle, ctypes.byref(code))
    kernel.CloseHandle(handle)
    return bool(ok and code.value == 259)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quest", action="store_true", help="Use Meta Quest Link / Oculus OpenXR runtime")
    parser.add_argument("--steamvr", action="store_true", help="Use SteamVR OpenXR runtime")
    parser.add_argument("--game-dir", type=pathlib.Path, default=elliott.GAME_DIR)
    parser.add_argument("--host", type=pathlib.Path, default=elliott.HOST)
    parser.add_argument("--runtime", type=pathlib.Path, default=None,
                        help="Path to openxr runtime json (defaults to Quest if --quest, SteamVR if --steamvr, else Elliott Simulator)")
    parser.add_argument("--headless", action="store_true", help="Use simulator IPC without desktop input")
    args = parser.parse_args()

    quest_runtime = pathlib.Path(r"C:\Program Files\Oculus\Support\oculus-runtime\oculus_openxr_64.json")
    steamvr_runtime = pathlib.Path(r"C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json")

    if args.quest:
        args.runtime = quest_runtime
    elif args.steamvr:
        args.runtime = steamvr_runtime
    elif args.runtime is None:
        if quest_runtime.is_file():
            args.runtime = quest_runtime
        elif steamvr_runtime.is_file():
            args.runtime = steamvr_runtime
        else:
            args.runtime = elliott.ELLIOTT_MANIFEST
    dll = elliott.ROOT / "build/Release/d3d9.dll"
    loader = elliott.ROOT / "third_party/openxr_loader.dll"
    executable = args.game_dir / "BrutalLegend.exe"
    for path in (dll, loader, executable, args.host, args.runtime):
        if not path.is_file():
            parser.error(f"Required file missing: {path}")
    elliott.ARTIFACTS.mkdir(parents=True, exist_ok=True)
    receipt = elliott.ARTIFACTS / "elliott-phase1-session.json"
    previous = json.loads(receipt.read_text()) if receipt.exists() else {}
    if previous.get("game_pid") and process_alive(previous["game_pid"]):
        print(f"BLVR is already running (PID {previous['game_pid']}).")
        return
    env = {key: value for key, value in os.environ.items()
           if not key.startswith("BLVR_") and key not in ("XR_API_LAYER_PATH", "XR_ENABLE_API_LAYERS")}
    env.update({
        "XR_RUNTIME_JSON": str(args.runtime.resolve()),
        "OPENXR_SIMULATOR_HEADLESS": "1" if args.headless else "0",
        "BLVR_XR_BRIDGE": "1", "BLVR_AUTOMATION": "0",
        "BLVR_FIRST_PERSON": "1", "BLVR_ARM_RIG": "0",
        "BLVR_NATIVE_BODY_HEAD": "0", "BLVR_FIRST_PERSON_OVERLAY": "0",
        "BLVR_KEEP_NATIVE_CAMERA": "0", "BLVR_PRESERVE_SCENE_POSITION": "0",
        "BLVR_CONTEXT_EYE": "0", "BLVR_W2P_OVERRIDE": "1",
        "BLVR_HEAD_ONLY_ARMS": "1", "BLVR_HEAD_ONLY_ARMS_MASK": "0x01",
        "BLVR_HEAD_HEIGHT": "1.72", "BLVR_HEAD_FORWARD": "0",
        "BLVR_WINDOWED": "1", "BLVR_NOFOCUS_PAUSE": "1",
        "BLVR_KEEP_RUNNING": "1", "BLVR_RECORD": "0", "BLVR_FRAME_LIMIT_FPS": "90",
        "BLVR_TITLE_EVENT_ONLY": "0",
    })
    host_pid = previous.get("host_pid", 0)
    if not host_pid or not process_alive(host_pid):
        elliott.clear_ipc()
        log = args.host.with_suffix(".log")
        offset = log.stat().st_size if log.exists() else 0
        host = subprocess.Popen([str(args.host), "--game"], cwd=args.host.parent,
                                env=env, creationflags=subprocess.CREATE_NO_WINDOW)
        host_pid = host.pid
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if host.poll() is not None:
                raise RuntimeError("OpenXR host exited during startup")
            if log.exists():
                with log.open(encoding="utf-8", errors="replace") as stream:
                    stream.seek(offset)
                    content = stream.read()
                    if any(s in content for s in ("session FOCUSED", "session VISIBLE", "session RUNNING")):
                        break
            time.sleep(.1)
        else:
            raise RuntimeError("OpenXR session did not become ready; inspect host log")
        if args.runtime.resolve() == elliott.ELLIOTT_MANIFEST.resolve():
            elliott.send_profile()
            elliott.send_sweep(False)
            elliott.send_head_pose()
            elliott.send_controller(1, 2, neutral=True, lease_ms=1000)
    elif pathlib.Path(previous.get("runtime", "")).resolve() != args.runtime.resolve():
        raise RuntimeError("The existing host uses another runtime")
    shutil.copy2(dll, args.game_dir / "d3d9.dll")
    shutil.copy2(loader, args.game_dir / "openxr_loader.dll")
    game = subprocess.Popen([str(executable)], cwd=args.game_dir, env=env)
    receipt.write_text(json.dumps({"host_pid": host_pid, "game_pid": game.pid,
                                   "runtime": str(args.runtime.resolve())}, indent=2))
    print(f"OpenXR host PID {host_pid}; Brütal Legend PID {game.pid}. Game is live in headset!")


if __name__ == "__main__":
    main()
