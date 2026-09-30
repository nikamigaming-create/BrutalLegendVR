import subprocess, time, ctypes
from ctypes import wintypes
user32 = ctypes.windll.user32

proc = subprocess.Popen([r"D:\SteamLibrary\steamapps\common\BrutalLegend\BrutalLegend.exe"], cwd=r"D:\SteamLibrary\steamapps\common\BrutalLegend")
time.sleep(3)

hwnd = user32.FindWindowA(b"DFAppClass", None)
print(f"Found DFAppClass hwnd: 0x{hwnd:08x}")

if hwnd:
    user32.SetForegroundWindow(hwnd)
    user32.SetActiveWindow(hwnd)
    time.sleep(0.5)
    # Send Enter (VK_RETURN = 0x0D)
    user32.keybd_event(0x0D, 0x1C, 0, 0)
    time.sleep(0.05)
    user32.keybd_event(0x0D, 0x1C, 2, 0)
    print("Sent ENTER to DFAppClass!")

time.sleep(2)
proc.kill()
