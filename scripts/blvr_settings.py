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
    candidates = [str(Path(os.environ.get('ProgramFiles', 'C:/Program Files')) /
                      'Oculus/Support/oculus-runtime/oculus_openxr_64.json')]
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Khronos\OpenXR\1',
                            0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as handle:
            candidates.append(winreg.QueryValueEx(handle, 'ActiveRuntime')[0])
    except (ImportError, OSError):
        pass
    found = []
    for candidate in candidates:
        if candidate and candidate not in found and Path(candidate).is_file():
            found.append(candidate)
    return found


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
