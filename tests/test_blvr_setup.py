"""Control-editor data, atomic saving and hidden Tk events without running the game."""
import configparser
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('blvr_setup', ROOT / 'scripts/blvr_setup.py')
setup = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(setup)


class ControlsEditorTest(unittest.TestCase):
    def test_all_actions_and_vr_controls_can_be_staged_together(self):
        controls = setup.controls_config(ROOT / 'assets/ui/controls-defaults.ini')
        self.assertEqual(len(controls.items('actions')), 51)
        self.assertEqual(len(controls.items('controls')), 20)
        self.assertEqual(controls['controls']['build_research_alternate'], 'left_trigger')
        self.assertEqual(controls['controls']['earthshaker_left'], 'left_grip')
        self.assertEqual(controls['controls']['earthshaker_right'], 'right_grip')
        self.assertEqual(controls['actions']['uix'], 'x')
        self.assertEqual(controls['actions']['uia'], 'a')
        self.assertEqual(set(setup.CONTROL_HELP), {key for section in controls.sections() for key in controls[section]})
        baseline = setup.control_snapshot(controls)
        for section in controls.sections():
            for key in controls[section]:
                setup.stage_control(controls, section, key,
                                    'right' if key in setup.STICKS else 'x', section == 'actions')
        # Editing a second row must leave the first edit ready for one save.
        setup.stage_control(controls, 'actions', 'guitar', 'left_trigger')
        self.assertEqual(controls['actions']['axe'], 'command+x')
        self.assertEqual(controls['actions']['guitar'], 'left_trigger')
        self.assertNotEqual(setup.control_snapshot(controls), baseline)
        for key in setup.STICKS:
            self.assertEqual(controls['controls'][key], 'right')
        self.assertEqual(setup.display_binding('command+x'), 'Command chord + X · left controller')
        self.assertEqual(setup.display_binding('left_up'), 'Left stick up')
        with self.assertRaises(ValueError):
            setup.stage_control(controls, 'controls', 'movement_stick', 'a')

    def test_parser_matches_native_case_comments_and_last_assignment_order(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'controls.ini'
            path.write_text('\ufeff[actions]\nAxe=X\n[ACTIONS]\nAxe=Y\n'
                            '[ actions ]\nAxe= COMMAND+  A#comment\n'
                            '[CONTROLS]\nMOVEMENT_STICK=RIGHT;comment\n', encoding='utf-8')
            controls = setup.controls_config(path)
            self.assertEqual(controls['actions']['axe'], 'command+a')
            self.assertEqual(controls['controls']['movement_stick'], 'right')
            self.assertEqual(controls.sections(), ['actions', 'controls'])
            path.write_text('[DEFAULT]\nAxe=x\n', encoding='utf-8')
            with self.assertRaises(configparser.Error):
                setup.controls_config(path)

    def test_validation_failure_preserves_active_file_and_removes_temporary(self):
        controls = setup.controls_config(ROOT / 'assets/ui/controls-defaults.ini')
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            active = root / 'controls.ini'; active.write_bytes(b'previous working layout')
            failures = [SimpleNamespace(returncode=1, stderr='Invalid controls entry', stdout=''),
                        FileNotFoundError('Host is missing'),
                        subprocess.TimeoutExpired('host', 10)]
            for failure in failures:
                with mock.patch.object(setup.subprocess, 'run', side_effect=failure if isinstance(failure, Exception) else None,
                                       return_value=failure):
                    with self.assertRaises((ValueError, OSError, subprocess.TimeoutExpired)):
                        setup.save_controls(root, controls)
                self.assertEqual(active.read_bytes(), b'previous working layout')
                self.assertEqual(list(root.glob('controls.*.tmp')), [])

    def test_success_validates_candidate_before_replacing_active_file(self):
        controls = setup.controls_config(ROOT / 'assets/ui/controls-defaults.ini')
        setup.stage_control(controls, 'actions', 'axe', 'left_trigger')
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            active = root / 'controls.ini'; active.write_text('old', encoding='utf-8')
            def validate(command, **options):
                self.assertEqual(command, [str(root / 'tools/blvr_xr_host.exe'), '--check-controls'])
                self.assertEqual(active.read_text(encoding='utf-8'), 'old')
                candidate = setup.controls_config(Path(options['env']['BLVR_CONTROLS_FILE']))
                self.assertEqual(candidate['actions']['axe'], 'left_trigger')
                self.assertEqual(len(candidate.items('actions')) + len(candidate.items('controls')), 71)
                return SimpleNamespace(returncode=0, stderr='', stdout='PASS')
            with mock.patch.object(setup.subprocess, 'run', side_effect=validate):
                setup.save_controls(root, controls)
            self.assertEqual(setup.controls_config(active)['actions']['axe'], 'left_trigger')
            self.assertEqual(list(root.glob('controls.*.tmp')), [])

    def test_broken_or_stale_settings_do_not_block_the_controls_editor(self):
        registry = SimpleNamespace(HKEY_CURRENT_USER=1, HKEY_LOCAL_MACHINE=2,
                                   OpenKey=mock.Mock(side_effect=FileNotFoundError()))
        with tempfile.TemporaryDirectory() as folder, mock.patch.dict('sys.modules', {'winreg': registry}):
            root = Path(folder)
            for invalid in ('{broken', '[]', '{"game_dir": 42}', '{"game_dir": "missing-game"}'):
                (root / 'settings.json').write_text(invalid, encoding='utf-8')
                self.assertEqual(setup.find_game(root), '')
            game = root / 'owned-game'; game.mkdir(); (game / 'BrutalLegend.exe').touch()
            (root / 'settings.json').write_text(setup.json.dumps({'game_dir': str(game)}), encoding='utf-8')
            self.assertEqual(setup.find_game(root), str(game))

    @unittest.skipUnless(os.name == 'nt', 'Tk event test uses the Windows desktop runtime')
    def test_gui_switching_rows_and_search_preserve_pending_edits(self):
        import tkinter as tk
        from tkinter import ttk
        native_tk = tk.Tk
        saved = []
        def hidden_window(*args, **kwargs):
            window = native_tk(*args, **kwargs); window.withdraw(); return window
        def children(widget):
            for child in widget.winfo_children():
                yield child
                yield from children(child)
        def exercise(window):
            try:
                window.update_idletasks()
                widgets = list(children(window))
                tree = next(widget for widget in widgets if isinstance(widget, ttk.Treeview))
                chooser = next(widget for widget in widgets if isinstance(widget, ttk.Combobox))
                rows = {tree.item(row, 'text'): row for group in tree.get_children() for row in tree.get_children(group)}
                self.assertEqual(len(rows), 71)
                for action, binding in [('Axe', 'left_trigger'), ('Guitar', 'a')]:
                    tree.selection_set(rows[action]); tree.event_generate('<<TreeviewSelect>>'); window.update()
                    chooser.set(setup.INPUT_LABELS[binding]); chooser.event_generate('<<ComboboxSelected>>'); window.update()
                search = [widget for widget in widgets if isinstance(widget, ttk.Entry) and not isinstance(widget, ttk.Combobox)][1]
                window.setvar(search.cget('textvariable'), 'earthshaker'); window.update()
                self.assertEqual(sum(len(tree.get_children(group)) for group in tree.get_children()), 2)
                save = next(widget for widget in widgets if isinstance(widget, ttk.Button) and widget.cget('text') == 'Save controls')
                save.invoke()
                self.assertEqual(saved[0]['axe'], 'left_trigger')
                self.assertEqual(saved[0]['guitar'], 'a')
                window.setvar(search.cget('textvariable'), ''); window.update()
                self.assertEqual(sum(len(tree.get_children(group)) for group in tree.get_children()), 71)
            finally:
                window.destroy()
        with mock.patch.object(tk, 'Tk', side_effect=hidden_window), mock.patch.object(tk.Misc, 'mainloop', exercise):
            with mock.patch.object(setup, 'find_game', return_value=''), mock.patch.object(setup, 'save_controls', side_effect=lambda _, config: saved.append(dict(config['actions']))):
                setup.gui(ROOT, controls_only=True)


if __name__ == '__main__':
    unittest.main()
