import argparse
import json
import sys
from pathlib import Path

import matplotlib
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from haptic_sim.config import REPO_DIR
from haptic_sim.hand import CHANNELS


def latest_log(directory: Path) -> Path:
    '''Return the newest run log in the log folder.'''
    logs = sorted(directory.glob("run_*.npz"))
    if not logs:
        raise FileNotFoundError(f"no run_*.npz in {directory}")
    return logs[-1]


def load_log(path: Path):
    '''Load a run log into a dict of columns plus its metadata.'''
    f = np.load(path)
    cols = {name: f["data"][:, i] for i, name in enumerate(f["fields"])}
    return cols, json.loads(str(f["meta"]))


def plot(cols: dict, meta: dict, title: str):
    '''Plot command, closure, feedback and contact force per channel, plus loop timing.'''
    import matplotlib.pyplot as plt

    t = cols["t"]
    fig, axes = plt.subplots(4, 3, figsize=(14, 10))
    for ax in list(axes[:3].flat) + [axes[3, 0]]:
        ax.sharex(axes[0, 0])
    for i, c in enumerate(CHANNELS):
        axes[0, i].plot(t, cols[f"cmd_{c}"], label="command")
        axes[0, i].plot(t, cols[f"closure_{c}"], label="measured")
        axes[0, i].set_title(c)
        axes[1, i].plot(t, cols[f"feedback_{c}"], color="C3")
        axes[2, i].plot(t, cols[f"contact_{c}"], color="C2")
        for r in range(3):
            axes[r, i].grid(alpha=0.3)
        axes[2, i].set_xlabel("time [s]")
    axes[0, 0].set_ylabel("closure [0-1]")
    axes[1, 0].set_ylabel("feedback [Nm, N for jaw]")
    axes[2, 0].set_ylabel("contact force [N]")
    axes[0, 2].legend(loc="upper right")

    period = cols["period"][1:] * 1e3
    step = cols["step_time"] * 1e3
    axes[3, 0].plot(t[1:], period, lw=0.5)
    axes[3, 0].set_ylabel("loop period [ms]")
    axes[3, 0].set_xlabel("time [s]")
    axes[3, 1].hist(np.clip(period, 0, 5), bins=100)
    axes[3, 1].set_yscale("log")
    axes[3, 1].set_xlabel("loop period [ms], clipped at 5")
    axes[3, 2].hist(np.clip(step, 0, 2), bins=100, color="C1")
    axes[3, 2].set_yscale("log")
    axes[3, 2].set_xlabel("step time [ms], clipped at 2")
    for ax in axes[3]:
        ax.grid(alpha=0.3)

    scene = meta.get("scene", {})
    objects = ", ".join(o["id"] for o in scene.get("objects", []))
    fig.suptitle(f"{title}   hand: {scene.get('hand')}   objects: {objects}")
    fig.tight_layout()
    return fig


def main() -> None:
    '''Plot a run log, the newest one by default.'''
    p = argparse.ArgumentParser()
    p.add_argument("log", nargs="?", help="path to run_*.npz, default newest in logs/")
    p.add_argument("--out", help="save to this image instead of opening a window")
    args = p.parse_args()

    path = Path(args.log) if args.log else latest_log(REPO_DIR / "logs")
    if args.out:
        matplotlib.use("Agg")
    cols, meta = load_log(path)
    fig = plot(cols, meta, path.name)
    if args.out:
        fig.savefig(args.out, dpi=110)
        print(f"plot: {args.out}")
    else:
        import matplotlib.pyplot as plt
        plt.show()


if __name__ == "__main__":
    main()
