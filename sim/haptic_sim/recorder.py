import json
import time
from pathlib import Path

import numpy as np

from .hand import CHANNELS

FIELDS = ["t", "period", "step_time"] + [
    f"{kind}_{c}" for kind in ("cmd", "closure", "feedback", "contact") for c in CHANNELS
] + ["trial", "mode"] + [f"tracked_{k}" for k in ("x", "y", "z", "qw", "qx", "qy", "qz")]


class Recorder:
    '''Buffers one row per physics step in memory and writes a compressed npz at the end.'''

    def __init__(self, directory: str, enabled: bool = True, chunk: int = 60_000):
        '''Prepare the first buffer chunk.'''
        self.directory = Path(directory)
        self.enabled = enabled
        self.chunk = chunk
        self.chunks = [np.empty((chunk, len(FIELDS)))]
        self.n = 0

    def record(self, t, period, step_time, cmd, closure, feedback, contact, trial, mode, tracked) -> None:
        '''Append one row, growing the buffer in chunks; the tracked pose is NaN while not tracked.'''
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
        self.n += 1

    def data(self) -> np.ndarray:
        '''Return all recorded rows as one array.'''
        return np.concatenate(self.chunks)[: self.n]

    def save(self, meta: dict) -> Path | None:
        '''Write the rows plus metadata to logs/run_<timestamp>.npz.'''
        if not self.enabled or self.n == 0:
            return None
        self.directory.mkdir(parents=True, exist_ok=True)
        path = self.directory / f"run_{time.strftime('%Y%m%d_%H%M%S')}.npz"
        np.savez_compressed(path, data=self.data(), fields=np.array(FIELDS), meta=json.dumps(meta))
        return path


def timing_summary(periods: np.ndarray, step_times: np.ndarray, target: float) -> str:
    '''Format loop period and step time statistics in milliseconds.'''
    if len(periods) < 2:
        return "no timing data"
    p, s = periods[1:] * 1e3, step_times * 1e3
    late = np.mean(p > 1.5 * target * 1e3) * 100
    return (f"loop period [ms] mean {p.mean():.3f}  std {p.std():.3f}  p99 {np.percentile(p, 99):.3f}  "
            f"max {p.max():.3f}  late(>1.5x) {late:.2f}%\n"
            f"step time   [ms] mean {s.mean():.3f}  p99 {np.percentile(s, 99):.3f}  max {s.max():.3f}")
