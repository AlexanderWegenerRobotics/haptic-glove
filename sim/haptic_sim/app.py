import argparse
import signal
import threading
import time

import mujoco
import numpy as np

from .config import CONFIG_DIR, load_object_library, load_scene, load_sim_config
from .hand import make_hand
from .inputs import make_finger_source, make_pose_source
from .loop import PhysicsLoop
from .outputs import UnrealPublisher
from .recorder import Recorder, timing_summary
from .scene import build_model, describe_scene, write_scene_description


def parse_args(argv=None) -> argparse.Namespace:
    '''Command line overrides for the most common config switches.'''
    p = argparse.ArgumentParser(prog="haptic_sim")
    p.add_argument("--config", default=str(CONFIG_DIR / "sim.yaml"))
    p.add_argument("--scene", help="scene file, relative to config/ or absolute")
    p.add_argument("--hand", choices=["dexterous", "parallel_jaw"])
    p.add_argument("--fingers", choices=["viewer", "script"])
    p.add_argument("--pose", choices=["viewer", "fixed"])
    p.add_argument("--headless", action="store_true")
    p.add_argument("--duration", type=float)
    p.add_argument("--no-record", action="store_true")
    p.add_argument("--no-unreal", action="store_true")
    return p.parse_args(argv)


def apply_overrides(cfg: dict, args: argparse.Namespace) -> dict:
    '''Merge command line overrides into the loaded config.'''
    if args.scene:
        cfg["scene"] = str((CONFIG_DIR / args.scene).resolve())
    if args.hand:
        cfg["hand"]["type"] = args.hand
    if args.fingers:
        cfg["input"]["finger_source"] = args.fingers
    if args.pose:
        cfg["input"]["pose_source"] = args.pose
    if args.headless:
        cfg["viewer"]["enabled"] = False
    if args.duration is not None:
        cfg["simulation"]["duration"] = args.duration
    if args.no_record:
        cfg["recording"]["enabled"] = False
    if args.no_unreal:
        cfg["unreal"]["enabled"] = False
    return cfg


def install_stop_handler(stop: threading.Event) -> None:
    '''Turn Ctrl+C into a stop request; under mjpython the script thread falls back to KeyboardInterrupt.'''
    try:
        signal.signal(signal.SIGINT, lambda *_: stop.set())
    except ValueError:
        pass


def run_viewer(model, data, cfg: dict, start_loop, stop: threading.Event) -> PhysicsLoop:
    '''Open the passive viewer first, then start physics under the viewer's lock and sync on the main thread.'''
    import mujoco.viewer

    fps = cfg["viewer"]["fps"]
    with mujoco.viewer.launch_passive(model, data) as viewer:
        loop = start_loop(viewer.lock)
        try:
            while viewer.is_running() and not loop.stop_event.is_set() and not stop.is_set():
                t0 = time.perf_counter()
                viewer.sync()
                time.sleep(max(0.0, 1.0 / fps - (time.perf_counter() - t0)))
        except KeyboardInterrupt:
            pass
        loop.stop()
        loop.join(timeout=2.0)
        viewer.close()
        deadline = time.perf_counter() + 2.0
        while viewer.is_running() and time.perf_counter() < deadline:
            time.sleep(0.01)
    return loop


def wait_headless(loop: PhysicsLoop, stop: threading.Event) -> None:
    '''Block until the loop ends or a stop is requested.'''
    try:
        while not loop.stop_event.wait(0.1) and not stop.is_set():
            pass
    except KeyboardInterrupt:
        pass


def main(argv=None) -> None:
    '''Build the scene, start the physics thread and run the viewer or wait headless.'''
    args = parse_args(argv)
    cfg = apply_overrides(load_sim_config(args.config), args)

    library = load_object_library(cfg["objects"])
    scene = load_scene(cfg["scene"], library)
    model = build_model(cfg, scene)
    data = mujoco.MjData(model)
    mujoco.mj_forward(model, data)

    hand = make_hand(model, cfg)
    description = describe_scene(scene, cfg)
    publisher = UnrealPublisher(model, hand, description, cfg) if cfg["unreal"]["enabled"] else None
    scene_path = write_scene_description(description, cfg["recording"]["directory"])
    print(f"scene '{scene.name}' id {description['scene_id']} seed {scene.seed} hand {cfg['hand']['type']}")
    print(f"objects: {', '.join(o.id for o in scene.objects)}")
    print(f"scene description: {scene_path}")
    if publisher:
        u = cfg["unreal"]
        print(f"unreal stream: {u['host']}:{u['state_port']} at {u['rate']} Hz, scene on :{u['scene_port']}")
        publisher.start()

    recorder = Recorder(cfg["recording"]["directory"], cfg["recording"]["enabled"])

    def start_loop(lock) -> PhysicsLoop:
        '''Create and start the physics thread with the given lock factory.'''
        loop = PhysicsLoop(model, data, lock, hand, make_pose_source(cfg), make_finger_source(cfg), recorder, cfg,
                           publisher)
        loop.start()
        return loop

    stop = threading.Event()
    install_stop_handler(stop)
    if cfg["viewer"]["enabled"]:
        loop = run_viewer(model, data, cfg, start_loop, stop)
    else:
        lock = threading.Lock()
        loop = start_loop(lambda: lock)
        wait_headless(loop, stop)
        loop.stop()
        loop.join(timeout=2.0)
    if publisher:
        publisher.stop()
    print("stopped")

    rows = recorder.data()
    if len(rows):
        print(timing_summary(rows[:, 1], rows[:, 2], model.opt.timestep))
    log_path = recorder.save({"config": cfg, "scene": description})
    if log_path:
        print(f"log: {log_path}")
