import subprocess, time, os

env = os.environ.copy()
env["BLVR_SIM"] = "1"
proc = subprocess.Popen([r"D:\SteamLibrary\steamapps\common\BrutalLegend\BrutalLegend.exe"], cwd=r"D:\SteamLibrary\steamapps\common\BrutalLegend", env=env)
time.sleep(12)
proc.terminate()
try:
    proc.wait(timeout=3)
except Exception:
    proc.kill()
subprocess.run(["taskkill", "/F", "/IM", "BrutalLegend.exe"], capture_output=True)
