"""Import Eddie's resources from a locally owned PC Brütal Legend installation.

Pack v5 field layout cross-checked with DoubleFine Explorer's published format
reader (bgbennyboy/DoubleFine-Explorer, uDFExplorer_PAKManager.pas) and the owned
pack headers. No game data is downloaded or distributed with this script.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib


def resources(header):
    data = header.read_bytes()
    if data[:5] != b'dfpf\x05':
        raise ValueError(f'Unsupported pack: {header}')
    u32 = lambda offset: struct.unpack_from('>I', data, offset)[0]
    type_at, names_at = struct.unpack_from('>QQ', data, 8)
    count, records_at = u32(32), struct.unpack_from('>Q', data, 56)[0]
    types = []
    for _ in range(u32(24)):
        length = u32(type_at)
        name = data[type_at+4:type_at+4+length].rstrip(b'\0').decode('ascii')
        types.append(name)
        type_at += 4 + length + 12
    for index in range(count):
        at = records_at + index*16
        name_at = names_at + (u32(at+3) >> 11)
        name = data[name_at:data.index(0, name_at)].decode('ascii')
        type_index = ((u32(at+14) >> 20) & 255) >> 1
        yield dict(name=name, type=types[type_index], unpacked=u32(at) >> 8,
                   offset=u32(at+8) >> 3, size=(u32(at+11) & 0x07ffffff) >> 4,
                   compression=data[at+15] & 15, pack=str(header.with_suffix('.~p')))


def extract(entry):
    with Path(entry['pack']).open('rb') as stream:
        stream.seek(entry['offset'])
        payload = stream.read(entry['size'])
    if entry['compression'] == 8:
        payload = zlib.decompress(payload)
    elif entry['compression'] != 4:
        raise ValueError(f"Unsupported compression {entry['compression']}")
    # The pack's in-memory size excludes some serialized resource headers;
    # e.g. AvatarBackstagePass texture has 36 more bytes on disk. Preserve the
    # complete validated deflate stream, including its format metadata.
    if not payload or len(payload) > 64*1024*1024:
        raise ValueError(f"Invalid resource size for {entry['name']}")
    entry['serialized_bytes'] = len(payload)
    return payload


def import_owned(installation, output, progress=print):
    installation, output = Path(installation), Path(output)
    output.mkdir(parents=True, exist_ok=True)
    manifest = []
    for header in sorted((installation/'Win/Packs').glob('*.~h')):
        for entry in resources(header):
            if not entry['name'].startswith('characters/bipeds/a01_avatar/'):
                continue
            if entry['type'] not in ('Mesh', 'Rig', 'Texture', 'Material'):
                continue
            payload = extract(entry)
            relative = Path(entry['name'] + '.' + entry['type'].lower())
            target = output/relative
            if not target.resolve().is_relative_to(output.resolve()):
                raise ValueError('Asset path escaped output directory')
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(payload)
            entry.update(output=str(relative), sha256=hashlib.sha256(payload).hexdigest())
            manifest.append(entry)
            progress(f"Importing {entry['type']}: {entry['name'].rsplit('/',1)[-1]}")
    if not manifest:
        raise ValueError('No supported Eddie resources found in this game installation')
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2))
    return manifest

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('installation', type=Path)
    parser.add_argument('--output', type=Path, default=Path('artifacts/eddie-assets'))
    args = parser.parse_args()
    manifest = import_owned(args.installation,args.output)
    print('Imported',len(manifest),'local resources')


if __name__ == '__main__':
    main()
