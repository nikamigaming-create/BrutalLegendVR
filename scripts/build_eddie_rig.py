"""Decode the locally imported PC Eddie resources into a private VR rig cache.

Mesh layout is checked against retail readers 0x64d650/0x66a720/0x66ac50
and live vertex declarations (formats 2 and 4). Cache contains owned game data;
never include it in a source or mod distribution.
"""
import argparse
import json
from pathlib import Path
import re
import struct

import numpy as np
from PIL import Image


def u32(data, at):
    return struct.unpack_from('<I', data, at)[0]


def skeleton(path):
    data = path.read_bytes()
    count = u32(data, 16)
    assert 0 < count <= 256
    base = 0x70
    ids, parents, qs = (base + u32(data, at) for at in (0x4c, 0x50, 0x5c))
    bones = []
    for i in range(count):
        at = base + (u32(data, ids + i*8) & 0xffff)
        name = data[at:data.index(0, at)].decode('ascii')
        parent = struct.unpack_from('<h', data, parents + i*2)[0]
        assert -1 <= parent < i
        values = struct.unpack_from('<12f', data, qs + i*48)
        x, y, z, w = values[4:8]
        m = np.eye(4, dtype=np.float32)
        # Row-vector transform, matching retail QsTransform conversion.
        m[:3, :3] = np.array([
            [1-2*(y*y+z*z), 2*(x*y+z*w), 2*(x*z-y*w)],
            [2*(x*y-z*w), 1-2*(x*x+z*z), 2*(y*z+x*w)],
            [2*(x*z+y*w), 2*(y*z-x*w), 1-2*(x*x+y*y)]])
        m[:3, :3] *= np.array(values[8:11])[:, None]
        m[3, :3] = values[:3]
        if parent >= 0:
            m = m @ bones[parent]['matrix']
        bones.append(dict(name=name, parent=parent, matrix=m))
    return bones


def mesh(path):
    data = path.read_bytes()
    assert data[:4] == b'hsem' and data[0x50:0x54] == b'lrtm'
    at = 0x58
    materials = []
    for _ in range(u32(data, 0x54)):
        size = u32(data, at)
        assert 1 < size < 512
        materials.append(data[at+4:at+4+size].rstrip(b'\0').decode('ascii'))
        at += 4+size
    count = u32(data, at)
    at += 4
    subsets = []
    for i in range(count):
        assert data[at:at+4] == b'tsbs'
        material, vbcount = struct.unpack_from('<2I', data, at+4)
        assert material < len(materials) and vbcount == 1
        vb = at+12
        assert data[vb:vb+4] == b'BVXD'
        vertices, usage, fmt = struct.unpack_from('<IBB', data, vb+4)
        assert usage == 0 and fmt in (2, 4) and vertices < 65536
        # The top LOD face embeds a variable morph bank after the VB header.
        # Identify its terminating IB tag, then validate all metadata, totals,
        # vertex ranges and final stream consumption instead of guessing size.
        ib = data.index(b'BIXD', vb+14)
        indexfmt, indices, topology = struct.unpack_from('<3I', data, ib+4)
        assert indexfmt == 0 and topology in (2, 3)
        palette_count = data[ib+16]
        palette = list(data[ib+17:ib+17+palette_count])
        assert (fmt == 2 and not palette) or (fmt == 4 and palette)
        subsets.append(dict(material=materials[material], vertices=vertices,
                            indices=indices, topology=topology, fmt=fmt,
                            palette=palette))
        at = ib+73+palette_count
    # Resource-wide skin range (static meshes use ffff/ffff).
    at += 4
    for sub in subsets:
        fmt, nv, ni = sub['fmt'], sub['vertices'], sub['indices']
        stride = 48 if fmt == 4 else 40
        raw = np.frombuffer(data[at:at+nv*stride], dtype=np.uint8).reshape(nv, stride)
        at += nv*stride
        position_at, normal_at, uv_at = (8, 24, 16) if fmt == 4 else (0, 8, 24)
        def half(offset, n):
            return np.frombuffer(raw[:, offset:offset+n*2].copy(), dtype='<f2').reshape(nv, n).astype('<f4')
        sub['p'], sub['n'], sub['uv'] = half(position_at, 3), half(normal_at, 3), half(uv_at, 2)
        assert np.isfinite(sub['p']).all() and np.max(np.abs(sub['p'])) < 20
        if fmt == 4:
            local = raw[:, :4].copy()
            weights = raw[:, 4:8].astype(np.float32)/255
            assert np.max(np.abs(weights.sum(axis=1)-1)) < .02
            assert np.all((local < len(sub['palette'])) | (weights == 0))
            local[weights == 0] = 0
            sub['joints'] = np.array(sub['palette'], dtype='<u4')[local]
            sub['weights'] = weights / weights.sum(axis=1)[:, None]
        else:
            sub['joints'] = np.zeros((nv, 4), dtype='<u4')
            sub['weights'] = np.tile([1., 0., 0., 0.], (nv, 1)).astype('<f4')
        indices = np.frombuffer(data[at:at+ni*2], dtype='<u2').astype('<u4')
        at += ni*2
        assert np.all(indices < nv)
        if sub['topology'] == 2:
            assert ni % 3 == 0
            tris = indices.reshape(-1, 3)
        else:
            tris = np.array([indices[i:i+3] if i % 2 == 0 else indices[[i+1, i, i+2]]
                             for i in range(ni-2)], dtype='<u4')
            tris = tris[(tris[:, 0] != tris[:, 1]) & (tris[:, 0] != tris[:, 2]) & (tris[:, 1] != tris[:, 2])]
        sub['triangles'] = tris
    assert at == len(data), (path, at, len(data))
    return subsets


def fixed(text, size):
    raw = text.encode('utf-8')
    assert len(raw) < size
    return raw.ljust(size, b'\0')


def build(root, output):
    avatar = root/'characters/bipeds/a01_avatar'
    bones = skeleton(avatar/'rig/a01_avatar.rig')
    head = next(i for i, b in enumerate(bones) if b['name'] == 'Head')
    hidden = []
    for i, b in enumerate(bones):
        ancestor = i
        while ancestor >= 0 and ancestor != head:
            ancestor = bones[ancestor]['parent']
        hidden.append(ancestor == head)
    output.mkdir(parents=True, exist_ok=True)
    textures = {}
    subsets = []
    audit = []
    for group, name in enumerate(('rig/a01_avatar.mesh', 'model/a01_avatar_axe.mesh', 'model/a01_avatar_guitar.mesh')):
        for sub in mesh(avatar/name):
            material_name = sub['material'].rsplit('/', 1)[-1]
            if group == 0 and material_name in ('wing_structure', 'wingwebbing'):
                continue
            if group == 0:
                hidden_weight = (np.array(hidden)[sub['joints']] * sub['weights']).sum(axis=1)
                sub['triangles'] = sub['triangles'][~np.any(hidden_weight[sub['triangles']] > .45, axis=1)]
            if not len(sub['triangles']):
                continue
            text = (root/(sub['material']+'.material')).read_bytes().decode('ascii', errors='ignore')
            match = re.search(r'(?:[;{])DiffuseTexture=@([^;]*);', text)
            texture_name = match.group(1) if match else ''
            if texture_name and texture_name not in textures:
                source = root/(texture_name+'.texture')
                if not source.is_file():
                    raise ValueError(f'Missing diffuse texture {source}')
                raw = source.read_bytes()
                dds_at = raw.index(b'DDS ')
                temporary = output/'decode.dds'
                temporary.write_bytes(raw[dds_at:])
                texture = Image.open(temporary).convert('RGBA')
                # Linear RGBA bytes with an sRGB shader resource view in host.
                filename = texture_name.rsplit('/', 1)[-1]+'.rgba'
                (output/filename).write_bytes(struct.pack('<2I', *texture.size)+texture.tobytes())
                textures[texture_name] = filename
                temporary.unlink()
            match = re.search(r'(?:[;{])DiffuseColor=<([^>]+)>', text)
            color = [float(v) for v in match.group(1).split(',')] if match else [1, 1, 1]
            sub.update(group=group, texture=textures.get(texture_name, ''), color=color+[1])
            subsets.append(sub)
            audit.append(dict(group=group, material=material_name, texture=sub['texture'],
                              vertices=len(sub['p']), triangles=len(sub['triangles']),
                              bounds=[sub['p'].min(axis=0).tolist(), sub['p'].max(axis=0).tolist()]))
    with (output/'eddie.rigcache').open('wb') as f:
        f.write(b'BLVRIG02'+struct.pack('<2I', len(bones), len(subsets)))
        for b in bones:
            f.write(fixed(b['name'], 64)+struct.pack('<i', b['parent'])+b['matrix'].astype('<f4').tobytes())
        for sub in subsets:
            nv, ni = len(sub['p']), sub['triangles'].size
            f.write(struct.pack('<3I', sub['group'], nv, ni)+fixed(sub['texture'], 128)+struct.pack('<4f', *sub['color']))
            v = np.empty(nv, dtype=[('p','<f4',3),('n','<f4',3),('uv','<f4',2),('w','<f4',4),('j','<u4',4)])
            for key, src in [('p','p'),('n','n'),('uv','uv'),('w','weights'),('j','joints')]:
                v[key] = sub[src]
            f.write(v.tobytes())
            f.write(sub['triangles'].astype('<u4').tobytes())
    (output/'audit.json').write_text(json.dumps(dict(bones=[dict(name=b['name'],parent=b['parent'],position=b['matrix'][3,:3].tolist()) for b in bones],subsets=audit), indent=2))
    print(f'Built {len(bones)} bones, {len(subsets)} subsets, {len(textures)} diffuse textures in {output}')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input', type=Path, default=Path('artifacts/eddie-assets'))
    p.add_argument('--output', type=Path, default=Path('artifacts/eddie-rig'))
    args = p.parse_args()
    build(args.input, args.output)
