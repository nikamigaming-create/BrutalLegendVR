"""Allowlist, provenance, architecture and checksum gate for a public runtime ZIP."""
import argparse
import hashlib
from pathlib import Path
import struct
import zipfile

RETAIL = {'.rigcache', '.actionclip', '.mesh', '.rig', '.texture', '.material', '.dds',
          '.bik', '.sav', '.~h', '.~p', '.pak', '.pack', '.pdb'}
ART = {'assets/room/basalt.png', 'assets/room/iron-medallion.png',
       'assets/ui/default-controls.png', 'assets/ui/vr-controls-frame.png'}
ROOT_FILES = {'Play VR.cmd', 'Setup VR.cmd', 'Remap Controls.cmd', 'Uninstall VR.cmd',
              'controls.ini', 'README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md'}


def audit(stage):
    files = sorted(p for p in stage.rglob('*') if p.is_file())
    assert files, 'Empty package'
    for path in files:
        relative = path.relative_to(stage).as_posix()
        assert path.resolve().is_relative_to(stage.resolve()), f'Escaped path: {relative}'
        assert not path.is_symlink(), f'Symlink: {relative}'
        assert path.suffix.lower() not in RETAIL, f'Retail resource/debug data: {relative}'
        assert not relative.startswith(('artifacts/', 'build/', 'third_party/')), f'Private/source tree: {relative}'
        assert relative in ROOT_FILES or relative.startswith(('bin/', 'tools/', 'scripts/', 'docs/', 'assets/', 'licenses/')), f'Unexpected file: {relative}'
        assert path.suffix.lower() not in {'.log', '.csv'} and path.name != 'settings.json', f'Local session data: {relative}'
        if relative.startswith('assets/') and path.suffix.lower() == '.png':
            assert relative in ART, f'Unreviewed illustration: {relative}'
        if path.suffix.lower() in {'.exe', '.dll', '.pyd'}:
            data = path.read_bytes()
            assert data[:2] == b'MZ', f'Invalid PE: {relative}'
            pe = struct.unpack_from('<I', data, 0x3c)[0]
            assert data[pe:pe+4] == b'PE\0\0', f'Invalid PE signature: {relative}'
            machine = struct.unpack_from('<H', data, pe+4)[0]
            expected = 0x14c if relative.startswith('bin/') else 0x8664
            assert machine == expected, f'Wrong architecture: {relative} ({machine:x})'
            if path.suffix.lower() == '.exe':
                assert relative in {'tools/blvr_xr_host.exe', 'tools/blvr_setup.exe'}, f'Unexpected executable: {relative}'
    required = ROOT_FILES | ART | {'bin/d3d9.dll', 'bin/openxr_loader.dll',
        'tools/blvr_xr_host.exe', 'tools/blvr_setup.exe', 'tools/_internal/python311.dll',
        'assets/ui/controls-defaults.ini', 'licenses/MinHook.txt', 'licenses/OpenXR.txt',
        'licenses/JsonCpp.txt', 'licenses/Python.txt', 'licenses/Pillow.txt',
        'licenses/OpenSSL.txt', 'licenses/defusedxml.txt',
        'licenses/PyInstaller.txt', 'licenses/Tcl-Tk.txt', 'licenses/NumPy/LICENSE.txt',
        'scripts/launch_vr.ps1', 'scripts/deploy.ps1', 'scripts/uninstall.ps1', 'scripts/common.ps1'}
    available = {p.relative_to(stage).as_posix() for p in files}
    assert required <= available, f'Missing package files: {sorted(required-available)}'
    if 'bin/dxvk_d3d9.dll' in available:
        assert 'licenses/DXVK.txt' in available
        assert hashlib.sha256((stage/'bin/dxvk_d3d9.dll').read_bytes()).hexdigest() == '44a2e749694128710cce3a545c954bd9d986dd6a8ea61bb08d931c2ef0190488'
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    stage = args.stage.resolve()
    files = audit(stage)
    sums = ''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(stage).as_posix()}\n' for p in files)
    (stage/'SHA256SUMS.txt').write_text(sums, encoding='utf-8')
    archive = args.output / f'BrutalLegendVR-{args.version}-windows.zip'
    prefix = f'BrutalLegendVR-{args.version}'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as bundle:
        for path in files + [stage/'SHA256SUMS.txt']:
            bundle.write(path, prefix+'/'+path.relative_to(stage).as_posix())
    with zipfile.ZipFile(archive) as bundle:
        assert bundle.testzip() is None, 'ZIP integrity failure'
        for path in files:
            relative = path.relative_to(stage).as_posix()
            assert bundle.read(prefix+'/'+relative) == path.read_bytes(), f'ZIP mismatch: {relative}'
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix('.zip.sha256').write_text(f'{digest}  {archive.name}\n', encoding='ascii')
    print(f'PASS: {len(files)} runtime files, correct PE architectures, no owned game resources or session data')
    print(f'ZIP: {archive} ({archive.stat().st_size:,} bytes)')
    print(f'SHA256: {digest}')


if __name__ == '__main__':
    main()
