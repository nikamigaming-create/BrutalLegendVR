"""Local expressive narration with a wholly synthetic male voice reference."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'release-work/voice-work'
WORK.mkdir(parents=True, exist_ok=True)
os.environ['HF_HUB_CACHE'] = str(WORK / 'model-cache')
os.environ['HF_HUB_DISABLE_XET'] = '1'
os.environ['TOKENIZERS_PARALLELISM'] = 'false'

import numpy as np
import soundfile as sf
import torch
from chatterbox.tts import ChatterboxTTS


def deliberate(wav, rate, pace):
    """Change tempo without pitching the voice down or stretching its pauses."""
    audio = wav.squeeze().numpy().astype('<f4')
    result = subprocess.run(['ffmpeg', '-v', 'error', '-f', 'f32le', '-ar', str(rate),
        '-ac', '1', '-i', 'pipe:0', '-af', f'atempo={pace}', '-f', 'f32le', 'pipe:1'],
        input=audio.tobytes(), capture_output=True, check=True)
    return np.frombuffer(result.stdout, dtype='<f4').copy()


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--sample',action='store_true')
    parser.add_argument('--only',nargs='*')
    parser.add_argument('--pace',type=float,default=.9)
    args=parser.parse_args()
    torch.set_num_threads(min(12,os.cpu_count() or 4))
    random.seed(17);np.random.seed(17);torch.manual_seed(17)
    start=time.monotonic()
    reference=ROOT/'assets/tutorial/rugged-synthetic-reference.wav'
    if not reference.exists():
        raise FileNotFoundError('Run tutorial_voice_reference.py first.')
    print('Loading Chatterbox 0.1.6 with the synthetic male reference...',flush=True)
    model=ChatterboxTTS.from_pretrained(device='cpu')
    print(f'Model ready after {time.monotonic()-start:.1f}s',flush=True)
    scenes=json.loads((ROOT/'assets/tutorial/narration.json').read_text(encoding='utf-8'))
    out=ROOT/'outputs/support-tutorial/audio-neural'
    out.mkdir(parents=True,exist_ok=True)
    if args.sample:
        scenes=[{'id':'voice-sample','beats':[
            {'text':"Alright. Let's get you back in the game.",'pause':.65},
            {'text':"If something goes wrong... take a breath.",'pause':.75},
            {'text':"Open VR settings. Turn on detailed troubleshooting logs, then hit Save.",'pause':.85},
            {'text':"Try it again. Then export the support ZIP... and send it our way.",'pause':.45}]}]
    elif args.only:
        scenes=[scene for scene in scenes if scene['id'] in args.only]
    for scene in scenes:
        start=time.monotonic()
        chunks=[];timing=[];position=0.
        beats=scene.get('beats',[{'text':scene.get('speech',''),'pause':.45}])
        for index,beat in enumerate(beats):
            torch.manual_seed(17+index)
            wav=model.generate(beat['text'],audio_prompt_path=str(reference),
                exaggeration=.5,cfg_weight=.4,temperature=.7)
            audio=deliberate(wav,model.sr,beat.get('pace',args.pace))
            timing.append({'start':position,'end':position+len(audio)/model.sr,'text':beat['text']})
            silence=np.zeros(round(beat.get('pause',.55)*model.sr),dtype=np.float32)
            chunks.extend((audio,silence));position+=(len(audio)+len(silence))/model.sr
        audio=np.concatenate(chunks)
        sf.write(out/(scene['id']+'.wav'),audio,model.sr,subtype='PCM_16')
        (out/(scene['id']+'.timing.json')).write_text(json.dumps(timing,indent=2))
        print(f"Saved {scene['id']}: {len(audio)/model.sr:.1f}s audio in {time.monotonic()-start:.1f}s",flush=True)
    (out/'voice-source.json').write_text(json.dumps({'engine':'chatterbox-tts','version':'0.1.6',
        'model':'ResembleAI/chatterbox','voice':'Original synthetic male reference, generated with Kokoro 0.9.4; 80% am_fenrir + 20% am_michael',
        'reference':'assets/tutorial/rugged-synthetic-reference.wav',
        'reference_sha256':hashlib.sha256(reference.read_bytes()).hexdigest(),
        'exaggeration':.5,'cfg_weight':.4,'temperature':.7,'seed':17,
        'tempo':args.pace,'delivery':'Authored sentence groups with varied breath pauses; no human recording or celebrity reference'},indent=2))


if __name__=='__main__':
    main()
