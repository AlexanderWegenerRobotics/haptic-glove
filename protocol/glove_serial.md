# Glove serial protocol (v2)

Link between the glove controller (ESP32-S3) and the sim. Closures go up, feedback torques come down.

## Transport

- USB serial. The firmware speaks on both the native USB port (CDC) and the UART bridge port (921600 baud), the host opens one of them.
- The fake glove (`sim/tools/fake_glove.py`) uses the same frames over UDP, one frame per datagram, sim listening on port 9880.
- All values little endian, floats are IEEE 754 float32.

## Timing

- The glove is the clock: it sends a state frame at a fixed 500 Hz (`STATE_PERIOD_US` in firmware).
- The host answers every state frame right away with one feedback frame carrying the latest torques. The feedback rate is the glove rate.
- Every feedback frame echoes the `seq` and `t_us` of the state frame it answers. The glove computes the round trip `rtt_us = now - echo_t_us` and reports it in its next state frame.

## Frame

| Field | Type | Notes |
|---|---|---|
| sync | 2 bytes | `0xA5 0x5A` |
| type | u8 | `0x01` state, `0x02` feedback |
| len | u8 | payload length in bytes, max 64 |
| payload | len bytes | see below |
| crc | u16 | CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over type, len and payload |

A frame with a wrong CRC is dropped and the parser resyncs on the next sync bytes.

## State, glove to host (type 0x01, 38 bytes)

| Field | Type | Notes |
|---|---|---|
| version | u8 | protocol version, 2 |
| seq | u32 | counts up per state frame |
| t_us | u32 | glove clock in µs when the frame was built, wraps |
| closure | 3 × f32 | thumb, index, middle; 0 open, 1 closed |
| torque | 3 × f32 | measured servo torque in Nm (from present current), 0 on the stub |
| rtt_us | u32 | last measured round trip in µs, 0 before the first feedback |
| flags | u8 | bit 0 kill switch pressed, bit 1 torque on, bit 2 feedback timeout, bit 3 stub servos |

## Feedback, host to glove (type 0x02, 33 bytes)

| Field | Type | Notes |
|---|---|---|
| seq | u32 | counts up per feedback frame |
| echo_seq | u32 | seq of the state frame this answers |
| echo_t_us | u32 | t_us of that state frame |
| torque | 3 × f32 | commanded servo torque in Nm, thumb, index, middle; positive resists closing |
| damping | f32 | Nm·s per unit closure, glove-side damping on closing speed while in contact |
| slew | f32 | Nm/s, glove-side torque rate limit, 0 = off |
| flags | u8 | bit 0 enable (sim hand engaged and not stopped) |

Damping and slew come from `glove:` in `config/sim.yaml`, so they can be tuned without reflashing.

## Torque shaping on the glove

Per channel, every state period: closing velocity from the closure, low-passed at `VELOCITY_FILTER_HZ`. While the commanded torque is above 0, the target is `torque + damping × max(velocity, 0)`, clamped to [0, `TORQUE_LIMIT`]; otherwise 0. The output moves toward the target by at most `slew × dt`. Disabling (kill switch, timeout, enable 0) sets the output to 0 at once, without slew.

## Safety

- The glove only renders torque while all of these hold: enable bit set, kill switch released, a valid feedback frame within the last 50 ms (`FEEDBACK_TIMEOUT_US`).
- Torques are clamped to [0, `TORQUE_LIMIT`] and damping to [0, `DAMPING_LIMIT`] on the glove; NaN or negative values become 0. The glove never pushes the fingers open.
- The host low-passes the sim feedback (`glove.filter_hz`), scales it with `glove.torque_scale` and clamps to `glove.torque_limit` (`config/sim.yaml`).
