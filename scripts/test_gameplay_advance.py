import subprocess, time, ctypes

user32 = ctypes.windll.user32
VK_RETURN = 0x0D
VK_SPACE = 0x20

print("Launching BrutalLegend...")
proc = subprocess.Popen([r"D:\SteamLibrary\steamapps\common\BrutalLegend\BrutalLegend.exe"], cwd=r"D:\SteamLibrary\steamapps\common\BrutalLegend")

for i in range(30):
    time.sleep(1)
    if proc.poll() is not None:
        print(f"Process exited early at second {i}!")
        break
    
    hwnd = user32.FindWindowA(b"DFAppClass", None)
    if hwnd:
        user32.SetForegroundWindow(hwnd)
        user32.SetActiveWindow(hwnd)
        # Send ENTER
        user32.keybd_event(VK_RETURN, 0x1C, 0, 0)
        time.sleep(0.05)
        user32.keybd_event(VK_RETURN, 0x1C, 2, 0)
        time.sleep(0.1)
        # Send SPACE
        user32.keybd_event(VK_SPACE, 0x39, 0, 0)
        time.sleep(0.05)
        user32.keybd_event(VK_SPACE, 0x39, 2, 0)

    # Check log for camera active
    try:
        with open(r"D:\SteamLibrary\steamapps\common\BrutalLegend\blvr.log", "r") as f:
            lines = f.readlines()
            for line in lines[-5:]:
                if "CameraActive=1" in line:
                    print(f"CAMERA ACTIVE DETECTED at second {i}! Line: {line.strip()}")
                if "Video finalized" in line:
                    print(f"VIDEO FINALIZED DETECTED at second {i}! Line: {line.strip()}")
    except:
        pass

if proc.poll() is None:
    print("Stopping process...")
    proc.kill()
