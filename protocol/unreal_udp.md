# Sim <-> Unreal UDP protocol (v3)

Two UDP streams from the sim to the renderer and one command channel back. Reference decoder: `sim/haptic_sim/outputs.py` (`decode_state`), test listener: `sim/tools/udp_listen.py`. Unreal implementation: `rendering/HapticGlove/Source/HapticGlove/Private/Networking/SimProtocol.cpp`.

| Stream | Direction | Default port | Rate | Content |
|---|---|---|---|---|
| state | sim -> renderer | 9870 | 250 Hz | binary, hand and object state |
| scene | sim -> renderer | 9871 | 1 Hz (repeated) | `HGSC` + UTF-8 JSON scene description |
| command | renderer -> sim | 9872 | on demand | UTF-8 JSON, one command per datagram |

The renderer (re)builds the scene whenever `scene_id` in the scene message changes, and drops state packets whose `scene_id` does not match the current scene.

## Frames and units

MuJoCo world frame: right-handed, z up, meters. Quaternions are `w, x, y, z`.

To Unreal (left-handed, z up, cm): `x_ue = 100 x`, `y_ue = -100 y`, `z_ue = 100 z`. Quaternion: `(w, x, y, z) -> (w, -x, y, -z)`, in Unreal's `FQuat(X, Y, Z, W)` order `FQuat(-x, y, -z, w)`. The same mapping applies to local geom offsets.

## Tracking alignment

The sim maps SteamVR standing space into the MuJoCo world with one calibration, sent as `tracking` in the scene message (`null` without SteamVR tracking):

```
p_world = Rz(yaw) * C * p_steamvr + position,   C: x = -z_vr, y = -x_vr, z = y_vr
```

Unreal uses the same alignment so the headset view and the hand agree. With the OpenXR tracking origin set to stage (floor), the VR origin (pawn) goes to location `(100 x, -100 y, 100 z)` cm from `position` and yaw `-degrees(yaw)`. `tracking` changes when the operator recalibrates (same `scene_id`), so apply it whenever `created` changes.

## Scene message

`"HGSC"` followed by JSON:

```
scene_id, scene_name, seed, created, frame, hand ("dexterous" | "parallel_jaw"), protocol_version,
hand_joints [names, order of joints in the state packet],
hand_bodies [names, order of body poses in the state packet],
hand_geoms [{body (index into hand_bodies), type, size, position, orientation, rgba}],
table {position (center of the top plate), size [x, y, thickness], height, rgba},
objects [{id, type, shape, size, rgba, deformable, position, orientation}],
stream {host, state_port, scene_port, rate},
tracking {yaw (rad), position [x, y, z], grip [w, x, y, z], created} or null
```

Geom types are `sphere | capsule | ellipsoid | cylinder | box` with MuJoCo sizes: sphere `[r]`, capsule and cylinder `[r, half_length]` along local z, ellipsoid and box half sizes `[x, y, z]`. Hand geom `position`/`orientation` are relative to their body. Object order in `objects` is the order used in the state packet.

## State packet

Little endian, packed, no padding. 742 bytes for the dexterous hand (15 bodies) with 3 objects, 358 for the parallel jaw.

| Field | Type | Notes |
|---|---|---|
| magic | char[4] | `HGST` |
| version | uint16 | 3 |
| hand_type | uint8 | 0 dexterous, 1 parallel_jaw |
| flags | uint8 | bit 0 engaged, 1 ghost visible, 2 stopped, 3 tracked, 4 calibrated |
| seq | uint32 | increments per packet, gaps = loss |
| sim_time | float64 | s |
| send_time | float64 | s, sender wall clock (unix time), for latency on the same PC |
| scene_id | char[8] | ASCII, zero padded |
| trial | uint32 | resets since start |
| command_seq | uint32 | `seq` of the last applied command |
| countdown | float32 | s until a pending calibration, 0 when none |
| wrist_pos | float32[3] | hand root position |
| wrist_quat | float32[4] | w x y z |
| ghost_pos | float32[3] | tracked hand pose, meaningful when the ghost bit is set |
| ghost_quat | float32[4] | w x y z |
| n_joints | uint16 | |
| joints | float32[n_joints] | rad (dexterous) or m (jaw), order = `hand_joints` |
| n_bodies | uint16 | |
| bodies | n_bodies x 7 float32 | position[3], quat[4] per hand body, order = `hand_bodies` |
| closure | float32[3] | thumb, index, middle, 0 open to 1 closed |
| feedback | float32[3] | thumb, index, middle, Nm (dexterous) or N (jaw) |
| n_objects | uint16 | |
| objects | n_objects x 11 float32 | position[3], quat[4], squash_depth (m), squash_dir[3] |

`squash_depth` is the deepest current contact penetration on that object, `squash_dir` the unit direction pointing into the object at that contact. Zero when untouched.

## Commands

JSON object per datagram, `seq` increasing per sender. The sim echoes the last applied `seq` as `command_seq`, so the renderer knows a command arrived.

| command | arguments | effect |
|---|---|---|
| `reset_same` | | objects and hand back to the start, back to waiting |
| `reset_new` | | new random draw, new `scene_id` |
| `calibrate` | `delay` s (optional) | calibrate after the delay, `countdown` shows the remaining time |
| `stop` | | freeze hand and fingers, stopped bit set |
| `resume` | | leave stop, back to waiting |
| `set_hand` | `hand` (optional, toggles when missing) | rebuild with the other hand, new `scene_id` |
| `quit` | | stop the sim |

Example: `{"seq": 4, "command": "calibrate", "delay": 3.0}`

## Engagement

With a tracked pose source the sim starts in waiting: the sim hand holds its pose and the ghost bit is set, render the ghost at `ghost_pos`/`ghost_quat`. When the operator brings the tracked hand onto the sim hand the engaged bit is set, the ghost bit clears and the sim hand follows. Tracking loss or a reset goes back to waiting. Stop overrides everything until resume.
