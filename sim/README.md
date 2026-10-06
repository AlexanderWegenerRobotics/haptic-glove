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
--headless --duration 10
--no-record
```

## Viewer controls

- Fingers (`finger_source: viewer`): Control panel sliders on the right, one per actuator.
- Wrist (`pose_source: viewer`): double-click the green target sphere, then Ctrl + right-drag to move, Ctrl + left-drag to rotate.

## Config

- `config/sim.yaml` rates, hand type, input sources, recording
- `config/objects.yaml` object library (shape, size, mass, contact softness)
- `config/scenes/*.yaml` table, slots, and objects or a random sample

Contact softness is set by `contact_time`. MuJoCo contacts are mass-normalized, so the felt stiffness also depends on object mass. Use `tools/probe_grasp.py` to measure what an object actually feels like.

## Outputs

Every run writes to `logs/`:

- `scene_<id>.json` scene description for the renderer
- `run_<timestamp>.npz` per-step log (`data`, `fields`, `meta`)

## Unreal stream

While running, the sim streams state over UDP (`unreal:` in `sim.yaml`, `--no-unreal` to turn off). Packet layout: `protocol/unreal_udp.md`. Without Unreal, check the stream with:

```
python tools/udp_listen.py
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
