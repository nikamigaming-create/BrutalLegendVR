"""Bounded offline validation of the user's disposable model cache."""
import math
from pathlib import Path
import struct


def validate(root):
    folder=Path(root)/'artifacts/eddie-rig'
    cache=folder/'eddie.rigcache'
    if not cache.is_file() or not 16 <= cache.stat().st_size <= 64*1024*1024:
        raise ValueError('Owned model cache is missing or damaged. Choose Prepare / repair game.')
    def exact(stream,count):
        data=stream.read(count)
        if len(data)!=count:raise ValueError('Owned model cache is incomplete. Choose Prepare / repair game.')
        return data
    names=set();textures=set()
    with cache.open('rb') as stream:
        magic,bones,meshes=struct.unpack('<8s2I',exact(stream,16))
        if magic!=b'BLVRIG02' or not 0<bones<=256 or not 0<meshes<=200:
            raise ValueError('Owned model cache version or counts are invalid. Repair the game model.')
        for index in range(bones):
            name,parent,*matrix=struct.unpack('<64si16f',exact(stream,132))
            if not -1<=parent<index or not all(math.isfinite(value) for value in matrix):
                raise ValueError('Owned skeleton is damaged. Repair the game model.')
            names.add(name.split(b'\0')[0])
        required={b'Head',b'Lf_Eye',b'Rt_Eye'}
        for side in ('Lf','Rt'):
            required.update(f'{side}_{joint}'.encode() for joint in
                            ('Shoulder','Elbow','Forearm','Wrist','Pinky1',
                             'Index1','Index2','Index3','Middle1','Middle2','Middle3'))
        if not required<=names:raise ValueError('Owned skeleton is missing required joints. Repair the game model.')
        for _ in range(meshes):
            group,vertices,indices,raw_path,*color=struct.unpack('<3I128s4f',exact(stream,156))
            if group>2 or not 0<vertices<=100000 or not 0<indices<=500000 or indices%3 or not all(math.isfinite(v) for v in color):
                raise ValueError('Owned model subset is invalid. Repair the game model.')
            filename=raw_path.split(b'\0')[0].decode('utf-8')
            if filename in {'.','..'} or any(value in filename for value in '/\\:'):
                raise ValueError('Owned cache contains an invalid texture path.')
            if filename:textures.add(filename)
            for vertex in struct.iter_unpack('<12f4I',exact(stream,vertices*64)):
                if not all(math.isfinite(value) for value in vertex[:12]) or any(joint>=bones for joint in vertex[12:]):
                    raise ValueError('Owned mesh skin is damaged. Repair the game model.')
            if any(index>=vertices for index, in struct.iter_unpack('<I',exact(stream,indices*4))):
                raise ValueError('Owned model triangles are damaged. Repair the game model.')
        if stream.read(1):raise ValueError('Owned model cache has unexpected trailing data. Repair the game model.')
    for filename in textures:
        path=folder/filename
        with path.open('rb') as stream:width,height=struct.unpack('<2I',exact(stream,8))
        if not 0<width<=4096 or not 0<height<=4096 or path.stat().st_size!=8+width*height*4:
            raise ValueError(f'Owned texture {filename} is damaged. Repair the game model.')
    return dict(bones=bones,meshes=meshes,textures=len(textures))
