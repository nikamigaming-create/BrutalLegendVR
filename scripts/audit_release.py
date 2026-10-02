"""Allowlist, provenance, architecture and checksum gate for a public runtime ZIP."""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import zipfile

RETAIL = {'.rigcache', '.actionclip', '.mesh', '.rig', '.texture', '.material', '.dds',
          '.rgba', '.bik', '.sav', '.~h', '.~p', '.pak', '.pack', '.pdb', '.lua',
          '.mp4', '.avi', '.mov', '.mkv', '.webm', '.wav', '.mp3', '.ogg', '.flac',
          '.ppm', '.jpg', '.jpeg', '.webp', '.log', '.csv', '.json', '.jsonl'}
ART = {'assets/room/basalt.png', 'assets/room/iron-medallion.png',
       'assets/ui/default-controls.png', 'assets/ui/vr-controls-frame.png'}
ASSETS = ART | {'assets/room/basalt-prompt.txt', 'assets/room/generation-prompt.txt',
                'assets/room/README.md', 'assets/ui/controls-defaults.ini',
                'assets/ui/generation-prompt.txt', 'assets/ui/README.md'}
ROOT_FILES = {'Play VR.cmd', 'Setup VR.cmd', 'Remap Controls.cmd', 'Uninstall VR.cmd',
              'controls.ini', 'README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md'}
SCRIPTS = {'scripts/launch_vr.ps1', 'scripts/deploy.ps1', 'scripts/uninstall.ps1',
           'scripts/common.ps1'}
DOCS = {'docs/VR_CONTROLS.md', 'docs/PLAY_VR.md', 'docs/QUEST_HINTS_2026-09-28.md',
        'docs/RELEASE_0.1.0-preview.1.md'}
LICENSES = {'licenses/' + name for name in (
    'MinHook.txt', 'OpenXR.txt', 'JsonCpp.txt', 'DXVK.txt', 'Python.txt', 'Pillow.txt',
    'OpenSSL.txt', 'defusedxml.txt', 'PyInstaller.txt', 'Tcl-Tk.txt', 'PyYAML.txt',
    'LibYAML.txt',
    'NumPy/LICENSE.txt',
    'NumPy/numpy/_core/include/numpy/libdivide/LICENSE.txt',
    'NumPy/numpy/_core/src/common/pythoncapi-compat/COPYING',
    'NumPy/numpy/_core/src/highway/LICENSE',
    'NumPy/numpy/_core/src/multiarray/dragon4_LICENSE.txt',
    'NumPy/numpy/_core/src/npysort/x86-simd-sort/LICENSE.md',
    'NumPy/numpy/_core/src/umath/svml/LICENSE',
    'NumPy/numpy/fft/pocketfft/LICENSE.md', 'NumPy/numpy/linalg/lapack_lite/LICENSE.txt',
    'NumPy/numpy/ma/LICENSE', 'NumPy/numpy/random/LICENSE.md',
    'NumPy/numpy/random/src/distributions/LICENSE.md',
    'NumPy/numpy/random/src/mt19937/LICENSE.md', 'NumPy/numpy/random/src/pcg64/LICENSE.md',
    'NumPy/numpy/random/src/philox/LICENSE.md', 'NumPy/numpy/random/src/sfc64/LICENSE.md',
    'NumPy/numpy/random/src/splitmix64/LICENSE.md')}
RUNTIME = {'bin/d3d9.dll', 'bin/openxr_loader.dll', 'tools/blvr_xr_host.exe',
           'tools/blvr_setup.exe'}
OPTIONAL_RUNTIME = {'bin/dxvk_d3d9.dll'}
# Only the reviewed PyInstaller dependency collection may use this subtree.
# Executables, private session/retail formats, and unreviewed images are still
# refused there. Versioned dependency directories are pinned to this release.
INTERNAL_DIRS = {'_tcl_data', '_tk_data', 'tcl8', 'numpy', 'numpy.libs',
                 'numpy-2.4.6.dist-info', 'PIL', 'yaml'}
INTERNAL_ROOT = set(('''
_asyncio.pyd _bz2.pyd _ctypes.pyd _decimal.pyd _elementtree.pyd _hashlib.pyd
_lzma.pyd _multiprocessing.pyd _overlapped.pyd _queue.pyd _socket.pyd _ssl.pyd
_tkinter.pyd pyexpat.pyd select.pyd unicodedata.pyd base_library.zip python311.dll
libcrypto-3.dll libffi-8.dll libssl-3.dll tcl86t.dll tk86t.dll ucrtbase.dll
VCRUNTIME140.dll VCRUNTIME140_1.dll
''').split())
API_SET_DLL = re.compile(r'api-ms-win-(?:core|crt)-[a-z0-9-]+-l\d+-\d+-\d+\.dll\Z')


class ReleaseAuditError(ValueError):
    pass


def require(condition, message):
    # Packaging gates must remain effective under python -O.
    if not condition:
        raise ReleaseAuditError(message)


def manifest(version):
    require(bool(re.fullmatch(r'[0-9A-Za-z][0-9A-Za-z._-]*', version)), 'Invalid release version')
    return {'rootFiles': sorted(ROOT_FILES), 'assetFiles': sorted(ASSETS),
            'scriptFiles': sorted(SCRIPTS), 'licenseFiles': sorted(LICENSES),
            'docFiles': sorted(DOCS | {f'docs/RELEASE_{version}.md'})}


def pe_machine(data, relative):
    require(len(data) >= 64 and data[:2] == b'MZ', f'Invalid PE: {relative}')
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    require(offset <= len(data) - 6 and data[offset:offset+4] == b'PE\0\0',
            f'Invalid PE signature: {relative}')
    return struct.unpack_from('<H', data, offset+4)[0]


def internal_dependency(relative):
    prefix = 'tools/_internal/'
    if not relative.startswith(prefix):
        return False
    tail = relative[len(prefix):]
    parts = tail.split('/')
    return (len(parts) > 1 and parts[0] in INTERNAL_DIRS or
            len(parts) == 1 and (tail in INTERNAL_ROOT or bool(API_SET_DLL.fullmatch(tail))))


def check_private_format(relative):
    path = Path(relative)
    require(not {'artifacts', 'retail-private', 'release-work', 'build', '.git'} &
            {part.lower() for part in relative.split('/')}, f'Private/source tree: {relative}')
    require(path.suffix.lower() not in RETAIL, f'Owned resource or session data: {relative}')
    require(path.name.lower() != 'settings.json', f'Local settings: {relative}')
    if path.suffix.lower() == '.png':
        require(relative in ART, f'Unreviewed illustration: {relative}')


def check_python_library_zip(path, relative):
    # A dependency ZIP must not bypass the outer owned-data/session gate.
    require(relative == 'tools/_internal/base_library.zip', f'Unreviewed archive: {relative}')
    with zipfile.ZipFile(path) as archive:
        for member in archive.infolist():
            name = member.filename
            require(not name.startswith(('/', '\\')) and '\\' not in name and
                    ':' not in name and '..' not in name.split('/'), f'Unsafe dependency ZIP entry: {name}')
            if member.is_dir():
                continue
            check_private_format(name)
            require(Path(name).suffix.lower() == '.pyc', f'Unexpected Python library ZIP entry: {name}')


def audit(stage, version='0.1.0-preview.1'):
    groups = manifest(version)
    stage = Path(stage).resolve()
    files = sorted(p for p in stage.rglob('*') if p.is_file())
    require(bool(files), 'Empty package')
    allowed = RUNTIME | OPTIONAL_RUNTIME | set().union(*(set(items) for items in groups.values()))
    # Reject symlinked directories too, even when their target stays in stage.
    for path in stage.rglob('*'):
        require(not path.is_symlink(), f'Symlink: {path.relative_to(stage).as_posix()}')
    for path in files:
        relative = path.relative_to(stage).as_posix()
        require(path.resolve().is_relative_to(stage), f'Escaped path: {relative}')
        check_private_format(relative)
        require(relative in allowed or internal_dependency(relative), f'Unexpected file: {relative}')
        if path.suffix.lower() == '.zip':
            check_python_library_zip(path, relative)
        if path.suffix.lower() in {'.exe', '.dll', '.pyd'}:
            if path.suffix.lower() == '.exe':
                require(relative in {'tools/blvr_xr_host.exe', 'tools/blvr_setup.exe'},
                        f'Unexpected executable: {relative}')
            machine = pe_machine(path.read_bytes(), relative)
            expected = 0x14c if relative.startswith('bin/') else 0x8664
            require(machine == expected, f'Wrong architecture: {relative} ({machine:x})')
    required = RUNTIME | set().union(*(set(items) for items in groups.values())) | {
        'tools/_internal/python311.dll', 'tools/_internal/base_library.zip'}
    available = {p.relative_to(stage).as_posix() for p in files}
    require(required <= available, f'Missing package files: {sorted(required-available)}')
    if 'bin/dxvk_d3d9.dll' in available:
        require(hashlib.sha256((stage/'bin/dxvk_d3d9.dll').read_bytes()).hexdigest() ==
                '44a2e749694128710cce3a545c954bd9d986dd6a8ea61bb08d931c2ef0190488',
                'DXVK provenance mismatch')
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path)
    parser.add_argument('--version', required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--print-manifest', action='store_true')
    args = parser.parse_args()
    if args.print_manifest:
        import json
        print(json.dumps(manifest(args.version)))
        return
    if not args.stage or not args.output:
        parser.error('--stage and --output are required when packaging')
    stage = args.stage.resolve()
    files = audit(stage, args.version)
    sums = ''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(stage).as_posix()}\n' for p in files)
    (stage/'SHA256SUMS.txt').write_text(sums, encoding='utf-8')
    archive = args.output / f'BrutalLegendVR-{args.version}-windows.zip'
    prefix = f'BrutalLegendVR-{args.version}'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as bundle:
        for path in files + [stage/'SHA256SUMS.txt']:
            bundle.write(path, prefix+'/'+path.relative_to(stage).as_posix())
    with zipfile.ZipFile(archive) as bundle:
        require(bundle.testzip() is None, 'ZIP integrity failure')
        for path in files:
            relative = path.relative_to(stage).as_posix()
            require(bundle.read(prefix+'/'+relative) == path.read_bytes(), f'ZIP mismatch: {relative}')
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix('.zip.sha256').write_text(f'{digest}  {archive.name}\n', encoding='ascii')
    print(f'PASS: {len(files)} runtime files, correct PE architectures, no owned game resources or session data')
    print(f'ZIP: {archive} ({archive.stat().st_size:,} bytes)')
    print(f'SHA256: {digest}')


if __name__ == '__main__':
    main()
