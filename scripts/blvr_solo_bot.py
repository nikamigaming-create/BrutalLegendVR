"""Select/play the owned Relic Raiser through the simulator's XR controllers.

Targets actual native-rendered fingertip/guitar geometry and reads the native
solo clock. Does not write game state.
"""
import argparse
import math
import json
import struct
import time
from pathlib import Path

import numpy as np
from blvr_bot import Bot
import run_blvr_elliott_proof as sim
from rig_bridge import read_current_rig, read_rendered_ui_pair, RIG_SKIN_OFFSET, RIG_WEAPON_OFFSET

# OpenXR grip points down the grasped tube, not along an aiming ray. The
# simulator has yaw/pitch but no controller roll: these poses put the neck
# across the body, strings up, and the picking hand's fingers forward.
GUITAR_GRIP=dict(yaw=-math.pi/2,pitch=math.pi)
PICKING_GRIP=dict(yaw=0,pitch=math.pi/2)


def rig(bot=None):
    if bot is not None:
        bot.world.live()
    publication=read_current_rig(required_hands=3,
        expected_pid=bot.world.receipt.get('host_pid') if bot is not None else None)
    return publication['frame'],publication['data']


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


def guitar_headstock_y():
    """Use the same owned mesh extent as EddieLobby, without a pose guess."""
    with (sim.ROOT/'artifacts/eddie-rig/eddie.rigcache').open('rb') as stream:
        header=stream.read(16)
        if len(header)!=16 or header[:8]!=b'BLVRIG02':raise RuntimeError('Imported rig cache changed')
        bones,meshes=struct.unpack_from('<2I',header,8)
        if not 0<bones<=256 or meshes>200:raise RuntimeError('Invalid imported rig counts')
        stream.seek(bones*132,1);highest=-math.inf
        for _ in range(meshes):
            header=stream.read(156)
            if len(header)!=156:raise RuntimeError('Truncated imported mesh')
            group,vertices,indices=struct.unpack_from('<3I',header)
            if group>2 or vertices>100000 or indices>500000 or indices%3:
                raise RuntimeError('Invalid imported mesh counts')
            if group==2:
                data=stream.read(vertices*64)
                if len(data)!=vertices*64:raise RuntimeError('Truncated imported guitar')
                positions=np.frombuffer(data,dtype='<f4').reshape(vertices,16)[:,:3]
                if not np.isfinite(positions).all():raise RuntimeError('Invalid imported guitar geometry')
                if vertices:highest=max(highest,float(positions[:,1].max()))
            else:stream.seek(vertices*64,1)
            stream.seek(indices*4,1)
        if not math.isfinite(highest) or highest<=-100:raise RuntimeError('Imported guitar headstock is unavailable')
        return highest


def poke(bot):
    # The deterministic fixture holds the guitar across the torso, brings its
    # neck within the opposite hand's reach, and leaves the right trigger idle.
    try:
        existing=native_solo(bot)()
        if existing['radial'] or existing['notes']: bot.press('b');time.sleep(.3)
    except RuntimeError: pass
    sim.send_head_pose(y=1.7,yaw=0,pitch=0)
    bot.command(0,neutral=True,posX=.12,posY=-.48,posZ=-.28,**GUITAR_GRIP,lease_ms=500)
    bot.command(1,neutral=True,posX=.2,posY=-.3,posZ=-.4,**PICKING_GRIP,lease_ms=500)
    time.sleep(.4)
    bot.press('solo');time.sleep(.4)
    read=native_solo(bot)
    if not read()['radial']: raise RuntimeError('Native solo wheel did not open')
    headstock_y=guitar_headstock_y()
    grip=np.array([.2,-.3,-.4])
    # Upper-left wedge in source UV, then the exact attached surface crop.
    u=.494-.236*.35;v=.417-.278*.60621778
    width=.62
    for depth in (.12,.01):
        for attempt in range(18):
            bot.world.live()
            publication=read_rendered_ui_pair(headstock_y,bot.receipt['game_pid'],bot.receipt['host_pid'])
            bot.world.live()
            height=.62*(.73*publication['ui_height'])/(.54*publication['ui_width'])
            local=np.array([((u-.23)/.54-.5)*width,(.5-(v-.10)/.73)*height,depth,1.])
            eye=publication['eyes'][0]
            plane=np.array(eye['screen']).reshape(4,4)
            tip=np.array(eye['right_tip'])
            target=local@plane;error=target[:3]-tip
            bot.event('finger-target',transaction=publication['transaction'],source=eye['source_frame'],
                frame=eye['pose_frame'],display_time=eye['display_time'],depth=depth,
                geometry='exact native rendered pair',error=float(np.linalg.norm(error)),
                tip=tip.tolist(),target=target[:3].tolist())
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
