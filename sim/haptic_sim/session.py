import sys
import threading
from collections import deque

import numpy as np

from .tracking import quat_angle

WAITING, ENGAGED = 0, 1
COMMANDS = ("reset_same", "reset_new", "calibrate", "quit")
TERMINAL_KEYS = {"r": "reset_same", "n": "reset_new", "c": "calibrate", "q": "quit"}
TERMINAL_HELP = "commands (type + Enter): r reset, n new scene, c calibrate, q quit"


class CommandQueue:
    '''Thread-safe inbox for session commands from any input method.'''

    def __init__(self):
        '''Start empty.'''
        self.items = deque()

    def put(self, command: str) -> None:
        '''Queue a command; unknown names are rejected.'''
        if command not in COMMANDS:
            raise ValueError(f"unknown command '{command}'")
        self.items.append(command)

    def drain(self) -> list:
        '''Pop all queued commands.'''
        out = []
        while self.items:
            out.append(self.items.popleft())
        return out


def start_terminal_commands(commands: CommandQueue) -> threading.Thread:
    '''Read one-letter commands typed into the terminal in a background thread.'''

    def run():
        '''Map each typed line to a command.'''
        for line in sys.stdin:
            key = line.strip().lower()
            if key in TERMINAL_KEYS:
                commands.put(TERMINAL_KEYS[key])
            elif key:
                print(TERMINAL_HELP)

    thread = threading.Thread(target=run, daemon=True, name="terminal")
    thread.start()
    print(TERMINAL_HELP)
    return thread


class Session:
    '''Engage logic: a tracked hand drives the sim hand only after it was brought onto it, and is released on loss.'''

    def __init__(self, cfg: dict, tracked: bool):
        '''Read thresholds; untracked pose sources are always engaged.'''
        s = cfg["session"]
        self.tracked = tracked
        self.distance = s["engage_distance"]
        self.angle = np.radians(s["engage_angle"])
        self.lost_timeout = s["lost_timeout"]
        self.lost_since = None
        self.mode = WAITING if tracked else ENGAGED

    @property
    def ghost_visible(self) -> bool:
        '''The ghost hand is shown while waiting for the operator to engage.'''
        return self.tracked and self.mode == WAITING

    def reset(self) -> None:
        '''Go back to waiting, after a reset or a new calibration.'''
        self.mode = WAITING if self.tracked else ENGAGED
        self.lost_since = None

    def update(self, now: float, tracked_pose, hand_pose):
        '''Advance the state machine and return the pose for the mocap target, None to hold it.'''
        if not self.tracked:
            return tracked_pose
        if tracked_pose is None:
            if self.mode == ENGAGED:
                self.lost_since = now if self.lost_since is None else self.lost_since
                if now - self.lost_since > self.lost_timeout:
                    self.mode = WAITING
                    print("tracking lost, hand released")
            return None
        self.lost_since = None
        if self.mode == WAITING and self._near(tracked_pose, hand_pose):
            self.mode = ENGAGED
            print("engaged")
        return tracked_pose if self.mode == ENGAGED else None

    def _near(self, a, b) -> bool:
        '''True when two poses are within the engage distance and angle.'''
        return (np.linalg.norm(np.asarray(a[0]) - b[0]) < self.distance
                and quat_angle(a[1], b[1]) < self.angle)
