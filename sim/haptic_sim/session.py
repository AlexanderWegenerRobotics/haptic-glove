import json
import socket
import sys
import threading
from collections import deque

import numpy as np

from .tracking import quat_angle

WAITING, ENGAGED, STOPPED = 0, 1, 2
MODE_NAMES = {WAITING: "waiting", ENGAGED: "engaged", STOPPED: "stopped"}
COMMANDS = ("reset_same", "reset_new", "calibrate", "stop", "resume", "set_hand", "quit")
HAND_TYPES = ("dexterous", "parallel_jaw")
TERMINAL_KEYS = {"r": "reset_same", "n": "reset_new", "c": "calibrate", "s": "stop", "g": "resume", "h": "set_hand",
                 "q": "quit"}
TERMINAL_HELP = ("commands (type + Enter): r reset, n new scene, c calibrate, s stop, g resume, h switch hand, "
                 "q quit")


class CommandQueue:
    '''Thread-safe inbox for session commands from any input method; remembers the last applied sequence number.'''

    def __init__(self):
        '''Start empty.'''
        self.items = deque()
        self.last_seq = 0

    def put(self, command: str, **args) -> None:
        '''Queue a command with optional arguments (seq, delay, hand); unknown names are rejected.'''
        if command not in COMMANDS:
            raise ValueError(f"unknown command '{command}'")
        self.items.append({"command": command, **args})

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


def start_udp_commands(commands: CommandQueue, cfg: dict) -> threading.Thread:
    '''Receive JSON commands from the renderer, e.g. {"seq": 3, "command": "calibrate", "delay": 3.0}.'''
    ucfg = cfg["unreal"]
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((ucfg["command_host"], ucfg["command_port"]))

    def run():
        '''Validate and queue each datagram; malformed ones are reported and dropped.'''
        while True:
            packet = sock.recv(4096)
            try:
                msg = json.loads(packet)
                commands.put(msg.pop("command"), **msg)
            except (ValueError, KeyError, TypeError) as e:
                print(f"udp command rejected: {e}")

    thread = threading.Thread(target=run, daemon=True, name="udp_commands")
    thread.start()
    print(f"commands on udp {ucfg['command_host']}:{ucfg['command_port']}")
    return thread


class Session:
    '''Engage logic: a tracked hand drives the sim hand only after it was brought onto it; stop holds everything.'''

    def __init__(self, cfg: dict, tracked: bool):
        '''Read thresholds; untracked pose sources are always engaged.'''
        s = cfg["session"]
        self.tracked = tracked
        self.distance = s["engage_distance"]
        self.angle = np.radians(s["engage_angle"])
        self.lost_timeout = s["lost_timeout"]
        self.lost_since = None
        self.trial = 0
        self.mode = self._idle()

    def _idle(self) -> int:
        '''Mode to fall back to: waiting for tracked sources, engaged otherwise.'''
        return WAITING if self.tracked else ENGAGED

    @property
    def stopped(self) -> bool:
        '''True while the operator stop is active.'''
        return self.mode == STOPPED

    @property
    def ghost_visible(self) -> bool:
        '''The ghost hand is shown while waiting for the operator to engage.'''
        return self.tracked and self.mode == WAITING

    def reset(self) -> None:
        '''Go back to waiting after a reset or a new calibration; a stop stays active.'''
        if not self.stopped:
            self.mode = self._idle()
        self.lost_since = None

    def stop(self) -> None:
        '''Freeze the hand and fingers until resume.'''
        if not self.stopped:
            print("stopped")
        self.mode = STOPPED

    def resume(self) -> None:
        '''Leave the stop; a tracked hand has to engage again.'''
        if self.stopped:
            self.mode = self._idle()
            self.lost_since = None
            print("resumed")

    def update(self, now: float, tracked_pose, hand_pose):
        '''Advance the state machine and return the pose for the mocap target, None to hold it.'''
        if self.stopped:
            return None
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
