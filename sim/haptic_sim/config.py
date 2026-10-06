import random
from dataclasses import dataclass, field
from pathlib import Path

import yaml

REPO_DIR = Path(__file__).resolve().parents[2]
CONFIG_DIR = REPO_DIR / "config"
MODEL_DIR = REPO_DIR / "sim" / "models"
HAND_MODELS = {"dexterous": "hands/dexterous.xml", "parallel_jaw": "hands/parallel_jaw.xml"}


@dataclass
class ObjectType:
    name: str
    shape: str
    size: list
    mass: float
    rgba: list
    friction: float = 1.0
    contact_time: float = 0.01
    damping_ratio: float = 1.0
    deformable: bool = False

    def half_height(self) -> float:
        '''Distance from the object origin to its resting contact with the table.'''
        if self.shape == "sphere":
            return self.size[0]
        if self.shape == "box":
            return self.size[2]
        if self.shape == "cylinder":
            return self.size[1]
        if self.shape == "capsule":
            return self.size[0] + self.size[1]
        raise ValueError(f"unknown shape '{self.shape}'")


@dataclass
class ObjectInstance:
    id: str
    type: ObjectType
    position: list
    orientation: list = field(default_factory=lambda: [1.0, 0.0, 0.0, 0.0])


@dataclass
class TableSpec:
    position: list
    size: list
    height: float
    rgba: list


@dataclass
class SceneSpec:
    name: str
    table: TableSpec
    objects: list
    seed: int | None


def load_yaml(path: Path) -> dict:
    '''Read a yaml file into a dict.'''
    with open(path) as f:
        return yaml.safe_load(f) or {}


def load_sim_config(path: str | Path | None = None) -> dict:
    '''Load the main sim config and resolve its file paths relative to the config folder.'''
    path = Path(path) if path else CONFIG_DIR / "sim.yaml"
    cfg = load_yaml(path)
    base = path.parent
    cfg["scene"] = str((base / cfg["scene"]).resolve())
    cfg["objects"] = str((base / cfg["objects"]).resolve())
    cfg["recording"]["directory"] = str((base / cfg["recording"]["directory"]).resolve())
    cfg["tracking"]["calibration"] = str((base / cfg["tracking"]["calibration"]).resolve())
    return cfg


def load_object_library(path: str | Path) -> dict:
    '''Load all object types from the object library file.'''
    return {name: ObjectType(name=name, **params) for name, params in load_yaml(Path(path)).items()}


def load_scene(path: str | Path, library: dict, reseed: bool = False) -> SceneSpec:
    '''Resolve a scene file into concrete object instances placed on table slots; reseed forces a new random draw.'''
    path = Path(path)
    raw = load_yaml(path)
    table = TableSpec(**raw["table"])
    slots = raw.get("slots", [])
    seed = None

    if "sample" in raw:
        sample = raw["sample"]
        seed = sample.get("seed")
        if seed is None or reseed:
            seed = random.randrange(2**31)
        rng = random.Random(seed)
        pool, count = sample["from"], sample["count"]
        names = [rng.choice(pool) for _ in range(count)] if sample.get("replace", True) else rng.sample(pool, count)
        entries = [{"type": n, "slot": i} for i, n in enumerate(names)]
    else:
        entries = raw.get("objects", [])

    top = table.height
    objects = []
    for i, entry in enumerate(entries):
        otype = library[entry["type"]]
        if "slot" in entry:
            sx, sy = slots[entry["slot"]]
            xy = [table.position[0] + sx, table.position[1] + sy]
        else:
            xy = entry["position"][:2]
        position = [xy[0], xy[1], top + otype.half_height() + 0.001]
        orientation = entry.get("orientation", [1.0, 0.0, 0.0, 0.0])
        objects.append(ObjectInstance(id=f"{otype.name}_{i}", type=otype, position=position, orientation=orientation))

    return SceneSpec(name=path.stem, table=table, objects=objects, seed=seed)
