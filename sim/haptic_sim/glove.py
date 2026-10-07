import socket
import sys
import threading
import time
from collections import deque

import numpy as np

from .glove_protocol import FEEDBACK_ENABLE, FrameParser, GloveState, encode_feedback

U32 = 1 << 32


class SerialTransport:
    '''USB serial to the ESP32, opened without toggling DTR/RTS so the board is not reset.'''

    def __init__(self, port: str, baud: int):
        '''Open the port.'''
        import serial

        self.ser = serial.Serial()
        self.ser.port, self.ser.baudrate, self.ser.timeout = port, baud, 0.01
        self.ser.dtr = False
        self.ser.rts = False
        self.ser.open()
        self.name = f"serial {port}"

    def read(self) -> bytes:
        '''Return whatever arrived, waiting at most the read timeout.'''
        return self.ser.read(max(1, self.ser.in_waiting))

    def write(self, data: bytes) -> None:
        '''Send bytes.'''
        self.ser.write(data)

    def close(self) -> None:
        '''Close the port.'''
        self.ser.close()


class UdpTransport:
    '''Local UDP socket for the fake glove; replies go to whoever sent the last datagram.'''

    def __init__(self, port: int, host: str = "127.0.0.1"):
        '''Bind the socket.'''
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((host, port))
        self.sock.settimeout(0.01)
        self.peer = None
        self.name = f"udp {host}:{port}"

    def read(self) -> bytes:
        '''Return one datagram or nothing after the timeout.'''
        try:
            data, self.peer = self.sock.recvfrom(256)
            return data
        except OSError:
            return b""

    def write(self, data: bytes) -> None:
        '''Answer the last sender.'''
        if self.peer is not None:
            try:
                self.sock.sendto(data, self.peer)
            except OSError:
                pass

    def close(self) -> None:
        '''Close the socket.'''
        self.sock.close()


def open_transport(gcfg: dict):
    '''Create the configured transport.'''
    if gcfg["transport"] == "serial":
        return SerialTransport(gcfg["port"], gcfg["baud"])
    if gcfg["transport"] == "udp":
        return UdpTransport(gcfg["udp_port"])
    raise ValueError(f"unknown glove transport '{gcfg['transport']}'")


class GloveLink:
    '''Receives glove states in a background thread and answers each one at once with the latest feedback.'''

    def __init__(self, gcfg: dict):
        '''Open the transport and start the receive thread.'''
        sys.setswitchinterval(0.0002)  # default 5 ms GIL slices would add up to 5 ms to every reply
        self.transport = open_transport(gcfg)
        self.torque_scale = gcfg["torque_scale"]
        self.torque_limit = gcfg["torque_limit"]
        self.damping = gcfg["damping"]
        self.slew = gcfg["slew_rate"]
        self.timeout = gcfg["timeout"]
        self.lock = threading.Lock()
        self.parser = FrameParser()
        self.state: GloveState | None = None
        self.received_at = 0.0
        self.torque = np.zeros(3)
        self.sent = np.zeros(3)
        self.enable = False
        self.seq = 0
        self.frames = 0
        self.lost = 0
        self.rtts = deque(maxlen=10_000)
        self.connected = False
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True, name="glove")
        self.thread.start()
        print(f"glove link on {self.transport.name}")

    def set_feedback(self, feedback: np.ndarray, enable: bool) -> None:
        '''Scale and clamp sim feedback into servo torque for the next replies.'''
        torque = np.clip(np.nan_to_num(np.asarray(feedback, float)) * self.torque_scale, 0.0, self.torque_limit)
        with self.lock:
            self.torque, self.enable = torque, enable

    def latest(self) -> GloveState | None:
        '''Latest glove state, None if none arrived within the timeout.'''
        with self.lock:
            state, at = self.state, self.received_at
        if state is None or time.perf_counter() - at > self.timeout:
            return None
        return state

    def take_stats(self) -> dict:
        '''Frame count, lost frames and round trips in ms since the last call.'''
        with self.lock:
            stats = {"frames": self.frames, "lost": self.lost, "crc_errors": self.parser.crc_errors,
                     "rtt_ms": np.array(self.rtts) / 1000.0}
            self.frames, self.lost = 0, 0
            self.rtts.clear()
        return stats

    def close(self) -> None:
        '''Stop the thread and close the transport.'''
        self.stop_event.set()
        self.thread.join(timeout=1.0)
        self.transport.close()

    def _run(self) -> None:
        '''Receive, reply and watch the link.'''
        while not self.stop_event.is_set():
            data = self.transport.read()
            now = time.perf_counter()
            for msg in self.parser.feed(data) if data else ():
                if isinstance(msg, GloveState):
                    self._handle(msg, now)
            if self.connected and now - self.received_at > self.timeout:
                self.connected = False
                print("glove lost")

    def _handle(self, state: GloveState, now: float) -> None:
        '''Store the state, answer it and report changes.'''
        with self.lock:
            previous = self.state
            torque, enable = self.torque.copy(), self.enable
            self.state, self.received_at = state, now
            self.frames += 1
            gap = (state.seq - previous.seq) % U32 if previous is not None else 1
            if 0 < gap < 10_000:
                self.lost += gap - 1
            if state.rtt_us:
                self.rtts.append(state.rtt_us)
        self.seq += 1
        self.sent = torque if enable else np.zeros(3)
        self.transport.write(encode_feedback(self.seq, state.seq, state.t_us, torque, self.damping, self.slew,
                                             FEEDBACK_ENABLE if enable else 0))
        if not self.connected:
            self.connected = True
            print(f"glove connected{' (servo stub)' if state.stub else ''}")
        if previous is not None and state.kill != previous.kill:
            print("glove kill switch " + ("pressed, torque off" if state.kill else "released"))


_links: dict = {}


def open_glove(cfg: dict) -> GloveLink:
    '''Return the glove link, opened once and kept across scene rebuilds.'''
    gcfg = cfg["glove"]
    key = (gcfg["transport"], gcfg["port"], gcfg["udp_port"])
    if key not in _links:
        _links[key] = GloveLink(gcfg)
    return _links[key]
