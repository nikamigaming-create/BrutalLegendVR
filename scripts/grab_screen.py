import subprocess, time, ctypes
from ctypes import wintypes
from PIL import ImageGrab

user32 = ctypes.windll.user32

proc = subprocess.Popen([r"D:\SteamLibrary\steamapps\common\BrutalLegend\BrutalLegend.exe"], cwd=r"D:\SteamLibrary\steamapps\common\BrutalLegend")
time.sleep(4)

hwnd = user32.FindWindowA(b"DFAppClass", None)
print(f"Game window hwnd: 0x{hwnd:08x}")
if hwnd:
    user32.SetForegroundWindow(hwnd)
    time.sleep(1)
    rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    bbox = (rect.left, rect.top, rect.right, rect.bottom)
    img = ImageGrab.grab(bbox)
    img.save(r"D:\code\blvr\artifacts\current_screen.png")
    print(f"Saved screenshot to current_screen.png! Size: {img.size}")

proc.kill()
