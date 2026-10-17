[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$voicePython=Join-Path $root 'release-work\voice-env\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $voicePython)) { throw 'Create the isolated Chatterbox environment described in assets/tutorial/README.md first.' }
& $voicePython (Join-Path $PSScriptRoot 'tutorial_neural_voice.py')
if ($LASTEXITCODE -ne 0) { throw 'Neural voice generation failed; no desktop voice fallback is used.' }
