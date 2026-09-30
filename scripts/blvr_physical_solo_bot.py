"""Play the native solo using left-hand frets and right-hand physical strokes."""
import mmap,struct,time
import numpy as np
from PIL import Image
from blvr_bot import Bot
from blvr_solo_bot import rig,fingertip_reference,native_solo,GUITAR_GRIP,PICKING_GRIP
import run_blvr_elliott_proof as sim


def capture_ui(bot,label):
    with mmap.mmap(-1,64+1280*720*4,tagname=r'Local\BLVR_NativeUi_v2',access=mmap.ACCESS_READ) as view:
        for _ in range(100):
            header=view[:64];sequence=struct.unpack_from('<I',header,12)[0]
            if sequence&1:continue
            width,height,row=struct.unpack_from('<3I',header,20)
            if not 0<width<=1280 or not 0<height<=720:raise RuntimeError('Invalid UI dimensions')
            pixels=view[64:64+height*row]
            if struct.unpack_from('<I',view,12)[0]==sequence:
                Image.frombytes('RGBA',(width,height),pixels,'raw','BGRA',row).save(bot.out/(label+'.png'))
                return
    raise RuntimeError('No coherent UI publication')


def play(bot):
    read=native_solo(bot)
    if not read()['notes'] or read()['count']:raise RuntimeError('Solo must await its first note')
    sim.send_head_pose(yaw=0,pitch=0)
    # Withdraw above the instrument before repositioning the fretting hand.
    # Approaching through the string plane would itself be a real first note.
    bot.command(1,neutral=True,posX=.7,posY=.15,posZ=-.5,**PICKING_GRIP,lease_ms=500)
    bot.command(0,squeeze=1,posX=.12,posY=-.48,posZ=-.50,**GUITAR_GRIP,lease_ms=10000)
    time.sleep(.5)
    bot.capture('physical-solo-headstock-notes')
    bot.command(0,squeeze=1,posX=-.28,posY=-.45,posZ=-.38,**GUITAR_GRIP,lease_ms=10000)
    time.sleep(.5)
    index,reference=fingertip_reference();grip=np.array([.7,.15,-.5])
    def current():
        frame,data=rig()
        guitar=np.array(struct.unpack_from('<16f',data,48+16384+64)).reshape(4,4)
        tip=reference@np.array(struct.unpack_from('<16f',data,48+index*64)).reshape(4,4)
        return guitar,tip
    for depth in (.32,.06):
        for i in range(18):
            guitar,tip=current();target=np.array([-.05,-.25,depth,1.])@guitar
            error=target[:3]-tip[:3]
            if np.linalg.norm(error)<.004:break
            grip+=np.clip(error,-.09,.09)*.9
            bot.command(1,neutral=True,posX=float(grip[0]),posY=float(grip[1]),posZ=float(grip[2]),**PICKING_GRIP,lease_ms=500)
            time.sleep(.12)
        else:raise RuntimeError('Could not stage the picking hand')
    time.sleep(.5)
    if read()['count']:raise RuntimeError('Staging accidentally played a note')
    capture_ui(bot,'physical-solo-native-notes')
    bot.capture('physical-solo-ready')
    for i,(target,note) in enumerate([(0,1),(1.8,2),(2.9000001,3),(3.7119999,2)]):
        fret={'primary':True} if note==2 else {'secondary':True} if note==3 else {}
        bot.command(0,squeeze=1,lease_ms=6000,**fret)
        deadline=time.monotonic()+6
        while read()['clock']<target-.07:
            if time.monotonic()>deadline:raise RuntimeError('Native solo clock stalled')
            if not read()['notes']:raise RuntimeError('Native solo ended before physical note')
            time.sleep(.005)
        guitar,tip=current()
        destination=np.array([.05 if i%2==0 else -.05,-.25,.06,1.])@guitar
        grip+=destination[:3]-tip[:3]
        before=read()
        bot.command(1,neutral=True,posX=float(grip[0]),posY=float(grip[1]),posZ=float(grip[2]),**PICKING_GRIP,lease_ms=500)
        deadline=time.monotonic()+.5
        while read()['count']==i and time.monotonic()<deadline:time.sleep(.005)
        after=read();bot.event('physical-solo-note',note=note,before=before,after=after)
        if after['count']!=i+1:raise RuntimeError('Native solo rejected the right-hand stroke')
    time.sleep(1)
    result=read();bot.event('physical-solo-result',native=result)
    if not result['success'] or result['count']!=4:raise RuntimeError('Physical solo did not succeed')
    bot.capture('physical-relic-performed')


if __name__=='__main__':
    bot=Bot()
    try:play(bot)
    finally:bot.close()
