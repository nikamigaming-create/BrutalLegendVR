"""Cache retail-evaluated Eddie animation poses from an explicit diagnostic game.

Run after Continue has loaded with the simulator, never against a headset user.
The retail animation system evaluates each action and all finger/weapon tracks.
Only the exact player's skeleton and inventory-owned weapon transforms are read.
This local, owned-data cache must not be distributed.
"""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import time

import numpy as np
import run_blvr_elliott_proof as sim


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--output', type=Path, default=Path('artifacts/eddie-rig'))
    args = parser.parse_args()
    receipt = json.loads(Path('artifacts/current-stereo-diagnostic.json').read_text())
    assert receipt['game_pid'] == args.pid and receipt['runtime'] == 'Elliott simulator'
    kernel = c.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.restype = c.c_void_p
    kernel.ReadProcessMemory.argtypes = [c.c_void_p,c.c_void_p,c.c_void_p,c.c_size_t,c.c_void_p]
    kernel.CloseHandle.argtypes = [c.c_void_p]
    native = c.WinDLL('ntdll')
    native.NtSuspendProcess.argtypes = native.NtResumeProcess.argtypes = [c.c_void_p]
    handle = kernel.OpenProcess(0xc10,False,args.pid)
    if not handle: raise OSError(c.get_last_error())
    def read(address, size):
        out = c.create_string_buffer(size)
        if not kernel.ReadProcessMemory(handle,address,out,size,None): raise OSError(hex(address))
        return out.raw
    def u32(address): return struct.unpack('<I',read(address,4))[0]
    def string(address): return read(address,128).split(b'\0')[0].decode('ascii')
    def rtti(address): return string(u32(u32(u32(address)-4)+12)+8)
    base = int(subprocess.check_output(['powershell','-NoProfile','-Command',f'(Get-Process -Id {args.pid}).Modules[0].BaseAddress.ToInt64()']).decode())
    log = sim.GAME_LOG.read_text()
    actor = int(re.findall(r'Player owner: .*actor=([A-F0-9]+)',log)[-1],16)
    mesh = u32(actor+0x38)
    assert u32(mesh+0x10)==actor and rtti(mesh)=='.?AVCoRenderMesh@@'
    sk = u32(actor+0x24)
    assert u32(sk+0x10)==actor and rtti(sk)=='.?AVCoSkeleton@@'
    animation = u32(sk+0x24)
    resource = u32(animation+4)
    pose = u32(animation+0x50)
    count = u32(resource+0x14)
    assert count == 212
    names = u32(resource+8); identifiers=u32(resource+0x18)
    bone_names = [string(names+(u32(identifiers+i*8)&0xffff)) for i in range(count)]
    inventory = u32(actor+0x64)
    assert rtti(inventory)=='.?AVCoInventory@@' and u32(inventory+0x10)==actor
    table=u32(base+0xb79d8c)
    weapons=[]
    for offset,expected in ((0x20,'A01_AvatarAxe'),(0x38,'A01_AvatarGuitar')):
        entity=u32(table+u32(inventory+offset)*12)
        assert string(u32(u32(u32(entity+4)+4)))==expected
        component=u32(entity+0x38)
        assert u32(component+0x10)==entity and rtti(component)=='.?AVCoRenderMesh@@'
        weapons.append(component)
    args.output.mkdir(parents=True,exist_ok=True)
    sequence=int(time.time()*1000)&0x7fffffff
    def command(hand,**fields):
        nonlocal sequence
        sequence+=1;sim.send_controller(sequence,hand,**fields)
    def sample():
        assert native.NtSuspendProcess(handle)>=0
        try:
            # One coherent retail update; always resume even on failed reads.
            data=read(u32(pose+0x28),count*48)
            world=np.frombuffer(read(mesh+0x70,64),dtype='<f4').reshape(4,4)
            transforms=[np.frombuffer(read(m+0x70,64),dtype='<f4').reshape(4,4) for m in weapons]
        finally: native.NtResumeProcess(handle)
        inverse=np.linalg.inv(world)
        return data,b''.join((w@inverse).astype('<f4').tobytes() for w in transforms)
    clips=[]
    try:
        for name,hand,fields,duration in (
            ('idle',2,dict(neutral=True,lease_ms=1000),1.0),
            ('axe',1,dict(trigger=1,lease_ms=170),1.8),
            ('guitar',0,dict(trigger=1,lease_ms=220),1.8),
            ('walk',0,dict(stickY=1,lease_ms=1600),1.6)):
            command(2,neutral=True,lease_ms=600);time.sleep(.65)
            start=time.perf_counter();frames=[(0.,*sample())]
            command(hand,**fields)
            while time.perf_counter()-start<duration:
                frames.append((time.perf_counter()-start,*sample()))
                time.sleep(1/90)
            command(2,neutral=True,lease_ms=600)
            destination=args.output/(name+'.actionclip')
            with destination.open('wb') as stream:
                stream.write(b'BLACT001'+struct.pack('<2I',len(frames),count))
                for timestamp,bones,attachments in frames:
                    stream.write(struct.pack('<f',timestamp)+bones+attachments)
            clips.append(dict(name=name,frames=len(frames),seconds=frames[-1][0],action=fields,
                              sha256=hashlib.sha256(destination.read_bytes()).hexdigest()))
            print(name,len(frames),round(frames[-1][0],3),flush=True)
        (args.output/'action-provenance.json').write_text(json.dumps(dict(pid=args.pid,session=receipt,
            executable_sha256=hashlib.sha256(sim.GAME_EXE.read_bytes()).hexdigest(),
            actor=hex(actor),bone_names=bone_names,clips=clips),indent=2))
    finally:
        command(2,neutral=True,lease_ms=600)
        kernel.CloseHandle(handle)


if __name__=='__main__': main()
