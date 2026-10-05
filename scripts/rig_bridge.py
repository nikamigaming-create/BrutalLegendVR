"""Read-only QA access to the current tracked-rig ABI, shared by live probes.

Open existing publications only. Creating a blank named mmap when the host is
absent can mask a missing producer and must not be used as live evidence.
"""
from contextlib import ExitStack, contextmanager
import ctypes
import math
import os
import struct
import time

RIG_MAPPING = r"Local\BLVR_TrackedEddie_v4"
POSE_MAPPING = r"Local\BLVR_XR_PoseBridge_v1"
RIG_VERSION = 4
RIG_FRAME_BYTES = 16600
RIG_HISTORY = 64
RIG_SKIN_OFFSET = 48
RIG_WEAPON_OFFSET = RIG_SKIN_OFFSET + 256 * 64
RIG_EXTENSION_OFFSET = RIG_WEAPON_OFFSET + 128
POSE_BYTES = 384
FRAME_MAPPING = r"Local\BLVR_XR_FrameBridge_v6"
FRAME_BYTES = 256
NATIVE_UI_MAPPING = r"Local\BLVR_RenderedUi_v3"
NATIVE_UI_BYTES = 272
NATIVE_UI_HISTORY = 64
NATIVE_UI_VERSION = 3
CPU_MAPPING = r"Local\BLVR_XR_CpuFrame_v3"
CPU_HEADER_BYTES = 80
CPU_MAPPING_BYTES = CPU_HEADER_BYTES + 3 * 2 * 2048 * 2048 * 4
NATIVE_UI_CONTENT_MAPPING = r"Local\BLVR_NativeUi_v2"
NATIVE_UI_CONTENT_BYTES = 64


def _usable_matrix(values):
    if (any(abs(values[index]) > .00001 for index in (3, 7, 11))
            or abs(values[15] - 1) > .00001):
        return False
    determinant = (values[0] * (values[5] * values[10] - values[6] * values[9])
                   - values[1] * (values[4] * values[10] - values[6] * values[8])
                   + values[2] * (values[4] * values[9] - values[5] * values[8]))
    return math.isfinite(determinant) and abs(determinant) > .00000001


def parse_pose(data, now_tick_ms, expected_pid=None):
    if len(data) != POSE_BYTES:
        raise RuntimeError("Pose publication size changed")
    magic, version, size, pid, sequence, flags = struct.unpack_from("<6I", data)
    epoch, frame, display_time, heartbeat = struct.unpack_from("<QQqQ", data, 24)
    if (magic != 0x50525847 or version != 1 or size != POSE_BYTES
            or not sequence or sequence & 1 or flags & 11 != 11
            or not pid or not epoch or not frame or display_time <= 0):
        raise RuntimeError("No active coherent pose publication")
    if expected_pid is not None and pid != expected_pid:
        raise RuntimeError("Pose producer differs from the diagnostic session")
    if not 0 <= now_tick_ms - heartbeat < 1500:
        raise RuntimeError("Pose producer heartbeat is stale")
    return dict(epoch=epoch, frame=frame, display_time=display_time, sequence=sequence)


def parse_rig(data, pose, required_hands=0):
    if len(data) != RIG_FRAME_BYTES:
        raise RuntimeError("Tracked rig publication size changed")
    sequence, magic, epoch, frame, display_time, signature, bones, weapon = struct.unpack_from("<IIQQqQII", data)
    controls, action, weight, support, hands, version, size = struct.unpack_from("<QIfIIII", data, RIG_EXTENSION_OFFSET)
    presentation, fret = struct.unpack_from('<II', data, RIG_EXTENSION_OFFSET + 32)
    if (not sequence or sequence & 1 or magic != 0x47495242 or epoch != pose["epoch"]
            or frame != pose["frame"] or display_time != pose["display_time"]
            or not signature or not controls or not 0 < bones <= 256 or weapon > 2
            or version != RIG_VERSION or size != RIG_FRAME_BYTES or hands > 3
            or support > 1 or presentation & ~1 or fret > 24 or action > 3 or not math.isfinite(weight) or not 0 <= weight <= 1):
        raise RuntimeError("No exact coherent v4 tracked rig for the current pose")
    if required_hands & ~3 or hands & required_hands != required_hands:
        raise RuntimeError("Required controller tracking is unavailable")
    values = struct.unpack_from(f"<{bones * 16}f", data, RIG_SKIN_OFFSET)
    weapons = struct.unpack_from("<32f", data, RIG_WEAPON_OFFSET)
    if any(not math.isfinite(value) or abs(value) > 100 for value in values + weapons):
        raise RuntimeError("Tracked rig contains invalid geometry")
    if any(not _usable_matrix(values[index:index + 16]) for index in range(0, len(values), 16)):
        raise RuntimeError("Tracked rig contains a singular or non-affine bone")
    if required_hands == 3 and any(not _usable_matrix(weapons[index:index + 16]) for index in (0, 16)):
        raise RuntimeError("Tracked instrument attachment is unavailable")
    return dict(frame=frame, weapon=weapon, hands=hands, bones=bones, sequence=sequence, data=data)


class _View:
    def __init__(self, address, size):
        self.address, self.size = address, size

    def read(self, offset, count):
        if offset < 0 or count < 0 or offset + count > self.size:
            raise RuntimeError("QA mapping read exceeds the published ABI")
        return ctypes.string_at(self.address + offset, count)

    def sequence(self, offset):
        return struct.unpack("<I", self.read(offset, 4))[0]


@contextmanager
def _existing_mapping(kernel, name, size):
    handle = kernel.OpenFileMappingW(4, False, name)  # FILE_MAP_READ
    if not handle:
        raise RuntimeError(f"No live publication: {name}")
    address = None
    try:
        address = kernel.MapViewOfFile(handle, 4, 0, 0, size)
        if not address:
            raise RuntimeError(f"Publication cannot supply the current ABI: {name}")
        yield _View(address, size)
    finally:
        if address:
            kernel.UnmapViewOfFile(address)
        kernel.CloseHandle(handle)


def read_current_rig(required_hands=0, expected_pid=None):
    """Join a fresh host pose to its exact rig slot, without an older fallback."""
    if os.name != "nt":
        raise RuntimeError("Live rig mappings require Windows")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenFileMappingW.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_wchar_p]
    kernel.OpenFileMappingW.restype = ctypes.c_void_p
    kernel.MapViewOfFile.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_size_t]
    kernel.MapViewOfFile.restype = ctypes.c_void_p
    kernel.UnmapViewOfFile.argtypes = [ctypes.c_void_p]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    kernel.GetTickCount64.restype = ctypes.c_ulonglong
    with _existing_mapping(kernel, POSE_MAPPING, POSE_BYTES) as poses, \
            _existing_mapping(kernel, RIG_MAPPING, RIG_FRAME_BYTES * RIG_HISTORY) as rigs:
        for _ in range(20):
            data = poses.read(0, POSE_BYTES)
            sequence = struct.unpack_from("<I", data, 16)[0]
            if not sequence or sequence & 1 or poses.sequence(16) != sequence:
                time.sleep(.002)
                continue
            pose = parse_pose(data, kernel.GetTickCount64(), expected_pid)
            offset = pose["frame"] % RIG_HISTORY * RIG_FRAME_BYTES
            data = rigs.read(offset, RIG_FRAME_BYTES)
            sequence = struct.unpack_from("<I", data)[0]
            if not sequence or sequence & 1 or rigs.sequence(offset) != sequence:
                time.sleep(.002)
                continue
            # The rig is committed before its matching pose. A missing exact
            # slot is a missing publication, not permission to use an old one.
            return parse_rig(data, pose, required_hands)
    raise RuntimeError("Tracked rig publication remained busy")


def _pose_geometry(data, required_pointers):
    flags, = struct.unpack_from('<I', data, 20)
    if flags & 15 != 15 or flags & ~127 or required_pointers & ~3:
        raise RuntimeError('No valid host stereo/head geometry')
    for offset in (64, 96, 128):
        quaternion = struct.unpack_from('<4f', data, offset)
        position = struct.unpack_from('<3f', data, offset + 16)
        norm = sum(value * value for value in quaternion)
        if (not all(math.isfinite(value) for value in quaternion + position)
                or not .5 < norm < 1.5 or any(abs(value) > 10000 for value in position)):
            raise RuntimeError('Host pose has an invalid quaternion or position')
    for hand in range(2):
        if not required_pointers & (1 << hand):
            continue
        active, = struct.unpack_from('<I', data, 192 + hand * 96 + 84)
        if not flags & (1 << (4 + hand)) or not active & 1:
            raise RuntimeError('Required current controller tracking is unavailable')
        quaternion = struct.unpack_from('<4f', data, 192 + hand * 96)
        position = struct.unpack_from('<3f', data, 208 + hand * 96)
        norm = sum(value * value for value in quaternion)
        if (not all(math.isfinite(value) for value in quaternion + position)
                or not .5 < norm < 1.5 or any(abs(value) > 10000 for value in position)):
            raise RuntimeError('Required current controller pose is malformed')


def _plane_pose(matrix):
    if (any(not math.isfinite(value) or abs(value) > 20 for value in matrix)
            or not _usable_matrix(matrix)):
        raise RuntimeError('Rendered UI contains invalid or singular geometry')
    rows, scales = [], []
    for start in (0, 4, 8):
        scale = math.sqrt(sum(value * value for value in matrix[start:start + 3]))
        if not .00001 < scale <= 20:
            raise RuntimeError('Rendered UI contains an invalid transform scale')
        rows.append(tuple(value / scale for value in matrix[start:start + 3]))
        scales.append(scale)
    dot = lambda first, second: sum(a * b for a, b in zip(first, second))
    cross = (rows[0][1] * rows[1][2] - rows[0][2] * rows[1][1],
             rows[0][2] * rows[1][0] - rows[0][0] * rows[1][2],
             rows[0][0] * rows[1][1] - rows[0][1] * rows[1][0])
    if (any(abs(dot(rows[first], rows[second])) > .0005 for first, second in ((0, 1), (0, 2), (1, 2)))
            or dot(cross, rows[2]) < .999):
        raise RuntimeError('Rendered UI contains shear or a reflected orientation')
    # DirectX uses row vectors. Transpose its unit rotation before converting
    # to the usual x/y/z/w quaternion; UI Pose discards decomposed mesh scale.
    rotation = [[rows[column][row] for column in range(3)] for row in range(3)]
    trace = sum(rotation[index][index] for index in range(3))
    if trace > 0:
        length = math.sqrt(trace + 1) * 2
        quaternion = ((rotation[2][1] - rotation[1][2]) / length,
                      (rotation[0][2] - rotation[2][0]) / length,
                      (rotation[1][0] - rotation[0][1]) / length, .25 * length)
    else:
        axis = max(range(3), key=lambda index: rotation[index][index])
        following, last = (axis + 1) % 3, (axis + 2) % 3
        length = math.sqrt(1 + rotation[axis][axis] - rotation[following][following] - rotation[last][last]) * 2
        components = [0.] * 4
        components[axis] = .25 * length
        components[following] = (rotation[following][axis] + rotation[axis][following]) / length
        components[last] = (rotation[last][axis] + rotation[axis][last]) / length
        components[3] = (rotation[last][following] - rotation[following][last]) / length
        quaternion = tuple(components)
    norm = math.sqrt(sum(value * value for value in quaternion))
    quaternion = tuple(value / norm for value in quaternion)
    unit_matrix = tuple(value for row in rows for value in (*row, 0.)) + tuple(matrix[12:16])
    return dict(matrix=unit_matrix, position=tuple(matrix[12:15]), orientation=quaternion,
                right=rows[0], up=rows[1], normal=rows[2], source_scale=tuple(scales), unit_scale=1.)


def parse_rendered_ui_pair(frame_data, native_frames, pose_data, now_tick_ms,
                           guitar_headstock_y, expected_game_pid, expected_host_pid,
                           required_pointers=2, allow_temporal=False, expected_reference_generation=None,
                           native_ui_header=None):
    """Validate the published image pair and actual UI geometry in source-head meters.

    Native UI epoch belongs to the host, while FrameBridge epoch belongs to the
    game. The current pose can advance beyond the image; it supplies identity,
    liveness and current tracking, never a replacement historical transform.
    Reference-space generation is current only: this ABI has no historical field.
    """
    if len(frame_data) != FRAME_BYTES or len(native_frames) != 2:
        raise RuntimeError('Native stereo publication size changed')
    if not expected_game_pid or not expected_host_pid:
        raise RuntimeError('Both diagnostic producer PIDs are required')
    if not math.isfinite(guitar_headstock_y) or not 0 < guitar_headstock_y <= 20:
        raise RuntimeError('Owned guitar headstock calibration is unavailable')
    pose = parse_pose(pose_data, now_tick_ms, expected_host_pid)
    _pose_geometry(pose_data, required_pointers)
    reference, = struct.unpack_from('<I', pose_data, 56)
    if expected_reference_generation is not None and reference != expected_reference_generation:
        raise RuntimeError('Host reference-space generation changed')
    magic, version, size, slots, sequence, pid = struct.unpack_from('<6I', frame_data)
    epoch, resources = struct.unpack_from('<QQ', frame_data, 24)
    width, height, pixel_format, flags, slot = struct.unpack_from('<5I', frame_data, 48)
    transaction, = struct.unpack_from('<Q', frame_data, 72)
    sources = struct.unpack_from('<2Q', frame_data, 80)
    poses = struct.unpack_from('<2Q', frame_data, 96)
    displays = struct.unpack_from('<2q', frame_data, 112)
    heartbeat, = struct.unpack_from('<Q', frame_data, 128)
    mapping_size, = struct.unpack_from('<Q', frame_data, 224)
    mode, reasons, ui_eye, content_width, content_height = struct.unpack_from('<5I', frame_data, 232)
    if (magic != 0x46525847 or version != 6 or size != FRAME_BYTES or mapping_size != FRAME_BYTES
            or slots != 3 or not sequence or sequence & 1 or pid != expected_game_pid
            or not epoch or not resources or not transaction or slot >= 3
            or not 0 < width <= 32768 or not 0 < height <= 32768 or pixel_format not in (1, 2, 3)
            or flags & ~0x3fff or flags & 37 != 37 or mode != 1 or reasons or ui_eye > 1
            or not 0 < content_width <= 32768 or not 0 < content_height <= 32768
            or not heartbeat or not 0 <= now_tick_ms - heartbeat <= 150
            or struct.unpack_from('<Q', frame_data, 168 + slot * 24)[0] != transaction):
        raise RuntimeError('No fresh coherent stereo image from the diagnostic game')
    same = sources[0] == sources[1] and poses[0] == poses[1] and displays[0] == displays[1]
    temporal = bool(flags & 64)
    if temporal:
        if (not allow_temporal or flags & (16 | 128 | 1024 | 2048)
                or not sources[0] < sources[1] or not poses[0] < poses[1] or not displays[0] < displays[1]):
            raise RuntimeError('Temporal eye stamps are not an explicitly allowed ordered pair')
    elif not same or not flags & 16 or not flags & (128 | 1024 | 2048):
        raise RuntimeError('Stereo image has mixed or unverified source stamps')
    if flags & 256 or flags & (4096 | 8192) and not flags & 2048:
        raise RuntimeError('Stereo image has incompatible presentation flags')
    if not flags & 8 and not (flags & 512 and flags & 1024 and same):
        raise RuntimeError('Stereo images are not distinct')
    if not flags & 512:
        handles = struct.unpack_from('<2Q', frame_data, 152 + slot * 24)
        fences = struct.unpack_from('<2Q', frame_data, 136)
        if not all(handles + fences) or handles[0] == handles[1]:
            raise RuntimeError('GPU eye resources are unavailable')
    eyes = []
    for eye, data in enumerate(native_frames):
        if len(data) != NATIVE_UI_BYTES:
            raise RuntimeError('Rendered UI publication size changed')
        ui_sequence, ui_magic, ui_epoch, source, source_pose, display, valid, ui_flags = struct.unpack_from('<IIQQQqII', data)
        pointers, ui_version = struct.unpack_from('<2I', data, 264)
        if (not ui_sequence or ui_sequence & 1 or ui_magic != 0x48444e42 or ui_version != NATIVE_UI_VERSION
                or ui_epoch != pose['epoch'] or source != sources[eye] or source_pose != poses[eye] or display != displays[eye]
                or not source or not source_pose or source_pose > pose['frame'] or display <= 0
                or not 0 <= pose['display_time'] - display <= 150_000_000
                or valid != 3 or pointers & ~3 or pointers & required_pointers != required_pointers
                or ui_flags != 2):
            raise RuntimeError('No exact current rendered guitar UI for both image eyes')
        palms = (struct.unpack_from('<16f', data, 48), struct.unpack_from('<16f', data, 112))
        for palm in palms:
            _plane_pose(palm)
        tips = (struct.unpack_from('<3f', data, 176), struct.unpack_from('<3f', data, 188))
        if any(not math.isfinite(value) or abs(value) > 20 for tip in tips for value in tip):
            raise RuntimeError('Rendered fingertips are malformed')
        guitar = struct.unpack_from('<16f', data, 200)
        _plane_pose(guitar)
        screen = (0., -1., 0., 0., 0., 0., 1., 0., -1., 0., 0., 0., 0., guitar_headstock_y - .10, .24, 1.)
        mount = tuple(sum(screen[row * 4 + index] * guitar[index * 4 + column] for index in range(4))
                      for row in range(4) for column in range(4))
        plane = _plane_pose(mount)
        eyes.append(dict(source_frame=source, pose_frame=source_pose, display_time=display, sequence=ui_sequence,
                         flags=ui_flags, pointer_valid_mask=pointers, palm_matrices_head=palms,
                         guitar_matrix_head=guitar, guitar_mount_head=plane, screen=plane['matrix'],
                         index_tips_head=tips, right_fingertip_head=tips[1], right_tip=tips[1],
                         stamp=dict(source_frame=source, pose_frame=source_pose, display_time=display, host_epoch=ui_epoch)))
    if eyes[0]['flags'] != eyes[1]['flags'] or eyes[0]['pointer_valid_mask'] != eyes[1]['pointer_valid_mask']:
        raise RuntimeError('Rendered UI eyes disagree about interaction context')
    output = dict(transaction=transaction, native_epoch=epoch, host_epoch=pose['epoch'], game_pid=pid,
                host_pid=expected_host_pid, frame_sequence=sequence, heartbeat_tick_ms=heartbeat,
                age_ms=now_tick_ms - heartbeat, current_pose_frame=pose['frame'],
                reference_space_generation=reference, flags=flags, temporal=temporal,
                pointer_valid_mask=eyes[0]['pointer_valid_mask'], coordinate_space='source-head meters', eyes=eyes)
    if native_ui_header is not None:
        if len(native_ui_header) != NATIVE_UI_CONTENT_BYTES:
            raise RuntimeError('Native UI content header size changed')
        content_magic, content_version, content_size, content_sequence, content_pid, ui_width, ui_height, row_bytes = struct.unpack_from('<8I', native_ui_header)
        content_epoch, content_tick, content_source, samples, content_flags = struct.unpack_from('<3Q2I', native_ui_header, 32)
        if (content_magic != 0x49554c42 or content_version != 2 or content_size != NATIVE_UI_CONTENT_BYTES
                or not content_sequence or content_sequence & 1 or content_pid != pid or content_epoch != epoch
                or not 0 < ui_width <= 1280 or not 0 < ui_height <= 720 or row_bytes != ui_width * 4
                or not content_tick or not 0 <= now_tick_ms - content_tick <= 150
                or not content_source or not samples or content_flags & ~7):
            raise RuntimeError('No fresh native UI content from the diagnostic game')
        # HUD content is allowed to refresh on a retained world image. Preserve
        # its separate source stamp; only the rendered contact geometry joins eyes.
        output.update(ui_width=ui_width, ui_height=ui_height, ui_flags=content_flags,
                      ui_sequence=content_sequence, ui_source_frame=content_source, ui_tick_ms=content_tick)
    return output


def _stable_snapshot(view, offset, count, sequence_offset=0):
    sequence = view.sequence(offset + sequence_offset)
    if not sequence or sequence & 1:
        return None
    data = view.read(offset, count)
    if (struct.unpack_from('<I', data, sequence_offset)[0] != sequence
            or view.sequence(offset + sequence_offset) != sequence):
        return None
    return data


def read_rendered_ui_pair(guitar_headstock_y, expected_game_pid, expected_host_pid,
                          required_pointers=2, allow_temporal=False, expected_reference_generation=None):
    """Read existing mappings only; join actual UI to the latest published image.

    Per-eye mounts/tips remain in their stamped head space. This function never
    maps them through a newer HMD or invents unavailable historical head data.
    """
    if os.name != 'nt':
        raise RuntimeError('Live rendered UI mappings require Windows')
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenFileMappingW.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_wchar_p]
    kernel.OpenFileMappingW.restype = ctypes.c_void_p
    kernel.MapViewOfFile.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_size_t]
    kernel.MapViewOfFile.restype = ctypes.c_void_p
    kernel.UnmapViewOfFile.argtypes = [ctypes.c_void_p]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    kernel.GetTickCount64.restype = ctypes.c_ulonglong
    failure = 'Rendered UI publications remained busy'
    with ExitStack() as stack:
        poses = stack.enter_context(_existing_mapping(kernel, POSE_MAPPING, POSE_BYTES))
        frames = stack.enter_context(_existing_mapping(kernel, FRAME_MAPPING, FRAME_BYTES))
        ui = stack.enter_context(_existing_mapping(kernel, NATIVE_UI_MAPPING, NATIVE_UI_BYTES * NATIVE_UI_HISTORY))
        content = stack.enter_context(_existing_mapping(kernel, NATIVE_UI_CONTENT_MAPPING, NATIVE_UI_CONTENT_BYTES))
        cpu = None
        for _ in range(20):
            pose_data = _stable_snapshot(poses, 0, POSE_BYTES, 16)
            frame_data = _stable_snapshot(frames, 0, FRAME_BYTES, 16)
            if pose_data is None or frame_data is None:
                time.sleep(.002)
                continue
            flags, slot = struct.unpack_from('<2I', frame_data, 60)
            sources = struct.unpack_from('<2Q', frame_data, 80)
            native = [_stable_snapshot(ui, source % NATIVE_UI_HISTORY * NATIVE_UI_BYTES, NATIVE_UI_BYTES) for source in sources]
            content_data = _stable_snapshot(content, 0, NATIVE_UI_CONTENT_BYTES, 12)
            if any(data is None for data in native) or content_data is None:
                time.sleep(.002)
                continue
            try:
                if flags & 512:
                    if cpu is None:
                        cpu = stack.enter_context(_existing_mapping(kernel, CPU_MAPPING, CPU_HEADER_BYTES))
                    if slot >= 3:
                        raise RuntimeError('CPU image slot is invalid')
                    cpu_data = _stable_snapshot(cpu, 0, CPU_HEADER_BYTES, 32 + slot * 4)
                    if (cpu_data is None or struct.unpack_from('<8I', cpu_data) != (0x50435847, 3, 80, 3, 2048, 2048, 4, 2)
                            or struct.unpack_from('<Q', cpu_data, 72)[0] != CPU_MAPPING_BYTES
                            or struct.unpack_from('<Q', cpu_data, 48 + slot * 8)[0] != struct.unpack_from('<Q', frame_data, 72)[0]):
                        raise RuntimeError('CPU image pair is not committed to this transaction')
                latest_pose = _stable_snapshot(poses, 0, POSE_BYTES, 16)
                if latest_pose is None:
                    raise RuntimeError('Host pose publication changed during the UI join')
                if (struct.unpack_from('<I', pose_data, 12)[0] != struct.unpack_from('<I', latest_pose, 12)[0]
                        or pose_data[24:32] != latest_pose[24:32] or pose_data[56:60] != latest_pose[56:60]):
                    raise RuntimeError('Host identity or reference space changed during the UI join')
                output = parse_rendered_ui_pair(frame_data, native, latest_pose, kernel.GetTickCount64(),
                    guitar_headstock_y, expected_game_pid, expected_host_pid, required_pointers,
                    allow_temporal, expected_reference_generation, content_data)
                if frames.sequence(16) != output['frame_sequence'] or any(
                        ui.sequence(source % NATIVE_UI_HISTORY * NATIVE_UI_BYTES) != output['eyes'][eye]['sequence']
                        for eye, source in enumerate(sources)):
                    raise RuntimeError('Rendered source changed during the UI join')
                if flags & 512 and cpu.sequence(32 + slot * 4) != struct.unpack_from('<I', cpu_data, 32 + slot * 4)[0]:
                    raise RuntimeError('CPU image pair changed during the UI join')
                if content.sequence(12) != output['ui_sequence']:
                    raise RuntimeError('Native UI content changed during the geometry join')
                return output
            except RuntimeError as error:
                failure = str(error)
            time.sleep(.002)
    raise RuntimeError(failure)
