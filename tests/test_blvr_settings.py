"""Saved launch preferences and runtime validation failure/recovery."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('blvr_settings', Path(__file__).resolve().parents[1] / 'scripts/blvr_settings.py')
settings = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(settings)

class SettingsTest(unittest.TestCase):
    def test_repair_stale_settings_and_preserve_game_and_unknown_preferences(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            for text in ('null','[]','bad json'):
                (root/'settings.json').write_text(text)
                self.assertEqual(settings.load(root),settings.DEFAULTS)
            (root/'settings.json').write_text(json.dumps({'game_dir':'owned game','custom':'preserved','render_resolution':999999,'edge_aa':'false'}))
            loaded=settings.load(root)
            self.assertEqual(loaded['render_resolution'],1536)
            saved=settings.save(root,{**loaded,'guitar_height_cm':60,'frame_limit_fps':120})
            self.assertEqual(saved['game_dir'],'owned game');self.assertEqual(saved['custom'],'preserved')
            self.assertEqual(settings.load(root)['guitar_height_cm'],60)

    def test_invalid_preferences_or_failed_write_preserve_last_working_settings(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);settings.save(root,settings.DEFAULTS)
            before=(root/'settings.json').read_bytes()
            for key,value in [('frame_limit_fps',0),('guitar_volume',101),('render_resolution','1536.0'),('edge_aa','false')]:
                with self.subTest(key=key),self.assertRaises(ValueError):
                    settings.save(root,{key:value})
                self.assertEqual((root/'settings.json').read_bytes(),before)
            with patch.object(settings.os,'replace',side_effect=PermissionError('File is locked')):
                with self.assertRaises(PermissionError):settings.save(root,{'guitar_volume':40})
            self.assertEqual((root/'settings.json').read_bytes(),before)
            self.assertEqual(list(root.glob('settings.*.tmp')),[])

    def test_runtime_relative_library_and_architecture(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);runtime=root/'runtime.json';library=root/'runtime.dll'
            data=bytearray(134);data[:2]=b'MZ';struct.pack_into('<I',data,60,128);data[128:132]=b'PE\0\0'
            struct.pack_into('<H',data,132,0x8664);library.write_bytes(data)
            runtime.write_text(json.dumps({'runtime':{'library_path':'runtime.dll'}}))
            self.assertEqual(settings.validate_runtime(runtime),str(runtime.resolve()))
            struct.pack_into('<H',data,132,0x14c);library.write_bytes(data)
            with self.assertRaisesRegex(ValueError,'64-bit'):settings.validate_runtime(runtime)
            library.unlink()
            with self.assertRaises(ValueError):settings.validate_runtime(runtime)

if __name__=='__main__':unittest.main()
