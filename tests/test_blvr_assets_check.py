"""Corrupt imported geometry must fail before starting the native host."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from blvr_assets_check import validate

IDENTITY=(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)
NAMES=['Head','Lf_Eye','Rt_Eye']+[f'{side}_{joint}' for side in ('Lf','Rt')
       for joint in ('Shoulder','Elbow','Forearm','Wrist','Pinky1','Index1',
                     'Index2','Index3','Middle1','Middle2','Middle3')]

def model(texture='skin.rgba',joint=0,index=0):
    bones=b''.join(struct.pack('<64si16f',name.encode(),-1,*IDENTITY) for name in NAMES)
    mesh=struct.pack('<3I128s4f',0,1,3,texture.encode(),1,1,1,1)
    vertex=struct.pack('<12f4I',*(0,0,0,0,0,1,0,0,1,0,0,0),joint,0,0,0)
    return struct.pack('<8s2I',b'BLVRIG02',len(NAMES),1)+bones+mesh+vertex+struct.pack('<3I',index,0,0)

class AssetsTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.folder=self.root/'artifacts/eddie-rig';self.folder.mkdir(parents=True)
        self.cache=self.folder/'eddie.rigcache';self.cache.write_bytes(model())
        self.texture=self.folder/'skin.rgba';self.texture.write_bytes(struct.pack('<2I',1,1)+b'\xff'*4)
    def tearDown(self):self.temp.cleanup()
    def test_complete_owned_cache(self):
        self.assertEqual(validate(self.root),dict(bones=len(NAMES),meshes=1,textures=1))
    def test_interrupted_cache_and_trailing_data(self):
        good=model()
        for data in (b'',good[:16],good[:-1],good+b'garbage'):
            with self.subTest(size=len(data)):
                self.cache.write_bytes(data)
                with self.assertRaises(ValueError):validate(self.root)
    def test_bad_triangle_or_skin_joint(self):
        for data in (model(index=1),model(joint=len(NAMES))):
            self.cache.write_bytes(data)
            with self.assertRaises(ValueError):validate(self.root)
    def test_texture_paths_and_missing_or_truncated_texture(self):
        for name in ('../skin.rgba','C:skin.rgba','..','.'):
            self.cache.write_bytes(model(texture=name))
            with self.assertRaises(ValueError):validate(self.root)
        self.cache.write_bytes(model());self.texture.unlink()
        with self.assertRaises(OSError):validate(self.root)
        self.texture.write_bytes(struct.pack('<2I',2,2)+b'\xff'*4)
        with self.assertRaises(ValueError):validate(self.root)
    def test_missing_joint_and_nonfinite_skeleton(self):
        data=bytearray(model());data[16:80]=b'Unknown'.ljust(64,b'\0');self.cache.write_bytes(data)
        with self.assertRaises(ValueError):validate(self.root)
        data=bytearray(model());struct.pack_into('<f',data,84,float('nan'));self.cache.write_bytes(data)
        with self.assertRaises(ValueError):validate(self.root)

if __name__=='__main__':unittest.main()
