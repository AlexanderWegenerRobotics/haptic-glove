# sim

MuJoCo simulation of the hand and scene. Python, physics in its own thread at 1 kHz, optional MuJoCo viewer on the main thread.

## Setup

```
cd sim
python -m venv .venv
source .venv/bin/activate        # Windows: .venv\Scripts\activate
pip install -r requirements.txt
```

Python 3.11+ recommended (high resolution sleep on Windows).

## Run

```
mjpython -m haptic_sim           # macOS, viewer needs mjpython
python -m haptic_sim             # Windows / Linux
```

Useful overrides:

```
--scene scenes/random_three.yaml
--hand parallel_jaw
--fingers script                 # automatic open/close cycle
--pose fixed                     # hand stays at start pose
--pose vive --fingers trigger    # SteamVR wand or tracker (Windows)
--fingers glove                  # haptic glove or tools/fake_glove.py
--headless --duration 10
--no-record
```

## Viewer controls

- Fingers (`finger_source: viewer`): Control panel sliders on the right, one per actuator.
- Wrist (`pose_source: viewer`): double-click the green target sphere, then Ctrl + right-drag to move, Ctrl + left-drag to rotate.

## Tracking (SteamVR, Windows)

`pose_source: vive` reads the wand or Vive Tracker through pyopenvr as a background app, so it runs next to Unreal. SteamVR must be running. `finger_source: trigger` closes all fingers with the wand trigger. Device, polling rate, wrist offset and button mapping are under `tracking:` in `sim.yaml`.

Calibration maps SteamVR space into the sim world and the way you hold the device onto the hand. Hold the hand where it should rest, palm down and fingers forward like the sim hand, look forward (or set `yaw_from: device` and point the wand forward) and run calibrate: the wrist lands on `start_pose` in position and orientation, and the forward direction becomes +x. It is saved to `config/calibration.yaml` (not in git). Redo it after moving the base station, redoing the SteamVR room setup or switching between wand and tracker.

Each run starts in waiting: the sim hand holds still and a blue ghost shows the tracked hand. Bring the ghost onto the sim hand (within `engage_distance` and `engage_angle`) to engage. Tracking loss longer than `lost_timeout` releases the hand back to waiting.

## Commands

Type in the terminal and press Enter, or use the wand buttons (mapping in `tracking.buttons`):

| Key | Default button | Command |
|---|---|---|
| r | menu | reset same: objects and hand back to the start, back to waiting |
| n | grip | reset new: new random draw (new scene id), viewer reopens |
| c | trackpad | calibrate |
| s | | stop: freeze hand and fingers until resume |
| g | | resume: leave stop, engage again |
| h | | switch hand (rebuilds the scene) |
| q | | quit |

The renderer sends the same commands as JSON on UDP 9872 (`protocol/unreal_udp.md`), e.g. calibrate with a countdown so the operator can look forward first.

## Config

- `config/sim.yaml` rates, hand type, input sources, tracking, session, recording
- `config/objects.yaml` object library (shape, size, mass, contact softness)
- `config/scenes/*.yaml` table, slots, and objects or a random sample

Contact softness is set by `contact_time`. MuJoCo contacts are mass-normalized, so the felt stiffness also depends on object mass. Use `tools/probe_grasp.py` to measure what an object actually feels like.

## Outputs

Every run writes to `logs/`:

- `scene_<id>.json` scene description for the renderer
- `run_<timestamp>.npz` per-step log (`data`, `fields`, `meta`), one per scene; `trial` counts resets, `mode` is 0 waiting / 1 engaged, `tracked_*` is the tracked wrist pose (NaN while not tracked)

## Unreal stream

While running, the sim streams state over UDP (`unreal:` in `sim.yaml`, `--no-unreal` to turn off). Packet layout: `protocol/unreal_udp.md`. Without Unreal, check the stream with:

```
python tools/udp_listen.py
```

## Glove

`finger_source: glove` (or `--fingers glove`) takes the closures from the glove and sends the per-channel feedback back as servo torque (`torque_scale`, `torque_limit` under `glove:` in `sim.yaml`). Torque is only enabled while the hand is engaged and not stopped. Protocol: `protocol/glove_serial.md`, firmware: `firmware/`.

Without hardware, set `glove.transport: udp` and run the fake glove next to the sim:

```
python tools/fake_glove.py                   # k + Enter kill switch, q + Enter quit
```

It closes and opens the hand every 3 s and gives way to the rendered torque like a compliant operator (mass-spring-damper finger, same model and torque shaping as the firmware stub). `--delay-ms 1.7` gives the ~2 ms round trip of the real USB link, `--natural-hz 50` a nearly massless finger as a worst case for stability.

Stability knobs under `glove:` in `sim.yaml`: `filter_hz` low-passes the feedback in the sim, `damping` and `slew_rate` are applied on the glove and sent with every feedback frame. Defaults (15 Hz, 0, 10 Nm/s) were tuned offline with the fake glove at 2 ms round trip: they remove the torque chatter even for the massless finger on the hard ball and cost little peak torque. Run logs contain `glove_sent_*` (torque sent), `glove_torque_*` (torque the glove applies), `glove_rtt` (ms) and `glove_flags`; `tools/plot_log.py` shows them in an extra row. To test the ESP32 alone (rate, loss, round trip), without the sim:

```
python tools/glove_monitor.py --port COM4
```

## Tools

```
python tools/probe_grasp.py --scene scenes/default.yaml --hand dexterous
```

Grasps and lifts each object and plots feedback over commanded closure.

```
python tools/plot_log.py                     # newest log in logs/
python tools/plot_log.py logs/run_x.npz --out plot.png
```

Plots command vs measured closure, feedback and contact force per channel, plus loop timing.
