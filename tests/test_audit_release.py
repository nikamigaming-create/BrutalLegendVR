"""Asset-free packaging gates; no real executables, game data or output archives."""
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('audit_release', ROOT/'scripts/audit_release.py')
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)
VERSION = '0.1.0-preview.2'


class ReleaseAuditTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.stage = Path(self.temporary.name)
        groups = release.manifest(VERSION)
        required = release.RUNTIME | set().union(*(set(paths) for paths in groups.values())) | {
            'tools/_internal/python311.dll', 'tools/_internal/base_library.zip'}
        for relative in required:
            self.write(relative)
        # All files contain plain ASCII markers. No PE or Python bytecode file
        # is written; those readers are separately exercised with in-memory data.
        self.pe = mock.patch.object(release, 'pe_machine', side_effect=lambda data, relative:
                                    0x14c if relative.startswith('bin/') else 0x8664)
        self.library = mock.patch.object(release, 'check_python_library_zip')
        self.pe.start()
        self.library.start()
        self.addCleanup(self.pe.stop)
        self.addCleanup(self.library.stop)

    def write(self, relative):
        target = self.stage/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('asset-free offline marker\n', encoding='ascii')
        return target

    def audit(self):
        return release.audit(self.stage, VERSION)

    def test_complete_reviewed_inventory_and_known_dependencies_pass(self):
        self.write('tools/_internal/numpy/_core/_multiarray_umath.pyd')
        self.write('tools/_internal/yaml/_yaml.pyd')
        self.write('tools/_internal/_tk_data/ttk/defaults.tcl')
        self.write('tools/_internal/api-ms-win-core-file-l1-1-0.dll')
        self.assertGreater(len(self.audit()), 40)

    def test_owned_formats_and_session_data_refused_inside_dependency_tree(self):
        for suffix in ('.rigcache', '.rgba', '.lua', '.actionclip', '.dds', '.mp4',
                       '.wav', '.json', '.jsonl', '.csv', '.log', '.jpg', '.png'):
            with self.subTest(suffix=suffix):
                path = self.write('tools/_internal/numpy/private'+suffix)
                with self.assertRaises(release.ReleaseAuditError):
                    self.audit()
                path.unlink()

    def test_arbitrary_files_in_previously_allowed_prefixes_are_refused(self):
        for relative in ('assets/room/retail.txt', 'licenses/research.txt',
                         'scripts/private_probe.py', 'docs/private-notes.md',
                         'tools/probe.exe', 'tools/_internal/private.txt',
                         'tools/_internal/unreviewed_package/license.txt',
                         'tools/_internal/numpy/artifacts/receipt.txt'):
            with self.subTest(relative=relative):
                path = self.write(relative)
                with self.assertRaises(release.ReleaseAuditError):
                    self.audit()
                path.unlink()

    def test_unknown_executable_refused_even_in_reviewed_dependency_subtree(self):
        self.write('tools/_internal/numpy/probe.exe')
        with self.assertRaisesRegex(release.ReleaseAuditError, 'Unexpected executable'):
            self.audit()

    def test_wrong_architecture_refused_for_game_hook_and_setup_dependency(self):
        for relative, bad_machine in (('bin/d3d9.dll', 0x8664),
                                      ('tools/_internal/python311.dll', 0x14c)):
            with self.subTest(relative=relative):
                with mock.patch.object(release, 'pe_machine', side_effect=lambda data, name:
                        bad_machine if name == relative else (0x14c if name.startswith('bin/') else 0x8664)):
                    with self.assertRaisesRegex(release.ReleaseAuditError, 'Wrong architecture'):
                        self.audit()

    def test_missing_notices_linked_docs_or_runtime_are_refused(self):
        for relative in ('licenses/PyYAML.txt', 'licenses/LibYAML.txt',
                         'docs/QUEST_HINTS_2026-09-28.md',
                         'docs/RELEASE_'+VERSION+'.md', 'tools/_internal/python311.dll',
                         'assets/ui/controls-defaults.ini'):
            with self.subTest(relative=relative):
                path = self.stage/relative
                path.unlink()
                with self.assertRaisesRegex(release.ReleaseAuditError, 'Missing package files'):
                    self.audit()
                self.write(relative)

    def test_release_version_and_optional_backend_provenance_are_checked(self):
        self.write('docs/RELEASE_0.1.0-preview.9.md')
        with self.assertRaisesRegex(release.ReleaseAuditError, 'Unexpected file'):
            self.audit()
        (self.stage/'docs/RELEASE_0.1.0-preview.9.md').unlink()
        self.write('bin/dxvk_d3d9.dll')
        with self.assertRaisesRegex(release.ReleaseAuditError, 'DXVK provenance mismatch'):
            self.audit()
        for version in ('../private', '..', '', '0.1/preview'):
            with self.subTest(version=version):
                with self.assertRaises(release.ReleaseAuditError):
                    release.manifest(version)

    def test_python_library_archive_cannot_hide_owned_or_traversal_entries(self):
        self.library.stop()
        with mock.patch.object(release.zipfile, 'ZipFile') as archive:
            entries = archive.return_value.__enter__.return_value
            entries.infolist.return_value = [zipfile.ZipInfo('collections/__init__.pyc')]
            release.check_python_library_zip(self.stage/'unused', 'tools/_internal/base_library.zip')
            for name in ('private.lua', 'private.rgba', 'recording.mp4', '../os.pyc',
                         '/os.pyc', 'C:/os.pyc', 'artifacts/private.pyc', 'research.txt'):
                with self.subTest(name=name):
                    entries.infolist.return_value = [zipfile.ZipInfo(name)]
                    with self.assertRaises(release.ReleaseAuditError):
                        release.check_python_library_zip(self.stage/'unused', 'tools/_internal/base_library.zip')

    def test_pe_reader_checks_bounded_signature_without_real_binaries(self):
        self.pe.stop()
        with self.assertRaises(release.ReleaseAuditError):
            release.pe_machine(b'offline text', 'memory only')
        data = bytearray(80)
        data[:2] = b'MZ'
        struct.pack_into('<I', data, 0x3c, 64)
        data[64:68] = b'PE\0\0'
        struct.pack_into('<H', data, 68, 0x8664)
        self.assertEqual(release.pe_machine(data, 'memory only'), 0x8664)
        struct.pack_into('<I', data, 0x3c, 0xffffffff)
        with self.assertRaises(release.ReleaseAuditError):
            release.pe_machine(data, 'memory only')

    def test_gate_remains_active_with_python_optimization(self):
        code = (f'import runpy; m=runpy.run_path({str(ROOT/"scripts/audit_release.py")!r});'
                'm["require"](False,"optimization must not bypass release gates")')
        result = subprocess.run([sys.executable, '-O', '-c', code], capture_output=True,
                                text=True, env={**os.environ, 'PYTHONDONTWRITEBYTECODE': '1'})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('optimization must not bypass release gates', result.stderr)


if __name__ == '__main__':
    unittest.main()
