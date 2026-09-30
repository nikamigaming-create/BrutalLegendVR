"""Exercise native right-wheel grip and freely tracked left arm in the simulator.

Requires an already mounted, stationary Eddie. Reads the real hand publication;
only simulator OpenXR head/controller input is changed. Never drives or saves.
"""
import ctypes as c
import math
import struct
import time

from blvr_bot import Bot
import run_blvr_elliott_proof as sim


def native_hands(bot):
    k=bot.world.k
    k.OpenFileMappingW.argtypes=[c.c_uint,c.c_int,c.c_wchar_p]
    k.OpenFileMappingW.restype=c.c_void_p
    k.MapViewOfFile.argtypes=[c.c_void_p,c.c_uint,c.c_uint,c.c_uint,c.c_size_t]
    k.MapViewOfFile.restype=c.c_void_p
    k.UnmapViewOfFile.argtypes=[c.c_void_p]
    handle=k.OpenFileMappingW(4,False,r'Local\BLVR_NativeDrivingHands_v1')
    if not handle:raise RuntimeError('No native driving-hand publication')
    ptr=k.MapViewOfFile(handle,4,0,0,12800)
    try:
        if not ptr:raise OSError(c.get_last_error())
        frames=[]
        for slot in range(64):
            address=ptr+slot*200
            data=c.string_at(address,200)
            seq,magic,epoch,source,pose,timestamp,valid=struct.unpack_from('<IIQQQqI',data)
            if seq&1 or seq!=c.c_uint.from_address(address).value:continue
            if magic!=0x48444e42 or valid!=3:continue
            frames.append(dict(source=source,pose=pose,epoch=epoch,display_time=timestamp,
                left=struct.unpack_from('<16f',data,48),right=struct.unpack_from('<16f',data,112)))
        if not frames:raise RuntimeError('No coherent mounted hands')
        return max(frames,key=lambda frame:frame['source'])
    finally:
        if ptr:k.UnmapViewOfFile(ptr)
        k.CloseHandle(handle)


def run(bot):
    bot.wait_gameplay(5)
    first=native_hands(bot)
    start=time.monotonic()
    for sample in range(10):
        head_yaw=.55+.15*math.sin(sample*.7)
        head_pitch=-.18
        sim.send_head_pose(y=0,yaw=head_yaw,pitch=head_pitch)
        for step in range(12):
            phase=(sample+step/12)*1.7
            # Raise the free left arm outside the driver's side. Fingers point
            # upward, palm outward. Keep poses fixed in LOCAL while looking.
            x,y,z=-.48-.10*math.sin(phase),-.10+.10*math.cos(phase),-.28
            bot.command(0,neutral=True,
                posX=x*math.cos(head_yaw)+z*math.sin(head_yaw),posY=y,
                posZ=-x*math.sin(head_yaw)+z*math.cos(head_yaw),
                yaw=math.pi-head_yaw+.12*math.sin(phase),pitch=math.pi-head_pitch,
                lease_ms=500)
            time.sleep(.025)
        actual=native_hands(bot)
        if actual['source']<=first['source']:raise RuntimeError('Driving hands stopped advancing')
        bot.event('free-left-wave',sample=sample,head_yaw=head_yaw,head_pitch=head_pitch,**actual)
        bot.capture('free-left-wave-%02d'%sample)
    bot.event('free-left-wave-complete',seconds=time.monotonic()-start,
              evidence='Final compositor both eyes; inspect wheel contact and left-arm attachment')


if __name__=='__main__':
    bot=Bot()
    try:run(bot)
    finally:bot.close()
