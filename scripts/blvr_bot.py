"""Session-scoped BLVR gameplay operator. Read native state; act only through XR.

The planner uses observed traversals and collision feedback, not fabricated
clearance. Unknown space is explored in bounded steps. No memory writes, OS
input, teleporting, collision changes, or mission-state changes are used.
"""
import argparse
import ctypes as c
import datetime
import hashlib
import heapq
import json
import math
import mmap
from pathlib import Path
import re
import struct
import time

from PIL import Image
import run_blvr_elliott_proof as sim
from rig_bridge import read_current_rig

RECEIPT = sim.ARTIFACTS / 'current-stereo-diagnostic.json'
EXE_HASH = '872dc676e8fd77ad3351dd9dfcc99e89353aae0ed9857272a65fd47f298fb0b1'


def distance(a, b):
    return math.hypot(a[0]-b[0], a[2]-b[2])


class NativeWorld:
    """Read-only adapter for the verified retail build and current process."""
    def __init__(self, receipt):
        self.receipt = receipt
        self.k = c.WinDLL('kernel32', use_last_error=True)
        self.k.OpenProcess.restype = c.c_void_p
        self.k.ReadProcessMemory.argtypes = [c.c_void_p,c.c_void_p,c.c_void_p,c.c_size_t,c.c_void_p]
        self.k.GetExitCodeProcess.argtypes = [c.c_void_p,c.POINTER(c.c_ulong)]
        self.k.CloseHandle.argtypes = [c.c_void_p]
        self.k.GetTickCount64.restype = c.c_ulonglong
        self.handle = self.k.OpenProcess(0x410,False,receipt['game_pid'])
        if not self.handle: raise RuntimeError('Diagnostic game is not running')
        deadline = time.monotonic()+20
        while True:
            log = sim.GAME_LOG.read_text(errors='replace') if sim.GAME_LOG.exists() else ''
            bases = re.findall(r'CameraHook: ExeBase=0x([0-9A-Fa-f]+)',log)
            if bases:
                self.base = int(bases[-1],16)
                try:
                    if self.read(self.base,2) == b'MZ': break
                except OSError: pass
            status = c.c_ulong()
            if not self.k.GetExitCodeProcess(self.handle,c.byref(status)) or status.value != 259:
                self.k.CloseHandle(self.handle)
                raise RuntimeError('Game exited during initialization')
            if time.monotonic() >= deadline:
                self.k.CloseHandle(self.handle)
                raise RuntimeError('No verified native adapter after launch')
            time.sleep(.1)
        self.telemetry = Path(receipt['session'])/'telemetry.jsonl'
        self.names = {}
        self.player_actor = None
        self.vehicle_actor = None
        self.mount_scan_at = 0

    def live(self):
        status = c.c_ulong()
        if not self.k.GetExitCodeProcess(self.handle,c.byref(status)) or status.value != 259:
            raise RuntimeError('Game exited; refusing stale state')
        if json.loads(RECEIPT.read_text())['game_pid'] != self.receipt['game_pid']:
            raise RuntimeError('Diagnostic session changed')

    def read(self, address, size):
        data = c.create_string_buffer(size)
        if not self.k.ReadProcessMemory(self.handle,address,data,size,None):
            raise OSError(hex(address))
        return data.raw

    def u32(self, address): return struct.unpack('<I',self.read(address,4))[0]

    def frame(self):
        """A coherent, current image from this game, not a stale camera object."""
        self.live()
        with mmap.mmap(-1,256,tagname=r'Local\BLVR_XR_FrameBridge_v6',access=mmap.ACCESS_READ) as view:
            for _ in range(20):
                data=view[:]
                sequence=struct.unpack_from('<I',data,16)[0]
                if not sequence&1 and struct.unpack_from('<I',view,16)[0]==sequence: break
                time.sleep(.002)
            else: raise RuntimeError('Frame publication is busy')
        if struct.unpack_from('<I',data)[0]!=0x46525847 or struct.unpack_from('<I',data,20)[0]!=self.receipt['game_pid']:
            raise RuntimeError('No image from the current game')
        tick=struct.unpack_from('<Q',data,128)[0]
        if not 0<=self.k.GetTickCount64()-tick<1500: raise RuntimeError('Game image is stale')
        return dict(mode=struct.unpack_from('<I',data,232)[0],
                    reasons=struct.unpack_from('<I',data,236)[0],
                    transaction=struct.unpack_from('<Q',data,72)[0],tick_ms=tick)

    def prompt(self):
        """Current native tutorial, after the same remap shown in the headset."""
        with mmap.mmap(-1,1760,tagname=r'Local\BLVR_ControlPrompts_v1',access=mmap.ACCESS_READ) as view:
            for _ in range(3):
                data=view[:];sequence=struct.unpack_from('<I',data)[0]
                if sequence and not sequence&1 and struct.unpack_from('<I',view)[0]==sequence:break
            else:return None
        seq,magic,pid,active,epoch,tick=struct.unpack_from('<4I2Q',data)
        if magic!=0x50524c42 or pid!=self.receipt['game_pid'] or not active or not 0<=self.k.GetTickCount64()-tick<1000:return None
        return dict(title=data[32:224].split(b'\0')[0].decode('utf-8','replace'),
                    body=data[224:1760].split(b'\0')[0].decode('utf-8','replace'))

    def frontend(self):
        """Observe the native menu manager (the image marks all menus loading)."""
        root=self.u32(self.base+0xc09cb0)
        if not root:return None
        registry=self.u32(root+0x44);mode=self.u32(root+0x48)
        if not registry:return None
        index=self.u32(registry+0x38);count=self.u32(registry+0x20)>>6;entries=self.u32(registry+0x28)
        if not entries or index>=count or count>1024:return None
        for i in range(index,-1,-1):
            candidate=self.u32(entries+i*4)
            if not candidate:continue
            obj=self.u32(candidate+8)
            if not obj:continue
            if mode==3:
                typed=self.u32(obj+0x10)
                typed=self.u32(typed+8) if typed else 0
                if not typed or self.u32(typed+8)!=5:continue
            elif self.u32(obj+0x38)<5:continue
            manager=self.u32(candidate+0x14);front=self.u32(self.base+0xc09ad0)
            if not manager:return None
            return dict(state=self.u32(manager+0x8e4),busy=self.u32(manager+0x910),
                        locked=self.u32(front+0x128) if front else 1)
        return None

    def cutscene(self):
        self.live()
        manager=self.u32(self.base+0xc09d30)
        if not manager: return False
        count=self.u32(manager+0x10)>>6; entries=self.u32(manager+0x18)
        if count>1024: raise RuntimeError('Invalid cutscene registry')
        for i in range(count):
            scene,status=struct.unpack('<2I',self.read(entries+i*8,8))
            if scene and status in (2,3):
                data=self.u32(scene+0xc)
                if data and self.read(data+0xe0,1)!=b'\0': return True
        return False

    def string(self, address):
        return self.read(address,128).split(b'\0')[0].decode('ascii')

    def actor_name(self,actor):
        return self.string(self.u32(self.u32(self.u32(actor+4)+4)))

    def actor_valid(self,actor):
        if not actor:return False
        try:
            handle=self.u32(actor+0x14)
            return handle<self.u32(self.base+0xb79d90) and self.u32(self.u32(self.base+0xb79d8c)+handle*12)==actor
        except OSError:return False

    def player(self):
        if not self.actor_valid(self.player_actor):self.player_actor=None
        if self.player_actor and self.actor_name(self.player_actor)!='Player_A':self.player_actor=None
        if not self.player_actor:
            actors=[e for e in self.entities() if e['name']=='Player_A']
            if len(actors)!=1:raise RuntimeError('No unique live player')
            self.player_actor=actors[0]['actor']
        return self.player_actor

    def component(self,actor,vtable_rva):
        count=self.u32(actor+0x3c)>>6;entries=self.u32(actor+0x44)
        if not entries or count>256:return None
        for _,component in struct.iter_unpack('<2I',self.read(entries,count*8)):
            if component and self.u32(component)==self.base+vtable_rva and self.u32(component+0x10)==actor:return component
        return None

    def damage(self,actor):
        # CoDamageable's inherited RTTI key is present for derived components.
        # Retail Invulnerable getter RVA 2408B0 reads the nesting count at +48;
        # The named MaxHealth getter RVA 3BB4C0 reads +2C. The max-health
        # setter RVA 450160 and heal consumer RVA 3BB4D0 use +30 for current
        # health, preserving/capping it against +2C. Do not infer this field
        # from the misleading raw CurrentHealth registration descriptor.
        count=self.u32(actor+0x3c)>>6;entries=self.u32(actor+0x44)
        if not entries or count>256:return None
        for key,component in struct.iter_unpack('<2I',self.read(entries,count*8)):
            if key==self.base+0xc29770 and component and self.u32(component+0x10)==actor:
                return dict(invulnerable=struct.unpack('<i',self.read(component+0x48,4))[0]>0,
                            health=struct.unpack('<f',self.read(component+0x30,4))[0],
                            max_health=struct.unpack('<f',self.read(component+0x2c,4))[0])
        return None

    def weapon(self):
        self.live()
        return read_current_rig(expected_pid=self.receipt.get('host_pid'))['weapon']

    def vehicle(self):
        """Current native mount and its heading; independent of headset/camera."""
        player_handle=self.u32(self.player()+0x14)
        def occupied(actor):
            if not self.actor_valid(actor):return False
            mount=self.component(actor,0xad38f4)
            return mount and self.u32(mount+0x20)==player_handle
        if not occupied(self.vehicle_actor):
            self.vehicle_actor=None
            co_player=self.u32(self.player()+0x28)
            if co_player and self.u32(co_player)==self.base+0xab71ec and self.u32(co_player+0x10)==self.player():
                handle=self.u32(co_player+0x34)
                if handle<self.u32(self.base+0xb79d90):
                    carrier=self.u32(self.u32(self.base+0xb79d8c)+handle*12)
                    if occupied(carrier):self.vehicle_actor=carrier
            if self.vehicle_actor:
                self.mount_scan_at=0
            else:
                return None
        if not self.vehicle_actor:return None
        mesh=self.u32(self.vehicle_actor+0x38)
        if not mesh or self.u32(mesh+0x10)!=self.vehicle_actor:return None
        return dict(actor=self.vehicle_actor,name=self.actor_name(self.vehicle_actor),
                    position=list(struct.unpack('<3f',self.read(mesh+0xa0,12))),
                    forward=list(struct.unpack('<3f',self.read(mesh+0x90,12)))[::2])

    def camera(self):
        self.live()
        deadline=time.monotonic()+2
        while True:
            with self.telemetry.open('rb') as stream:
                stream.seek(max(0,self.telemetry.stat().st_size-100000))
                lines = stream.read().splitlines()
            for line in reversed(lines):
                try: event = json.loads(line)
                except (ValueError,UnicodeError): continue
                if event.get('kind') != 'camera': continue
                if self.k.GetTickCount64()-event['tick_ms'] > 1500: break
                actor=self.player()
                if actor:
                    mesh=self.u32(actor+0x38)
                    if not mesh or self.u32(mesh+0x10)!=actor:raise RuntimeError('Native player owner changed')
                    # Trace output is batched. Read the actor's live root
                    # so a delayed log flush cannot invent a collision/stall.
                    root=list(struct.unpack('<3f',self.read(mesh+0xa0,12)))
                    if not all(math.isfinite(v) for v in root):raise RuntimeError('Invalid player root')
                    event['root']=root
                return event
            if time.monotonic()>=deadline:raise RuntimeError('No fresh gameplay camera telemetry')
            time.sleep(.02)

    def entities(self):
        self.live()
        table = self.u32(self.base+0xb79d8c)
        capacity = self.u32(self.base+0xb79d90)
        count = self.u32(self.base+0xb79d94)
        if not table or not 0 < count <= capacity <= 0x100000:
            raise RuntimeError('Invalid native entity table')
        # This is a sparse registry: live count can be lower than a surviving
        # actor's handle after a checkpoint unload. Capacity bounds the slots.
        rows = self.read(table,capacity*12)
        result = []
        for handle,(actor,generation,flags) in enumerate(struct.iter_unpack('<3I',rows)):
            if actor < 65536: continue
            try:
                if self.u32(actor+0x14)!=handle:continue
                key = (handle,actor,generation)
                name = self.names.get(key)
                if name is None:
                    name = self.string(self.u32(self.u32(self.u32(actor+4)+4)))
                    if not name or len(name)>100: continue
                    self.names[key] = name
                mesh = self.u32(actor+0x38)
                if mesh and self.u32(mesh+0x10)==actor:
                    position=list(struct.unpack('<3f',self.read(mesh+0xa0,12)))
                else:
                    # Native non-rendered interaction/trigger entities still
                    # have CoTransform. Retail readers use its published pair
                    # at +40/+60, selected by +20; do not invent mesh positions.
                    transform=self.u32(actor+0x18)
                    if not transform or self.u32(transform+0x10)!=actor: continue
                    offset=0x60 if self.u32(transform+0x20) else 0x40
                    position=list(struct.unpack('<3f',self.read(transform+offset,12)))
                if not all(math.isfinite(x) and abs(x)<1e7 for x in position): continue
                result.append(dict(handle=handle,actor=actor,name=name,position=position))
            except (OSError,UnicodeError): continue
        # No traversal cache survives an entity-table replacement.
        if self.u32(self.base+0xb79d8c) != table:
            self.names.clear()
            return self.entities()
        return result


class ObservedGraph:
    """A* over measured successful segments; blocked edges are session-local."""
    def __init__(self):
        self.points = {}
        self.edges = {}
        self.blocked = set()

    @staticmethod
    def key(p): return (round(p[0]/2),round(p[1]/2),round(p[2]/2))

    def add(self, a, b):
        ka,kb = self.key(a),self.key(b)
        self.points[ka],self.points[kb] = a,b
        if ka == kb: return
        self.edges.setdefault(ka,{})[kb] = distance(a,b)
        # The opposite direction still requires an observed native traversal.

    def path(self, start, goal):
        queue = [(0,start)]
        previous = {}
        costs = {start:0}
        while queue:
            _,node = heapq.heappop(queue)
            if node == goal:
                route = [node]
                while node in previous:
                    node = previous[node]; route.append(node)
                return list(reversed(route))
            for nxt,cost in self.edges.get(node,{}).items():
                if (node,nxt) in self.blocked: continue
                candidate = costs[node]+cost
                if candidate < costs.get(nxt,float('inf')):
                    costs[nxt],previous[nxt] = candidate,node
                    heuristic = math.dist(nxt,goal)*2
                    heapq.heappush(queue,(candidate+heuristic,nxt))
        return None


class Bot:
    def __init__(self):
        self.receipt = json.loads(RECEIPT.read_text())
        if self.receipt['runtime'] != 'Elliott simulator':
            raise RuntimeError('Bot only controls its simulator session')
        if hashlib.sha256(sim.GAME_EXE.read_bytes()).hexdigest() != EXE_HASH:
            raise RuntimeError('Unsupported retail executable')
        self.world = NativeWorld(self.receipt)
        self.world.k.CreateMutexW.argtypes=[c.c_void_p,c.c_int,c.c_wchar_p]
        self.world.k.CreateMutexW.restype=c.c_void_p
        self.mutex=self.world.k.CreateMutexW(None,True,'Local\\BLVR_GameplayBot_InputOwner')
        if c.get_last_error()==183:
            self.world.k.CloseHandle(self.mutex)
            raise RuntimeError('Another bot owns simulator input; wait for it to finish')
        self.out = Path(self.receipt['session'])/'bot'
        self.out.mkdir(exist_ok=True)
        self.trace = (self.out/'trace.jsonl').open('a',buffering=1)
        self.graph = ObservedGraph()
        self.sequence = int(time.time()*1000)&0x7fffffff
        self.head_yaw = 0.

    def event(self, event, **fields):
        row = dict(time=datetime.datetime.now().isoformat(),event=event,**fields)
        self.trace.write(json.dumps(row)+'\n')
        print(json.dumps(row),flush=True)

    def command(self, hand, **fields):
        if fields.get('neutral'):
            inputs=('trigger','squeeze','stickX','stickY','primary','secondary','menu','thumbClick')
            if any(fields.get(key) for key in inputs):
                raise ValueError('Simulator neutral suppresses all action fields; send the action without neutral')
        self.world.live(); self.sequence=max(self.sequence+1,int(time.time()*1000)&0x7fffffff)
        sim.send_controller(self.sequence,hand,**fields)

    def release(self): self.command(2,neutral=True,lease_ms=500)

    def press(self, name, seconds=.18):
        hand,field = {'a':(1,'primary'),'b':(1,'secondary'),'x':(0,'primary'),
                      'y':(0,'secondary'),'menu':(0,'menu'),'attack':(1,'trigger'),
                      'target':(0,'trigger'),'solo':(1,'thumbClick')}[name]
        self.command(hand,lease_ms=int(seconds*1000),**{field:1})
        time.sleep(seconds+.10)
        self.command(hand,neutral=True,lease_ms=150)
        self.event('button',name=name,seconds=seconds)

    def capture(self, label):
        self.world.live()
        source = sim.ELLIOTT_DATA/'screenshot.bmp'
        previous = source.stat().st_mtime_ns if source.exists() else 0
        sim.write_atomic(sim.ELLIOTT_DATA/'screenshot_request.json',dict(eye='both',layer='all'))
        deadline = time.monotonic()+4
        while time.monotonic()<deadline:
            if source.exists() and source.stat().st_mtime_ns>previous:
                try:
                    with Image.open(source) as image:
                        image.load(); image.save(self.out/(label+'.png'))
                    self.event('capture',label=label,source='final compositor both eyes')
                    return
                except OSError: pass
            time.sleep(.025)
        raise RuntimeError('No fresh final-eye capture')

    def state(self):
        camera = self.world.camera()
        if not camera['active'] or not camera['has_root'] or not camera['fp_valid'] or self.world.frame()['mode']!=1:
            raise RuntimeError('Gameplay is not active')
        return camera

    def wait_gameplay(self,timeout=60):
        deadline=time.monotonic()+timeout
        stable_since=None
        previous_transaction=None
        while time.monotonic()<deadline:
            try:
                camera=self.state()
                transaction=self.world.frame()['transaction']
                if not self.world.cutscene() and transaction!=previous_transaction:
                    if stable_since is None: stable_since=time.monotonic()
                    if time.monotonic()-stable_since>=2: return camera
                    previous_transaction=transaction
                else: stable_since=None
            except (RuntimeError,OSError): stable_since=None
            time.sleep(.1)
        raise RuntimeError('No current stereo gameplay image before timeout')

    def enter_game(self,timeout=120,skip_cutscenes=False):
        deadline=time.monotonic()+timeout;next_accept=time.monotonic()+2;accepts=0
        while time.monotonic()<deadline:
            try:
                frame=self.world.frame()
                if frame['mode']==1:return self.wait_gameplay()
                if self.world.cutscene():
                    if skip_cutscenes:
                        self.press('b');time.sleep(.6);continue
                    return self.wait_gameplay(timeout=max(60,deadline-time.monotonic()))
                # The startup image and loading image share a reason bit.
                # Before a player has joined there is no menu context either.
                # Use a bounded native A sequence, stopping at gameplay/cinema.
                menu=self.world.frontend()
                if accepts<6 and (not menu or (not menu['busy'] and not menu['locked'])) and frame['transaction']>=120 and time.monotonic()>=next_accept:
                    self.event('startup-menu',menu=menu,accept=accepts+1)
                    self.press('a',.3);next_accept=time.monotonic()+4;accepts+=1
            except (RuntimeError,OSError):pass
            time.sleep(.15)
        raise RuntimeError('Native startup did not reach gameplay')

    def aim_at(self,target,**actions):
        """Aim the controller in world space independently of the head."""
        camera=self.state();p=camera['root'];fx,fz=camera['body_fwd']
        dx,dz=target[0]-p[0],target[2]-p[2]
        length=math.hypot(dx,dz)
        if length<.01:return
        # XR -Z is the navigation forward; positive yaw turns left.
        yaw=math.atan2(fz*dx-fx*dz,fx*dx+fz*dz)
        pitch=math.atan2(target[1]-(p[1]+1.45),length)
        self.command(1,yaw=yaw,pitch=pitch,lease_ms=500,**actions)
        return yaw,pitch

    def look_at(self, target):
        camera = self.state(); p = camera['root']; f = camera['look']
        desired = math.atan2(target[0]-p[0],target[2]-p[2])
        current = math.atan2(f[0],f[2])
        delta = (desired-current+math.pi)%(2*math.pi)-math.pi
        self.head_yaw = camera['hmd_euler'][0]+delta
        sim.send_head_pose(yaw=self.head_yaw)
        deadline=time.monotonic()+2
        while time.monotonic()<deadline:
            observed=self.state()
            error=(observed['hmd_euler'][0]-self.head_yaw+math.pi)%(2*math.pi)-math.pi
            if observed['tick_ms']>camera['tick_ms'] and abs(error)<.035: return
            time.sleep(.015)
        raise RuntimeError('Head pose was not consumed before locomotion')

    def step(self, target, duration=.3, speed=.65):
        before = self.state(); p=before['root']; f=before['look']
        dx,dz = target[0]-p[0],target[2]-p[2]
        length = math.hypot(dx,dz)
        if length<.05: return 0
        norm = math.hypot(f[0],f[2]); fx,fz=f[0]/norm,f[2]/norm
        x = (-fz*dx+fx*dz)/length*speed
        y = (fx*dx+fz*dz)/length*speed
        self.command(0,lease_ms=int(duration*1000),stickX=x,stickY=y)
        time.sleep(duration+.1)
        self.command(0,neutral=True,lease_ms=100)
        after=self.state(); q=after['root']; moved=distance(p,q)
        progress=length-distance(q,target)
        self.event('move',start=p,end=q,target=target,stick=[x,y],moved=moved,progress=progress)
        if moved>.15: self.graph.add(p,q)
        return progress

    def approach(self, name=None, point=None, radius=2.5, timeout=45):
        deadline=time.monotonic()+timeout; stalled=0; attempts=0
        target=point
        while time.monotonic()<deadline:
            p=self.state()['root']
            if name:
                candidates=[e for e in self.world.entities() if e['name']==name]
                if not candidates: raise RuntimeError('Target disappeared: '+name)
                target=min(candidates,key=lambda e:distance(p,e['position']))['position']
            if distance(p,target)<=radius and abs(p[1]-target[1])<3:
                self.release(); self.event('arrived',target=name or point,position=p)
                return p
            self.look_at(target)
            remaining=distance(p,target)-radius
            progress=self.step(target,duration=min(.35,max(.10,remaining/15)))
            stalled=stalled+1 if progress<.12 else 0
            if stalled>=3:
                self.release(); attempts+=1
                if distance(self.state()['root'],target)<=radius:continue
                self.capture(f'blocked-{attempts}')
                self.event('blocked',position=self.state()['root'],target=target,attempt=attempts)
                if attempts>4: raise RuntimeError('Route blocked after bounded recovery')
                # Probe around a dynamic/concave obstacle; native collision
                # decides how far Eddie can actually travel. No clearance claim.
                p=self.state()['root']; dx,dz=target[0]-p[0],target[2]-p[2]
                norm=math.hypot(dx,dz); side=1 if attempts%2 else -1
                detour=[p[0]+side*dz/norm*4,p[1],p[2]-side*dx/norm*4]
                for _ in range(4): self.step(detour,.3,.6)
                stalled=0
        self.release(); raise RuntimeError('Approach timed out')

    def close(self):
        try: self.release()
        finally:
            self.trace.close()
            self.world.k.CloseHandle(self.mutex)
            self.world.k.CloseHandle(self.world.handle)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='command',required=True)
    sub.add_parser('state')
    shot=sub.add_parser('capture');shot.add_argument('label')
    follow=sub.add_parser('follow');follow.add_argument('name');follow.add_argument('--radius',type=float,default=2.5)
    go=sub.add_parser('goto');go.add_argument('point',type=float,nargs=3);go.add_argument('--radius',type=float,default=1.)
    press=sub.add_parser('press');press.add_argument('name');press.add_argument('--seconds',type=float,default=.18)
    sub.add_parser('continue')
    args=parser.parse_args();bot=Bot()
    try:
        if args.command=='state':
            camera=bot.world.camera();p=camera['root']
            entities=sorted(bot.world.entities(),key=lambda e:distance(e['position'],p))
            bot.event('state',camera=camera,entities=[e for e in entities if distance(p,e['position'])<60])
        elif args.command=='capture':bot.capture(args.label)
        elif args.command=='press':bot.press(args.name,args.seconds)
        elif args.command=='follow':bot.approach(name=args.name,radius=args.radius);bot.capture('arrived-'+args.name)
        elif args.command=='goto':bot.approach(point=args.point,radius=args.radius);bot.capture('arrived-waypoint')
        elif args.command=='continue':
            bot.enter_game()
            bot.capture('continue-gameplay')
    finally:bot.close()


if __name__=='__main__':main()
