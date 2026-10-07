import binascii
import struct
from dataclasses import dataclass

import numpy as np

PROTOCOL_VERSION = 2
SYNC = b"\xa5\x5a"
TYPE_STATE = 0x01
TYPE_FEEDBACK = 0x02
MAX_PAYLOAD = 64
STATE = struct.Struct("<BII3f3fIB")
FEEDBACK = struct.Struct("<III3fffB")
CRC = struct.Struct("<H")
STATE_KILL = 1
STATE_TORQUE_ON = 2
STATE_TIMEOUT = 4
STATE_STUB = 8
FEEDBACK_ENABLE = 1
U32 = 1 << 32


@dataclass
class GloveState:
    seq: int
    t_us: int
    closure: np.ndarray
    torque: np.ndarray
    rtt_us: int
    flags: int

    @property
    def kill(self) -> bool:
        '''Kill switch pressed.'''
        return bool(self.flags & STATE_KILL)

    @property
    def torque_on(self) -> bool:
        '''Glove is currently rendering torque.'''
        return bool(self.flags & STATE_TORQUE_ON)

    @property
    def timeout(self) -> bool:
        '''Glove has not seen feedback recently.'''
        return bool(self.flags & STATE_TIMEOUT)

    @property
    def stub(self) -> bool:
        '''Firmware runs the servo stub.'''
        return bool(self.flags & STATE_STUB)


@dataclass
class Feedback:
    seq: int
    echo_seq: int
    echo_t_us: int
    torque: np.ndarray
    damping: float
    slew: float
    flags: int

    @property
    def enable(self) -> bool:
        '''Host allows torque.'''
        return bool(self.flags & FEEDBACK_ENABLE)


def crc16(data: bytes) -> int:
    '''CRC-16/CCITT-FALSE.'''
    return binascii.crc_hqx(data, 0xFFFF)


def encode_frame(ftype: int, payload: bytes) -> bytes:
    '''Wrap a payload into sync, type, length and CRC.'''
    head = bytes((ftype, len(payload)))
    return SYNC + head + payload + CRC.pack(crc16(head + payload))


def encode_state(seq: int, t_us: int, closure, torque, rtt_us: int, flags: int) -> bytes:
    '''Build a state frame as the glove sends it.'''
    return encode_frame(TYPE_STATE, STATE.pack(PROTOCOL_VERSION, seq % U32, t_us % U32, *closure, *torque,
                                               rtt_us % U32, flags))


def encode_feedback(seq: int, echo_seq: int, echo_t_us: int, torque, damping: float, slew: float,
                    flags: int) -> bytes:
    '''Build a feedback frame as the host sends it.'''
    return encode_frame(TYPE_FEEDBACK, FEEDBACK.pack(seq % U32, echo_seq, echo_t_us, *torque, damping, slew, flags))


def decode(ftype: int, payload: bytes):
    '''Turn a frame payload into a GloveState or Feedback; None for unknown types or wrong sizes.'''
    if ftype == TYPE_STATE and len(payload) == STATE.size:
        v = STATE.unpack(payload)
        if v[0] != PROTOCOL_VERSION:
            return None
        return GloveState(v[1], v[2], np.array(v[3:6]), np.array(v[6:9]), v[9], v[10])
    if ftype == TYPE_FEEDBACK and len(payload) == FEEDBACK.size:
        v = FEEDBACK.unpack(payload)
        return Feedback(v[0], v[1], v[2], np.array(v[3:6]), v[6], v[7], v[8])
    return None


class FrameParser:
    '''Splits a byte stream into frames and resyncs after garbage or CRC errors.'''

    def __init__(self):
        '''Start with an empty buffer.'''
        self.buf = bytearray()
        self.crc_errors = 0

    def feed(self, data: bytes) -> list:
        '''Add received bytes and return the decoded messages completed by them.'''
        self.buf += data
        out = []
        while True:
            i = self.buf.find(SYNC)
            if i < 0:
                del self.buf[:-1 if self.buf.endswith(SYNC[:1]) else len(self.buf)]
                return out
            del self.buf[:i]
            if len(self.buf) < 4:
                return out
            n = self.buf[3]
            if n > MAX_PAYLOAD:
                del self.buf[:1]
                continue
            end = 6 + n
            if len(self.buf) < end:
                return out
            body = bytes(self.buf[2:4 + n])
            if crc16(body) == CRC.unpack_from(self.buf, 4 + n)[0]:
                msg = decode(body[0], body[2:])
                if msg is not None:
                    out.append(msg)
                del self.buf[:end]
            else:
                self.crc_errors += 1
                del self.buf[:1]
