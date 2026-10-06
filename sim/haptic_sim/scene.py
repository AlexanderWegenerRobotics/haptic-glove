import json
import time
import uuid
from pathlib import Path

import mujoco

from .config import HAND_MODELS, MODEL_DIR, SceneSpec

HAND_PREFIX = "hand/"
TARGET_BODY = "hand_target"
GHOST_BODY = "hand_ghost"
GHOST_RGBA = [0.3, 0.55, 1.0, 0.35]
GHOST_GEOMS = {"palm": ([0.045, 0.04, 0.012], [0.045, 0, 0]), "fingers": ([0.035, 0.035, 0.008], [0.125, 0, 0])}
SHAPES = {
    "sphere": mujoco.mjtGeom.mjGEOM_SPHERE,
    "box": mujoco.mjtGeom.mjGEOM_BOX,
    "cylinder": mujoco.mjtGeom.mjGEOM_CYLINDER,
    "capsule": mujoco.mjtGeom.mjGEOM_CAPSULE,
}


def _pad3(size: list) -> list:
    '''Pad a size list to the three entries MuJoCo expects.'''
    return list(size) + [0.0] * (3 - len(size))


def _add_table(spec: mujoco.MjSpec, scene: SceneSpec) -> None:
    '''Add the static table top and legs from the scene description.'''
    t = scene.table
    lx, ly, th = t.size
    body = spec.worldbody.add_body(name="table", pos=[t.position[0], t.position[1], 0.0])
    body.add_geom(name="table_top", type=mujoco.mjtGeom.mjGEOM_BOX, size=[lx / 2, ly / 2, th / 2],
                  pos=[0, 0, t.height - th / 2], rgba=t.rgba)
    leg_h = (t.height - th) / 2
    for i, (sx, sy) in enumerate([(1, 1), (1, -1), (-1, 1), (-1, -1)]):
        body.add_geom(name=f"table_leg_{i}", type=mujoco.mjtGeom.mjGEOM_BOX, size=[0.02, 0.02, leg_h],
                      pos=[sx * (lx / 2 - 0.04), sy * (ly / 2 - 0.04), leg_h], rgba=t.rgba)


def _add_objects(spec: mujoco.MjSpec, scene: SceneSpec) -> None:
    '''Add one free body per object instance with its contact softness set on the geom.'''
    for obj in scene.objects:
        o = obj.type
        body = spec.worldbody.add_body(name=obj.id, pos=obj.position, quat=obj.orientation)
        body.add_freejoint(name=f"{obj.id}_free")
        body.add_geom(name=obj.id, type=SHAPES[o.shape], size=_pad3(o.size), mass=o.mass, rgba=o.rgba,
                      friction=[o.friction, 0.01, 0.001], condim=4, priority=1,
                      solref=[o.contact_time, o.damping_ratio])


def _add_hand(spec: mujoco.MjSpec, cfg: dict) -> None:
    '''Attach the selected hand model, weld it softly to a mocap target body and add the hidden ghost hand.'''
    hand_cfg = cfg["hand"]
    pos = hand_cfg["start_pose"]["position"]
    quat = hand_cfg["start_pose"]["orientation"]

    target = spec.worldbody.add_body(name=TARGET_BODY, mocap=True, pos=pos, quat=quat)
    target.add_geom(type=mujoco.mjtGeom.mjGEOM_SPHERE, size=[0.018, 0, 0], pos=[-0.035, 0, 0],
                    rgba=[0.2, 0.8, 0.2, 0.5], contype=0, conaffinity=0)

    ghost = spec.worldbody.add_body(name=GHOST_BODY, mocap=True, pos=pos, quat=quat)
    for name, (size, gpos) in GHOST_GEOMS.items():
        ghost.add_geom(name=f"ghost_{name}", type=mujoco.mjtGeom.mjGEOM_BOX, size=size, pos=gpos,
                       rgba=GHOST_RGBA[:3] + [0.0], contype=0, conaffinity=0)

    hand_spec = mujoco.MjSpec.from_file(str(MODEL_DIR / HAND_MODELS[hand_cfg["type"]]))
    frame = spec.worldbody.add_frame(pos=pos, quat=quat)
    spec.attach(hand_spec, prefix=HAND_PREFIX, frame=frame)

    spec.add_equality(type=mujoco.mjtEq.mjEQ_WELD, objtype=mujoco.mjtObj.mjOBJ_BODY,
                      name1=TARGET_BODY, name2=f"{HAND_PREFIX}hand_root", solref=hand_cfg["weld_solref"],
                      solimp=hand_cfg["weld_solimp"])


def build_model(cfg: dict, scene: SceneSpec) -> mujoco.MjModel:
    '''Compose base scene, table, objects and hand into one compiled model.'''
    spec = mujoco.MjSpec.from_file(str(MODEL_DIR / "base_scene.xml"))
    spec.option.timestep = cfg["simulation"]["timestep"]
    _add_table(spec, scene)
    _add_objects(spec, scene)
    _add_hand(spec, cfg)
    return spec.compile()


def describe_scene(scene: SceneSpec, cfg: dict) -> dict:
    '''Build the scene description that the renderer uses to spawn identical primitives.'''
    t = scene.table
    return {
        "scene_id": uuid.uuid4().hex[:8],
        "scene_name": scene.name,
        "seed": scene.seed,
        "created": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "frame": "mujoco_z_up_meters",
        "hand": cfg["hand"]["type"],
        "table": {"position": [t.position[0], t.position[1], t.height - t.size[2] / 2],
                  "size": t.size, "height": t.height, "rgba": t.rgba},
        "objects": [
            {"id": o.id, "type": o.type.name, "shape": o.type.shape, "size": o.type.size,
             "rgba": o.type.rgba, "deformable": o.type.deformable,
             "position": o.position, "orientation": o.orientation}
            for o in scene.objects
        ],
    }


def write_scene_description(description: dict, directory: str | Path) -> Path:
    '''Write the scene description as json next to the logs and return its path.'''
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f"scene_{description['scene_id']}.json"
    path.write_text(json.dumps(description, indent=2))
    return path
