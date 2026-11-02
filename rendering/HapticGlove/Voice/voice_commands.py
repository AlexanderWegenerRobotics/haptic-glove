import argparse
import json
import logging
import os
import queue
import re
import socket
import sys
import threading
import time
from collections import deque
from pathlib import Path

SAMPLE_RATE = 16000
CHUNK = 512
VAD_THRESHOLD = 0.4
SILENCE_CHUNKS = 15
PRE_ROLL_CHUNKS = 10
MIN_SECONDS = 0.25
MAX_SECONDS = 4.0
NO_SPEECH_REJECT = 0.6

PHRASES = {
    "stop": "stop",
    "pause": "stop",
    "start": "start",
    "resume": "start",
    "continue": "start",
    "reset": "reset",
    "restart": "reset",
    "new scene": "new_scene",
    "next scene": "new_scene",
    "next": "new_scene",
    "calibrate": "calibrate",
    "calibration": "calibrate",
    "switch hand": "switch_hand",
    "change hand": "switch_hand",
    "other hand": "switch_hand",
    "debug": "debug",
    "show debug": "debug",
    "hide debug": "debug",
    "debug panel": "debug",
}
MISHEARINGS = {
    "re set": "reset",
    "reset it": "reset",
    "new seen": "new scene",
    "next seen": "next scene",
    "new scenes": "new scene",
    "calibrates": "calibrate",
    "calibrated": "calibrate",
    "de bug": "debug",
    "switch hands": "switch hand",
    "change hands": "change hand",
    "stopp": "stop",
}
FILLERS = {"please", "okay", "ok", "now", "the", "hey", "um", "uh", "so", "and", "a"}
PROMPT = "Commands: stop, start, resume, reset, new scene, next scene, calibrate, switch hand, debug."

log = logging.getLogger("voice")


def register_cuda_dlls() -> None:
    '''Make pip-installed cuBLAS/cuDNN DLLs loadable, since a hidden process does not get the conda PATH.'''
    if not hasattr(os, "add_dll_directory"):
        return
    try:
        import nvidia
    except ImportError:
        return
    for directory in sorted(Path(nvidia.__path__[0]).glob("*/bin")):
        try:
            os.add_dll_directory(str(directory))
        except OSError:
            pass


def parent_alive(pid: int) -> bool:
    '''True while the launching Unreal process still runs.'''
    if pid <= 0:
        return True
    if sys.platform == "win32":
        import ctypes

        handle = ctypes.windll.kernel32.OpenProcess(0x00100000, False, pid)
        if not handle:
            return False
        running = ctypes.windll.kernel32.WaitForSingleObject(handle, 0) == 0x102
        ctypes.windll.kernel32.CloseHandle(handle)
        return running
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def match_command(text: str) -> str | None:
    '''Map a whole utterance to a command; anything with extra words is ignored so normal talk never triggers.'''
    t = re.sub(r"[^a-z ]", " ", text.lower())
    t = " ".join(t.split())
    for heard, meant in MISHEARINGS.items():
        t = re.sub(rf"\b{heard}\b", meant, t)
    words = [w for w in t.split() if w not in FILLERS]
    collapsed = [w for i, w in enumerate(words) if i == 0 or w != words[i - 1]]
    return PHRASES.get(" ".join(collapsed))


def confidence(avg_logprob: float) -> float:
    '''Squash Whisper's average log probability into 0..1.'''
    import math

    return 1.0 / (1.0 + math.exp(-2.0 * (avg_logprob + 0.7)))


def find_microphone(pa, name: str) -> int | None:
    '''Index of the first input device whose name contains the given text, None for the system default.'''
    inputs = [(i, pa.get_device_info_by_index(i)["name"]) for i in range(pa.get_device_count())
              if pa.get_device_info_by_index(i)["maxInputChannels"] > 0]
    for i, device in inputs:
        log.info("input device [%d] %s", i, device)
    if name:
        for i, device in inputs:
            if name.lower() in device.lower():
                log.info("using microphone [%d] %s", i, device)
                return i
        log.warning("no microphone matches '%s', using the system default", name)
    return None


def load_whisper(model: str, compute: str):
    '''Load Whisper and run one encode so a broken CUDA setup fails here, falling back to the CPU.'''
    import numpy as np
    from faster_whisper import WhisperModel

    for device in ([compute, "cpu"] if compute != "cpu" else ["cpu"]):
        try:
            whisper = WhisperModel(model, device=device, compute_type="int8_float16" if device == "cuda" else "int8")
            list(whisper.transcribe(np.zeros(SAMPLE_RATE, dtype=np.float32), language="en", beam_size=1)[0])
            log.info("whisper %s ready on %s", model, device)
            return whisper, device
        except Exception as error:
            log.warning("whisper on %s failed: %s", device, error)
    raise RuntimeError("whisper could not be loaded")


class Segmenter(threading.Thread):
    '''Reads the microphone, cuts speech segments with silero-vad and queues them for transcription.'''

    def __init__(self, segments: queue.Queue, device_index: int | None, stop: threading.Event):
        '''Open the microphone stream and the VAD model.'''
        super().__init__(daemon=True)
        import pyaudio
        from silero_vad import load_silero_vad

        self.segments, self.stop = segments, stop
        self.vad = load_silero_vad()
        self.pa = pyaudio.PyAudio()
        self.stream = self.pa.open(rate=SAMPLE_RATE, channels=1, format=pyaudio.paInt16, input=True,
                                   frames_per_buffer=CHUNK, input_device_index=device_index)

    def speech_probability(self, chunk) -> float:
        '''VAD probability for one 512-sample chunk.'''
        import numpy as np
        import torch

        with torch.no_grad():
            return self.vad(torch.from_numpy(chunk.astype(np.float32) / 32768.0), SAMPLE_RATE).item()

    def run(self) -> None:
        '''Collect chunks from speech onset until enough silence, with pre-roll so first consonants survive.'''
        import numpy as np

        pre_roll = deque(maxlen=PRE_ROLL_CHUNKS)
        active, silence = [], 0
        max_chunks = int(MAX_SECONDS * SAMPLE_RATE / CHUNK)
        while not self.stop.is_set():
            try:
                chunk = np.frombuffer(self.stream.read(CHUNK, exception_on_overflow=False), dtype=np.int16)
            except OSError as error:
                log.warning("microphone read failed: %s", error)
                time.sleep(0.05)
                continue
            speech = self.speech_probability(chunk) > VAD_THRESHOLD
            if not active:
                pre_roll.append(chunk)
                if speech:
                    active, silence = list(pre_roll), 0
                continue
            active.append(chunk)
            silence = 0 if speech else silence + 1
            if silence >= SILENCE_CHUNKS or len(active) >= max_chunks:
                audio = np.concatenate(active).astype(np.float32) / 32768.0
                if len(audio) >= MIN_SECONDS * SAMPLE_RATE:
                    if self.segments.full():
                        self.segments.get_nowait()
                    self.segments.put_nowait(audio)
                active = []
                pre_roll.clear()

    def close(self) -> None:
        '''Release the microphone.'''
        self.stream.stop_stream()
        self.stream.close()
        self.pa.terminate()


def transcribe(whisper, audio) -> tuple[str, float, float]:
    '''Text, confidence and no-speech probability of one segment.'''
    parts = list(whisper.transcribe(audio, language="en", beam_size=1, temperature=0.0, initial_prompt=PROMPT,
                                    condition_on_previous_text=False, vad_filter=False)[0])
    if not parts:
        return "", 0.0, 1.0
    text = " ".join(p.text.strip() for p in parts).strip()
    avg = sum(p.avg_logprob for p in parts) / len(parts)
    return text, confidence(avg), max(p.no_speech_prob for p in parts)


def run(args) -> None:
    '''Listen until the parent exits and send every recognised command to Unreal as one JSON datagram.'''
    register_cuda_dlls()
    import pyaudio

    whisper, device = load_whisper(args.model, args.compute)
    pa = pyaudio.PyAudio()
    microphone = find_microphone(pa, args.microphone)
    pa.terminate()

    stop = threading.Event()
    segments = queue.Queue(maxsize=4)
    segmenter = Segmenter(segments, microphone, stop)
    segmenter.start()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = (args.host, args.port)
    sock.sendto(json.dumps({"status": "ready", "model": args.model, "compute": device}).encode(), target)
    log.info("listening, sending to %s:%d", *target)

    try:
        while parent_alive(args.parent_pid):
            try:
                audio = segments.get(timeout=0.5)
            except queue.Empty:
                continue
            started = time.time()
            text, conf, no_speech = transcribe(whisper, audio)
            ms = (time.time() - started) * 1000.0
            command = match_command(text) if no_speech < NO_SPEECH_REJECT else None
            if command and conf >= args.min_confidence:
                sock.sendto(json.dumps({"command": command, "text": text, "confidence": round(conf, 3),
                                        "time": time.time()}).encode(), target)
                log.info("SENT %-12s '%s' conf %.2f (%.0f ms)", command, text, conf, ms)
            elif text:
                log.info("skip '%s' conf %.2f no_speech %.2f (%.0f ms)", text, conf, no_speech, ms)
        log.info("parent process ended")
    finally:
        stop.set()
        segmenter.join(timeout=2.0)
        segmenter.close()
        sock.close()


def main() -> None:
    '''Parse the command line, set up logging and run.'''
    parser = argparse.ArgumentParser(description="Voice commands for the haptic glove renderer")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9873)
    parser.add_argument("--model", default="small.en", help="tiny.en | base.en | small.en | medium.en")
    parser.add_argument("--compute", default="cuda", choices=["cuda", "cpu"])
    parser.add_argument("--microphone", default="", help="part of the input device name, empty = system default")
    parser.add_argument("--min-confidence", type=float, default=0.5)
    parser.add_argument("--parent-pid", type=int, default=0)
    parser.add_argument("--log", default="", help="log file, empty = console only")
    parser.add_argument("--test", nargs="*", help="print the command for each given phrase and exit")
    args = parser.parse_args()

    handlers = [logging.StreamHandler(sys.stdout)]
    if args.log:
        handlers.append(logging.FileHandler(args.log, mode="w", encoding="utf-8"))
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", handlers=handlers)

    if args.test is not None:
        for phrase in args.test:
            print(f"{phrase!r} -> {match_command(phrase)}")
        return
    try:
        run(args)
    except Exception:
        log.exception("voice commands stopped")
        raise


if __name__ == "__main__":
    main()
