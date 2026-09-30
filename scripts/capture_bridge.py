"""Inspect BLVR's source mailbox. This is NOT a final compositor capture."""
import argparse
import json
import mmap
import pathlib
import struct
import time

from PIL import Image


def capture(output):
    eye_bytes = 2048 * 2048 * 4
    with mmap.mmap(-1, 256, tagname=r"Local\BLVR_XR_FrameBridge_v6",
                   access=mmap.ACCESS_READ) as descriptor, mmap.mmap(
            -1, 80 + 6 * eye_bytes, tagname=r"Local\BLVR_XR_CpuFrame_v3",
            access=mmap.ACCESS_READ) as pixels:
        for _ in range(100):
            frame = descriptor[:]
            sequence = struct.unpack_from("<I", frame, 16)[0]
            if struct.unpack_from("<I", frame)[0] != 0x46525847:
                raise RuntimeError("No live BLVR frame descriptor")
            width, height = struct.unpack_from("<II", frame, 48)
            slot = struct.unpack_from("<I", frame, 64)[0]
            transaction = struct.unpack_from("<Q", frame, 72)[0]
            if slot >= 3 or not (0 < width <= 2048 and 0 < height <= 2048):
                raise RuntimeError("Invalid frame dimensions or slot")
            pixel_sequence = struct.unpack_from("<I", pixels, 32 + slot * 4)[0]
            offset = 80 + slot * 2 * eye_bytes
            left = pixels[offset:offset + width * height * 4]
            right = pixels[offset + eye_bytes:offset + eye_bytes + width * height * 4]
            if (not sequence & 1 and not pixel_sequence & 1 and
                    struct.unpack_from("<I", descriptor, 16)[0] == sequence and
                    struct.unpack_from("<I", pixels, 32 + slot * 4)[0] == pixel_sequence and
                    struct.unpack_from("<Q", pixels, 48 + slot * 8)[0] == transaction):
                break
            time.sleep(.005)
        else:
            raise RuntimeError("Could not read a coherent eye pair")
    output = pathlib.Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    for label, data in (("left", left), ("right", right)):
        Image.frombytes("RGBA", (width, height), data, "raw", "BGRA").save(
            output.with_name(output.stem + "-" + label + ".png"))
    metadata = {"source": "BLVR CPU mailbox (before compositor)",
                "transaction": transaction, "source_frames": struct.unpack_from("<QQ", frame, 80),
                "pose_sequences": struct.unpack_from("<QQ", frame, 96),
                "presentation_mode": struct.unpack_from("<I", frame, 232)[0]}
    output.with_suffix(".json").write_text(json.dumps(metadata, indent=2))
    print(json.dumps(metadata))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=pathlib.Path)
    capture(parser.parse_args().output)
