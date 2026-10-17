"""Check local neural narration for missing phrases with an open ASR model."""
import argparse
import json
import os
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
CACHE=ROOT/'release-work/voice-work/model-cache'
os.environ['HF_HUB_CACHE']=str(CACHE)
os.environ['TRANSFORMERS_CACHE']=str(CACHE)
os.environ['HF_HUB_DISABLE_XET']='1'
os.environ['TOKENIZERS_PARALLELISM']='false'

import librosa
import torch
from transformers import pipeline


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--sample',action='store_true');parser.add_argument('--only',nargs='*');args=parser.parse_args()
    torch.set_num_threads(6)
    print('Loading local Whisper tiny English speech checker...',flush=True)
    checker=pipeline('automatic-speech-recognition',model='openai/whisper-tiny.en',device='cpu')
    folder=ROOT/'outputs/support-tutorial/audio-neural'
    files=[folder/'voice-sample.wav'] if args.sample else [folder/(s['id']+'.wav') for s in json.loads((ROOT/'assets/tutorial/narration.json').read_text())]
    if args.only:files=[path for path in files if path.stem in args.only]
    result={}
    for path in files:
        audio,sr=librosa.load(path,sr=16000)
        transcript=checker({'array':audio,'sampling_rate':sr},generate_kwargs={'max_new_tokens':180})['text'].strip()
        result[path.stem]=transcript
        print(path.stem+': '+transcript,flush=True)
    output=folder/('sample-transcript.json' if args.sample else 'transcripts.json')
    output.write_text(json.dumps(result,indent=2))


if __name__=='__main__':main()
