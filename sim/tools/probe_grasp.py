import argparse
import sys
from pathlib import Path

import matplotlib
import mujoco
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from haptic_sim.config import CONFIG_DIR, load_object_library, load_scene, load_sim_config
from haptic_sim.hand import CHANNELS, make_hand
from haptic_sim.scene import HAND_PREFIX, TARGET_BODY, build_model

GRASP_OFFSET = {"dexterous": np.array([-0.09, 0.0, 0.055]), "parallel_jaw": np.array([-0.105, 0.0, 0.0])}


def place_hand(model, data, position) -> int:
    '''Move mocap target and hand body to the same pose and return the mocap index.'''
    mocap = model.body_mocapid[model.body(TARGET_BODY).id]
    data.mocap_pos[mocap] = position
    data.mocap_quat[mocap] = [1, 0, 0, 0]
    adr = model.jnt_qposadr[model.joint(HAND_PREFIX + "hand_free").id]
    data.qpos[adr:adr + 3] = position
    data.qpos[adr + 3:adr + 7] = [1, 0, 0, 0]
    mujoco.mj_forward(model, data)
    return mocap


def probe_object(model, hand, hand_type, obj, close_time=3.0, lift=0.1):
    '''Close the hand around one object, then lift, and return the closure/feedback trace and lift height.'''
    data = mujoco.MjData(model)
    mujoco.mj_forward(model, data)
    grasp_pos = np.array(obj.position) + GRASP_OFFSET[hand_type]
    mocap = place_hand(model, data, grasp_pos)
    dt = model.opt.timestep
    rows = []
    for k in range(int(close_time / dt)):
        hand.set_command(data, np.full(3, k * dt / close_time))
        mujoco.mj_step(model, data)
        rows.append(np.concatenate([[k * dt / close_time], hand.closure(data), *hand.contact_feedback(data)]))
    z0 = data.body(obj.id).xpos[2]
    for k in range(int(1.0 / dt)):
        data.mocap_pos[mocap] = grasp_pos + [0, 0, lift * min(1.0, k * dt / 0.7)]
        mujoco.mj_step(model, data)
    return np.array(rows), data.body(obj.id).xpos[2] - z0


def main() -> None:
    '''Probe every object of a scene and plot feedback over commanded closure.'''
    p = argparse.ArgumentParser()
    p.add_argument("--scene", default="scenes/default.yaml")
    p.add_argument("--hand", default="dexterous", choices=list(GRASP_OFFSET))
    p.add_argument("--out", default="probe_grasp.png")
    args = p.parse_args()

    cfg = load_sim_config()
    cfg["hand"]["type"] = args.hand
    scene = load_scene(CONFIG_DIR / args.scene, load_object_library(cfg["objects"]))
    model = build_model(cfg, scene)
    hand = make_hand(model, cfg)

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(1, 3, figsize=(13, 3.6), sharey=True)
    for obj in scene.objects:
        rows, lifted = probe_object(model, hand, args.hand, obj)
        print(f"{obj.id:16s} peak feedback {rows[:, 4:7].max(0).round(3)}  lifted {lifted * 100:.1f} cm")
        for i, c in enumerate(CHANNELS):
            axes[i].plot(rows[:, 0], rows[:, 4 + i], label=obj.id)
    for i, c in enumerate(CHANNELS):
        axes[i].set_title(c)
        axes[i].set_xlabel("commanded closure")
        axes[i].grid(alpha=0.3)
    axes[0].set_ylabel("feedback [Nm, N for jaw]")
    axes[-1].legend()
    fig.tight_layout()
    fig.savefig(args.out, dpi=120)
    print(f"plot: {args.out}")


if __name__ == "__main__":
    main()
