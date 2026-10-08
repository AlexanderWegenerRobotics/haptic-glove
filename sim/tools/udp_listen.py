import argparse
import json
import selectors
import socket
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from haptic_sim.outputs import SCENE_MAGIC, decode_state, unix_now


def open_socket(port: int) -> socket.socket:
    '''Bind a non-blocking UDP socket on all interfaces.'''
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", port))
    s.setblocking(False)
    return s


def main() -> None:
    '''Stand-in for Unreal: receive the stream and print rate, loss, latency and the latest state once per second.'''
    p = argparse.ArgumentParser()
    p.add_argument("--state-port", type=int, default=9870)
    p.add_argument("--scene-port", type=int, default=9871)
    p.add_argument("--duration", type=float, default=0.0)
    p.add_argument("--verbose", action="store_true")
    args = p.parse_args()

    sel = selectors.DefaultSelector()
    state_sock, scene_sock = open_socket(args.state_port), open_socket(args.scene_port)
    sel.register(state_sock, selectors.EVENT_READ, "state")
    sel.register(scene_sock, selectors.EVENT_READ, "scene")
    print(f"listening on state :{args.state_port}, scene :{args.scene_port}")

    scene_id, last_seq, lost, latencies, count, last = None, None, 0, [], 0, None
    start = window = time.time()
    try:
        while not args.duration or time.time() - start < args.duration:
            for key, _ in sel.select(timeout=0.1):
                packet = key.fileobj.recv(65536)
                if key.data == "scene":
                    desc = json.loads(packet[len(SCENE_MAGIC):])
                    if desc["scene_id"] != scene_id:
                        scene_id = desc["scene_id"]
                        print(f"scene {scene_id}: hand {desc['hand']}, objects {[o['id'] for o in desc['objects']]}, "
                              f"{len(desc['hand_joints'])} joints, {len(desc['hand_bodies'])} bodies, "
                              f"{len(desc['hand_geoms'])} geoms")
                    continue
                last = decode_state(packet)
                if last_seq is not None and last["seq"] > last_seq + 1:
                    lost += last["seq"] - last_seq - 1
                last_seq = last["seq"]
                latencies.append(unix_now() - last["send_time"])
                count += 1
            now = time.time()
            if now - window >= 1.0:
                if count > 0:
                    lat = np.array(latencies) * 1e3
                    print(f"{count / (now - window):6.1f} Hz  lost {lost}  latency mean {lat.mean():.2f} ms  "
                          f"max {lat.max():.2f} ms  sim t {last['sim_time']:.2f}  "
                          f"closure {np.round(last['closure'], 2)}  feedback {np.round(last['feedback'], 2)}  "
                          f"{'stopped' if last['stopped'] else 'engaged' if last['engaged'] else 'waiting'}"
                          f"{'  ghost' if last['ghost_visible'] else ''}  trial {last['trial']}  "
                          f"cmd {last['command_seq']}")
                    if args.verbose:
                        print(last)
                count, lost, latencies, window = 0, 0, [], now
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
