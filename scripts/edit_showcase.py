"""Reproducible single-eye debut edit from genuine compositor takes and audio."""
import json,pathlib,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/showcase-2026-09-27'
FONT="C\\:/Windows/Fonts/COPRGTB.TTF"
BODY="C\\:/Windows/Fonts/COPRGTL.TTF"

def run(args,log):
    with (OUT/log).open('w') as stream:
        p=subprocess.run(['ffmpeg','-hide_banner','-loglevel','warning','-y',*args],cwd=OUT,stdout=stream,stderr=subprocess.STDOUT)
    if p.returncode:raise RuntimeError((OUT/log).read_text())
def title(text,name,size=28,x='54',y='52',font=FONT,color='0xe2ce9a'):
    (OUT/(name+'.txt')).write_text(text,encoding='utf-8')
    return f"drawtext=fontfile='{font}':textfile='{name}.txt':fontsize={size}:fontcolor={color}:x={x}:y={y}:shadowcolor=black@0.8:shadowx=2:shadowy=2"

def clip(index,source,start,duration,label,intro=False,ending=False):
    name=f'edit-{index:02}';meta=json.loads((OUT/(source+'.json')).read_text())
    delay=round((meta['audio']['first_qpc_seconds']-meta['video_start_qpc'])*1000)
    # These are editorial titles, not replacements for captured in-game UI.
    filters=['eq=contrast=1.03:brightness=0.006:saturation=1.04',
             'drawbox=x=0:y=0:w=iw:h=30:color=0x070809:t=fill',
             'drawbox=x=0:y=690:w=iw:h=30:color=0x070809:t=fill']
    if intro or ending:
        filters += [title('BR\u00dcTAL LEGEND',name+'-title',54,'(w-tw)/2','64'),
                    title('OPENXR  /  VR DEBUT',name+'-sub',26,'(w-tw)/2','127',BODY),
                    'drawbox=x=452:y=168:w=376:h=2:color=0xcda75b:t=fill']
        if intro:filters+=['fade=t=in:st=0:d=0.6']
        if ending:filters+=[title('SINGLE-EYE SIMULATOR CAPTURE',name+'-note',18,'(w-tw)/2','648',BODY),f'fade=t=out:st={duration-.9}:d=0.9']
    else:
        filters += ['drawbox=x=42:y=48:w=3:h=30:color=0xcda75b:t=fill',title(label,name+'-label')]
    vf=','.join(filters)
    audio=f'adelay={delay}|{delay},atrim=start={start}:duration={duration},asetpts=PTS-STARTPTS,apad=whole_dur={duration},loudnorm=I=-20:TP=-2:LRA=9,aresample=48000'
    graph=f'[0:v]trim=start={start}:duration={duration},setpts=PTS-STARTPTS,{vf},fps=30,format=yuv420p[v];[1:a]{audio}[a]'
    (OUT/(name+'.filter')).write_text(graph,encoding='utf-8')
    run(['-i',source+'.mp4','-i',source+'.wav','-filter_complex_script',name+'.filter','-map','[v]','-map','[a]','-t',str(duration),'-c:v','libx264','-preset','fast','-crf','17','-c:a','aac','-b:a','224k',name+'.mp4'],name+'.log')
    return duration

def split(index):
    name=f'edit-{index:02}';duration=5.0
    # Two different moments, each from the same left eye. This is an editorial
    # side-by-side, not stereo footage or a retail/parity comparison.
    graph='[0:v]trim=start=13:duration=5,setpts=PTS-STARTPTS,scale=640:360,setsar=1[l];[1:v]trim=start=1:duration=5,setpts=PTS-STARTPTS,scale=640:360,setsar=1[r];[l][r]hstack,pad=1280:720:0:180:color=0x090b0e,drawbox=x=638:y=178:w=4:h=364:color=0xcda75b:t=fill,'
    graph+=title('RIG ROOM',name+'-left',24,'54','139')+','+title('GAMEPLAY',name+'-right',24,'694','139')+','+title('BR\u00dcTAL LEGEND VR',name+'-foot',24,'(w-tw)/2','594')+',format=yuv420p[v];[2:a]atrim=start=1:duration=5,asetpts=PTS-STARTPTS,loudnorm=I=-20:TP=-2:LRA=9,aresample=48000[a]'
    (OUT/(name+'.filter')).write_text(graph,encoding='utf-8')
    run(['-i','01-room.mp4','-i','03-axe-reveal.mp4','-i','03-axe-reveal.wav','-filter_complex_script',name+'.filter','-map','[v]','-map','[a]','-t',str(duration),'-c:v','libx264','-preset','fast','-crf','17','-c:a','aac','-b:a','224k',name+'.mp4'],name+'.log')
    return duration

def main():
    durations=[]
    specs=[('01-room',.7,6.6,'',True,False),('01-room',8.3,3.1,'TRACKED HANDS',False,False),
           ('01-room',12.65,5.8,'THE AXE',False,False),('04-physical-guitar',1.5,9.2,'PHYSICAL STRUMMING',False,False),
           ('03-axe-reveal',.3,8.3,'FIRST-PERSON OPENXR',False,False),('02-world',14.1,6.4,'IN-GAME HANDS',False,False)]
    for index,spec in enumerate(specs):
        durations.append(clip(index,*spec));print('edited',index,flush=True)
    durations.append(split(6));durations.append(clip(7,'01-room',1.0,5.5,'',ending=True))
    args=[]
    for i in range(8):args+=['-i',f'edit-{i:02}.mp4']
    graph=[]
    for i in range(8):graph += [f'[{i}:v]settb=AVTB,setpts=PTS-STARTPTS[v{i}]',f'[{i}:a]asetpts=PTS-STARTPTS[a{i}]']
    current='v0';sound='a0';elapsed=durations[0];overlap=.65
    transitions=['fade','wipeleft','smoothright','wipeleft','fade','wipeleft','wiperight']
    for i in range(1,8):
        nxt=f'x{i}';anext=f'm{i}'
        graph += [f'[{current}][v{i}]xfade=transition={transitions[i-1]}:duration={overlap}:offset={elapsed-overlap}[{nxt}]',
                  f'[{sound}][a{i}]acrossfade=d={overlap}:c1=tri:c2=tri[{anext}]']
        current=nxt;sound=anext;elapsed+=durations[i]-overlap
    graph+=[f'[{sound}]afade=t=in:st=0:d=0.4,afade=t=out:st={elapsed-.9}:d=0.9,alimiter=limit=.89:level=false[audio]']
    (OUT/'final.filter').write_text(';'.join(graph),encoding='utf-8')
    destination='Brutal-Legend-VR-OpenXR-Showcase.mp4'
    run([*args,'-filter_complex_script','final.filter','-map',f'[{current}]','-map','[audio]','-c:v','libx264','-preset','slow','-crf','18','-pix_fmt','yuv420p','-r','30','-c:a','aac','-b:a','224k','-movflags','+faststart',destination],'final-encode.log')
    (OUT/'edit.json').write_text(json.dumps(dict(video=destination,seconds=elapsed,source='Final OpenXR simulator composition, left eye',audio='Captured game render endpoint, editorial loudness normalization and crossfades',titles='Copperplate Gothic / Rockwell Condensed',transitions=transitions,split='Two left-eye moments: rig room and native gameplay',clips=[dict(source=s[0],start=s[1],duration=s[2],label=s[3]) for s in specs]),indent=2))
    print(OUT/destination,flush=True)

if __name__=='__main__':main()
