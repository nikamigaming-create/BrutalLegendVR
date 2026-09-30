"""Inspect the metric rig through the simulator's real OpenXR input path.

LOCAL y=0 reproduces a headset-centered reference origin. Controller grip -Z
is the held handle direction, distinct from aim-forward; use physical grip
orientations instead of the previous identity-quaternion hand fixture.
"""
import math
import time

from blvr_bot import Bot
import run_blvr_elliott_proof as sim


def run(bot):
    sim.send_head_pose(y=0)
    for hand in (0,1):
        bot.command(hand,neutral=True,posX=-.28 if hand==0 else .28,
                    posY=-.45,posZ=-.38,yaw=0,pitch=math.pi/2,lease_ms=500)
    bot.command(0,menu=True,lease_ms=800)
    bot.command(1,thumbClick=True,lease_ms=800)
    time.sleep(.20)
    bot.release()
    time.sleep(.4)
    sim.send_head_pose(y=0,pitch=-.4)
    for hand in (0,1):
        bot.command(hand,neutral=True,yaw=0,pitch=math.pi/2+.4,lease_ms=500)
    bot.capture('local-zero-opening-hands')
    bot.press('x')
    bot.capture('local-zero-opening-axe')
    # The simulator supports yaw/pitch, not controller roll. This orientation
    # holds the neck across the chest with the strings upward for inspection.
    bot.command(0,neutral=True,yaw=-math.pi/2,pitch=math.pi+.4,lease_ms=500)
    bot.press('y')
    bot.capture('local-zero-opening-guitar')
    for i,(yaw,pitch) in enumerate([(-1.4,3.0),(-1.75,3.3),(-1.55,3.14)]):
        bot.command(0,neutral=True,yaw=yaw,pitch=pitch+.4,lease_ms=500)
        time.sleep(.15)
        bot.capture('guitar-grip-motion-'+str(i))
    bot.press('y')
    bot.command(0,neutral=True,yaw=0,pitch=math.pi/2+.4,lease_ms=500)
    bot.command(0,stickY=.75,lease_ms=900)
    time.sleep(1)
    bot.release()
    bot.capture('local-zero-walk-to-continue')
    bot.event('grip-room-check-complete',head_local_y=0,
              evidence='Final compositor both eyes; physical headset comfort remains unverified')


if __name__=='__main__':
    bot=Bot()
    try:run(bot)
    finally:bot.close()
