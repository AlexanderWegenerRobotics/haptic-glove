import threading
import time
from typing import Callable, ContextManager

import mujoco
import numpy as np

from .hand import HandAdapter
from .inputs import FingerSource, PoseSource
from .outputs import UnrealPublisher
from .recorder import Recorder
from .scene import GHOST_BODY, GHOST_RGBA, HAND_PREFIX, TARGET_BODY
from .session import ENGAGED, CommandQueue, Session


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
    '''Runs commands, input polling, engagement, mj_step and recording at the sim timestep in its own thread.'''

    def __init__(self, model: mujoco.MjModel, data: mujoco.MjData, lock: Callable[[], ContextManager], hand: HandAdapter,
                 pose_source: PoseSource, finger_source: FingerSource, recorder: Recorder, cfg: dict,
                 session: Session, commands: CommandQueue, publisher: UnrealPublisher | None = None, tracking=None):
        '''Store references and look up mocap and ghost indices; lock() must return a context manager.'''
        super().__init__(daemon=True, name="physics")
        self.model, self.data, self.lock, self.hand = model, data, lock, hand
        self.pose_source, self.finger_source, self.recorder = pose_source, finger_source, recorder
        self.session, self.commands, self.publisher, self.tracking = session, commands, publisher, tracking
        self.realtime = cfg["simulation"]["realtime"]
        self.duration = cfg["simulation"]["duration"]
        self.mocap_id = model.body_mocapid[model.body(TARGET_BODY).id]
        ghost_body = model.body(GHOST_BODY).id
        self.ghost_id = model.body_mocapid[ghost_body]
        self.ghost_geoms = np.flatnonzero(model.geom_bodyid == ghost_body)
        self.ghost_visible = False
        self.root_body = model.body(HAND_PREFIX + "hand_root").id
        self.trial = 0
        self.exit_reason = "stopped"
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
            for command in self.commands.drain():
                self._apply_command(command)
            if self.stop_event.is_set():
                break
            now = time.perf_counter()
            period, last = now - last, now
            t = d.time
            tracked = self.pose_source.read(t)
            cmd = self.finger_source.read(t)

            t0 = time.perf_counter()
            with self.lock():
                pose = self.session.update(now, tracked, (d.xpos[self.root_body], d.xquat[self.root_body]))
                if pose is not None:
                    d.mocap_pos[self.mocap_id], d.mocap_quat[self.mocap_id] = pose
                self._update_ghost(tracked)
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
                    packet = self.publisher.capture(d, closure, feedback, self.session.mode == ENGAGED,
                                                    self.ghost_visible)
            step_time = time.perf_counter() - t0
            if packet is not None:
                self.publisher.send(packet)

            self.recorder.record(t, period, step_time, cmd, closure, feedback, contact, self.trial, self.session.mode,
                                 tracked)
            if self.duration and d.time >= self.duration:
                self.exit_reason = "duration"
                break
            if self.realtime:
                timer.wait()
        self.stop_event.set()

    def _update_ghost(self, tracked) -> None:
        '''Move the ghost to the tracked pose and show it only while waiting for engagement.'''
        d = self.data
        if tracked is not None:
            d.mocap_pos[self.ghost_id], d.mocap_quat[self.ghost_id] = tracked
        visible = self.session.ghost_visible and tracked is not None
        if visible != self.ghost_visible:
            self.model.geom_rgba[self.ghost_geoms, 3] = GHOST_RGBA[3] if visible else 0.0
            self.ghost_visible = visible

    def _apply_command(self, command: str) -> None:
        '''Apply one session command between steps; reset_new and quit end the loop for the app to handle.'''
        if command == "reset_same":
            with self.lock():
                mujoco.mj_resetData(self.model, self.data)
                mujoco.mj_forward(self.model, self.data)
            self.session.reset()
            self.trial += 1
            print(f"reset, trial {self.trial}")
        elif command == "calibrate":
            if self.tracking is None:
                print("calibrate needs SteamVR tracking (pose_source vive)")
            elif self.tracking.calibrate():
                self.session.reset()
                if self.publisher is not None:
                    self.publisher.set_tracking(self.tracking.calibration.as_dict())
        else:
            self.exit_reason = command
            self.stop_event.set()
