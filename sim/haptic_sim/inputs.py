import numpy as np

from .hand import CHANNELS


class PoseSource:
    '''Provides the wrist target pose; None means the mocap body is left alone (viewer dragging).'''

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


class FingerSource:
    '''Provides normalized channel closures; None means the viewer control sliders are used.'''

    def read(self, t: float):
        '''Return an array of closures in [0, 1] per channel or None.'''
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


def make_pose_source(cfg: dict) -> PoseSource:
    '''Create the configured wrist pose source.'''
    name = cfg["input"]["pose_source"]
    if name == "viewer":
        return PoseSource()
    if name == "fixed":
        return FixedPose(cfg)
    raise ValueError(f"unknown pose_source '{name}'")


def make_finger_source(cfg: dict) -> FingerSource:
    '''Create the configured finger command source.'''
    name = cfg["input"]["finger_source"]
    if name == "viewer":
        return FingerSource()
    if name == "script":
        return ScriptFingers(cfg)
    raise ValueError(f"unknown finger_source '{name}'")
