# Third-party notices

BrutalLegendVR's original code is licensed under MIT (LICENSE).
Retail game data is imported locally and is not part of this distribution.
The generated room, tutorial frame and default-control illustrations are
original mod artwork. Room and tutorial-frame prompts are included in assets.

| Component | Source / version | License file |
| --- | --- | --- |
| MinHook | Tsuda Kageyu, 1.3.3; commit c3fcafdc10146beb5919319d0683e44e3c30d537 | licenses/MinHook.txt |
| OpenXR loader / headers | Khronos OpenXR SDK 1.1.63, commit 3ed64d0f9bb680f24b80a085091e5c8fab38f7b7 | licenses/OpenXR.txt |
| JsonCpp inside the OpenXR loader | SDK vendored sources | licenses/JsonCpp.txt |
| DXVK x86 D3D9 backend | doitsujin/dxvk, official v3.0.2, unmodified | licenses/DXVK.txt |
| Python | CPython 3.11.9 | licenses/Python.txt |
| Python's bundled libraries | OpenSSL 3.0.13, libffi, Microsoft distributable runtime | licenses/OpenSSL.txt and licenses/Python.txt |
| NumPy and vendored components | 2.4.6 | licenses/NumPy/ |
| Pillow and vendored codecs | 12.2.0 | licenses/Pillow.txt |
| defusedxml | 0.7.1, included through Pillow's plugin imports | licenses/defusedxml.txt |
| PyYAML | 6.0.3, included in the setup dependency collection; MIT | licenses/PyYAML.txt |
| LibYAML inside PyYAML | 0.2.5; MIT | licenses/LibYAML.txt |
| Tcl / Tk | Python 3.11's Tcl/Tk 8.6 distribution | licenses/Tcl-Tk.txt |
| PyInstaller bootloader/runtime | 6.22.2; GPL with the bundled-application exception | licenses/PyInstaller.txt |

The packaged setup utility includes Python, NumPy, Pillow, PyYAML and Tcl/Tk so
players do not need a Python installation. Their supplied notices accompany
the package. The OpenXR source submodule includes upstream notices for
additional build-time dependencies.

DXVK source: https://github.com/doitsujin/dxvk/tree/v3.0.2
OpenXR source: https://github.com/KhronosGroup/OpenXR-SDK-Source
MinHook source: https://github.com/TsudaKageyu/minhook
PyYAML source: https://github.com/yaml/pyyaml/tree/6.0.3
LibYAML source: https://github.com/yaml/libyaml/tree/0.2.5
