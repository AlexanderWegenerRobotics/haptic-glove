# Sim → Unreal UDP protocol (v1)

Two UDP streams from the sim to the renderer. Reference decoder: `sim/haptic_sim/outputs.py` (`decode_state`), test listener: `sim/tools/udp_listen.py`.

| Stream | Default port | Rate | Content |
|---|---|---|---|
| state | 9870 | 250 Hz | binary, hand and object state |
| scene | 9871 | 1 Hz (repeated) | `HGSC` + UTF-8 JSON scene description |

The renderer (re)builds the scene whenever `scene_id` in the scene message changes, and drops state packets whose `scene_id` does not match the current scene.

## Frames and units

MuJoCo world frame: right-handed, z up, meters. Quaternions are `w, x, y, z`.

To Unreal (left-handed, z up, cm): `x_ue = 100 x`, `y_ue = -100 y`, `z_ue = 100 z`. Quaternion: `(w, x, y, z) -> (w, -x, y, -z)` in Unreal's `FQuat(X, Y, Z, W)` order: `FQuat(-x, y, -z, w)`. Check this once with a known rotation before relying on it.

## Scene message

`"HGSC"` followed by JSON:

```
scene_id, scene_name, seed, created, frame, hand ("dexterous" | "parallel_jaw"),
protocol_version, hand_joints [names, same order as in the state packet],
table {position (center of the top plate), size [x, y, thickness], height, rgba},
objects [{id, type, shape, size (MuJoCo half sizes), rgba, deformable, position, orientation}],
stream {host, state_port, scene_port, rate}
```

Object order in `objects` is the order used in the state packet.

## State packet

Little endian, packed, no padding. 280 bytes for the dexterous hand with 3 objects.

| Field | Type | Notes |
|---|---|---|
| magic | char[4] | `HGST` |
| version | uint16 | 1 |
| hand_type | uint8 | 0 dexterous, 1 parallel_jaw |
| reserved | uint8 | |
| seq | uint32 | increments per packet, gaps = loss |
| sim_time | float64 | s |
| send_time | float64 | s, sender wall clock (`time.time()`), for latency on the same PC |
| scene_id | char[8] | ASCII, zero padded |
| wrist_pos | float32[3] | hand root position |
| wrist_quat | float32[4] | hand root orientation, w x y z |
| n_joints | uint16 | |
| joints | float32[n_joints] | rad (dexterous) or m (jaw), order = `hand_joints` |
| closure | float32[3] | thumb, index, middle, 0 open to 1 closed |
| feedback | float32[3] | thumb, index, middle, Nm (dexterous) or N (jaw) |
| n_objects | uint16 | |
| objects | n_objects × 11 float32 | position[3], quat[4], squash_depth (m), squash_dir[3] |

`squash_depth` is the deepest current contact penetration on that object, `squash_dir` the unit direction pointing into the object at that contact. Zero when untouched. Use it to drive the visual squash of `deformable` objects.
