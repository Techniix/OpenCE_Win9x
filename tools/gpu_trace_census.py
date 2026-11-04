#!/usr/bin/env python3
"""Count a traced frame's draws by the pass of render_window that made them.

The OpenGL 1.1 renderer (port/retro_gl/src/retro_device.c) logs every draw of
a traced frame into halo.log: Scroll Lock traces the next frame, and
debug.gpu_trace_frame (HALO_GPU_TRACE) a frame by number. Each pass of
source/render/render.c's render_window starts a "trace pass: <name>" line
(halo_render_pass), and the draws after it are that pass's, up to the next
mark. This reads the log and tells, for each pass, how many draws, vertices
(or indices) and triangles, how many different shaders and textures, and how
many draws blend, alpha test, skip the depth write or the culling.

It only reads the log; the game is not changed by it. Pass marks draw
nothing and change no game state. A window's passes start again at "sky", so
a frame of several windows (split screen) is counted whole, with a total.

    python tools/gpu_trace_census.py halo.log
    python tools/gpu_trace_census.py halo.log --json census.json

The counts are the renderer's, not a timing: use them to see which passes
have the draws and vertices, then measure those on the machine.
"""

import argparse
import json
import re
import sys
from collections import OrderedDict

DRAW = re.compile(
    r"trace (?P<call>indexed|draw): program (?P<program>\d+), shader (?P<shader>[0-9a-f]+), "
    r"(?P<kind>quads|strip|fan|triangles|other) (?P<count>\d+),(?P<textures>.*?); "
    r"blend (?P<blend>\d+) (?P<src>[0-9a-f]+) (?P<dst>[0-9a-f]+), "
    r"alpha test (?P<atest>\d+) [0-9a-f]+ \d+, z (?P<z>\d+) (?P<zwrite>\d+), "
    r"cull (?P<cull>[0-9a-f]+), view (?P<vx>\d+) (?P<vy>\d+) (?P<vw>\d+) (?P<vh>\d+)")
PASS = re.compile(r"trace pass: (?P<name>\S+)")
LEFT_OUT = re.compile(r"trace: left out \((?P<why>[^)]*)\)")
CLEAR = re.compile(r"trace clear:")
SCROLL = re.compile(r"Scroll Lock pressed")
TEXTURE = re.compile(r"t\d+=([0-9a-f]+)/")
D3DCULL_NONE = 1


def triangles(kind, count):
    if kind == "triangles":
        return count // 3
    if kind in ("strip", "fan"):
        return max(count - 2, 0)
    if kind == "quads":
        return (count // 4) * 2
    return 0


class Pass:
    def __init__(self):
        self.draws = 0
        self.vertices = 0
        self.triangles = 0
        self.shaders = set()
        self.textures = set()
        self.blended = 0
        self.alpha_tested = 0
        self.no_depth_write = 0
        self.no_cull = 0
        self.left_out = 0
        self.clears = 0

    def add(self, other):
        for field in ("draws", "vertices", "triangles", "blended", "alpha_tested",
                      "no_depth_write", "no_cull", "left_out", "clears"):
            setattr(self, field, getattr(self, field) + getattr(other, field))
        self.shaders |= other.shaders
        self.textures |= other.textures

    def row(self):
        return {"draws": self.draws, "vertices": self.vertices, "triangles": self.triangles,
                "shaders": len(self.shaders), "textures": len(self.textures),
                "blended": self.blended, "alpha_tested": self.alpha_tested,
                "no_depth_write": self.no_depth_write, "no_cull": self.no_cull,
                "left_out": self.left_out, "clears": self.clears}


def read_frames(lines):
    """A list of frames, each an ordered {pass name: Pass}; draws before the
    first mark are under "(before any pass)"."""
    frames = []
    frame = None
    current = None

    def start_frame():
        nonlocal frame, current
        frame = OrderedDict()
        frames.append(frame)
        current = None

    def pass_for(name):
        nonlocal current
        if frame is None:
            start_frame()
        current = frame.setdefault(name, Pass())
        return current

    for line in lines:
        if SCROLL.search(line):
            start_frame()
            continue
        marked = PASS.search(line)
        if marked:
            if frame is None:
                start_frame()
            pass_for(marked.group("name"))
            continue
        draw = DRAW.search(line)
        if draw:
            target = current if current is not None else pass_for("(before any pass)")
            count = int(draw.group("count"))
            target.draws += 1
            target.vertices += count
            target.triangles += triangles(draw.group("kind"), count)
            target.shaders.add((draw.group("program"), draw.group("shader")))
            for texture in TEXTURE.findall(draw.group("textures")):
                target.textures.add(texture)
            if draw.group("blend") != "0":
                target.blended += 1
            if draw.group("atest") != "0":
                target.alpha_tested += 1
            if draw.group("zwrite") == "0":
                target.no_depth_write += 1
            if int(draw.group("cull"), 16) == D3DCULL_NONE:
                target.no_cull += 1
            continue
        if LEFT_OUT.search(line):
            (current if current is not None else pass_for("(before any pass)")).left_out += 1
        elif CLEAR.search(line):
            (current if current is not None else pass_for("(before any pass)")).clears += 1
    return [f for f in frames if f]


def total(frame):
    result = Pass()
    for entry in frame.values():
        result.add(entry)
    return result


def print_frame(index, frame):
    columns = ("draws", "vertices", "triangles", "shaders", "textures", "blended", "alpha_tested",
               "no_depth_write", "no_cull", "left_out")
    print(f"frame {index}")
    print(f"  {'pass':28}" + "".join(f"{name:>16}" for name in columns))
    for name, entry in frame.items():
        row = entry.row()
        print(f"  {name:28}" + "".join(f"{row[c]:>16}" for c in columns))
    row = total(frame).row()
    print(f"  {'TOTAL':28}" + "".join(f"{row[c]:>16}" for c in columns))
    print("  (shaders and textures in TOTAL are the whole frame's different ones, not a sum)")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("log", help="halo.log with a traced frame")
    parser.add_argument("--json", help="also write the counts to this file")
    args = parser.parse_args()
    with open(args.log, errors="replace") as handle:
        frames = read_frames(handle)
    if not frames:
        print("no traced draws found: press Scroll Lock in the game, or set debug.gpu_trace_frame", file=sys.stderr)
        return 1
    for index, frame in enumerate(frames, 1):
        print_frame(index, frame)
    if args.json:
        with open(args.json, "w") as handle:
            json.dump([{"passes": {name: entry.row() for name, entry in frame.items()},
                        "total": total(frame).row()} for frame in frames], handle, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
