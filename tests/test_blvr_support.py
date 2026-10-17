"""Troubleshooting survives failed launches and exports only bounded known files."""
import json
import logging
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import blvr_setup as setup
import blvr_support as support


class SupportTest(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.logs = self.root / 'logs'; self.logs.mkdir()
        self.patch = mock.patch.object(support, 'logs_dir', return_value=self.logs)
        self.patch.start(); self.addCleanup(self.patch.stop)
        self.logger = logging.getLogger('blvr_setup')
        self.close_logging()
        self.addCleanup(self.close_logging)

    def close_logging(self):
        for handler in self.logger.handlers[:]:
            handler.close(); self.logger.removeHandler(handler)

    def saved_logs(self):
        return '\n'.join(p.read_text(encoding='utf-8') for p in self.logs.glob('launcher-*.log'))

    def test_nonzero_launch_and_timeout_keep_both_output_streams(self):
        with mock.patch.object(setup, 'validate_game', return_value=self.root), mock.patch.dict(os.environ, {'SystemRoot': 'C:/Windows'}):
            with mock.patch.object(setup.subprocess, 'run', return_value=SimpleNamespace(
                    returncode=7, stdout='runtime check failed', stderr='runtime DLL unavailable')):
                code, detail = setup.run_launcher(self.root, 'game', True)
            self.assertEqual(code, 7)
            self.assertIn('runtime DLL unavailable', detail)
            self.assertIn('Launcher exit code: 7', self.saved_logs())
            self.assertIn('runtime check failed', self.saved_logs())
            with mock.patch.object(setup.subprocess, 'run', side_effect=subprocess.TimeoutExpired(
                    'powershell', 420, output=b'last progress before timeout', stderr=b'last error')):
                with self.assertRaises(subprocess.TimeoutExpired):
                    setup.run_launcher(self.root, 'game')
            self.assertIn('last progress before timeout', self.saved_logs())
            self.assertIn('last error', self.saved_logs())

    def test_export_missing_files_log_tails_and_no_game_assets(self):
        game = self.root / 'game'; game.mkdir()
        (game / 'blvr.log').write_bytes(b'old lines\n' * 40 + b'FINAL ERROR')
        (game / 'BrutalLegend.exe').write_bytes(b'executable identity only')
        (game / 'save.sav').write_text('private save')
        (game / 'retail.pak').write_text('private owned assets')
        (self.root / 'settings.json').write_text('{"runtime_json":"missing runtime.json"}')
        (self.logs / 'installer-latest.log').write_text('failed install')
        (self.logs / 'unrelated.log').write_text('never include arbitrary logs')
        destination = self.root / 'support.zip'
        with mock.patch.object(support, 'system_info', return_value={'windows': 'fixture'}), mock.patch('blvr_settings.runtimes', return_value=[]), mock.patch.object(support, 'FILE_LIMIT', 64):
            support.export_support(self.root, game, destination)
        with zipfile.ZipFile(destination) as bundle:
            self.assertEqual(bundle.testzip(), None)
            report = json.loads(bundle.read('report.json'))
            self.assertTrue(bundle.read('game/blvr.log').endswith(b'FINAL ERROR'))
            self.assertEqual(len(bundle.read('game/blvr.log')), 64)
            self.assertIn('launcher/installer-latest.log', bundle.namelist())
            self.assertFalse(any(name.endswith(('.sav', '.pak', '.exe')) for name in bundle.namelist()))
            self.assertFalse(any('unrelated' in name for name in bundle.namelist()))
            files = {entry['name']: entry for entry in report['files']}
            self.assertTrue(files['game/blvr.log']['truncated'])
            self.assertIn('error', files['host/blvr_xr_host.log'])
            self.assertEqual(len(report['binaries']['game/BrutalLegend.exe']['sha256']), 64)

    def test_launcher_dialog_keeps_actionable_error_and_logs_keep_raw_output(self):
        output = 'Checking runtime\n[12:34] FATAL: Headset unavailable (-35). Connect Quest Link.\n' + 'At scripts/launch_vr.ps1\n+ throw error\n' * 100
        self.assertEqual(setup.launcher_summary(output), 'Headset unavailable (-35). Connect Quest Link.')

    def test_export_budget_and_failed_replace_preserve_existing_zip(self):
        (self.logs / 'installer-latest.log').write_bytes(b'x' * 128)
        destination = self.root / 'support.zip'
        destination.write_bytes(b'previous report')
        with mock.patch.object(support, 'system_info', return_value={}), mock.patch('blvr_settings.runtimes', return_value=[]), mock.patch.object(support, 'TOTAL_LIMIT', 16):
            with mock.patch.object(support.os, 'replace', side_effect=PermissionError('locked ZIP')):
                with self.assertRaises(PermissionError):
                    support.export_support(self.root, '', destination)
            self.assertEqual(destination.read_bytes(), b'previous report')
            self.assertEqual(list(self.root.glob('support.zip.*.tmp')), [])
            support.export_support(self.root, '', destination)
        with zipfile.ZipFile(destination) as bundle:
            report = json.loads(bundle.read('report.json'))
            self.assertLessEqual(sum(entry.get('included_bytes', 0) for entry in report['files']), 16)

    @unittest.skipUnless(os.name == 'nt', 'Tk event test uses Windows')
    def test_gui_failed_prepare_is_logged_and_support_controls_fit(self):
        import tkinter as tk
        from tkinter import ttk, messagebox
        (self.root / 'assets/ui').mkdir(parents=True)
        (self.root / 'assets/ui/controls-defaults.ini').write_bytes((ROOT / 'assets/ui/controls-defaults.ini').read_bytes())
        native_tk = tk.Tk
        def hidden():
            window = native_tk(); window.withdraw(); return window
        def children(widget):
            for child in widget.winfo_children():
                yield child
                yield from children(child)
        def exercise(window):
            try:
                window.deiconify(); window.update_idletasks()
                widgets = list(children(window))
                prepare = next(w for w in widgets if isinstance(w, ttk.Button) and w.cget('text') == 'Prepare / repair game')
                prepare.invoke()
                self.assertIn('Game selection failed', self.saved_logs())
                tabs = next(w for w in widgets if isinstance(w, ttk.Notebook))
                for label in ('Play & setup', 'VR settings'):
                    index = next(i for i in range(len(tabs.tabs())) if tabs.tab(i, 'text') == label)
                    tabs.select(index); window.update_idletasks()
                    button_text = 'Export support ZIP…' if label == 'Play & setup' else 'Save VR settings'
                    button = next(w for w in widgets if isinstance(w, ttk.Button) and w.cget('text') == button_text)
                    self.assertLessEqual(button.winfo_rooty() + button.winfo_height(), window.winfo_rooty() + window.winfo_height())
            finally:
                window.destroy()
        with mock.patch.object(tk, 'Tk', side_effect=hidden), mock.patch.object(tk.Misc, 'mainloop', exercise), mock.patch.object(setup, 'find_game', return_value=''), mock.patch.object(messagebox, 'showerror') as error:
            setup.gui(self.root)
            error.assert_called_once()


if __name__ == '__main__':
    unittest.main()
