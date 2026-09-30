"""Capture a real single composed eye plus the game's WASAPI endpoint.

Simulator-only, uses acknowledged OpenXR poses/actions and its final compositor
tap. No desktop input or window manipulation. CFR repeats retain wall time;
the CSV records every distinct compositor frame and its actual QPC timestamp.
"""
import argparse,csv,json,math,mmap,pathlib,struct,subprocess,threading,time
import numpy as np
from blvr_bot import Bot
from blvr_solo_bot import rig,fingertip_reference,GUITAR_GRIP,PICKING_GRIP
import run_blvr_elliott_proof as sim

ROOT=pathlib.Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts'/'showcase-2026-09-27'
CAPACITY=64+1920*1080*4

class Recorder:
    def __init__(self,name,pid):
        OUT.mkdir(exist_ok=True);self.name=name;self.stop=threading.Event();self.ready=threading.Event();self.error=None
        self.stopfile=OUT/(name+'.stop');self.stopfile.unlink(missing_ok=True)
        self.audio=subprocess.Popen([str(ROOT/'tools/blvr_audio_record.exe'),str(pid),str(OUT/(name+'.wav')),'600',str(self.stopfile)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,creationflags=subprocess.CREATE_NO_WINDOW)
        self.thread=threading.Thread(target=self.run);self.thread.start();self.ready.wait(15)
        if self.error:raise self.error
    def read(self,view):
        for _ in range(30):
            seq,w,h,fmt,qpc,frame=struct.unpack_from('<IIIIdQ',view)
            if not seq or seq&1 or not w:time.sleep(.001);continue
            pixels=view[64:64+w*h*4]
            if struct.unpack_from('<I',view)[0]==seq:return w,h,fmt,qpc,frame,pixels
        raise RuntimeError('No coherent final-eye frame')
    def run(self):
        try:
            with mmap.mmap(-1,CAPACITY,tagname=r'Local\BLVR_Showcase_FinalEye',access=mmap.ACCESS_READ) as view:
                w,h,fmt,*_=self.read(view)
                log=(OUT/(self.name+'-encode.log')).open('w')
                encoder=subprocess.Popen(['ffmpeg','-hide_banner','-loglevel','warning','-y','-f','rawvideo','-pixel_format','rgba' if fmt in (28,29) else 'bgra','-video_size',f'{w}x{h}','-framerate','30','-i','pipe:0','-an','-c:v','libx264','-preset','veryfast','-crf','17','-pix_fmt','yuv420p',str(OUT/(self.name+'.mp4'))],stdin=subprocess.PIPE,stdout=subprocess.DEVNULL,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
                self.t0=time.perf_counter();self.ready.set();index=0;seen=set()
                with (OUT/(self.name+'-frames.csv')).open('w',newline='') as file:
                    rows=csv.writer(file);rows.writerow(['video_frame','video_qpc','source_qpc','compositor_frame','age_ms'])
                    while not self.stop.is_set():
                        due=self.t0+index/30
                        time.sleep(max(0,due-time.perf_counter()))
                        width,height,_,qpc,frame,pixels=self.read(view)
                        assert(width,height)==(w,h)
                        encoder.stdin.write(pixels);seen.add(frame)
                        rows.writerow([index,due,qpc,frame,(due-qpc)*1000]);index+=1
                encoder.stdin.close();assert encoder.wait()==0;log.close()
                self.summary=dict(width=w,height=h,frames=index,seconds=index/30,distinct_compositor_frames=len(seen),video_start_qpc=self.t0,source='Simulator D3D11 compositor after all OpenXR layers; left eye',cadence='30 fps wall-clock output, source frame identities in CSV')
        except Exception as error:self.error=error;self.ready.set()
    def mark(self,name):
        print(json.dumps(dict(shot=name,at=round(time.perf_counter()-self.t0,3))),flush=True)
    def close(self):
        self.stop.set();self.thread.join();self.stopfile.write_text('stop')
        audio,error=self.audio.communicate(timeout=10)
        if self.error:raise self.error
        if self.audio.returncode:raise RuntimeError(f'Audio failed {self.audio.returncode}: {error!r}')
        self.summary['audio']=json.loads(audio);self.summary['audio']['capture']='WASAPI loopback of game process render endpoint, no microphone'
        (OUT/(self.name+'.json')).write_text(json.dumps(self.summary,indent=2));print(json.dumps(self.summary),flush=True)

def head(**kw):sim.send_head_pose(y=1.7,**kw)
def pose(bot,hand,xyz,orientation=PICKING_GRIP,**kw):
    # Simulator controller positions are offsets from the HMD, not stage positions.
    bot.command(hand,posX=float(xyz[0]),posY=float(xyz[1]),posZ=float(xyz[2]),**orientation,lease_ms=4000,**kw)
def sweep(yaw,pitch,frequency=.035):
    sim.clear_file(sim.ACK_FILE);sim.write_atomic(sim.SWEEP_FILE,dict(enabled=True,yaw_amp_deg=yaw,pitch_amp_deg=pitch,roll_amp_deg=0,freq_hz=frequency));sim.wait_for_ack('pose_sweep')
def no_sweep():
    sim.clear_file(sim.ACK_FILE);sim.write_atomic(sim.SWEEP_FILE,dict(enabled=False));sim.wait_for_ack('pose_sweep')

def room(bot):
    no_sweep();head(pitch=-.22)
    pose(bot,0,[-.28,-.45,-.38]);pose(bot,1,[.28,-.45,-.38]);bot.release()
    # Start with empty hands even when repeating a take.
    with mmap.mmap(-1,384,tagname=r'Local\BLVR_XR_PoseBridge_v1',access=mmap.ACCESS_READ) as p:
        selected=struct.unpack_from('<I',p,376)[0]
    if selected in (1,2):bot.press('x' if selected==1 else 'y')
    record=Recorder('01-room',bot.receipt['game_pid'])
    try:
        record.mark('room');sweep(24,8,.027);time.sleep(8)
        no_sweep();head(pitch=-.3);record.mark('hands')
        for i in range(36):
            t=i/35;pose(bot,1,[.28+.09*math.sin(t*math.pi*2),-.37+.10*math.sin(t*math.pi),-.40-.05*math.sin(t*math.pi)],dict(yaw=.2*math.sin(t*math.pi*2),pitch=math.pi/2+.15*math.sin(t*math.pi)));time.sleep(.04)
        bot.press('x');record.mark('axe');time.sleep(.8)
        for i in range(65):
            t=i/64;pose(bot,1,[.26+.09*math.sin(t*math.pi*2),-.36+.12*math.sin(t*math.pi),-.40],dict(yaw=.25*math.sin(t*math.pi*2),pitch=math.pi/2+.20*math.sin(t*math.pi*2)));time.sleep(.04)
        bot.press('x');pose(bot,0,[-.28,-.45,-.38],GUITAR_GRIP);pose(bot,1,[.2,-.3,-.4]);bot.press('y');head(pitch=-.47);time.sleep(.7)
        record.mark('guitar')
        for i in range(90):
            t=i/89;pose(bot,1,[.25,-.40+.09*math.sin(t*math.pi*12),-.38]);time.sleep(.035)
        time.sleep(2)
    finally:record.close();bot.release();no_sweep()

def world(bot):
    bot.enter_game(skip_cutscenes=True);bot.wait_gameplay(timeout=120)
    no_sweep();head(pitch=-.15);pose(bot,0,[-.28,-.45,-.38]);pose(bot,1,[.28,-.45,-.38]);bot.release()
    record=Recorder('02-world',bot.receipt['game_pid'])
    try:
        record.mark('world');sweep(15,7,.035);time.sleep(8);no_sweep();head(pitch=-.25)
        record.mark('locomotion');bot.command(0,stickY=.7,lease_ms=3500);time.sleep(3.5);bot.release();time.sleep(2)
        record.mark('tracked-axe');bot.press('x');time.sleep(.8)
        for i in range(45):
            t=i/44;pose(bot,1,[.27+.08*math.sin(t*math.pi*2),-.36+.07*math.sin(t*math.pi*2),-.42],dict(yaw=.18*math.sin(t*math.pi*2),pitch=math.pi/2+.25*math.sin(t*math.pi*2)));time.sleep(.035)
        bot.press('attack');time.sleep(1);bot.press('attack');time.sleep(2)
        record.mark('outlook');sweep(18,6,.035);time.sleep(7)
    finally:record.close();bot.release();no_sweep()

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('scene',choices=['room','world']);args=parser.parse_args()
    bot=Bot()
    try:(room if args.scene=='room' else world)(bot)
    finally:bot.close()
