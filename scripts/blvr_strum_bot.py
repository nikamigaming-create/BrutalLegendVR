"""Exercise picking-hand string crossings through the simulator's XR poses.

The left controller holds the neck. Only the right controller makes the stroke.
Observe the actual host pulse and native input; never write either publication.
"""
import json
import mmap
import re
import struct
import time
import numpy as np
from blvr_bot import Bot
from blvr_solo_bot import rig,fingertip_reference,GUITAR_GRIP,PICKING_GRIP
import run_blvr_elliott_proof as sim


def pose():
    with mmap.mmap(-1,384,tagname=r'Local\BLVR_XR_PoseBridge_v1',access=mmap.ACCESS_READ) as view:
        for _ in range(30):
            data=view[:];sequence=struct.unpack_from('<I',data,16)[0]
            if not sequence&1 and struct.unpack_from('<I',view,16)[0]==sequence:return data
            time.sleep(.001)
    raise RuntimeError('No coherent XR pose')


def run(bot):
    sim.send_head_pose(yaw=0,pitch=0)
    bot.command(0,neutral=True,posX=-.28,posY=-.45,posZ=-.38,**GUITAR_GRIP,lease_ms=500)
    bot.command(1,neutral=True,posX=.2,posY=-.3,posZ=-.4,**PICKING_GRIP,lease_ms=500)
    time.sleep(.4)
    if struct.unpack_from('<I',pose(),376)[0]!=2:bot.press('y')
    time.sleep(.4)
    if struct.unpack_from('<I',pose(),376)[0]!=2:raise RuntimeError('Guitar did not equip')
    index,reference=fingertip_reference();grip=np.array([.2,-.3,-.4])

    def locate(x):
        frame,data=rig()
        guitar=np.array(struct.unpack_from('<16f',data,48+16384+64)).reshape(4,4)
        tip=reference@np.array(struct.unpack_from('<16f',data,48+index*64)).reshape(4,4)
        target=np.array([x,-.25,.06,1.])@guitar
        return frame,guitar,tip,target

    for i in range(18):
        frame,guitar,tip,target=locate(-.085)
        error=target[:3]-tip[:3]
        if np.linalg.norm(error)<.006:break
        grip+=np.clip(error,-.09,.09)*.85
        if np.linalg.norm(grip)>1.3:raise RuntimeError('Picking hand exceeds reach')
        bot.command(1,neutral=True,posX=float(grip[0]),posY=float(grip[1]),posZ=float(grip[2]),**PICKING_GRIP,lease_ms=400)
        time.sleep(.10)
    else:raise RuntimeError('Picking fingertip failed to reach strings')
    bot.event('strum-start',frame=frame,tip=tip[:3].tolist(),target=target[:3].tolist())
    bot.capture('right-picking-start')
    time.sleep(.5)
    for stroke,(start,end) in enumerate([(-.085,.085),(.085,-.085)]):
        frame,guitar,tip,target=locate(start)
        begin=grip.copy();delta=guitar[0,:3]*(end-start)
        observed=[]
        for i in range(1,8):
            grip=begin+delta*(i/7)
            bot.command(1,neutral=True,posX=float(grip[0]),posY=float(grip[1]),posZ=float(grip[2]),**PICKING_GRIP,lease_ms=300)
            for _ in range(4):
                published=pose()
                observed.append(dict(frame=struct.unpack_from('<Q',published,32)[0],physical=struct.unpack_from('<I',published,380)[0],trigger=struct.unpack_from('<f',published,352)[0],buttons=struct.unpack_from('<I',published,368)[0]))
                time.sleep(.004)
        if not any(p['physical']==2 for p in observed):
            bot.event('strum-failed',stroke=stroke,observed=observed)
            raise RuntimeError('Right-hand stroke did not publish a guitar action')
        if any(p['trigger'] or p['buttons'] for p in observed):raise RuntimeError('Stroke used an unintended button')
        bot.event('physical-strum',direction='down' if stroke==0 else 'up',observed=observed)
        bot.capture('right-picking-'+str(stroke))
        time.sleep(.6)
    bot.event('right-hand-strum-complete',native_effect='Must be verified separately in an eligible on-foot gameplay state')


if __name__=='__main__':
    bot=Bot()
    try:run(bot)
    finally:bot.close()
