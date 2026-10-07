import json
import socket
import struct
import threading
import time

import mujoco
import numpy as np

from .hand import HandAdapter
from .scene import GHOST_BODY, HAND_PREFIX

PROTOCOL_VERSION = 3
STATE_MAGIC = b"HGST"
SCENE_MAGIC = b"HGSC"
HAND_TYPES = {"dexterous": 0, "parallel_jaw": 1}
FLAG_ENGAGED = 1
FLAG_GHOST = 2
FLAG_STOPPED = 4
FLAG_TRACKED = 8
FLAG_CALIBRATED = 16
GEOM_TYPES = {mujoco.mjtGeom.mjGEOM_SPHERE: "sphere", mujoco.mjtGeom.mjGEOM_CAPSULE: "capsule",
              mujoco.mjtGeom.mjGEOM_ELLIPSOID: "ellipsoid", mujoco.mjtGeom.mjGEOM_CYLINDER: "cylinder",
              mujoco.mjtGeom.mjGEOM_BOX: "box"}
HEADER = struct.Struct("<4sHBBIdd8s")
STATUS = struct.Struct("<IIf")
POSE = struct.Struct("<7f")
COUNT = struct.Struct("<H")
CHANNELS3 = struct.Struct("<6f")
OBJECT = struct.Struct("<11f")


class UnrealPublisher:
    '''Streams hand and object state to Unreal over UDP and repeats the scene description on a second port.'''

    def __init__(self, model: mujoco.MjModel, hand: HandAdapter, description: dict, cfg: dict):
        '''Open the socket, resolve body ids and extend the scene description with the stream layout.'''
        ucfg = cfg["unreal"]
        self.model, self.hand = model, hand
        self.state_addr = (ucfg["host"], ucfg["state_port"])
        self.scene_addr = (ucfg["host"], ucfg["scene_port"])
        self.period = 1.0 / ucfg["rate"]
        self.scene_interval = ucfg["scene_interval"]
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.seq = 0
        self.next_send = 0.0

        self.root_body = model.body(HAND_PREFIX + "hand_root").id
        self.ghost_body = model.body(GHOST_BODY).id
        self.object_ids = [o["id"] for o in description["objects"]]
        self.object_bodies = np.array([model.body(i).id for i in self.object_ids], dtype=int)
        self.object_geoms = {model.geom(i).id: k for k, i in enumerate(self.object_ids)}
        self.hand_bodies = np.array(sorted(hand.hand_bodies), dtype=int)
        self.scene_id = description["scene_id"].encode()[:8].ljust(8, b"\0")
        self.hand_type = HAND_TYPES[description["hand"]]

        description["protocol_version"] = PROTOCOL_VERSION
        description["hand_joints"] = list(hand.joint_names)
        description["hand_bodies"] = [model.body(b).name.removeprefix(HAND_PREFIX) for b in self.hand_bodies]
        description["hand_geoms"] = self._hand_geoms()
        description["stream"] = {"host": ucfg["host"], "state_port": ucfg["state_port"],
                                 "scene_port": ucfg["scene_port"], "rate": ucfg["rate"]}
        self.description = description
        self.scene_packet = SCENE_MAGIC + json.dumps(description).encode()
        self.stop_event = threading.Event()
        self.scene_thread = threading.Thread(target=self._scene_loop, daemon=True, name="unreal_scene")

    def start(self) -> None:
        '''Start repeating the scene description.'''
        self.scene_thread.start()

    def stop(self) -> None:
        '''Stop the scene thread and close the socket.'''
        self.stop_event.set()
        self.scene_thread.join(timeout=1.0)
        self.sock.close()

    def _hand_geoms(self) -> list:
        '''Visual description of every hand geom, attached to its body by index into hand_bodies.'''
        m = self.model
        index = {b: k for k, b in enumerate(self.hand_bodies)}
        return [{"body": index[int(m.geom_bodyid[g])], "type": GEOM_TYPES[int(m.geom_type[g])],
                 "size": [float(v) for v in m.geom_size[g]], "position": [float(v) for v in m.geom_pos[g]],
                 "orientation": [float(v) for v in m.geom_quat[g]], "rgba": [float(v) for v in m.geom_rgba[g]]}
                for g in range(m.ngeom) if int(m.geom_bodyid[g]) in index and int(m.geom_type[g]) in GEOM_TYPES]

    def set_tracking(self, tracking: dict) -> None:
        '''Update the tracking calibration in the repeated scene message after a recalibration.'''
        self.description["tracking"] = tracking
        self.scene_packet = SCENE_MAGIC + json.dumps(self.description).encode()

    def _scene_loop(self) -> None:
        '''Send the scene description every scene_interval seconds.'''
        while not self.stop_event.is_set():
            self._send(self.scene_packet, self.scene_addr)
            self.stop_event.wait(self.scene_interval)

    def due(self, now: float) -> bool:
        '''Return True when the next state packet should go out.'''
        if now < self.next_send:
            return False
        self.next_send = max(self.next_send + self.period, now)
        return True

    def capture(self, data: mujoco.MjData, closure: np.ndarray, feedback: np.ndarray, flags: int, trial: int,
                command_seq: int, countdown: float) -> bytes:
        '''Pack the current state into one packet; call while holding the sim lock.'''
        self.seq += 1
        parts = [HEADER.pack(STATE_MAGIC, PROTOCOL_VERSION, self.hand_type, flags, self.seq, data.time,
                             time.time(), self.scene_id),
                 STATUS.pack(trial, command_seq, countdown),
                 POSE.pack(*data.xpos[self.root_body], *data.xquat[self.root_body]),
                 POSE.pack(*data.xpos[self.ghost_body], *data.xquat[self.ghost_body])]
        joints = data.qpos[self.hand.joint_qpos]
        parts.append(COUNT.pack(len(joints)))
        parts.append(struct.pack(f"<{len(joints)}f", *joints))
        parts.append(COUNT.pack(len(self.hand_bodies)))
        for b in self.hand_bodies:
            parts.append(POSE.pack(*data.xpos[b], *data.xquat[b]))
        parts.append(CHANNELS3.pack(*closure, *feedback))
        squash = self._squash(data)
        parts.append(COUNT.pack(len(self.object_bodies)))
        for k, b in enumerate(self.object_bodies):
            parts.append(OBJECT.pack(*data.xpos[b], *data.xquat[b], *squash[k]))
        return b"".join(parts)

    def _squash(self, data: mujoco.MjData) -> np.ndarray:
        '''Deepest contact per object as (depth, direction into the object), for the visual squash.'''
        out = np.zeros((len(self.object_bodies), 4))
        for i in range(data.ncon):
            c = data.contact[i]
            if c.dist >= 0:
                continue
            for geom, sign in ((c.geom2, 1.0), (c.geom1, -1.0)):
                k = self.object_geoms.get(geom)
                if k is not None and -c.dist > out[k, 0]:
                    out[k, 0] = -c.dist
                    out[k, 1:] = sign * c.frame[:3]
        return out

    def send(self, packet: bytes) -> None:
        '''Send one state packet.'''
        self._send(packet, self.state_addr)

    def _send(self, packet: bytes, addr) -> None:
        '''Send without ever blocking or crashing the sim when nobody listens.'''
        try:
            self.sock.sendto(packet, addr)
        except OSError:
            pass


def decode_state(packet: bytes) -> dict:
    '''Decode a state packet; reference implementation for the Unreal side.'''
    magic, version, hand_type, flags, seq, sim_time, send_time, scene_id = HEADER.unpack_from(packet, 0)
    if magic != STATE_MAGIC:
        raise ValueError("not a state packet")
    off = HEADER.size
    trial, command_seq, countdown = STATUS.unpack_from(packet, off)
    off += STATUS.size
    pose = POSE.unpack_from(packet, off)
    off += POSE.size
    ghost = POSE.unpack_from(packet, off)
    off += POSE.size
    (nj,) = COUNT.unpack_from(packet, off)
    off += COUNT.size
    joints = struct.unpack_from(f"<{nj}f", packet, off)
    off += 4 * nj
    (nb,) = COUNT.unpack_from(packet, off)
    off += COUNT.size
    bodies = [POSE.unpack_from(packet, off + k * POSE.size) for k in range(nb)]
    off += nb * POSE.size
    ch = CHANNELS3.unpack_from(packet, off)
    off += CHANNELS3.size
    (no,) = COUNT.unpack_from(packet, off)
    off += COUNT.size
    objects = []
    for _ in range(no):
        v = OBJECT.unpack_from(packet, off)
        off += OBJECT.size
        objects.append({"position": v[0:3], "quaternion": v[3:7], "squash_depth": v[7], "squash_dir": v[8:11]})
    return {"version": version, "hand_type": hand_type, "engaged": bool(flags & FLAG_ENGAGED),
            "ghost_visible": bool(flags & FLAG_GHOST), "stopped": bool(flags & FLAG_STOPPED),
            "tracked": bool(flags & FLAG_TRACKED), "calibrated": bool(flags & FLAG_CALIBRATED),
            "trial": trial, "command_seq": command_seq, "countdown": countdown, "bodies": bodies, "seq": seq, "sim_time": sim_time, "send_time": send_time,
            "scene_id": scene_id.rstrip(b"\0").decode(), "wrist_position": pose[0:3], "wrist_quaternion": pose[3:7],
            "ghost_position": ghost[0:3], "ghost_quaternion": ghost[3:7],
            "joints": joints, "closure": ch[0:3], "feedback": ch[3:6], "objects": objects}
