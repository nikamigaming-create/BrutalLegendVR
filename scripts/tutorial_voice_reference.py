"""Build an original synthetic male reference; never uses a person's recording."""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'release-work/voice-work'
os.environ['HF_HUB_CACHE'] = str(WORK / 'model-cache')
os.environ['HF_HUB_DISABLE_XET'] = '1'
os.environ['TOKENIZERS_PARALLELISM'] = 'false'

import numpy as np
import soundfile as sf
import torch
from kokoro import KPipeline

torch.set_num_threads(min(12, os.cpu_count() or 4))
pipeline = KPipeline(lang_code='a', repo_id='hexgrad/Kokoro-82M', device='cpu')
voice = pipeline.load_voice('am_fenrir') * .8 + pipeline.load_voice('am_michael') * .2
out = ROOT / 'outputs/support-tutorial/audio-neural'
out.mkdir(parents=True, exist_ok=True)

def say(text, speed):
    return np.concatenate([audio.numpy() for _, _, audio in pipeline(text, voice=voice, speed=speed)])

reference = say("Alright. Take your time. We'll get this sorted out. Open the launcher, and head into the settings. Turn on the detailed logs, save your changes, and try it again.", .94)
sf.write(ROOT / 'assets/tutorial/rugged-synthetic-reference.wav', reference, 24000, subtype='PCM_16')
parts = [
    ("Alright. Let's get you back in the game.", .88, .65),
    ("If something goes wrong... take a breath.", .87, .75),
    ("Open VR settings. Turn on detailed troubleshooting logs, then hit Save.", .9, .85),
    ("Try it again. Then export the support ZIP... and send it our way.", .88, .45),
]
audio = np.concatenate([piece for text, speed, pause in parts for piece in (say(text, speed), np.zeros(round(pause * 24000), dtype=np.float32))])
sf.write(out / 'voice-rugged-kokoro.wav', audio, 24000, subtype='PCM_16')
print(f'Created synthetic reference and {len(audio)/24000:.1f}s deliberate sample.', flush=True)
