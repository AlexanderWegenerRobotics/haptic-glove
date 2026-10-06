import threading
import time
from typing import Callable, ContextManager

import mujoco
import numpy as np

from .hand import HandAdapter
from .inputs import FingerSource, PoseSource
from .outputs import UnrealPublisher
from .recorder import Recorder
from .scene import TARGET_BODY


class RateTimer:
    '''Paces a loop on absolute deadlines: coarse sleep, then a short spin for precision.'''

    def __init__(self, period: float, spin: float = 0.0004):
        '''Set the target period and the spin window before each deadline.'''
        self.period = period
        self.spin = spin
        self.next = time.perf_counter() + period

    def wait(self) -> None:
        '''Block until the next deadline; resync instead of bursting if far behind.'''
        now = time.perf_counter()
        if now > self.next + 10 * self.period:
            self.next = now
        remaining = self.next - now - self.spin
        if remaining > 0:
            time.sleep(remaining)
        while time.perf_counter() < self.next:
            pass
        self.next += self.period


class PhysicsLoop(threading.Thread):
    '''Runs input polling, mj_step and recording at the sim timestep in its own thread.'''

    def __init__(self, model: mujoco.MjModel, data: mujoco.MjData, lock: Callable[[], ContextManager], hand: HandAdapter,
                 pose_source: PoseSource, finger_source: FingerSource, recorder: Recorder, cfg: dict,
                 publisher: UnrealPublisher | None = None):
        '''Store references and look up the mocap target index; lock() must return a context manager.'''
        super().__init__(daemon=True, name="physics")
        self.model, self.data, self.lock, self.hand = model, data, lock, hand
        self.pose_source, self.finger_source, self.recorder = pose_source, finger_source, recorder
        self.publisher = publisher
        self.realtime = cfg["simulation"]["realtime"]
        self.duration = cfg["simulation"]["duration"]
        self.mocap_id = model.body_mocapid[model.body(TARGET_BODY).id]
        self.stop_event = threading.Event()
        self.state = {"t": 0.0, "cmd": np.zeros(3), "closure": np.zeros(3), "feedback": np.zeros(3)}

    def stop(self) -> None:
        '''Ask the loop to finish after the current step.'''
        self.stop_event.set()

    def run(self) -> None:
        '''Main physics loop.'''
        m, d = self.model, self.data
        timer = RateTimer(m.opt.timestep)
        last = time.perf_counter()
        while not self.stop_event.is_set():
            now = time.perf_counter()
            period, last = now - last, now
            t = d.time
            pose = self.pose_source.read(t)
            cmd = self.finger_source.read(t)

            t0 = time.perf_counter()
            with self.lock():
                if pose is not None:
                    d.mocap_pos[self.mocap_id], d.mocap_quat[self.mocap_id] = pose
                if cmd is not None:
                    self.hand.set_command(d, cmd)
                else:
                    cmd = self.hand.command_from_ctrl(d)
                mujoco.mj_step(m, d)
                closure = self.hand.closure(d)
                feedback, contact = self.hand.contact_feedback(d)
                self.state.update(t=d.time, cmd=cmd, closure=closure, feedback=feedback)
                packet = None
                if self.publisher is not None and self.publisher.due(now):
                    packet = self.publisher.capture(d, closure, feedback)
            step_time = time.perf_counter() - t0
            if packet is not None:
                self.publisher.send(packet)

            self.recorder.record(t, period, step_time, cmd, closure, feedback, contact)
            if self.duration and d.time >= self.duration:
                break
            if self.realtime:
                timer.wait()
        self.stop_event.set()
