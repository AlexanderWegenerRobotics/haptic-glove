import math
import threading
import time
from pathlib import Path

import mujoco
import numpy as np
import yaml

STEAMVR_TO_MUJOCO = np.array([[0.0, 0.0, -1.0], [-1.0, 0.0, 0.0], [0.0, 1.0, 0.0]])
DEVICE_FORWARD = np.array([0.0, 0.0, -1.0])
BUTTON_BITS = {"menu": 1, "grip": 2, "trackpad": 32}
TRIGGER_AXIS = 1
RESOLVE_INTERVAL = 1.0


def quat_to_mat(q) -> np.ndarray:
    '''Rotation matrix from a wxyz quaternion.'''
    m = np.zeros(9)
    mujoco.mju_quat2Mat(m, np.asarray(q, float))
    return m.reshape(3, 3)


def mat_to_quat(m) -> np.ndarray:
    '''Unit wxyz quaternion from a rotation matrix.'''
    q = np.zeros(4)
    mujoco.mju_mat2Quat(q, np.ascontiguousarray(m, float).ravel())
    return q


def yaw_matrix(yaw: float) -> np.ndarray:
    '''Rotation about the world z axis.'''
    c, s = math.cos(yaw), math.sin(yaw)
    return np.array([[c, -s, 0.0], [s, c, 0.0], [0.0, 0.0, 1.0]])


def quat_angle(q1, q2) -> float:
    '''Angle in rad between two orientations.'''
    return 2.0 * math.acos(min(1.0, abs(float(np.dot(q1, q2)))))


class Calibration:
    '''World alignment (yaw, position) of the SteamVR frame plus the grip rotation from device to wrist.'''

    def __init__(self, path: str | Path):
        '''Load the stored calibration or fall back to the uncalibrated default (wand pointing forward).'''
        self.path = Path(path)
        self.yaw, self.position, self.created = 0.0, np.zeros(3), None
        self.grip = mat_to_quat(STEAMVR_TO_MUJOCO.T)
        if self.path.exists():
            raw = yaml.safe_load(self.path.read_text()) or {}
            self.yaw = float(raw.get("yaw", 0.0))
            self.position = np.array(raw.get("position", [0.0, 0.0, 0.0]), float)
            self.grip = np.array(raw.get("grip", self.grip), float)
            self.created = raw.get("created")

    @property
    def valid(self) -> bool:
        '''True once a calibration was stored.'''
        return self.created is not None

    def rotation(self) -> np.ndarray:
        '''Rotation from the SteamVR frame to the world frame.'''
        return yaw_matrix(self.yaw) @ STEAMVR_TO_MUJOCO

    def to_world(self, rot: np.ndarray, pos: np.ndarray):
        '''Map a SteamVR pose (rotation, position) into the world frame.'''
        r = self.rotation()
        return r @ rot, r @ pos + self.position

    def update(self, yaw: float, position, grip) -> None:
        '''Store a new calibration and write it to disk.'''
        self.yaw, self.position, self.grip = float(yaw), np.asarray(position, float), np.asarray(grip, float)
        self.created = time.strftime("%Y-%m-%dT%H:%M:%S")
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(yaml.safe_dump(self.as_dict(), sort_keys=False))

    def as_dict(self) -> dict:
        '''Calibration as plain values, for the file and the Unreal scene message.'''
        return {"yaw": self.yaw, "position": [float(v) for v in self.position],
                "grip": [float(v) for v in self.grip], "created": self.created}


class TrackingSystem:
    '''Polls SteamVR in a background thread and serves the latest hand device pose, trigger value and button presses.'''

    def __init__(self, cfg: dict, on_button=None):
        '''Connect to SteamVR as a background app so it runs next to the renderer.'''
        import openvr

        tcfg = cfg["tracking"]
        self.openvr = openvr
        self.device = str(tcfg["device"])
        self.period = 1.0 / tcfg["rate"]
        self.yaw_from = tcfg["yaw_from"]
        self.offset_pos = np.array(tcfg["wrist_offset"], float)
        self.buttons = {cmd: BUTTON_BITS[b] for cmd, b in tcfg["buttons"].items() if b != "none"}
        self.start_position = np.array(cfg["hand"]["start_pose"]["position"], float)
        self.start_rot = quat_to_mat(cfg["hand"]["start_pose"]["orientation"])
        self.calibration = Calibration(tcfg["calibration"])
        self.on_button = on_button
        self.vr = openvr.init(openvr.VRApplication_Background)

        self.lock = threading.Lock()
        self.index = None
        self.next_resolve = 0.0
        self.device_pose = None
        self.hmd_pose = None
        self.trigger_value = 0.0
        self.pressed = 0
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True, name="tracking")

    def start(self) -> None:
        '''Start polling.'''
        self.thread.start()

    def stop(self) -> None:
        '''Stop polling and disconnect from SteamVR.'''
        self.stop_event.set()
        self.thread.join(timeout=1.0)
        self.openvr.shutdown()

    def _run(self) -> None:
        '''Poll at the configured rate until stopped.'''
        next_t = time.perf_counter()
        while not self.stop_event.is_set():
            self._poll()
            now = time.perf_counter()
            next_t = max(next_t + self.period, now)
            time.sleep(next_t - now)

    def _poll(self) -> None:
        '''Read device and headset poses plus the controller state, and fire the button callback on presses.'''
        ovr = self.openvr
        now = time.perf_counter()
        if (self.index is None or not self.vr.isTrackedDeviceConnected(self.index)) and now >= self.next_resolve:
            index = self._resolve()
            if index != self.index:
                print(f"tracking: device '{self.device}' " + ("not found" if index is None else f"at index {index}"))
            self.index, self.next_resolve = index, now + RESOLVE_INTERVAL

        poses = self.vr.getDeviceToAbsoluteTrackingPose(ovr.TrackingUniverseStanding, 0.0,
                                                        ovr.k_unMaxTrackedDeviceCount)
        hmd = self._pose(poses[ovr.k_unTrackedDeviceIndex_Hmd])
        device, trigger, pressed = None, 0.0, 0
        if self.index is not None:
            device = self._pose(poses[self.index])
            ok, state = self.vr.getControllerState(self.index)
            if ok:
                trigger, pressed = float(state.rAxis[TRIGGER_AXIS].x), int(state.ulButtonPressed)
        with self.lock:
            self.device_pose, self.hmd_pose, self.trigger_value = device, hmd, trigger

        rising = pressed & ~self.pressed
        self.pressed = pressed
        if rising and self.on_button is not None:
            for command, bit in self.buttons.items():
                if rising >> bit & 1:
                    self.on_button(command)

    def _pose(self, pose):
        '''Convert a SteamVR pose into (rotation, position), None when not tracked.'''
        if not pose.bPoseIsValid or pose.eTrackingResult != self.openvr.TrackingResult_Running_OK:
            return None
        m = pose.mDeviceToAbsoluteTracking
        a = np.array([[m[i][j] for j in range(4)] for i in range(3)])
        return a[:, :3], a[:, 3]

    def _resolve(self):
        '''Find the configured device by controller role, tracker class or serial number.'''
        ovr, vr = self.openvr, self.vr
        roles = {"right": ovr.TrackedControllerRole_RightHand, "left": ovr.TrackedControllerRole_LeftHand}
        if self.device in roles:
            i = vr.getTrackedDeviceIndexForControllerRole(roles[self.device])
            return None if i == ovr.k_unTrackedDeviceIndexInvalid else i
        for i in range(ovr.k_unMaxTrackedDeviceCount):
            if not vr.isTrackedDeviceConnected(i):
                continue
            if self.device == "tracker":
                if vr.getTrackedDeviceClass(i) == ovr.TrackedDeviceClass_GenericTracker:
                    return i
            elif vr.getStringTrackedDeviceProperty(i, ovr.Prop_SerialNumber_String) == self.device:
                return i
        return None

    def _wrist_in_steamvr(self, device):
        '''Apply the calibrated grip rotation and the configured wrist offset in the SteamVR frame.'''
        rot, pos = device
        return rot @ quat_to_mat(self.calibration.grip), pos + rot @ self.offset_pos

    def wrist_pose(self):
        '''Latest wrist pose in the world frame as (position, quaternion wxyz), None while not tracked.'''
        with self.lock:
            device = self.device_pose
        if device is None:
            return None
        rot, pos = self.calibration.to_world(*self._wrist_in_steamvr(device))
        return pos, mat_to_quat(rot)

    def trigger(self) -> float:
        '''Latest trigger value in [0, 1].'''
        with self.lock:
            return self.trigger_value

    def calibrate(self) -> bool:
        '''Map the current hold onto the start pose: forward along +x, wrist on the start position and orientation.'''
        with self.lock:
            device, hmd = self.device_pose, self.hmd_pose
        if device is None or (self.yaw_from == "hmd" and hmd is None):
            print("calibrate: device or headset not tracked, nothing changed")
            return False
        rot, pos = device
        forward = STEAMVR_TO_MUJOCO @ (-hmd[0][:, 2] if self.yaw_from == "hmd" else rot @ DEVICE_FORWARD)
        yaw = -math.atan2(forward[1], forward[0])
        world = yaw_matrix(yaw) @ STEAMVR_TO_MUJOCO
        grip = mat_to_quat((world @ rot).T @ self.start_rot)
        self.calibration.update(yaw, self.start_position - world @ (pos + rot @ self.offset_pos), grip)
        print(f"calibrate: yaw {math.degrees(yaw):.1f} deg, saved to {self.calibration.path}")
        return True


def make_tracking(cfg: dict, on_button=None) -> TrackingSystem | None:
    '''Start SteamVR tracking only when a pose or finger source needs it.'''
    if cfg["input"]["pose_source"] != "vive" and cfg["input"]["finger_source"] != "trigger":
        return None
    tracking = TrackingSystem(cfg, on_button)
    tracking.start()
    if not tracking.calibration.valid:
        print("tracking not calibrated: hold the hand where it should rest and run calibrate")
    return tracking
