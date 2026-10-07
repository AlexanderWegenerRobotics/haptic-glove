import math
import shutil
import sys
from pathlib import Path

REF = Path(sys.argv[1])
OUT = Path(sys.argv[2])
(OUT / "svg").mkdir(parents=True, exist_ok=True)

FONT = "-apple-system,SF Pro Display,Helvetica Neue,sans-serif"


def button(top, bottom, body, width=200):
    """Glossy rounded button in the teleop style: two-tone gradient, gloss on the upper half, faint rim."""
    w = width - 20
    return f'''<?xml version="1.0" encoding="UTF-8"?>
<svg viewBox="0 0 {width} 200" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <linearGradient id="bgGrad" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="{top}"/>
      <stop offset="100%" stop-color="{bottom}"/>
    </linearGradient>
    <linearGradient id="glossGrad" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="white" stop-opacity="0.15"/>
      <stop offset="50%" stop-color="white" stop-opacity="0.0"/>
    </linearGradient>
    <clipPath id="clip1">
      <rect x="10" y="10" width="{w}" height="180" rx="40" ry="40"/>
    </clipPath>
  </defs>
  <rect x="10" y="10" width="{w}" height="180" rx="40" ry="40" fill="url(#bgGrad)"/>
  <rect x="10" y="10" width="{w}" height="90" rx="40" ry="40" fill="url(#glossGrad)" clip-path="url(#clip1)"/>
{body}
  <rect x="10" y="10" width="{w}" height="180" rx="40" ry="40" fill="none" stroke="white" stroke-width="1.5" opacity="0.15"/>
</svg>
'''


def calibrate():
    """Alignment reticle centred on a wide button that matches START and STOP."""
    ticks = []
    for x1, y1, x2, y2 in [(100, 38, 100, 72), (100, 128, 100, 162), (38, 100, 72, 100), (128, 100, 162, 100)]:
        ticks.append(f'  <line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="white" stroke-width="12" stroke-linecap="round"/>')
    return button("#7a5a10", "#1a1204", "\n".join([
        '  <g transform="translate(120,0)">',
        '  <circle cx="100" cy="100" r="44" fill="white" fill-opacity="0.10" stroke="white" stroke-width="12"/>',
        *ticks,
        '  <circle cx="100" cy="100" r="11" fill="white"/>',
        '  </g>',
    ]), width=440)


def new_scene():
    """Two dice: a faint one behind, a solid one in front with five pips."""
    pips = [(-20, -20), (20, -20), (0, 0), (-20, 20), (20, 20)]
    pip_svg = "\n".join(f'    <circle cx="{x}" cy="{y}" r="7.5" fill="#2a1446"/>' for x, y in pips)
    return button("#5a2d8a", "#1a0a2e", f'''  <g transform="translate(120,78) rotate(16)">
    <rect x="-34" y="-34" width="68" height="68" rx="14" fill="white" fill-opacity="0.12" stroke="white" stroke-width="8" opacity="0.55"/>
    <circle cx="-14" cy="-14" r="6" fill="white" opacity="0.55"/>
    <circle cx="14" cy="14" r="6" fill="white" opacity="0.55"/>
  </g>
  <g transform="translate(82,118) rotate(-10)">
    <rect x="-42" y="-42" width="84" height="84" rx="16" fill="white"/>
{pip_svg}
  </g>''')


def arc_arrow(cx, cy, r, start, end):
    """Arc from start to end angle in degrees (clockwise on screen) with a filled arrowhead at the end."""
    a0, a1 = math.radians(start), math.radians(end)
    x0, y0 = cx + r * math.cos(a0), cy + r * math.sin(a0)
    x1, y1 = cx + r * math.cos(a1), cy + r * math.sin(a1)
    tx, ty = -math.sin(a1), math.cos(a1)
    nx, ny = math.cos(a1), math.sin(a1)
    tip = (x1 + tx * 16, y1 + ty * 16)
    left = (x1 + nx * 13 - tx * 4, y1 + ny * 13 - ty * 4)
    right = (x1 - nx * 13 - tx * 4, y1 - ny * 13 - ty * 4)
    path = f'  <path d="M {x0:.1f} {y0:.1f} A {r} {r} 0 0 1 {x1:.1f} {y1:.1f}" fill="none" stroke="white" stroke-width="10" stroke-linecap="round"/>'
    head = (f'  <path d="M {tip[0]:.1f} {tip[1]:.1f} L {left[0]:.1f} {left[1]:.1f} L {right[0]:.1f} {right[1]:.1f} Z" '
            f'fill="white" stroke="white" stroke-width="5" stroke-linejoin="round"/>')
    return path + "\n" + head


def switch_hand():
    """Open hand inside two circular swap arrows."""
    fingers = [(-15, 0, -16, -30), (-5, -2, -5, -38), (6, -2, 7, -36), (16, 2, 18, -24)]
    finger_svg = "\n".join(
        f'    <line x1="{a}" y1="{b}" x2="{c}" y2="{d}" stroke="white" stroke-width="10" stroke-linecap="round"/>'
        for a, b, c, d in fingers)
    return button("#0d6e6e", "#001a1a", f'''{arc_arrow(100, 100, 66, 205, 325)}
{arc_arrow(100, 100, 66, 25, 145)}
  <g transform="translate(102,104)">
    <rect x="-21" y="-4" width="42" height="36" rx="13" fill="white"/>
{finger_svg}
    <line x1="-17" y1="16" x2="-33" y2="-2" stroke="white" stroke-width="10" stroke-linecap="round"/>
  </g>''')


def debug_panel():
    """Signal bars with ms label from the teleop stats icon, recoloured to slate."""
    bars = [(46, 122, 0.45), (80, 100, 0.65), (114, 74, 1.0), (148, 112, 0.6)]
    bar_svg = "\n".join(
        f'  <line x1="{x}" y1="150" x2="{x}" y2="{y}" stroke="white" stroke-width="16" stroke-linecap="round" opacity="{o}"/>'
        for x, y, o in bars)
    return button("#4a4a5a", "#1a1a24", f'''{bar_svg}
  <line x1="32" y1="150" x2="168" y2="150" stroke="white" stroke-width="5" stroke-linecap="round" opacity="0.25"/>
  <text x="50" y="65" font-family="{FONT}" font-size="28" font-weight="700" fill="white" opacity="0.55">ms</text>''')


NEW = {"calibrate": calibrate, "new_scene": new_scene, "switch_hand": switch_hand, "debug_panel": debug_panel}
RECYCLED = {"start.svg": "start", "stop.svg": "stop", "reset_icon.svg": "reset"}

for name, make in NEW.items():
    (OUT / "svg" / f"{name}.svg").write_text(make(), newline="\n")
for source, name in RECYCLED.items():
    shutil.copyfile(REF / source, OUT / "svg" / f"{name}.svg")
print(sorted(p.name for p in (OUT / "svg").iterdir()))
