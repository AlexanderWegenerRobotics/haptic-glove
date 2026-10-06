import mujoco
import numpy as np

from .scene import HAND_PREFIX

CHANNELS = ("thumb", "index", "middle")
FINGER_COUPLING = {"mcp": 1.0, "pip": 1.1, "dip": 0.77}


class HandAdapter:
    '''Maps the three glove channels onto a hand model and reads per-channel feedback back out.'''

    joint_names: tuple = ()
    channel_coupling: tuple = ()

    def __init__(self, model: mujoco.MjModel):
        '''Cache joint, dof and body lookups shared by all hand types.'''
        self.model = model
        self.joint_ids = [model.joint(HAND_PREFIX + n).id for n in self.joint_names]
        self.joint_qpos = [model.jnt_qposadr[j] for j in self.joint_ids]
        self.channel_dofs = [np.array([model.jnt_dofadr[model.joint(HAND_PREFIX + j).id] for j in coupling], dtype=int)
                             for coupling in self.channel_coupling]
        self.channel_ratios = [np.array(list(coupling.values())) for coupling in self.channel_coupling]
        self.channel_bodies = [{model.jnt_bodyid[model.joint(HAND_PREFIX + j).id] for j in coupling}
                               for coupling in self.channel_coupling]
        self.body_channel = {b: k for k, bodies in enumerate(self.channel_bodies) for b in bodies}
        self.hand_bodies = self._bodies_below("hand_root")
        self.jacp = np.zeros((3, model.nv))
        self.f6 = np.zeros(6)

    def set_command(self, data: mujoco.MjData, closure: np.ndarray) -> None:
        '''Write normalized channel closures (0 open, 1 closed) into the actuator controls.'''
        raise NotImplementedError

    def command_from_ctrl(self, data: mujoco.MjData) -> np.ndarray:
        '''Recover the normalized channel command from the current actuator controls.'''
        raise NotImplementedError

    def closure(self, data: mujoco.MjData) -> np.ndarray:
        '''Return the measured normalized closure per channel.'''
        raise NotImplementedError

    def contact_feedback(self, data: mujoco.MjData):
        '''Project external contact forces on each channel into its driven coordinate; returns (feedback, normal force).'''
        m = self.model
        torque = np.zeros(len(CHANNELS))
        normal = np.zeros(len(CHANNELS))
        for i in range(data.ncon):
            c = data.contact[i]
            b1, b2 = m.geom_bodyid[c.geom1], m.geom_bodyid[c.geom2]
            on_hand1, on_hand2 = b1 in self.hand_bodies, b2 in self.hand_bodies
            if on_hand1 == on_hand2:
                continue
            body, sign = (b1, -1.0) if on_hand1 else (b2, 1.0)
            k = self.body_channel.get(body)
            if k is None:
                continue
            mujoco.mj_contactForce(m, data, i, self.f6)
            force = sign * (c.frame.reshape(3, 3).T @ self.f6[:3])
            mujoco.mj_jac(m, data, self.jacp, None, c.pos, body)
            torque[k] += self.channel_ratios[k] @ (self.jacp[:, self.channel_dofs[k]].T @ force)
            normal[k] += self.f6[0]
        return np.maximum(-torque, 0.0), normal

    def joint_state(self, data: mujoco.MjData) -> dict:
        '''Return all hand joint positions by name, used by the renderer.'''
        return {n: float(data.qpos[a]) for n, a in zip(self.joint_names, self.joint_qpos)}

    def _bodies_below(self, root: str) -> set:
        '''Collect a body and all of its descendants.'''
        root_id = self.model.body(HAND_PREFIX + root).id
        ids = {root_id}
        for b in range(self.model.nbody):
            p = b
            while p != 0:
                if p == root_id:
                    ids.add(b)
                    break
                p = self.model.body_parentid[p]
        return ids


class DexterousHand(HandAdapter):
    '''Five-finger capsule hand with three actuated channels; ring and pinky follow the middle finger.'''

    joint_names = ("thumb_mcp", "thumb_ip", "index_mcp", "index_pip", "index_dip",
                   "middle_mcp", "middle_pip", "middle_dip", "ring_mcp", "ring_pip", "ring_dip",
                   "pinky_mcp", "pinky_pip", "pinky_dip")
    channel_coupling = (
        {"thumb_mcp": 1.0, "thumb_ip": 1.0},
        {f"index_{j}": r for j, r in FINGER_COUPLING.items()},
        {f"{f}_{j}": r for f in ("middle", "ring", "pinky") for j, r in FINGER_COUPLING.items()},
    )

    def __init__(self, model: mujoco.MjModel):
        '''Look up the per-finger actuators and their control ranges.'''
        super().__init__(model)
        self.act = np.array([model.actuator(HAND_PREFIX + c).id for c in CHANNELS])
        self.ctrl_max = model.actuator_ctrlrange[self.act, 1].copy()
        self.drive_qpos = np.array([model.jnt_qposadr[model.actuator_trnid[a, 0]] for a in self.act])

    def set_command(self, data, closure):
        '''Scale closures to each finger's actuator range.'''
        data.ctrl[self.act] = np.clip(closure, 0.0, 1.0) * self.ctrl_max

    def command_from_ctrl(self, data):
        '''Normalize the finger actuator controls.'''
        return data.ctrl[self.act] / self.ctrl_max

    def closure(self, data):
        '''Normalize the driven joint angle of each finger.'''
        return data.qpos[self.drive_qpos] / self.ctrl_max


class ParallelJawGripper(HandAdapter):
    '''Two-jaw gripper driven by thumb and index; the thumb feels the right jaw, the index the left jaw.'''

    joint_names = ("jaw_left", "jaw_right")
    channel_coupling = ({"jaw_right": 1.0}, {"jaw_left": 1.0}, {})
    INPUT_WEIGHTS = {
        "thumb_index_mean": np.array([0.5, 0.5, 0.0]),
        "index": np.array([0.0, 1.0, 0.0]),
        "thumb": np.array([1.0, 0.0, 0.0]),
    }

    def __init__(self, model: mujoco.MjModel, jaw_input: str = "thumb_index_mean"):
        '''Look up the jaw actuator and select how glove channels map onto it.'''
        super().__init__(model)
        self.act = model.actuator(HAND_PREFIX + "jaw").id
        self.ctrl_max = model.actuator_ctrlrange[self.act, 1]
        self.drive_qpos = model.jnt_qposadr[model.actuator_trnid[self.act, 0]]
        self.weights = self.INPUT_WEIGHTS[jaw_input]
        self.output_mask = np.array([1.0, 1.0, 0.0])

    def set_command(self, data, closure):
        '''Blend the selected channels into one jaw closure.'''
        data.ctrl[self.act] = np.clip(self.weights @ closure, 0.0, 1.0) * self.ctrl_max

    def command_from_ctrl(self, data):
        '''Report the jaw command on the thumb and index channels.'''
        return self.output_mask * data.ctrl[self.act] / self.ctrl_max

    def closure(self, data):
        '''Report the jaw closure on the thumb and index channels.'''
        return self.output_mask * data.qpos[self.drive_qpos] / self.ctrl_max


def make_hand(model: mujoco.MjModel, cfg: dict) -> HandAdapter:
    '''Create the adapter matching the configured hand type.'''
    hand_type = cfg["hand"]["type"]
    if hand_type == "dexterous":
        return DexterousHand(model)
    if hand_type == "parallel_jaw":
        return ParallelJawGripper(model, cfg["hand"].get("jaw_input", "thumb_index_mean"))
    raise ValueError(f"unknown hand type '{hand_type}'")
