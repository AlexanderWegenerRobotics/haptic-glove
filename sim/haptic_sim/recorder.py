import json
import time
from pathlib import Path

import numpy as np

from .hand import CHANNELS

FIELDS = ["t", "period", "step_time"] + [
    f"{kind}_{c}" for kind in ("cmd", "closure", "feedback", "contact") for c in CHANNELS
] + ["trial", "mode"] + [f"tracked_{k}" for k in ("x", "y", "z", "qw", "qx", "qy", "qz")] + [
    f"glove_{kind}_{c}" for kind in ("sent", "torque") for c in CHANNELS
] + ["glove_rtt", "glove_flags", "wall"]


class Recorder:
    '''Buffers one row per physics step in memory and writes a compressed npz at the end.'''

    def __init__(self, directory: str, enabled: bool = True, chunk: int = 60_000):
        '''Prepare the first buffer chunk.'''
        self.directory = Path(directory)
        self.enabled = enabled
        self.chunk = chunk
        self.chunks = [np.empty((chunk, len(FIELDS)))]
        self.n = 0

    def record(self, t, period, step_time, cmd, closure, feedback, contact, trial, mode, tracked, glove=None) -> None:
        '''Append one row, growing the buffer in chunks; tracked pose and glove columns are NaN when absent.'''
        if not self.enabled:
            return
        row = self.n % self.chunk
        if row == 0 and self.n > 0:
            self.chunks.append(np.empty((self.chunk, len(FIELDS))))
        buf = self.chunks[-1][row]
        buf[0], buf[1], buf[2] = t, period, step_time
        buf[3:6], buf[6:9], buf[9:12], buf[12:15] = cmd, closure, feedback, contact
        buf[15], buf[16] = trial, mode
        if tracked is None:
            buf[17:24] = np.nan
        else:
            buf[17:20], buf[20:24] = tracked
        buf[24:32] = np.nan if glove is None else glove
        buf[32] = time.time()
        self.n += 1

    def data(self) -> np.ndarray:
        '''Return all recorded rows as one array.'''
        return np.concatenate(self.chunks)[: self.n]

    def save(self, meta: dict, stem: str) -> Path | None:
        '''Write the rows plus metadata to <directory>/<stem>.npz.'''
        if not self.enabled or self.n == 0:
            return None
        self.directory.mkdir(parents=True, exist_ok=True)
        path = self.directory / f"{stem}.npz"
        np.savez_compressed(path, data=self.data(), fields=np.array(FIELDS), meta=json.dumps(meta))
        return path


class SessionLog:
    '''One folder per sim session: scene logs, scene descriptions and session.json with the trial table.'''

    def __init__(self, root: str, cfg: dict, enabled: bool = True):
        '''Name the session after its start time and write the initial session.json.'''
        self.id = time.strftime("%Y%m%d_%H%M%S")
        self.directory = Path(root) / self.id
        self.enabled = enabled
        self.info = {"session_id": self.id, "created": time.strftime("%Y-%m-%dT%H:%M:%S"), "start_unix": time.time(),
                     "config": cfg, "scenes": [], "trials": []}
        self.scene_count = 0
        self._write()

    def as_dict(self) -> dict:
        '''Session id and folder for the scene message, so the renderer logs into the same folder.'''
        return {"id": self.id, "directory": str(self.directory)}

    def next_stem(self, scene_id: str) -> str:
        '''File stem for the next scene, numbered in session order.'''
        self.scene_count += 1
        return f"scene_{self.scene_count:02d}_{scene_id}"

    def add_scene(self, stem: str, scene_id: str, rows: np.ndarray) -> None:
        '''Append the scene and the trials it contained, with wall clock start and end per trial.'''
        if not self.enabled or not len(rows):
            return
        trial, wall = rows[:, FIELDS.index("trial")], rows[:, FIELDS.index("wall")]
        self.info["scenes"].append({"stem": stem, "scene_id": scene_id, "start_unix": float(wall[0]),
                                    "end_unix": float(wall[-1])})
        for number in dict.fromkeys(trial.astype(int).tolist()):
            sel = wall[trial == number]
            self.info["trials"].append({"trial": number, "scene": stem, "scene_id": scene_id,
                                        "start_unix": float(sel[0]), "end_unix": float(sel[-1])})
        self._write()

    def _write(self) -> None:
        '''Rewrite session.json.'''
        if not self.enabled:
            return
        self.directory.mkdir(parents=True, exist_ok=True)
        (self.directory / "session.json").write_text(json.dumps(self.info, indent=2))


def timing_summary(periods: np.ndarray, step_times: np.ndarray, target: float) -> str:
    '''Format loop period and step time statistics in milliseconds.'''
    if len(periods) < 2:
        return "no timing data"
    p, s = periods[1:] * 1e3, step_times * 1e3
    late = np.mean(p > 1.5 * target * 1e3) * 100
    return (f"loop period [ms] mean {p.mean():.3f}  std {p.std():.3f}  p99 {np.percentile(p, 99):.3f}  "
            f"max {p.max():.3f}  late(>1.5x) {late:.2f}%\n"
            f"step time   [ms] mean {s.mean():.3f}  p99 {np.percentile(s, 99):.3f}  max {s.max():.3f}")
