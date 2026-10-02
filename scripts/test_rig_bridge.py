"""Asset-free wire fixtures for the Python live QA rig reader."""
import math
import struct
import unittest
from contextlib import contextmanager
from pathlib import Path
from unittest.mock import Mock, patch
import rig_bridge

from rig_bridge import (POSE_BYTES, RIG_EXTENSION_OFFSET, RIG_FRAME_BYTES,
                        RIG_SKIN_OFFSET, RIG_VERSION, RIG_WEAPON_OFFSET,
                        FRAME_BYTES, NATIVE_UI_BYTES, NATIVE_UI_VERSION,
                        _existing_mapping, _stable_snapshot, parse_pose, parse_rig, parse_rendered_ui_pair)


class RigBridgeTests(unittest.TestCase):
    def setUp(self):
        self.pose_bytes = bytearray(POSE_BYTES)
        struct.pack_into('<6I4Q', self.pose_bytes, 0, 0x50525847, 1, POSE_BYTES, 123, 2, 11, 7, 65, 900, 10000)
        self.pose = parse_pose(bytes(self.pose_bytes), 10001, 123)
        self.rig = bytearray(RIG_FRAME_BYTES)
        struct.pack_into('<IIQQqQII', self.rig, 0, 2, 0x47495242, 7, 65, 900, 11, 10, 2)
        struct.pack_into('<QIfIIII', self.rig, RIG_EXTENSION_OFFSET, 12, 0, 0, 0, 3, RIG_VERSION, RIG_FRAME_BYTES)
        identity = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
        for index in range(10):
            struct.pack_into('<16f', self.rig, RIG_SKIN_OFFSET + index * 64, *identity)
        for index in range(2):
            struct.pack_into('<16f', self.rig, RIG_WEAPON_OFFSET + index * 64, *identity)

    def test_cpp_layout_contract(self):
        header = (Path(__file__).resolve().parents[1] / 'src/bridge/blvr_rig_bridge.h').read_text()
        self.assertIn(f'RigVersion={RIG_VERSION}u', header)
        self.assertIn(f'sizeof(RigFrame)=={RIG_FRAME_BYTES}', header)
        self.assertEqual(RIG_EXTENSION_OFFSET, 16560)
        self.assertEqual(parse_rig(bytes(self.rig), self.pose, 3)['weapon'], 2)

    def test_pose_liveness_and_session(self):
        for now, pid in ((11500, 123), (9999, 123), (10001, 456)):
            with self.subTest(now=now, pid=pid), self.assertRaises(RuntimeError):
                parse_pose(bytes(self.pose_bytes), now, pid)
        struct.pack_into('<I', self.pose_bytes, 20, 1)
        with self.assertRaises(RuntimeError):
            parse_pose(bytes(self.pose_bytes), 10001)

    def test_exact_versioned_transaction(self):
        for offset, fmt, value in ((0, 'I', 3), (8, 'Q', 6), (16, 'Q', 64),
                                   (24, 'q', 901), (40, 'I', 257),
                                   (16572, 'f', float('nan')), (16580, 'I', 4),
                                   (16584, 'I', 2), (16588, 'I', 16560)):
            candidate = bytearray(self.rig)
            struct.pack_into('<' + fmt, candidate, offset, value)
            with self.subTest(offset=offset), self.assertRaises(RuntimeError):
                parse_rig(bytes(candidate), self.pose)
        with self.assertRaises(RuntimeError):
            parse_rig(bytes(self.rig[:16560]), self.pose)

    def test_missing_tracking_and_malformed_geometry(self):
        struct.pack_into('<I', self.rig, 16580, 1)
        self.assertEqual(parse_rig(bytes(self.rig), self.pose)['hands'], 1)
        with self.assertRaises(RuntimeError):
            parse_rig(bytes(self.rig), self.pose, 3)
        struct.pack_into('<f', self.rig, RIG_SKIN_OFFSET, 0)
        with self.assertRaises(RuntimeError):
            parse_rig(bytes(self.rig), self.pose)

    def test_missing_producer_is_never_created(self):
        class MissingKernel:
            def OpenFileMappingW(self, access, inherit, name):
                return None
        with self.assertRaises(RuntimeError):
            with _existing_mapping(MissingKernel(), 'missing', RIG_FRAME_BYTES):
                self.fail('A missing producer yielded a view')


class RenderedUiTests(unittest.TestCase):
    def setUp(self):
        self.pose = bytearray(POSE_BYTES)
        struct.pack_into('<6I4Q', self.pose, 0, 0x50525847, 1, POSE_BYTES, 123, 2, 127,
                         7, 70, 950_000_000, 10000)
        struct.pack_into('<I', self.pose, 56, 4)
        for offset in (64, 96, 128, 192, 288):
            struct.pack_into('<4f', self.pose, offset, 0, 0, 0, 1)
        for hand in range(2):
            struct.pack_into('<I', self.pose, 192 + hand * 96 + 84, 1)
        self.frame = bytearray(FRAME_BYTES)
        struct.pack_into('<6I3Q', self.frame, 0, 0x46525847, 6, FRAME_BYTES, 3, 2, 456, 88, 1, 0)
        struct.pack_into('<6I', self.frame, 48, 1536, 1536, 1, 1 | 4 | 8 | 16 | 32 | 1024 | 512, 0, 1)
        struct.pack_into('<7Q', self.frame, 72, 42, 65, 65, 65, 65, 900_000_000, 900_000_000)
        struct.pack_into('<Q', self.frame, 128, 10000)
        struct.pack_into('<Q', self.frame, 168, 42)
        struct.pack_into('<Q5I', self.frame, 224, FRAME_BYTES, 1, 0, 0, 1536, 1536)
        self.ui = bytearray(NATIVE_UI_BYTES)
        struct.pack_into('<IIQQQqII', self.ui, 0, 2, 0x48444e42, 7, 65, 65, 900_000_000, 3, 2)
        struct.pack_into('<2I', self.ui, 264, 3, NATIVE_UI_VERSION)
        identity = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
        for offset in (48, 112, 200):
            struct.pack_into('<16f', self.ui, offset, *identity)
        struct.pack_into('<3f', self.ui, 188, .1, .2, -.3)
        self.content = bytearray(64)
        struct.pack_into('<8I3Q2I', self.content, 0, 0x49554c42, 2, 64, 2, 456, 960, 540, 3840,
                         88, 10000, 66, 500, 1)

    def parsed(self, frame=None, eyes=None, pose=None, now=10001, **options):
        return parse_rendered_ui_pair(bytes(frame if frame is not None else self.frame),
            eyes if eyes is not None else (bytes(self.ui), bytes(self.ui)),
            bytes(pose if pose is not None else self.pose), now, .94, 456, 123, **options)

    def test_native_abi_and_historical_image(self):
        header = (Path(__file__).resolve().parents[1] / 'src/bridge/native_hand_bridge.h').read_text()
        self.assertIn(f'NativeHandVersion={NATIVE_UI_VERSION}u', header)
        self.assertIn(f'sizeof(NativeHandFrame)=={NATIVE_UI_BYTES}', header)
        result = self.parsed()
        self.assertEqual(result['transaction'], 42)
        self.assertEqual((result['native_epoch'], result['host_epoch']), (88, 7))
        self.assertEqual(result['current_pose_frame'], 70)
        self.assertEqual(result['eyes'][0]['pose_frame'], 65)
        self.assertEqual(result['reference_space_generation'], 4)
        self.assertEqual(result['coordinate_space'], 'source-head meters')
        screen = result['eyes'][0]['screen']
        self.assertAlmostEqual(screen[13], .84)
        self.assertAlmostEqual(screen[14], .24)
        self.assertAlmostEqual(result['eyes'][0]['right_tip'][2], -.3)

    def test_actual_rotated_scaled_guitar_plane(self):
        guitar = (0, 0, -.8, 0, 0, .8, 0, 0, .8, 0, 0, 0, .1, .2, -.6, 1)
        struct.pack_into('<16f', self.ui, 200, *guitar)
        plane = self.parsed()['eyes'][0]['guitar_mount_head']
        for actual, expected in zip(plane['position'], (.292, .872, -.6)):
            self.assertAlmostEqual(actual, expected, places=5)
        self.assertEqual(plane['right'], (0., -1., 0.))
        self.assertEqual(plane['up'], (1., 0., 0.))
        self.assertEqual(plane['normal'], (0., 0., 1.))
        self.assertAlmostEqual(sum(value * value for value in plane['orientation']), 1.)
        self.assertAlmostEqual(plane['orientation'][2], -math.sqrt(.5))
        self.assertEqual(plane['unit_scale'], 1.)

    def test_mixed_or_wrong_stamp_fails(self):
        for offset, fmt, value in ((8, 'Q', 88), (16, 'Q', 66), (24, 'Q', 66),
                                   (32, 'q', 901_000_000), (40, 'I', 1), (44, 'I', 1),
                                   (264, 'I', 1), (268, 'I', 2), (0, 'I', 3)):
            bad = bytearray(self.ui)
            struct.pack_into('<' + fmt, bad, offset, value)
            with self.subTest(offset=offset), self.assertRaises(RuntimeError):
                self.parsed(eyes=(bytes(self.ui), bytes(bad)))
        with self.assertRaises(RuntimeError):
            self.parsed(eyes=(bytes(self.ui), bytes(self.ui[:-1])))

    def test_native_frame_and_host_identity_liveness(self):
        for offset, fmt, value in ((0, 'I', 0), (4, 'I', 5), (8, 'I', 255), (16, 'I', 3),
                                   (20, 'I', 123), (80, 'Q', 64), (96, 'Q', 64),
                                   (128, 'Q', 9850), (168, 'Q', 41), (224, 'Q', 255),
                                   (232, 'I', 2)):
            bad = bytearray(self.frame)
            struct.pack_into('<' + fmt, bad, offset, value)
            with self.subTest(offset=offset), self.assertRaises(RuntimeError):
                self.parsed(frame=bad)
        with self.assertRaises(RuntimeError):
            self.parsed(now=9999)
        pose = bytearray(self.pose);struct.pack_into('<I', pose, 12, 789)
        with self.assertRaises(RuntimeError):
            self.parsed(pose=pose)
        struct.pack_into('<I', pose, 12, 123);struct.pack_into('<Q', pose, 48, 8000)
        with self.assertRaises(RuntimeError):
            self.parsed(pose=pose)
        self.assertEqual(self.parsed(now=10150)['age_ms'], 150)
        with self.assertRaises(RuntimeError):
            self.parsed(now=10151)

    def test_pose_age_tracking_reference_and_quaternion(self):
        for offset, fmt, value in ((44, 'I', 1), (88, 'f', float('nan')), (76, 'f', 0),
                                   (372, 'I', 0), (304, 'f', float('inf'))):
            bad = bytearray(self.pose)
            struct.pack_into('<' + fmt, bad, offset, value)
            with self.subTest(offset=offset), self.assertRaises(RuntimeError):
                self.parsed(pose=bad)
        bad = bytearray(self.pose);struct.pack_into('<q', bad, 40, 1_051_000_000)
        with self.assertRaises(RuntimeError):
            self.parsed(pose=bad)
        struct.pack_into('<q', bad, 40, 899_000_000)
        with self.assertRaises(RuntimeError):
            self.parsed(pose=bad)
        with self.assertRaises(RuntimeError):
            self.parsed(expected_reference_generation=5)
        self.assertEqual(self.parsed(expected_reference_generation=4)['reference_space_generation'], 4)

    def test_malformed_rendered_geometry(self):
        for offset, value in ((48, 0), (112, float('nan')), (212, .2), (200, 0),
                              (204, .3), (188, float('inf')), (252, 21)):
            bad = bytearray(self.ui);struct.pack_into('<f', bad, offset, value)
            with self.subTest(offset=offset), self.assertRaises(RuntimeError):
                self.parsed(eyes=(bytes(bad), bytes(bad)))

    def test_temporal_pair_is_explicit_and_per_eye(self):
        frame = bytearray(self.frame)
        struct.pack_into('<I', frame, 60, 1 | 4 | 8 | 32 | 64 | 512)
        struct.pack_into('<Q', frame, 88, 66);struct.pack_into('<Q', frame, 104, 66)
        struct.pack_into('<q', frame, 120, 910_000_000)
        right = bytearray(self.ui)
        struct.pack_into('<QQq', right, 16, 66, 66, 910_000_000)
        with self.assertRaises(RuntimeError):
            self.parsed(frame=frame, eyes=(bytes(self.ui), bytes(right)))
        pair = self.parsed(frame=frame, eyes=(bytes(self.ui), bytes(right)), allow_temporal=True)
        self.assertTrue(pair['temporal'])
        self.assertEqual(tuple(eye['source_frame'] for eye in pair['eyes']), (65, 66))

    def test_seqlock_rejects_mutation_and_never_creates_mapping(self):
        class ChangingView:
            def __init__(self): self.calls = 0
            def sequence(self, offset):
                self.calls += 1
                return 2 if self.calls == 1 else 4
            def read(self, offset, count): return struct.pack('<I', 2)
        self.assertIsNone(_stable_snapshot(ChangingView(), 0, 4))
        class MissingKernel:
            def OpenFileMappingW(self, access, inherit, name):
                self.last = (access, inherit, name)
                return None
        kernel = MissingKernel()
        with self.assertRaises(RuntimeError):
            with _existing_mapping(kernel, 'native-ui', NATIVE_UI_BYTES):
                self.fail('A missing producer was created')
        self.assertEqual(kernel.last, (4, False, 'native-ui'))

    def test_content_dimensions_have_their_own_fresh_native_stamp(self):
        pair = self.parsed(native_ui_header=bytes(self.content))
        self.assertEqual((pair['ui_width'], pair['ui_height'], pair['ui_flags']), (960, 540, 1))
        self.assertEqual(pair['ui_source_frame'], 66)
        self.assertEqual(pair['eyes'][0]['source_frame'], 65)
        for offset, fmt, value in ((4, 'I', 1), (8, 'I', 63), (12, 'I', 3), (16, 'I', 123),
                                   (20, 'I', 1281), (28, 'I', 1), (32, 'Q', 7), (40, 'Q', 9850), (60, 'I', 8)):
            bad = bytearray(self.content);struct.pack_into('<' + fmt, bad, offset, value)
            with self.subTest(offset=offset), self.assertRaises(RuntimeError):
                self.parsed(native_ui_header=bytes(bad))

    def test_complete_existing_reader_and_cpu_commit_guards(self):
        class MemoryView:
            def __init__(self, data): self.data = data
            def read(self, offset, count): return bytes(self.data[offset:offset + count])
            def sequence(self, offset): return struct.unpack_from('<I', self.data, offset)[0]
        native = bytearray(NATIVE_UI_BYTES * 64)
        native[NATIVE_UI_BYTES:NATIVE_UI_BYTES * 2] = self.ui
        cpu = bytearray(80)
        struct.pack_into('<8I', cpu, 0, 0x50435847, 3, 80, 3, 2048, 2048, 4, 2)
        struct.pack_into('<I', cpu, 32, 2)
        struct.pack_into('<Q', cpu, 48, 42)
        struct.pack_into('<Q', cpu, 72, rig_bridge.CPU_MAPPING_BYTES)
        views = {rig_bridge.POSE_MAPPING: MemoryView(self.pose),
                 rig_bridge.FRAME_MAPPING: MemoryView(self.frame),
                 rig_bridge.NATIVE_UI_MAPPING: MemoryView(native),
                 rig_bridge.NATIVE_UI_CONTENT_MAPPING: MemoryView(self.content),
                 rig_bridge.CPU_MAPPING: MemoryView(cpu)}
        names = []
        @contextmanager
        def existing(kernel, name, size):
            names.append(name)
            self.assertEqual(len(views[name].data), size)
            yield views[name]
        kernel = Mock();kernel.GetTickCount64.return_value = 10001
        with patch.object(rig_bridge.os, 'name', 'nt'), patch.object(rig_bridge.ctypes, 'WinDLL', return_value=kernel), \
                patch.object(rig_bridge, '_existing_mapping', existing), patch.object(rig_bridge.time, 'sleep'):
            pair = rig_bridge.read_rendered_ui_pair(.94, 456, 123)
            self.assertEqual(pair['transaction'], 42)
            self.assertEqual(pair['ui_width'], 960)
            self.assertEqual(set(names), set(views))
            struct.pack_into('<Q', cpu, 48, 41)
            with self.assertRaisesRegex(RuntimeError, 'CPU image pair'):
                rig_bridge.read_rendered_ui_pair(.94, 456, 123)


if __name__ == '__main__':
    unittest.main()
