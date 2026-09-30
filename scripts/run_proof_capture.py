import os
import sys
import time
import subprocess

GAME_DIR = r"D:\SteamLibrary\steamapps\common\BrutalLegend"
EXE_PATH = os.path.join(GAME_DIR, "BrutalLegend.exe")
LOG_PATH = os.path.join(GAME_DIR, "blvr.log")
VIDEO_PATH = r"D:\code\blvr\artifacts\blvr-sim-camera-proof.mp4"
TEMP_VIDEO_PATH = r"D:\code\blvr\artifacts\blvr_temp_video.mp4"
WAV_PATH = r"D:\code\blvr\artifacts\blvr_audio.wav"
FFMPEG = r"C:\Users\nbrys\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-8.1.1-full_build\bin\ffmpeg.exe"
FNVVR_ROOT = r"D:\code\fnvvr"
SIM_INPUT_SCRIPT = os.path.join(FNVVR_ROOT, "scripts", "invoke-openxr-simulator-combat-demo.ps1")
SIM_MENU_SCRIPT = os.path.join(os.path.dirname(__file__), "pulse-simulator-menu.ps1")
SIM_DATA_DIR = os.environ.get(
    "BLVR_SIM_DATA_DIR",
    os.path.join(os.path.dirname(VIDEO_PATH), "fnvxr_meta_simulator_data"),
)

env = os.environ.copy()
bridge_mode = env.get("BLVR_FNVXR_BRIDGE", "").lower() in ("1", "true")
legacy_motion = env.get("BLVR_LEGACY_MOTION", "0").lower() in ("1", "true")
if bridge_mode:
    env.pop("BLVR_SIM", None)
else:
    env["BLVR_SIM"] = "1"
env["BLVR_WINDOWED"] = "1"
env["BLVR_NOFOCUS_PAUSE"] = "1"

print("=" * 60)
print("Starting BLVR Proof Capture Runner")
print(f"Exe: {EXE_PATH}")
print(f"Log: {LOG_PATH}")
print(f"Video target: {VIDEO_PATH}")
print(f"Pose source: {'FNVVR/Meta OpenXR bridge' if bridge_mode else 'BLVR internal SIM'}")
print("=" * 60)

for old_path in [
    VIDEO_PATH,
    TEMP_VIDEO_PATH,
    WAV_PATH,
    *(os.path.join(os.path.dirname(VIDEO_PATH), name) for name in (
        "proof_01s.png", "proof_02s.png", "proof_03s.png", "proof_05s.png",
        "proof_07s.png", "proof_08s.png", "proof_10s.png", "proof_11s.png",
        "proof_13s.png", "proof_15s.png", "proof_16s.png", "proof_18s.png", "proof_19s.png")),
]:
    if os.path.exists(old_path):
        try:
            os.remove(old_path)
            print(f"Removed old artifact: {old_path}")
        except Exception as e:
            print(f"Warning: could not remove old artifact {old_path}: {e}")

SIM_FLAG_PATH = os.path.join(GAME_DIR, "blvr_sim.txt")
try:
    with open(LOG_PATH, "w") as f:
        f.write("")
    if not bridge_mode:
        with open(SIM_FLAG_PATH, "w") as f:
            f.write("1")
except Exception:
    pass

try:
    proc = subprocess.Popen([EXE_PATH], cwd=GAME_DIR, env=env)
    print(f"Launched BrutalLegend.exe (PID: {proc.pid})")
except Exception as e:
    if os.path.exists(SIM_FLAG_PATH):
        os.remove(SIM_FLAG_PATH)
    raise e

log_pos = 0
start_time = time.time()
max_duration = 160.0
finished = False
last_advance_pulse = 0.0
gameplay_seen = False
sim_motion = None
sim_motion_log = None
title_menu = None
title_menu_log = None

while time.time() - start_time < max_duration:
    time.sleep(1.5)

    running = False
    if proc.poll() is None:
        running = True
    else:
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq BrutalLegend.exe"], capture_output=True, text=True).stdout
        if "BrutalLegend.exe" in out:
            running = True

    if not running:
        print("BrutalLegend is no longer running.")
        break

    # Advance the actual title quad through the existing FNVVR controller IPC.
    # The right Menu action is translated by the host to retail XInput START;
    # no desktop focus, key injection, mouse injection, or window activation.
    if (bridge_mode and title_menu is None and not gameplay_seen and
            time.time() - start_time >= 3.0 and os.path.isfile(SIM_MENU_SCRIPT)):
        os.makedirs(SIM_DATA_DIR, exist_ok=True)
        title_menu_log = open(
            os.path.join(os.path.dirname(VIDEO_PATH), "blvr_title_menu.log"),
            "w", encoding="utf-8")
        title_menu = subprocess.Popen(
            [
                "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
                "-File", SIM_MENU_SCRIPT,
                "-DataDirectory", SIM_DATA_DIR,
                "-Hand", "right",
            ],
            cwd=FNVVR_ROOT,
            stdout=title_menu_log,
            stderr=subprocess.STDOUT,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        print(f"[BLVR] started FNVVR right-controller Menu pulse PID={title_menu.pid}")

    if os.path.exists(LOG_PATH):
        try:
            with open(LOG_PATH, "r", encoding="utf-8", errors="ignore") as f:
                f.seek(log_pos)
                new_data = f.read()
                log_pos = f.tell()
                if new_data:
                    for line in new_data.strip().split("\n"):
                        if line.strip():
                            print(f"[BLVR] {line.strip()}")
                            if "CameraActive=1" in line or "Triggering VideoCapture" in line:
                                gameplay_seen = True
        except Exception:
            pass

    # Reuse FNVVR's bounded, acknowledged Meta simulator IPC sequence. It
    # owns only per-run pose/controller files; it never touches a window,
    # focus, keyboard, mouse, or simulator UI.
    if (legacy_motion and gameplay_seen and sim_motion is None and
            os.path.isfile(SIM_INPUT_SCRIPT)):
        os.makedirs(SIM_DATA_DIR, exist_ok=True)
        sim_motion_log = open(
            os.path.join(os.path.dirname(VIDEO_PATH), "blvr_sim_motion.log"),
            "w", encoding="utf-8")
        sim_motion = subprocess.Popen(
            [
                "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
                "-File", SIM_INPUT_SCRIPT,
                "-DataDirectory", SIM_DATA_DIR,
                "-ShotsToEmpty", "10",
                "-ShotsAfterReload", "2",
                "-UpdatesPerSecond", "12",
            ],
            cwd=FNVVR_ROOT,
            stdout=sim_motion_log,
            stderr=subprocess.STDOUT,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        print(f"[BLVR] started FNVVR Meta simulator IPC sequence PID={sim_motion.pid}")

    if os.path.exists(VIDEO_PATH):
        size = os.path.getsize(VIDEO_PATH)
        if size > 100000:
            try:
                with open(LOG_PATH, "r", encoding="utf-8", errors="ignore") as f:
                    content = f.read()
                    if "Final video with audio finalized" in content or "Video finalized!" in content:
                        print(f"\nSUCCESS: Video capture and audio muxing completed! Size: {size} bytes ({size / (1024*1024):.2f} MB)")
                        finished = True
                        break
            except Exception:
                pass

if not finished:
    print("Stopping capture after timeout...")

keep_running = finished and env.get("BLVR_KEEP_RUNNING", "0") == "1"
if not keep_running:
    try:
        proc.terminate()
        proc.wait(timeout=3)
    except Exception:
        try:
            proc.kill()
        except Exception:
            pass
    subprocess.run(["taskkill", "/F", "/IM", "BrutalLegend.exe"], capture_output=True)
else:
    print(f"Live game left running, PID={proc.pid}")

if sim_motion is not None:
    if sim_motion.poll() is None:
        sim_motion.terminate()
        try:
            sim_motion.wait(timeout=3)
        except subprocess.TimeoutExpired:
            sim_motion.kill()
    if sim_motion_log is not None:
        sim_motion_log.close()

if title_menu is not None:
    if title_menu.poll() is None:
        title_menu.terminate()
        try:
            title_menu.wait(timeout=3)
        except subprocess.TimeoutExpired:
            title_menu.kill()
    if title_menu_log is not None:
        title_menu_log.close()

if os.path.exists(LOG_PATH):
    try:
        with open(LOG_PATH, "r", encoding="utf-8", errors="ignore") as f:
            f.seek(log_pos)
            tail = f.read()
            if tail:
                for line in tail.strip().split("\n"):
                    if line.strip():
                        print(f"[BLVR-FINAL] {line.strip()}")
    except Exception:
        pass

# Fallback mux if game exited before completing mux
if not os.path.exists(VIDEO_PATH) and os.path.exists(TEMP_VIDEO_PATH):
    if os.path.exists(WAV_PATH) and os.path.getsize(WAV_PATH) > 1000:
        print("Muxing audio and video in runner fallback...")
        subprocess.run([FFMPEG, "-y", "-i", TEMP_VIDEO_PATH, "-i", WAV_PATH, "-c:v", "copy", "-c:a", "aac", "-b:a", "192k", "-shortest", VIDEO_PATH], capture_output=True)
    else:
        os.rename(TEMP_VIDEO_PATH, VIDEO_PATH)

if os.path.exists(VIDEO_PATH):
    size = os.path.getsize(VIDEO_PATH)
    print(f"\nFinal Video Artifact: {VIDEO_PATH} ({size / (1024*1024):.2f} MB)")
    
    # Check audio stream with ffprobe
    probe = subprocess.run([FFMPEG, "-i", VIDEO_PATH], capture_output=True, text=True)
    print(f"Video Streams Info:\n{'Audio stream present' if 'Audio:' in probe.stderr else 'No audio stream found'}")

    for t, out_name in [
        (2, "proof_02s.png"),
        (5, "proof_05s.png"),
        (7, "proof_07s.png"),
        (10, "proof_10s.png"),
        (13, "proof_13s.png"),
        (16, "proof_16s.png"),
        (19, "proof_19s.png"),
    ]:
        out_frame = os.path.join(r"D:\code\blvr\artifacts", out_name)
        cmd = [FFMPEG, "-y", "-ss", str(t), "-i", VIDEO_PATH, "-vframes", "1", "-q:v", "2", out_frame]
        subprocess.run(cmd, capture_output=True)
        if os.path.exists(out_frame):
            print(f"Extracted frame at {t}s -> {out_frame} ({os.path.getsize(out_frame)} bytes)")
if os.path.exists(SIM_FLAG_PATH):
    try:
        os.remove(SIM_FLAG_PATH)
    except Exception:
        pass

sys.exit(0 if os.path.exists(VIDEO_PATH) else 1)
