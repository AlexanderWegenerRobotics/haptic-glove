import argparse
import select
import socket
import sys
import threading
import time
from collections import deque
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from haptic_sim.glove_protocol import STATE_KILL, STATE_TIMEOUT, STATE_TORQUE_ON, Feedback, FrameParser, encode_state

FEEDBACK_TIMEOUT = 0.05
DAMPING_LIMIT = 0.05
VELOCITY_FILTER_HZ = 30.0


class OperatorFingers:
    '''Mass-spring-damper fingers pulled toward a slow open/close cycle and pushed back by the glove torque.'''

    def __init__(self, args):
        '''Derive inertia and damping from stiffness, natural frequency and damping ratio.'''
        self.period, self.max_closure, self.k = args.period, args.max_closure, args.stiffness
        omega = 2.0 * np.pi * args.natural_hz
        self.inertia = self.k / omega**2
        self.b = 2.0 * args.damping_ratio * np.sqrt(self.k * self.inertia)
        self.position = np.zeros(3)
        self.velocity = np.zeros(3)

    def step(self, t: float, dt: float, torque: np.ndarray) -> np.ndarray:
        '''Integrate one step and return the closures.'''
        desired = 0.5 * self.max_closure * (1.0 - np.cos(2.0 * np.pi * t / self.period - 0.3 * np.arange(3)))
        accel = (self.k * (desired - self.position) - self.b * self.velocity - torque) / self.inertia
        self.velocity += accel * dt
        self.position += self.velocity * dt
        hit = (self.position < 0.0) | (self.position > 1.0)
        self.position = np.clip(self.position, 0.0, 1.0)
        self.velocity[hit] = 0.0
        return self.position.copy()


class TorqueShaper:
    '''Same as the firmware: contact damping on closing velocity, slew limit, clamp, zero when disabled.'''

    def __init__(self, torque_limit: float):
        '''Start at rest.'''
        self.limit = torque_limit
        self.previous = None
        self.velocity = np.zeros(3)
        self.output = np.zeros(3)

    def update(self, closure, target, damping, slew, enable, dt) -> np.ndarray:
        '''Return the servo torque for this step.'''
        alpha = np.exp(-2.0 * np.pi * VELOCITY_FILTER_HZ * dt)
        if self.previous is not None:
            self.velocity = alpha * self.velocity + (1.0 - alpha) * (closure - self.previous) / dt
        self.previous = closure.copy()
        if not enable:
            self.output = np.zeros(3)
            return self.output
        base = np.clip(np.nan_to_num(target), 0.0, self.limit)
        b = np.clip(damping, 0.0, DAMPING_LIMIT)
        desired = np.where(base > 0, np.clip(base + b * np.maximum(self.velocity, 0.0), 0.0, self.limit), 0.0)
        step = slew * dt if slew > 0 else self.limit
        self.output = self.output + np.clip(desired - self.output, -step, step)
        return self.output


def start_keys(state: dict) -> None:
    '''k + Enter toggles the kill switch, q + Enter quits.'''

    def run():
        '''Read typed lines.'''
        for line in sys.stdin:
            key = line.strip().lower()
            if key == "k":
                state["kill"] = not state["kill"]
                print("kill switch " + ("pressed" if state["kill"] else "released"))
            elif key == "q":
                state["quit"] = True
                return

    threading.Thread(target=run, daemon=True).start()


def main() -> None:
    '''Speak the glove protocol to the sim over UDP like the ESP32 firmware does over serial.'''
    p = argparse.ArgumentParser()
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=9880)
    p.add_argument("--rate", type=float, default=500.0)
    p.add_argument("--period", type=float, default=3.0, help="s per open/close cycle")
    p.add_argument("--max-closure", type=float, default=1.0)
    p.add_argument("--stiffness", type=float, default=0.5, help="operator Nm per unit closure")
    p.add_argument("--natural-hz", type=float, default=5.0, help="operator finger natural frequency")
    p.add_argument("--damping-ratio", type=float, default=0.7)
    p.add_argument("--torque-limit", type=float, default=0.2)
    p.add_argument("--delay-ms", type=float, default=0.0, help="extra delay before feedback is applied")
    p.add_argument("--duration", type=float, default=0.0)
    args = p.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setblocking(False)
    target = (args.host, args.port)
    parser = FrameParser()
    fingers = OperatorFingers(args)
    shaper = TorqueShaper(args.torque_limit)
    keys = {"kill": False, "quit": False}
    start_keys(keys)
    print(f"fake glove -> {args.host}:{args.port} at {args.rate:.0f} Hz, k + Enter kill switch, q + Enter quit")

    period = 1.0 / args.rate
    start = time.perf_counter()
    next_tick = start
    next_print = start + 1.0
    feedback, feedback_at = None, -1.0
    pending = deque()
    delay = args.delay_ms / 1000.0
    rtt_us, rtts, sent = 0, [], 0
    torque = np.zeros(3)
    closure = np.zeros(3)
    seq = 0
    while not keys["quit"]:
        now = time.perf_counter()
        t_us = int((now - start) * 1e6)
        while True:
            try:
                data = sock.recv(256)
            except (BlockingIOError, ConnectionResetError):
                break
            pending.extend((now, msg) for msg in parser.feed(data) if isinstance(msg, Feedback))
        while pending and now - pending[0][0] >= delay:
            feedback = pending.popleft()[1]
            feedback_at = now
            rtt_us = (t_us - feedback.echo_t_us) % (1 << 32)
            rtts.append(rtt_us)

        if now >= next_tick:
            next_tick = max(next_tick + period, now)
            timeout = feedback is None or now - feedback_at > FEEDBACK_TIMEOUT
            enable = not keys["kill"] and not timeout and feedback.enable
            closure = fingers.step(now - start, period, torque)
            if feedback is None:
                torque = shaper.update(closure, np.zeros(3), 0.0, 0.0, False, period)
            else:
                torque = shaper.update(closure, feedback.torque, feedback.damping, feedback.slew, enable, period)
            flags = STATE_KILL * keys["kill"] | STATE_TORQUE_ON * enable | STATE_TIMEOUT * timeout
            seq += 1
            try:
                sock.sendto(encode_state(seq, t_us, closure, torque, rtt_us, flags), target)
            except OSError:
                pass
            sent += 1

        if now >= next_print:
            next_print += 1.0
            r = np.array(rtts) / 1000.0
            rtt = f"rtt {r.mean():.2f}/{r.max():.2f} ms" if len(r) else "no feedback"
            print(f"sent {sent} Hz, {rtt}, closure {np.round(closure, 2)}, torque {np.round(torque, 3)} Nm"
                  + (" KILL" if keys["kill"] else ""))
            rtts, sent = [], 0
            if args.duration and now - start > args.duration:
                break
        wake = min(next_tick, next_print, pending[0][0] + delay if pending else next_tick)
        select.select([sock], [], [], max(0.0, wake - time.perf_counter()))


if __name__ == "__main__":
    main()
