"""Render a narrated 1080p guide from real launcher captures and original art.

Run tutorial_neural_voice.py in the isolated voice environment first. Captures
use only example paths. Frames are piped to FFmpeg; no raw frame dump.
"""
import json
import math
from pathlib import Path
import re
import subprocess
import textwrap
import wave

import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'outputs/support-tutorial'
CAPTURES = ROOT / 'assets/tutorial/captures'
AUDIO = OUT / 'audio-neural'
W, H, FPS = 1920, 1080, 30
WHITE, GRAY, ORANGE = '#fff8ed', '#c8c2bc', '#ff8b45'
SCENES = json.loads((ROOT / 'assets/tutorial/narration.json').read_text(encoding='utf-8'))
ART = Image.open(ROOT / 'assets/tutorial/forge-guide.png').convert('RGB').resize((W, H), Image.Resampling.LANCZOS)
FONT = Path('C:/Windows/Fonts')
FONTS = {}


def font(size=32, bold=False, display=False):
    key = (size, bold, display)
    if key not in FONTS:
        FONTS[key] = ImageFont.truetype(str(FONT / ('impact.ttf' if display else 'segoeuib.ttf' if bold else 'segoeui.ttf')), size)
    return FONTS[key]


def label(draw, position, text, size=32, fill=WHITE, bold=False, display=False):
    draw.text(position, text, font=font(size, bold, display), fill=fill, spacing=10)


def paragraph(draw, xy, text, width=36, size=32, fill=WHITE, gap=16):
    y = xy[1]
    for line in textwrap.wrap(text, width):
        label(draw, (xy[0], y), line, size, fill)
        y += size + gap
    return y


def dark_art(amount=205):
    image = ART.copy().convert('RGBA')
    image.alpha_composite(Image.new('RGBA', image.size, (9, 9, 12, amount)))
    return image.convert('RGB')


def shot(name, crop=None):
    image = Image.open(CAPTURES / (name + '.png')).convert('RGB')
    return image.crop(crop) if crop else image


def panel(image, content, box, caption=None):
    x, y, w, h = box
    content = ImageOps.contain(content, (w, h), Image.Resampling.LANCZOS)
    px, py = x + (w - content.width) // 2, y + (h - content.height) // 2
    draw = ImageDraw.Draw(image)
    draw.rectangle((px - 2, py - 2, px + content.width + 2, py + content.height + 2), fill='#5c4133')
    image.paste(content, (px, py))
    if caption:
        draw = ImageDraw.Draw(image)
        label(draw, (px, py - 38), caption, 23, GRAY)
    return px, py, content.width, content.height


def zip_icon(draw, x, y, size=170):
    draw.polygon([(x, y), (x+size*.7, y), (x+size, y+size*.3), (x+size, y+size*1.15), (x, y+size*1.15)], fill='#242329', outline=ORANGE, width=3)
    for i in range(7):
        sx = x + size*.46 + (8 if i % 2 else 0)
        draw.rectangle((sx, y+14+i*12, sx+8, y+22+i*12), fill=WHITE)
    label(draw, (x+size*.22, y+size*.74), 'ZIP', int(size*.22), ORANGE, bold=True)


def monitor(image):
    source = shot('launcher-play', (25, 375, 1790, 1320))
    corners = [(748, 233), (1230, 203), (1224, 514), (747, 497)]
    src = [(0,0), (source.width,0), (source.width,source.height), (0,source.height)]
    matrix, values = [], []
    for (x,y), (u,v) in zip(corners,src):
        matrix.extend([[x,y,1,0,0,0,-u*x,-u*y], [0,0,0,x,y,1,-v*x,-v*y]])
        values.extend([u,v])
    coefficients = np.linalg.solve(np.array(matrix), np.array(values))
    transformed = source.transform((W,H), Image.Transform.PERSPECTIVE, coefficients, Image.Resampling.BICUBIC)
    mask = Image.new('L',(W,H)); ImageDraw.Draw(mask).polygon(corners,fill=255)
    image.paste(transformed,(0,0),mask)


def make_scene(scene_id, variant=0):
    image = dark_art()
    draw = ImageDraw.Draw(image)
    if scene_id in ('intro', 'outro'):
        image = ART.copy(); monitor(image)
        shade = Image.new('RGBA',(W,H))
        sd = ImageDraw.Draw(shade)
        for x in range(W):
            sd.line((x,0,x,H),fill=(7,8,10,int(215*max(0,min(1,(x-1010)/550)))))
        image = Image.alpha_composite(image.convert('RGBA'),shade).convert('RGB'); draw = ImageDraw.Draw(image)
        label(draw,(1270,210),'BRÜTAL LEGEND VR',32,ORANGE,bold=True)
        label(draw,(1265,285),'LAUNCH.\nLOG.\nSEND.' if scene_id=='intro' else 'BACK TO\nTHE METAL',106,display=True)
        paragraph(draw,(1270,720),'A clear report gets you back in the game.' if scene_id=='intro' else 'Reproduce → Export → Send',width=29,size=30)
        if scene_id=='outro':
            label(draw,(1270,830),'Turn detailed logging off afterward.',23,GRAY)
        return image
    titles = {item['id']:item['title'] for item in SCENES}
    label(draw,(70,50),'BRÜTAL LEGEND VR  /  SUPPORT GUIDE',26,ORANGE,bold=True)
    label(draw,(70,105),titles[scene_id],67,display=True)
    label(draw,(1410,58),'DEMO PATHS  •  REAL APP',22,GRAY)
    draw.line((70,207,1850,207),fill='#53372b',width=2)
    if scene_id == 'launch':
        panel(image,shot('launcher-play',(25,470,1790,1220)),(650,280,1200,550),'ACTUAL LAUNCHER')
        draw=ImageDraw.Draw(image)
        for y,n,title,sub in [(280,'01','INSTALL','Run the Setup EXE.'),(460,'02','PREPARE','Choose your Steam game.\nPrepare / repair game.'),(675,'03','PLAY','Connect Link / Air Link.\nChoose Play VR.')]:
            label(draw,(70,y),n,29,ORANGE,bold=True);label(draw,(135,y-8),title,44,display=True)
            label(draw,(135,y+53),sub,29,GRAY)
    elif scene_id == 'settings':
        name = ['settings-off','settings-on','settings-saved'][variant]
        panel(image,shot(name,(25,1110,1200,1485)),(600,345,1240,395),'ACTUAL VR SETTINGS • LOGGING CONTROLS')
        panel(image,shot('settings-off',(25,375,560,460)),(70,280,470,85))
        draw=ImageDraw.Draw(image)
        label(draw,(70,420),'1. Open VR settings',32)
        label(draw,(70,492),'2. Tick detailed logs',32)
        label(draw,(70,564),'3. Save VR settings',32,ORANGE,bold=True)
        label(draw,(620,785),'Applies on the next launch.',31,ORANGE)
        label(draw,(620,837),'Basic launcher / installer logs are always saved.',27,GRAY)
    elif scene_id == 'reproduce':
        panel(image,shot('launcher-checking',(25,740,1000,1040)),(70,330,870,280),'CHECK INSTALLATION OR PLAY VR')
        panel(image,shot('error'),(1010,300,840,450),'ACTUAL CONNECTION ERROR')
        draw=ImageDraw.Draw(image)
        label(draw,(70,705),'Keep the exact message.',44,display=True)
        paragraph(draw,(70,775),'Export immediately after the failure.',width=42,size=31,fill=GRAY)
        label(draw,(1010,800),'Example: headset unavailable.',31,ORANGE)
    elif scene_id == 'export':
        panel(image,shot('launcher-play',(25,1125,850,1230)),(70,300,960,125),'PLAY & SETUP → EXPORT SUPPORT ZIP…')
        panel(image,shot('save-controls'),(70,620,1780,260),'ACTUAL SAVE DIALOG • SAVE THE WHOLE ZIP')
        draw=ImageDraw.Draw(image)
        zip_icon(draw,95,470,88)
        label(draw,(230,480),'BLVR-support.zip',40,ORANGE,bold=True)
        label(draw,(1120,300),'Choose a folder.',37)
        label(draw,(1120,370),'Save the ZIP.',37)
        label(draw,(1120,490),'Send the whole file.',28,GRAY)
    elif scene_id == 'bundle':
        zip_icon(draw,90,325,220)
        label(draw,(70,620),'BLVR-support.zip',38,ORANGE,bold=True)
        label(draw,(70,693),'Review before sharing.',29)
        label(draw,(70,750),'Nothing uploads automatically.',25,GRAY)
        rows=[('report.json','Windows, GPU, runtime & file identity'),('app/','Saved settings and controls'),('launcher/','Launch, preflight & installer logs'),('host/ + game/','Available VR host and game logs')]
        draw.rounded_rectangle((660,280,1850,850),radius=12,fill='#16161b',outline='#594132',width=2)
        label(draw,(700,302),'CONTENTS OF THE ACTUAL EXPORT',25,GRAY,bold=True)
        label(draw,(700,350),'Available logs + settings • one support package',25,ORANGE)
        for i,(name,description) in enumerate(rows):
            y=422+i*100
            label(draw,(700,y),name,33,WHITE,bold=True)
            label(draw,(700,y+45),description,26,GRAY)
    elif scene_id == 'handoff':
        person=ImageOps.fit(ART.crop((0,90,760,H)),(515,620),centering=(.4,.5))
        image.paste(person,(70,260));draw=ImageDraw.Draw(image)
        label(draw,(100,805),'YOU / THE TESTER',24,ORANGE,bold=True)
        draw.rounded_rectangle((1010,280,1850,850),radius=12,fill='#17171c',outline='#6c4430',width=2)
        label(draw,(1050,315),'YOUR EXISTING SUPPORT THREAD',28,ORANGE,bold=True)
        zip_icon(draw,1055,395,75)
        label(draw,(1160,408),'BLVR-support.zip',37,bold=True)
        label(draw,(1050,540),'Headset + PC connection',31)
        label(draw,(1050,600),'The step that failed',31)
        label(draw,(1050,660),'Exact error + time it happened',31)
        label(draw,(1050,770),'Example handoff • no upload performed',23,GRAY)
    elif scene_id == 'fallback':
        label(draw,(70,290),'THE WINDOW WILL NOT OPEN',29,ORANGE,bold=True)
        label(draw,(70,385),'Collect Support Logs.cmd',39,bold=True)
        paragraph(draw,(70,470),'Run it from your installation or portable folder.',width=35,size=31)
        label(draw,(70,640),'Find the exported ZIP in:',29,GRAY)
        label(draw,(70,695),'%TEMP%\\BLVR-support.zip',34,ORANGE)
        draw.line((940,270,940,850),fill=GRAY,width=2)
        label(draw,(1020,290),'THE WINDOWS INSTALLER FAILS',29,ORANGE,bold=True)
        label(draw,(1020,385),'installer-latest.log',44,bold=True)
        label(draw,(1020,475),'Open the logs folder:',29,GRAY)
        label(draw,(1020,540),'%LOCALAPPDATA%\\\nBrutalLegendVR\\Logs',31)
        label(draw,(1020,685),'Setup saves it automatically.',30,ORANGE)
        label(draw,(1020,750),'Preview 5 and later',24,GRAY)
    elif scene_id == 'receive':
        left=[('report.json','SYSTEM & FILE IDENTITY'),('System details','Windows version + graphics driver'),('Runtime', 'The selected PC headset runtime'),('Binary identity','File hashes + missing file status')]
        draw.rounded_rectangle((70,280,940,850),radius=12,fill='#17171c',outline='#594132',width=2)
        for i,(name,value) in enumerate(left):
            y=310+i*124
            label(draw,(105,y),name,34,ORANGE,bold=True)
            paragraph(draw,(105,y+48),value,width=47,size=24,fill=GRAY,gap=6)
        draw.rounded_rectangle((1010,280,1850,850),radius=12,fill='#17171c',outline='#594132',width=2)
        label(draw,(1050,310),'blvr_xr_host.log',36,ORANGE,bold=True)
        fatal='FATAL: Headset unavailable (-35). Connect Quest Link / Air Link and wake the headset.'
        paragraph(draw,(1050,420),fatal,width=40,size=30,gap=18)
        label(draw,(1050,720),'Locate the failing step.',32,WHITE,bold=True)
        label(draw,(1050,790),'Example connection failure',24,GRAY)
    return image


def timecode(seconds):
    milliseconds=round(seconds*1000)
    return f'{milliseconds//3600000:02}:{milliseconds//60000%60:02}:{milliseconds//1000%60:02},{milliseconds%1000:03}'


def audio_and_captions():
    sample_rate=44100
    cues=[];chunks=[];offset=0
    for scene in SCENES:
        with wave.open(str(AUDIO/f"{scene['id']}.wav"),'rb') as voice:
            original_rate=voice.getframerate()
            samples=np.frombuffer(voice.readframes(voice.getnframes()),dtype='<i2').astype(np.float32)/32768
            if voice.getnchannels()==2:samples=samples.reshape(-1,2).mean(axis=1)
        samples=np.interp(np.arange(round(len(samples)*sample_rate/original_rate))*original_rate/sample_rate,np.arange(len(samples)),samples)
        duration=max(scene['seconds'],len(samples)/sample_rate+1.1)
        duration=math.ceil(duration*FPS)/FPS
        scene['duration']=duration;scene['start']=offset
        chunk=np.zeros(round(duration*sample_rate),dtype=np.float32)
        pad=round(.35*sample_rate);chunk[pad:pad+len(samples)]=samples*.9
        chunks.append(chunk)
        timing=json.loads((AUDIO/f"{scene['id']}.timing.json").read_text())
        for beat in timing:
            sentences=re.split(r'(?<=[.!?])\s+',beat['text'])
            count=sum(len(s.split()) for s in sentences);position=offset+.35+beat['start']
            for sentence in sentences:
                length=len(sentence.split())/count*(beat['end']-beat['start'])
                cues.append((position,position+length,sentence));position+=length
        offset+=duration
    voice=np.concatenate(chunks)
    # Original quiet, synthesized metal pulse, kept well below the narration.
    t=np.arange(len(voice),dtype=np.float32)/sample_rate
    beat=t%(.8)
    pitch=np.where((t//2.4).astype(int)%4==2,73.416, np.where((t//2.4).astype(int)%4==3,65.406,82.407))
    phase=2*np.pi*np.cumsum(pitch)/sample_rate
    guitar=np.tanh(2.2*(np.sin(phase)+.45*np.sin(phase*1.5)+.25*np.sin(phase*2)))*np.exp(-beat*7)*.012
    kick=np.sin(2*np.pi*(55*beat+12*(1-np.exp(-beat*35))))*np.exp(-beat*32)*.018
    rng=np.random.default_rng(14)
    hat=rng.uniform(-1,1,len(t)).astype(np.float32)*np.exp(-(t%.4)*100)*.002
    fade=np.minimum(1,np.minimum(t/1.5,(offset-t)/2))
    mix=np.clip(voice+(guitar+kick+hat)*fade,-.98,.98)
    with wave.open(str(AUDIO/'soundtrack.wav'),'wb') as output:
        output.setnchannels(1);output.setsampwidth(2);output.setframerate(sample_rate)
        output.writeframes((mix*32767).astype('<i2').tobytes())
    srt='\n\n'.join(f'{i+1}\n{timecode(a)} --> {timecode(b)}\n{textwrap.fill(text,84)}' for i,(a,b,text) in enumerate(cues))
    (OUT/'BLVR-support-guide.srt').write_text(srt+'\n',encoding='utf-8')
    return cues,offset


def render():
    cues,total=audio_and_captions()
    variants={s['id']:[make_scene(s['id'],i) for i in range(3 if s['id']=='settings' else 1)] for s in SCENES}
    for sid,images in variants.items():images[-1].save(OUT/f'preview-{sid}.jpg',quality=90)
    variants['intro'][0].save(OUT/'poster.jpg',quality=92)
    # Public source frames contain example paths only. No blur or cover-up layer
    # can obscure buttons, fields or labels in this edit.
    temporary=OUT/'BLVR-support-guide.rebuild.mp4'
    command=['ffmpeg','-hide_banner','-loglevel','warning','-y','-f','rawvideo','-pix_fmt','rgb24','-s',f'{W}x{H}','-r',str(FPS),'-i','pipe:0','-i',str(AUDIO/'soundtrack.wav'),'-c:v','libx264','-preset','fast','-crf','19','-pix_fmt','yuv420p','-af','loudnorm=I=-17:TP=-1.5:LRA=9','-ar','48000','-c:a','aac','-b:a','160k','-movflags','+faststart','-shortest',str(temporary)]
    with (OUT/'render.log').open('w') as log:
        encoder=subprocess.Popen(command,stdin=subprocess.PIPE,stderr=log)
        cue_index=0;previous=None
        for number,scene in enumerate(SCENES):
            count=round(scene['duration']*FPS)
            for n in range(count):
                local=n/FPS;now=scene['start']+local
                variant=min(2,int(local/(scene['duration']/3))) if scene['id']=='settings' else 0
                frame=variants[scene['id']][variant].copy()
                if scene['id'] in ('intro','outro'):
                    # A restrained camera push brings the illustrated host in.
                    zoom=1+.018*local/scene['duration']
                    scaled=frame.resize((round(W*zoom),round(H*zoom)),Image.Resampling.BICUBIC)
                    dx=(scaled.width-W)//2;dy=(scaled.height-H)//2
                    frame=scaled.crop((dx,dy,dx+W,dy+H))
                draw=ImageDraw.Draw(frame)
                # Restrained drifting forge embers along the margins.
                for i in range(22):
                    x=(i*719+93)%W;y=int((i*173-now*(18+i%9))%H)
                    if x<45 or x>1870:
                        draw.line((x,y,x+2,y-5),fill=ORANGE,width=2)
                if scene['id']=='handoff':
                    phase=min(1,max(0,(local-1)/(scene['duration']-3)))
                    x=int(615+phase*240)
                    zip_icon(draw,x,450,85)
                    draw.line((615,650,940,650),fill=ORANGE,width=3)
                    draw.polygon([(940,650),(922,639),(922,661)],fill=ORANGE)
                if scene['id']=='settings':
                    highlight=(634,448,1045,490) if variant<2 else (630,639,888,735)
                    draw.rounded_rectangle(highlight,radius=7,outline=ORANGE,width=3)
                if scene['id']=='launch':
                    highlight=(662,538,871,598) if local<scene['duration']*.6 else (662,615,807,678)
                    draw.rounded_rectangle(highlight,radius=7,outline=ORANGE,width=3)
                if scene['id']=='export':
                    highlight=(392,316,712,410) if local<scene['duration']*.55 else (1508,800,1662,852)
                    draw.rounded_rectangle(highlight,radius=7,outline=ORANGE,width=3)
                if previous is not None and local<.45:
                    reveal=int((local/.45)*(W+160))
                    mask=Image.new('L',(W,H));md=ImageDraw.Draw(mask)
                    md.polygon([(0,0),(reveal,0),(reveal-160,H),(0,H)],fill=255)
                    frame=Image.composite(frame,previous,mask);draw=ImageDraw.Draw(frame)
                    draw.line((reveal,0,reveal-160,H),fill=ORANGE,width=5)
                while cue_index+1<len(cues) and cues[cue_index][1]<now:cue_index+=1
                a,b,caption=cues[cue_index]
                if a<=now<=b:
                    lines=textwrap.wrap(caption,102)
                    draw.rounded_rectangle((48,916,W-48,1015),radius=10,fill='#101014')
                    for k,line in enumerate(lines[:2]):
                        box=draw.textbbox((0,0),line,font=font(30))
                        label(draw,((W-(box[2]-box[0]))//2,928+k*39),line,30)
                draw.rectangle((0,H-6,int(W*now/total),H),fill=ORANGE)
                label(draw,(70,1030),f"{number+1:02} / 10  •  {scene['id'].upper()}",20,GRAY,bold=True)
                label(draw,(1400,1030),'NIKAMI  /  SUPPORT GUIDE',20,GRAY,bold=True)
                encoder.stdin.write(frame.tobytes())
            previous=frame.copy()
            print(f"Rendered {scene['id']} ({scene['duration']:.1f}s)",flush=True)
        encoder.stdin.close()
        if encoder.wait():raise RuntimeError((OUT/'render.log').read_text())
    temporary.replace(OUT/'BLVR-support-guide.mp4')
    (OUT/'timeline.json').write_text(json.dumps({'duration':total,'fps':FPS,'width':W,'height':H,'scenes':SCENES},indent=2),encoding='utf-8')
    print(f'Finished {total:.1f}s: {OUT / "BLVR-support-guide.mp4"}')


if __name__=='__main__':
    render()
