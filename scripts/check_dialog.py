import subprocess, time, ctypes
from ctypes import wintypes

user32 = ctypes.windll.user32

proc = subprocess.Popen([r'D:\SteamLibrary\steamapps\common\BrutalLegend\BrutalLegend.exe'], cwd=r'D:\SteamLibrary\steamapps\common\BrutalLegend')

time.sleep(2)
def enum_cb(hwnd, lparam):
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    if pid.value == proc.pid:
        length = user32.GetWindowTextLengthW(hwnd)
        buff = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, buff, length + 1)
        if buff.value == 'Video Card Below Minimum Spec':
            print('Found dialog:', hwnd)
            def child_cb(chwnd, clparam):
                clength = user32.GetWindowTextLengthW(chwnd)
                cbuff = ctypes.create_unicode_buffer(clength + 1)
                user32.GetWindowTextW(chwnd, cbuff, clength + 1)
                ccls = ctypes.create_unicode_buffer(256)
                user32.GetClassNameW(chwnd, ccls, 256)
                print(f'  Child {chwnd}: {ccls.value} -> "{cbuff.value}"')
                return True
            WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
            user32.EnumChildWindows(hwnd, WNDENUMPROC(child_cb), 0)
    return True

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows(WNDENUMPROC(enum_cb), 0)

proc.kill()
