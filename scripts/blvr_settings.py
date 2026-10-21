"""Persistent launcher settings; no registry or system-runtime changes."""
import json
import os
from pathlib import Path
import struct

DEFAULTS = {
    'runtime_json': '', 'render_resolution': 1536, 'frame_limit_fps': 90,
    'edge_aa': True, 'telemetry': False, 'performance_logging': False,
    'guitar_height_cm': 53, 'guitar_distance_cm': 38,
    'guitar_angle_degrees': 35, 'guitar_face_degrees': 50, 'guitar_volume': 65,
}
LIMITS = {
    'render_resolution': (720, 2048), 'frame_limit_fps': (45, 144),
    'guitar_height_cm': (25, 85), 'guitar_distance_cm': (20, 70),
    'guitar_angle_degrees': (0, 70), 'guitar_face_degrees': (0, 80), 'guitar_volume': (0, 100),
}


def load(root):
    try:
        saved = json.loads((Path(root) / 'settings.json').read_text(encoding='utf-8-sig'))
        if not isinstance(saved, dict):
            raise ValueError('Settings must be an object.')
    except (OSError, ValueError, UnicodeError):
        saved = {}
    result = {**DEFAULTS, **saved}
    for key, (minimum, maximum) in LIMITS.items():
        value = result[key]
        if type(value) is not int or not minimum <= value <= maximum:
            result[key] = DEFAULTS[key]
    for key in ('edge_aa', 'telemetry', 'performance_logging'):
        if type(result[key]) is not bool:
            result[key] = DEFAULTS[key]
    if not isinstance(result['runtime_json'], str):
        result['runtime_json'] = ''
    return result


def validate_runtime(path):
    path = Path(path).resolve()
    try:
        data = json.loads(path.read_text(encoding='utf-8-sig'))
        library = Path(data['runtime']['library_path'])
        if not library.is_absolute():
            library = path.parent / library
        with library.open('rb') as stream:
            header = stream.read(64)
            if len(header) != 64 or header[:2] != b'MZ':
                raise ValueError('The runtime library is not a Windows DLL.')
            stream.seek(struct.unpack_from('<I', header, 60)[0])
            signature = stream.read(6)
            if signature[:4] != b'PE\0\0' or len(signature) != 6 or struct.unpack_from('<H', signature, 4)[0] != 0x8664:
                raise ValueError('Choose the 64-bit OpenXR runtime; the VR host is 64-bit.')
    except (OSError, KeyError, TypeError, ValueError) as error:
        raise ValueError(f'OpenXR runtime is unavailable or invalid: {path}\n{error}') from error
    return str(path)


def runtimes():
    candidates = [os.environ.get('XR_RUNTIME_JSON', '')]
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Khronos\OpenXR\1',
                            0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as handle:
            candidates.append(winreg.QueryValueEx(handle, 'ActiveRuntime')[0])
    except (ImportError, OSError):
        pass
    program_files = Path(os.environ.get('ProgramW6432') or os.environ.get('ProgramFiles', 'C:/Program Files'))
    candidates.extend(str(program_files / relative) for relative in (
        'Virtual Desktop Streamer/OpenXR/virtualdesktop-openxr.json',
        'Oculus/Support/oculus-runtime/oculus_openxr_64.json',
    ))
    candidates.append(str(Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) /
                          'Steam/steamapps/common/SteamVR/steamxr_win64.json'))
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Khronos\OpenXR\1\AvailableRuntimes',
                            0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as handle:
            index = 0
            while True:
                try:
                    path, disabled, _ = winreg.EnumValue(handle, index)
                except OSError:
                    break
                if disabled == 0:
                    candidates.append(path)
                index += 1
    except (NameError, ImportError, OSError):
        pass
    found = []
    seen = set()
    for candidate in candidates:
        if candidate and candidate.casefold() not in seen and Path(candidate).is_file():
            found.append(candidate)
            seen.add(candidate.casefold())
    return found


def runtime_label(path):
    name = Path(path).name.lower()
    if name == 'virtualdesktop-openxr.json':
        return 'Virtual Desktop (VDXR)'
    if name == 'steamxr_win64.json':
        return 'SteamVR'
    if name == 'oculus_openxr_64.json':
        return 'Meta Quest Link / Air Link'
    return 'OpenXR runtime'


def save(root, values):
    root = Path(root)
    result = load(root)
    for key in DEFAULTS:
        value = values.get(key, result[key])
        if key in LIMITS:
            if isinstance(value, bool):
                raise ValueError(f'Invalid {key}.')
            number = int(value)
            if str(value).strip() != str(number):
                raise ValueError(f'Invalid {key}.')
            minimum, maximum = LIMITS[key]
            if not minimum <= number <= maximum:
                raise ValueError(f'{key.replace("_", " ").capitalize()} must be between {minimum} and {maximum}.')
            value = number
        elif key == 'runtime_json':
            value = value.strip()
            if value:
                value = validate_runtime(value)
        elif type(value) is not bool:
            raise ValueError(f'Invalid {key}.')
        result[key] = value
    temporary = root / f'settings.{os.getpid()}.tmp'
    try:
        temporary.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        os.replace(temporary, root / 'settings.json')
    finally:
        temporary.unlink(missing_ok=True)
    return result
