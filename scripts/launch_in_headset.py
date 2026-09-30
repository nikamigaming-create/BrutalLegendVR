"""Launch Brutal Legend directly in Meta Quest / System OpenXR headset."""
import os
import pathlib
import subprocess
import time

ROOT = pathlib.Path(r"D:\code\blvr")
GAME_DIR = pathlib.Path(r"D:\SteamLibrary\steamapps\common\BrutalLegend")
HOST_EXE = ROOT / "tools/blvr_xr_host.exe"
GAME_EXE = GAME_DIR / "BrutalLegend.exe"
QUEST_RUNTIME = pathlib.Path(r"C:\Program Files\Oculus\Support\oculus-runtime\oculus_openxr_64.json")

# Ensure DLLs are deployed
dll = ROOT / "build/Release/d3d9.dll"
loader = ROOT / "third_party/openxr_loader.dll"
import shutil
shutil.copy2(dll, GAME_DIR / "d3d9.dll")
shutil.copy2(loader, GAME_DIR / "openxr_loader.dll")

env = os.environ.copy()
env.update({
    "XR_RUNTIME_JSON": str(QUEST_RUNTIME.resolve()),
    "BLVR_XR_BRIDGE": "1",
    "BLVR_SIM": "0",
    "BLVR_AUTOMATION": "0",
    "BLVR_RECORD": "0",
    "BLVR_FIRST_PERSON": "1",
    "BLVR_WINDOWED": "1",
    "BLVR_NOFOCUS_PAUSE": "1",
    "BLVR_KEEP_RUNNING": "1",
    "BLVR_FRAME_LIMIT_FPS": "90",
})

# Clean old log
host_log = ROOT / "tools/blvr_xr_host.log"

FLAGS = 0x00000008 | 0x00000200  # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP

# Start 64-bit OpenXR Host
print("Starting BLVR OpenXR host for headset...", flush=True)
host = subprocess.Popen(
    [str(HOST_EXE), "--game"],
    cwd=str(HOST_EXE.parent),
    env=env,
    creationflags=FLAGS,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
    stdin=subprocess.DEVNULL,
)
print(f"OpenXR Host started (PID {host.pid}). Waiting for session...", flush=True)

deadline = time.monotonic() + 15
ready = False
while time.monotonic() < deadline:
    if host.poll() is not None:
        raise RuntimeError(f"OpenXR host exited prematurely with code {host.returncode}")
    if host_log.exists():
        content = host_log.read_text(encoding="utf-8", errors="replace")
        if any(s in content for s in ("session FOCUSED", "session VISIBLE", "session RUNNING")):
            ready = True
            break
    time.sleep(0.1)

if not ready:
    raise RuntimeError("OpenXR host session did not become ready.")

print(f"OpenXR session active in headset! Launching Brütal Legend...", flush=True)
game = subprocess.Popen(
    [str(GAME_EXE)],
    cwd=str(GAME_DIR),
    env=env,
    creationflags=FLAGS,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
    stdin=subprocess.DEVNULL,
)
print(f"Brütal Legend launched (PID {game.pid})! You should now see the game in your headset.", flush=True)
