import numpy as np

from .hand import CHANNELS


class PoseSource:
    '''Provides the wrist target pose; None means the mocap body is left alone (viewer dragging).'''

    tracked = False

    def read(self, t: float):
        '''Return (position, quaternion wxyz) or None.'''
        return None


class FixedPose(PoseSource):
    '''Holds the wrist at the configured start pose.'''

    def __init__(self, cfg: dict):
        '''Store the start pose.'''
        pose = cfg["hand"]["start_pose"]
        self.pose = (np.array(pose["position"], float), np.array(pose["orientation"], float))

    def read(self, t):
        '''Always return the start pose.'''
        return self.pose


class VivePose(PoseSource):
    '''Wrist pose of the SteamVR device in the calibrated world frame; engagement is handled by the session.'''

    tracked = True

    def __init__(self, tracking):
        '''Keep the tracking system.'''
        self.tracking = tracking

    def read(self, t):
        '''Return the latest tracked wrist pose or None while not tracked.'''
        return self.tracking.wrist_pose()


class FingerSource:
    '''Provides normalized channel closures; None means the viewer control sliders are used.'''

    def read(self, t: float):
        '''Return an array of closures in [0, 1] per channel or None.'''
        return None

    def write_feedback(self, feedback: np.ndarray, enable: bool) -> None:
        '''Hand the per-channel feedback back to the source; only the glove uses it.'''

    def log_values(self):
        '''Glove columns for the run log, None for sources without a glove.'''
        return None


class ScriptFingers(FingerSource):
    '''Slow open/close cycle on all channels, for headless tests.'''

    def __init__(self, cfg: dict):
        '''Store period and amplitude of the cycle.'''
        script = cfg["input"]["script"]
        self.period = script["period"]
        self.max_closure = script["max_closure"]

    def read(self, t):
        '''Cosine ramp between open and max closure.'''
        c = 0.5 * self.max_closure * (1.0 - np.cos(2.0 * np.pi * t / self.period))
        return np.full(len(CHANNELS), c)


class TriggerFingers(FingerSource):
    '''Wand trigger closes all channels together, stand-in until the glove exists.'''

    def __init__(self, cfg: dict, tracking):
        '''Keep the tracking system and the closure at full trigger.'''
        self.tracking = tracking
        self.max_closure = cfg["input"]["trigger"]["max_closure"]

    def read(self, t):
        '''Scale the trigger value to the same closure on every channel.'''
        return np.full(len(CHANNELS), self.max_closure * self.tracking.trigger())


class GloveFingers(FingerSource):
    '''Closures from the haptic glove (ESP32 or fake glove); low-passed feedback goes back as servo torque.'''

    def __init__(self, cfg: dict):
        '''Open or reuse the glove link and set up the feedback filter.'''
        from .glove import open_glove

        self.link = open_glove(cfg)
        cutoff = cfg["glove"]["filter_hz"]
        dt = cfg["simulation"]["timestep"]
        self.alpha = float(np.exp(-2.0 * np.pi * cutoff * dt)) if cutoff > 0 else 0.0
        self.filtered = np.zeros(len(CHANNELS))

    def read(self, t):
        '''Latest glove closures, None while the glove is silent so the sliders take over.'''
        state = self.link.latest()
        return None if state is None else np.clip(state.closure, 0.0, 1.0)

    def write_feedback(self, feedback, enable):
        '''Low-pass the feedback and send it; enable is False while not engaged or stopped.'''
        self.filtered = self.alpha * self.filtered + (1.0 - self.alpha) * np.nan_to_num(feedback)
        self.link.set_feedback(self.filtered, enable)

    def log_values(self):
        '''Torque sent, torque the glove applies, round trip in ms and glove flags.'''
        state = self.link.latest()
        if state is None:
            return None
        return np.concatenate([self.link.sent, state.torque, [state.rtt_us / 1000.0, state.flags]])


def make_pose_source(cfg: dict, tracking=None) -> PoseSource:
    '''Create the configured wrist pose source.'''
    name = cfg["input"]["pose_source"]
    if name == "viewer":
        return PoseSource()
    if name == "fixed":
        return FixedPose(cfg)
    if name == "vive":
        return VivePose(tracking)
    raise ValueError(f"unknown pose_source '{name}'")


def make_finger_source(cfg: dict, tracking=None) -> FingerSource:
    '''Create the configured finger command source.'''
    name = cfg["input"]["finger_source"]
    if name == "viewer":
        return FingerSource()
    if name == "script":
        return ScriptFingers(cfg)
    if name == "trigger":
        return TriggerFingers(cfg, tracking)
    if name == "glove":
        return GloveFingers(cfg)
    raise ValueError(f"unknown finger_source '{name}'")
