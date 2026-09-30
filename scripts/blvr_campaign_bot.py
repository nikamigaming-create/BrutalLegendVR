"""Closed-loop campaign operator using native observations and leased XR input.

Strategies use the retail game's objectives and actors. Each transition is
observed again before acting; unsupported situations stop for a new strategy
instead of claiming a mission passed. Captures are final compositor eyes.
"""
import argparse
import math
import time
from blvr_bot import Bot, distance


class CampaignBot(Bot):
    def capture(self,label):
        try:return super().capture(label)
        except RuntimeError as error:self.event('capture-unavailable',label=label,reason=str(error))

    def dismiss_tutorial(self):
        prompt=self.world.prompt()
        if not prompt:return False
        self.event('tutorial',**prompt)
        self.capture('tutorial-'+str(self.sequence))
        self.press('b',.18)
        time.sleep(.4)
        return True

    def fight(self,name,timeout=90):
        try:return self._fight_active(name,timeout)
        except RuntimeError:
            if self.world.cutscene() or self.world.frame()['mode']!=1:
                self.release();self.capture('combat-transition')
                self.event('combat-transition',enemy=name);return
            raise

    def _fight_active(self,name,timeout):
        self.release()
        if self.world.vehicle():
            self.brake_stop();self.press('a');time.sleep(.8)
            if self.world.vehicle():raise RuntimeError('Combat requires dismounting; the game did not permit exit')
        self.press('y')
        deadline=time.monotonic()+timeout
        previous=None
        while time.monotonic()<deadline:
            if self.dismiss_tutorial():continue
            if self.world.cutscene() or self.world.frame()['mode']!=1:
                self.release();self.capture('combat-transition')
                self.event('combat-transition',enemy=name)
                return
            camera=self.state();p=camera['root']
            enemies=[e for e in self.world.entities() if e['name']==name]
            if not enemies:
                self.release();self.event('actors-cleared',enemy=name)
                self.capture('cleared-'+name);return
            enemy=min(enemies,key=lambda e:distance(p,e['position']))
            gap=distance(p,enemy['position'])
            if previous!=len(enemies):
                self.event('combat',enemy=name,remaining=len(enemies),range=gap)
                previous=len(enemies)
            if gap>10:
                self.look_at(enemy['position']);self.step(enemy['position'],.35,.85)
                continue
            target=enemy['position'].copy();target[1]+=1.1
            self.command(0,trigger=1,lease_ms=500)
            self.aim_at(target,trigger=1)
            time.sleep(.18)
            self.aim_at(target,trigger=0)
            time.sleep(.12)
        self.release();self.capture('combat-stalled')
        raise RuntimeError('Combat did not reach an observed transition: '+name)

    def stage_command(self,action):
        """Same grip+click chord used by the human controls, with clean edges."""
        buttons={'build':dict(primary=True),'beacon':dict(secondary=True),
                 'fly':None,'charge':dict(stickY=1),'defend':dict(stickY=-1),
                 'follow':dict(stickX=-1),'move':dict(stickX=1),
                 'ascend':dict(trigger=1),'descend':None}
        if action not in buttons:raise ValueError(action)
        self.command(0,squeeze=1,thumbClick=True,secondary=action=='fly',
                     trigger=float(action=='descend'),lease_ms=500)
        if buttons[action]:self.command(1,lease_ms=350,**buttons[action])
        time.sleep(.25);self.release()
        self.event('stage-command',action=action)

    def brake_stop(self,timeout=3):
        vehicle=self.world.vehicle()
        if not vehicle:return
        last=vehicle['position'];then=time.monotonic();deadline=then+timeout
        self.command(1,trigger=0,secondary=True,lease_ms=500)
        while time.monotonic()<deadline:
            self.command(0,trigger=1,lease_ms=350)
            self.command(1,secondary=True,lease_ms=350)
            time.sleep(.1)
            vehicle=self.world.vehicle()
            if not vehicle:break
            now=time.monotonic();speed=distance(vehicle['position'],last)/max(.01,now-then)
            if speed<1.5:break
            last=vehicle['position'];then=now
        self.release()

    def lamprey(self,timeout=300):
        """Observe native vulnerability; drive, stop, dismount, cut, remount."""
        deadline=time.monotonic()+timeout;phase=None;next_report=0;last_vulnerable=False
        last_position=None;last_sample=time.monotonic();next_nitro=0
        car_handle=None;failed_boards=0
        while time.monotonic()<deadline:
            if self.dismiss_tutorial():continue
            if self.world.cutscene() or self.world.frame()['mode']!=1:
                self.release()
                if phase!='cinema':self.event('boss-phase',phase='cinema');self.capture('boss-cinema-'+str(self.sequence))
                phase='cinema';time.sleep(.25);continue
            entities=self.world.entities()
            bosses=[e for e in entities if e['name']=='P1_010_LampreyBeast']
            if not bosses:
                self.release();self.event('boss-left-arena');self.capture('lamprey-transition');return
            boss=bosses[0];tongues=[e for e in entities if e['name']=='D27_LampreyTongue']
            vulnerable=[]
            for tongue in tongues:
                damage=self.world.damage(tongue['actor'])
                if damage and not damage['invulnerable'] and damage['health']>0:
                    tongue['health']=damage['health'];vulnerable.append(tongue)
            if bool(vulnerable)!=last_vulnerable:
                self.event('boss-vulnerability',active=bool(vulnerable),tongues=vulnerable)
                last_vulnerable=bool(vulnerable)
            vehicle=self.world.vehicle();p=vehicle['position'] if vehicle else self.state()['root']
            now=time.monotonic();speed=distance(p,last_position)/max(.05,now-last_sample) if last_position else 0
            last_position=p;last_sample=now
            if now>next_report:
                self.event('boss-status',driving=bool(vehicle),position=p,speed=speed,
                           tongues=[dict(position=t['position'],damage=self.world.damage(t['actor'])) for t in tongues])
                next_report=now+5
            if vehicle:
                car_handle=vehicle['actor'];failed_boards=0
                if vulnerable:
                    self.brake_stop();self.press('a');time.sleep(.7)
                    phase='attack';continue
                # Follow a circle around the boss, steering from the car's own
                # transform. Neither scripted boss focus nor head yaw steers.
                cx,cz=boss['position'][0],boss['position'][2]
                theta=math.atan2(p[2]-cz,p[0]-cx)
                target=[cx+48*math.cos(theta-.48),p[1],cz+48*math.sin(theta-.48)]
                fx,fz=vehicle['forward'];dx,dz=target[0]-p[0],target[2]-p[2]
                angle=math.atan2(-fz*dx+fx*dz,fx*dx+fz*dz)
                desired_speed=21/(1+abs(angle))
                nitro=now>next_nitro and abs(angle)<.3
                if nitro:next_nitro=now+12
                self.command(0,stickX=max(-1,min(1,angle*1.7)),
                             trigger=.65 if speed>desired_speed+3 else 0,thumbClick=nitro,lease_ms=450)
                self.command(1,trigger=.65 if speed<desired_speed else 0,lease_ms=450)
                phase='drive';time.sleep(.10)
            elif vulnerable:
                target=min(vulnerable,key=lambda e:distance(p,e['position']))
                if self.world.weapon()!=1:self.press('x')
                if distance(p,target['position'])>3.5:
                    self.look_at(target['position']);self.step(target['position'],.22,.85)
                else:
                    aim=target['position'].copy();aim[1]+=.3
                    self.command(0,trigger=1,lease_ms=450);self.aim_at(aim,trigger=1)
                    time.sleep(.2);self.aim_at(aim,trigger=0);time.sleep(.16)
                phase='attack'
            else:
                cars=[e for e in entities if e['name']=='Deuce']
                if not cars:raise RuntimeError('No Deuce available after tongue attack')
                car=min(cars,key=lambda e:distance(p,e['position']))
                if distance(p,car['position'])>2.5:
                    self.look_at(car['position']);self.step(car['position'],.25,.85)
                    failed_boards=0
                else:
                    self.press('a');time.sleep(.7);failed_boards+=1
                    if failed_boards>3:
                        self.look_at(car['position']);self.step(car['position'],.15,.4);failed_boards=0
                phase='remount'
        self.release();self.capture('lamprey-progress')
        self.event('boss-time-budget',seconds=timeout)

    def drive_to(self,target,timeout=60,radius=5):
        deadline=time.monotonic()+timeout
        vehicle=self.world.vehicle()
        if not vehicle:raise RuntimeError('Eddie is not driving')
        last=vehicle['position'];last_time=time.monotonic();stalls=0;next_report=0
        while time.monotonic()<deadline:
            if self.dismiss_tutorial():continue
            if self.world.cutscene() or self.world.frame()['mode']!=1:
                self.release();self.capture('driving-transition');return
            vehicle=self.world.vehicle()
            if not vehicle:raise RuntimeError('Eddie left the vehicle')
            p=vehicle['position'];fx,fz=vehicle['forward']
            now=time.monotonic();speed=distance(p,last)/max(.03,now-last_time)
            movement=distance(p,last);last=p;last_time=now
            dx,dz=target[0]-p[0],target[2]-p[2]
            gap=math.hypot(dx,dz)
            if gap<=radius and speed<2:
                self.release();self.event('driving-arrival',position=p,speed=speed);return
            angle=math.atan2(-fz*dx+fx*dz,fx*dx+fz*dz)
            steer=max(-1,min(1,angle*1.6))
            desired_speed=min(18,math.sqrt(2*8*max(0,gap-radius)))/(1+1.6*abs(angle))
            throttle=.5 if speed<desired_speed-1 else 0
            brake=.75 if speed>desired_speed+1 else 0
            if time.monotonic()>next_report:
                self.event('driving',position=p,target=target,range=gap,angle=angle,steer=steer,speed=speed)
                next_report=time.monotonic()+2
            self.command(0,trigger=brake,stickX=steer,lease_ms=350)
            self.command(1,trigger=throttle,secondary=gap<radius+3,lease_ms=350)
            time.sleep(.10)
            stalls=stalls+1 if movement<.1 else 0
            if stalls>10:
                self.release();self.capture('driving-blocked')
                self.event('driving-recovery',position=p)
                self.command(0,trigger=.8,stickX=-steer,lease_ms=800)
                time.sleep(.7);self.release();stalls=0
        self.release();raise RuntimeError('Driving route did not finish')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='mode',required=True)
    fight=sub.add_parser('fight');fight.add_argument('enemy');fight.add_argument('--seconds',type=float,default=90)
    drive=sub.add_parser('drive');drive.add_argument('point',type=float,nargs=3)
    stage=sub.add_parser('command');stage.add_argument('action')
    boss=sub.add_parser('lamprey');boss.add_argument('--seconds',type=float,default=300)
    args=parser.parse_args();bot=CampaignBot()
    try:
        if args.mode=='fight':bot.fight(args.enemy,args.seconds)
        elif args.mode=='drive':bot.drive_to(args.point)
        elif args.mode=='lamprey':bot.lamprey(args.seconds)
        else:bot.stage_command(args.action)
    finally:bot.close()


if __name__=='__main__':main()
