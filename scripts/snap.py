import subprocess, time, os
from PIL import ImageGrab
import ctypes
user32 = ctypes.windll.user32

env = os.environ.copy()
env["BLVR_SIM"] = "1"
proc = subprocess.Popen([r"D:\SteamLibrary\steamapps\common\BrutalLegend\BrutalLegend.exe"], cwd=r"D:\SteamLibrary\steamapps\common\BrutalLegend", env=env)
time.sleep(5)
hwnd = user32.FindWindowA(b"DFAppClass", None)
print("hwnd:", hex(hwnd) if hwnd else None)
if hwnd:
    user32.SetForegroundWindow(hwnd)
time.sleep(1)
img = ImageGrab.grab()
img.save(r"D:\code\blvr\artifacts\screen_test.png")
print("Saved screen_test.png", img.size)
proc.terminate()
try:
    proc.wait(timeout=3)
except Exception:
    proc.kill()
subprocess.run(["taskkill", "/F", "/IM", "BrutalLegend.exe"], capture_output=True)
