"""Select/play the owned Relic Raiser through the simulator's XR controllers.

Reads the published solved rig and native solo clock. Does not write game state.
"""
import argparse
import math
import json
import mmap
import struct
import time
from pathlib import Path

import numpy as np
from blvr_bot import Bot
import run_blvr_elliott_proof as sim

# OpenXR grip points down the grasped tube, not along an aiming ray. The
# simulator has yaw/pitch but no controller roll: these poses put the neck
# across the body, strings up, and the picking hand's fingers forward.
GUITAR_GRIP=dict(yaw=-math.pi/2,pitch=math.pi)
PICKING_GRIP=dict(yaw=0,pitch=math.pi/2)


def rig():
    with mmap.mmap(-1,384,tagname=r'Local\BLVR_XR_PoseBridge_v1',access=mmap.ACCESS_READ) as poses:
        for _ in range(20):
            pose=poses[:];sequence=struct.unpack_from('<I',pose,16)[0]
            if not sequence&1 and struct.unpack_from('<I',poses,16)[0]==sequence:break
        else:raise RuntimeError('No coherent pose producer')
        producer_epoch,latest=struct.unpack_from('<QQ',pose,24)
    with mmap.mmap(-1,16560*64,tagname=r'Local\BLVR_TrackedEddie_v1',access=mmap.ACCESS_READ) as view:
        candidates=[]
        for i in range(64):
            data=view[i*16560:(i+1)*16560]
            seq,magic,epoch,frame=struct.unpack_from('<IIQQ',data)
            if magic==0x47495242 and epoch==producer_epoch and frame<=latest and not seq&1 and struct.unpack_from('<I',view,i*16560)[0]==seq:
                candidates.append((frame,data))
    if not candidates: raise RuntimeError('No coherent tracked rig')
    frame,data=max(candidates)
    return frame,data


def native_solo(bot):
    def resolve():
        hud=None
        with bot.world.telemetry.open('rb') as stream:
            for line in stream:
                if b'"kind":"solo"' in line:
                    hud=json.loads(line)['hud']
        return hud
    hud=resolve()
    if not hud: raise RuntimeError('Native solo has not opened in this session')
    def read():
        nonlocal hud
        bot.world.live()
        for attempt in range(3):
            try:
                data=bot.world.read(hud,0x90)
                result=dict(radial=data[0x74],notes=data[0x75],finished=data[0x77],
                            success=data[0x11],count=struct.unpack_from('<I',data,0x4c)[0],
                            clock=struct.unpack_from('<f',data,0x88)[0])
                if all(result[key] in (0,1) for key in ('radial','notes','finished','success')) and result['count']<128 and math.isfinite(result['clock']):
                    return result
            except OSError:pass
            # Loading a checkpoint replaces the native HUD. A freed object
            # must never be treated as a successful interaction.
            time.sleep(.05);hud=resolve()
            if not hud:break
        raise RuntimeError('Native solo HUD changed or became invalid')
    return read


def fingertip_reference():
    data=(sim.ROOT/'artifacts/eddie-rig/eddie.rigcache').read_bytes()
    count=struct.unpack_from('<I',data,8)[0];bones={}
    for i in range(count):
        offset=16+i*132
        name=data[offset:offset+64].split(b'\0')[0].decode()
        bones[name]=(i,np.array(struct.unpack_from('<16f',data,offset+68)).reshape(4,4))
    index,tip=bones['Rt_Index3'];previous=bones['Rt_Index2'][1]
    return index,tip[3]+(tip[3]-previous[3])*.72


def poke(bot):
    # The deterministic fixture holds the guitar across the torso, brings its
    # neck within the opposite hand's reach, and leaves the right trigger idle.
    try:
        existing=native_solo(bot)()
        if existing['radial'] or existing['notes']: bot.press('b');time.sleep(.3)
    except RuntimeError: pass
    sim.send_head_pose(yaw=0,pitch=0)
    bot.command(0,neutral=True,posX=.12,posY=-.48,posZ=-.28,**GUITAR_GRIP,lease_ms=500)
    bot.command(1,neutral=True,posX=.2,posY=-.3,posZ=-.4,**PICKING_GRIP,lease_ms=500)
    time.sleep(.4)
    bot.press('solo');time.sleep(.4)
    read=native_solo(bot)
    if not read()['radial']: raise RuntimeError('Native solo wheel did not open')
    index,reference=fingertip_reference()
    grip=np.array([.2,-.3,-.4])
    # Upper-left wedge in source UV, then the exact attached surface crop.
    u=.494-.236*.35;v=.417-.278*.60621778
    width=.62;height=.62*(.73*720)/(.54*1280)
    local=np.array([((u-.23)/.54-.5)*width,(.5-(v-.10)/.73)*height,.12,1.])
    for depth in (.12,.01):
        local[2]=depth
        for attempt in range(18):
            frame,data=rig()
            guitar=np.array(struct.unpack_from('<16f',data,48+16384+64)).reshape(4,4)
            plane=np.array([[0,-1,0,0],[0,0,1,0],[-1,0,0,0],[0,.84189453,.24,1.]])@guitar
            # UI quad extents are meters; its mount discards skeleton scale.
            plane[:3,:3]/=np.linalg.norm(plane[:3,:3],axis=1)[:,None]
            tip=reference@np.array(struct.unpack_from('<16f',data,48+index*64)).reshape(4,4)
            target=local@plane;error=target[:3]-tip[:3]
            bot.event('finger-target',frame=frame,depth=depth,error=float(np.linalg.norm(error)),tip=tip[:3].tolist(),target=target[:3].tolist())
            if np.linalg.norm(error)<.008: break
            grip+=np.clip(error,-.09,.09)*.8
            if np.linalg.norm(grip)>1.3: raise RuntimeError('Finger target exceeds controller reach fixture')
            bot.command(1,neutral=True,posX=float(grip[0]),posY=float(grip[1]),posZ=float(grip[2]),**PICKING_GRIP,lease_ms=400)
            time.sleep(.16)
        else: raise RuntimeError('Solved fingertip could not reach guitar choice')
        bot.capture('guitar-finger-hover' if depth>.08 else 'guitar-finger-contact')
        time.sleep(.2)
    deadline=time.monotonic()+2
    while time.monotonic()<deadline and not read()['notes']: time.sleep(.02)
    if not read()['notes']: raise RuntimeError('Finger contact did not enter native timed notes')
    bot.event('finger-selection-complete',native=read())
    bot.capture('guitar-finger-selected-notes')


def play(bot):
    read=native_solo(bot)
    if not read()['notes'] or read()['count']: raise RuntimeError('Solo must await its first note')
    bot.press('a',.10)
    for target,name in [(1.8,'x'),(2.9000001,'y'),(3.7119999,'x')]:
        deadline=time.monotonic()+6
        while read()['clock']<target-.07 and time.monotonic()<deadline:
            if not read()['notes']: raise RuntimeError('Solo ended before note')
            time.sleep(.005)
        if time.monotonic()>=deadline: raise RuntimeError('Native solo clock stalled')
        bot.event('solo-note',target=target,native=read(),button=name)
        bot.press(name,.08)
    time.sleep(2)
    result=read();bot.event('solo-result',native=result)
    if not result['success'] or result['count']!=4: raise RuntimeError('Native game rejected the solo')
    bot.capture('relic-performed')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=['poke','play'])
    args=parser.parse_args();bot=Bot()
    try:
        (poke if args.command=='poke' else play)(bot)
    finally: bot.close()
