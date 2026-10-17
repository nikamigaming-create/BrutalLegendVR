"""Decode every frame and check that demo controls survive compositing clearly."""
import hashlib
import json
from pathlib import Path
import subprocess
import numpy as np
from PIL import Image
from render_support_tutorial import make_scene

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'outputs/support-tutorial'
timeline=json.loads((OUT/'timeline.json').read_text())
width,height,fps=timeline['width'],timeline['height'],timeline['fps']
scene={s['id']:s for s in timeline['scenes']}
regions={'launch':[(682,553,162,28),(685,632,95,23)],
         'settings':[(676,453,320,29),(654,670,197,29)],
         'export':[(419,345,270,41),(1540,814,83,24)],
         'reproduce':[(1140,461,640,51)]}
maximum={key:0.0 for key in regions}; counts={key:0 for key in regions}
references={key:[np.array(make_scene(key,i)) for i in range(3 if key=='settings' else 1)] for key in regions}
process=subprocess.Popen(['ffmpeg','-v','error','-i',str(OUT/'BLVR-support-guide.mp4'),
    '-f','rawvideo','-pix_fmt','rgb24','pipe:1'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
frame_size=width*height*3
number=0
while True:
    data=process.stdout.read(frame_size)
    if not data:break
    if len(data)!=frame_size:raise RuntimeError('Partial decoded video frame')
    frame=np.frombuffer(data,dtype=np.uint8).reshape(height,width,3)
    now=number/fps
    if float(frame.mean())<5:raise RuntimeError(f'Blank/dark frame at {now:.2f}s')
    for sid,boxes in regions.items():
        local=now-scene[sid]['start']
        if .5<=local<scene[sid]['duration']:
            variant=min(2,int(local/(scene[sid]['duration']/3))) if sid=='settings' else 0
            reference=references[sid][variant]
            for x,y,w,h in boxes:
                patch=frame[y:y+h,x:x+w].astype(np.float32)
                expected_patch=reference[y:y+h,x:x+w].astype(np.float32)
                error=float(np.abs(patch-expected_patch).mean())
                maximum[sid]=max(maximum[sid],error)
            counts[sid]+=1
    number+=1
if process.wait():raise RuntimeError(process.stderr.read().decode())
expected=round(timeline['duration']*fps)
if number!=expected:raise RuntimeError(f'Expected {expected} frames, decoded {number}')
if not all(counts.values()):raise RuntimeError('A control scene was not checked')
if any(value>12 for value in maximum.values()):raise RuntimeError(f'Button/label obscured or degraded: {maximum}')
captures=ROOT/'assets/tutorial/captures'
hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(captures.glob('*.png'))}
for path in captures.glob('*.png'):
    if Image.open(path).getexif():raise RuntimeError(f'Unexpected capture metadata: {path.name}')
receipt={'decoded_frames':number,'expected_frames':expected,'decode_passed':True,
         'control_scene_frames':counts,'max_control_mean_absolute_pixel_error':maximum,
         'source_capture_sha256':hashes,
         'privacy_method':'Production UI filmed with example paths; save dialog cropped to filename and buttons. No private desktop or path fields in the render inputs.',
         'visual_review':'Source captures and all ten composed scenes reviewed for personal fields, readable controls and layout. Pixel checks assess encoding/compositing, not semantic privacy.'}
(OUT/'verification.json').write_text(json.dumps(receipt,indent=2))
print(json.dumps(receipt,indent=2))
