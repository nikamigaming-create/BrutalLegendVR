import subprocess, time, ctypes
from ctypes import wintypes
user32 = ctypes.windll.user32

proc = subprocess.Popen([r"D:\SteamLibrary\steamapps\common\BrutalLegend\BrutalLegend.exe"], cwd=r"D:\SteamLibrary\steamapps\common\BrutalLegend")
time.sleep(3)

def enum_cb(hwnd, lparam):
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    if pid.value == proc.pid:
        length = user32.GetWindowTextLengthW(hwnd)
        buff = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, buff, length + 1)
        ccls = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, ccls, 256)
        is_visible = user32.IsWindowVisible(hwnd)
        print(f"Hwnd: 0x{hwnd:08x}, Class: '{ccls.value}', Title: '{buff.value}', Visible: {is_visible}")
    return True

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows(WNDENUMPROC(enum_cb), 0)

proc.kill()
