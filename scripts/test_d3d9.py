import ctypes
from ctypes import wintypes

class D3DPRESENT_PARAMETERS(ctypes.Structure):
    _fields_ = [
        ("BackBufferWidth", ctypes.c_uint),
        ("BackBufferHeight", ctypes.c_uint),
        ("BackBufferFormat", ctypes.c_uint),
        ("BackBufferCount", ctypes.c_uint),
        ("MultiSampleType", ctypes.c_uint),
        ("MultiSampleQuality", ctypes.c_ulong),
        ("SwapEffect", ctypes.c_uint),
        ("hDeviceWindow", wintypes.HWND),
        ("Windowed", wintypes.BOOL),
        ("EnableAutoDepthStencil", wintypes.BOOL),
        ("AutoDepthStencilFormat", ctypes.c_uint),
        ("Flags", ctypes.c_ulong),
        ("FullScreen_RefreshRateInHz", ctypes.c_uint),
        ("PresentationInterval", ctypes.c_uint),
    ]

d3d9 = ctypes.windll.d3d9
user32 = ctypes.windll.user32
hwnd = user32.GetDesktopWindow()

# In 32-bit vs 64-bit python:
print("Python pointer size:", ctypes.sizeof(ctypes.c_void_p))
