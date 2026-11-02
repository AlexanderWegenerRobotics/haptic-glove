# Voice commands

Sidecar that listens on the microphone, cuts speech with silero-vad, transcribes it with faster-whisper and sends recognised commands to Unreal as JSON over UDP (`127.0.0.1:9873`). `UVoiceCommandSubsystem` starts it when play begins and it exits on its own when Unreal closes. Its log is `Saved/Logs/voice_commands.log`.

## Commands

Only a whole short utterance counts, so normal talking does not trigger anything. Filler words like "okay" or "please" are ignored.

| say | command | Unreal does |
|---|---|---|
| stop, pause | `stop` | stops the sim |
| start, resume, continue | `start` | resumes when stopped |
| reset, restart | `reset` | `reset_same` |
| new scene, next scene, next | `new_scene` | `reset_new` |
| calibrate, calibration | `calibrate` | calibration with spoken countdown |
| switch hand, change hand, other hand | `switch_hand` | `set_hand` toggle |
| debug, show debug, hide debug | `debug` | toggles the debug panel |

Add phrases in `PHRASES`, common mishearings in `MISHEARINGS`, and keep `PROMPT` in step.

## Environment

Uses the `voice-control` conda env from teleop_vr_interface (same packages). To create it:

```bat
conda create -n voice-control python=3.11 -y
conda activate voice-control
pip install -r requirements.txt
```

If pyaudio does not build: `conda install -c conda-forge pyaudio`. For Whisper on the GPU, CTranslate2 needs `pip install nvidia-cublas-cu12 nvidia-cudnn-cu12`; without them it falls back to the CPU, which is fine for short commands.

## Testing without Unreal

```bat
python voice_commands.py --test "okay reset" "new seen"
python voice_commands.py --microphone VIVE
```

Settings live in Project Settings > Haptic Glove > Voice; the microphone is the Recording `MicrophoneName`, matched as part of the device name (use a short part like `VIVE`, Windows shortens some names).
