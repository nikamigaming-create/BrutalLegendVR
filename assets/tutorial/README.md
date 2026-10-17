# Support tutorial source

The guide uses a fictional illustrated rock fan, actual preview 5 launcher
controls filmed with example paths, local neural narration and an original
quiet background pulse. The artwork was made with the built-in image generation tool;
the [exact prompt](forge-guide-prompt.txt) is retained beside the image.

The approved voice is a lower male delivery with unhurried sentence groups and
varied breath pauses. It uses [Chatterbox 0.1.6](https://github.com/resemble-ai/chatterbox)
(MIT) with an original synthetic reference made using
[Kokoro 0.9.4 / Kokoro-82M](https://huggingface.co/hexgrad/Kokoro-82M) (Apache 2.0),
mixing 80% `am_fenrir` and 20% `am_michael`. No person's recording was supplied.
Reference generation, delivery settings and phrase timings are retained in
the scripts and output metadata. The sample approved in chat is
`outputs/support-tutorial/voice-rugged.mp3`.

Rebuild in a Python 3.11 virtual environment under `release-work/voice-env`:

```powershell
python -m venv release-work/voice-env
./release-work/voice-env/Scripts/python.exe -m pip install torch==2.6.0 torchaudio==2.6.0 --index-url https://download.pytorch.org/whl/cpu
./release-work/voice-env/Scripts/python.exe -m pip install chatterbox-tts==0.1.6 kokoro==0.9.4
./release-work/voice-env/Scripts/python.exe scripts/tutorial_voice_reference.py
./scripts/tutorial_voice.ps1
python scripts/render_support_tutorial.py
python scripts/check_support_video.py
```

The renderer also needs NumPy, Pillow, Windows fonts and FFmpeg. Model downloads
stay under `release-work/voice-work/model-cache`. The script uses 90% tempo
without changing pitch, Chatterbox exaggeration 0.5 and CFG 0.4. The authored
pauses in `narration.json` guide the delivery. Captions follow the actual phrase
timings rather than distributing text across the scene's entire duration.

`scripts/check_tutorial_speech.py` transcribes each scene with local Whisper
tiny English for a wording check. It does not certify whether a voice sounds
natural. `scripts/check_support_video.py` decodes every frame and compares key
button/label regions against the clean source composition to catch obscuring
overlays or encoding damage. Inspect the public frames as well.

Old personal screenshots and the example support ZIP stay in the ignored local
`artifacts/support-tutorial-private` directory. They are private working
material and must not be published. The current renderer only reads the clean
`captures/` assets: production UI opened via `scripts/tutorial_demo.py` with
`C:\Games\BrutalLegend`, a save-dialog crop limited to filename and buttons,
and an error dialog containing no personal fields. It does not read private
captures or a real support ZIP. The report example uses generic descriptions.
There are no blur layers. Orange outlines sit outside the controls.
The final guide and captions are written to `outputs/support-tutorial`.
