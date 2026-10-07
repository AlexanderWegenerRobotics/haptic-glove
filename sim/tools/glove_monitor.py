import argparse
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from haptic_sim.glove import GloveLink


def main() -> None:
    '''Talk to the glove without the sim: answer with a test torque and print rate, loss, round trip and state.'''
    p = argparse.ArgumentParser()
    p.add_argument("--port", default="COM4")
    p.add_argument("--baud", type=int, default=921600)
    p.add_argument("--transport", choices=["serial", "udp"], default="serial")
    p.add_argument("--udp-port", type=int, default=9880)
    p.add_argument("--torque", type=float, default=0.05, help="Nm sent on every channel")
    p.add_argument("--disable", action="store_true", help="send enable = 0")
    p.add_argument("--duration", type=float, default=0.0)
    p.add_argument("--out", help="save all round trips to this .npy file")
    args = p.parse_args()

    link = GloveLink({"transport": args.transport, "port": args.port, "baud": args.baud, "udp_port": args.udp_port,
                      "torque_scale": 1.0, "torque_limit": max(args.torque, 0.0), "damping": 0.0, "slew_rate": 0.0,
                      "timeout": 0.05})
    link.set_feedback(np.full(3, args.torque), not args.disable)
    start = time.perf_counter()
    all_rtts = []
    try:
        while not args.duration or time.perf_counter() - start < args.duration:
            time.sleep(1.0)
            s = link.take_stats()
            r = s["rtt_ms"]
            all_rtts.append(r)
            state = link.latest()
            if state is None:
                print(f"no glove state ({s['frames']} frames, crc errors {s['crc_errors']})")
                continue
            flags = [n for n, v in (("KILL", state.kill), ("torque on", state.torque_on),
                                    ("timeout", state.timeout), ("stub", state.stub)) if v]
            rtt = (f"rtt mean {r.mean():.2f} p99 {np.percentile(r, 99):.2f} max {r.max():.2f} ms" if len(r)
                   else "no rtt")
            print(f"{s['frames']} Hz, lost {s['lost']}, crc {s['crc_errors']}, {rtt}, "
                  f"closure {np.round(state.closure, 2)}, torque {np.round(state.torque, 3)}, {', '.join(flags)}")
    except KeyboardInterrupt:
        pass
    link.close()
    r = np.concatenate(all_rtts) if all_rtts else np.array([])
    if len(r):
        print(f"round trip over {len(r)} frames: mean {r.mean():.2f}, median {np.median(r):.2f}, "
              f"p99 {np.percentile(r, 99):.2f}, max {r.max():.2f} ms")
        if args.out:
            np.save(args.out, r)
            print(f"saved {args.out}")


if __name__ == "__main__":
    main()
