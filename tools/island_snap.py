#!/usr/bin/env python3
"""Snap the y values in a level file to its terrain.

Placing things on a heightmap by hand means knowing the ground height at every x/z, which is
tedious and goes stale the moment the terrain is regenerated. So a level may be authored with
the ground height written as a tilde:

    prop models/own/villa.part  -16 ~ -70  0 1.0
    prop models/own/lamp.part     4 ~+2.6 -12  0 1.0
    collider  -16 ~+4 -70  20 8 12
    trigger dock  -84 ~-4 -120  -54 ~+8 -100

`~` becomes the terrain height at that x/z; `~+N` / `~-N` offsets it. This script rewrites the
file in place with real numbers, so the result is a perfectly ordinary level file that the world
editor can load, edit and save. It is an authoring aid, not a build step: run it once after
writing tildes, then keep editing numbers.

Handled commands (and which token holds the y): prop 3, npc 4, spawn 2, boss 2, block 2,
collider 2, light 2, emitter 3, trigger 3 and 6. `block` / `collider` y is the box centre, so
`~+4` there means "centre 4 m above the ground".

Usage:
    python3 tools/island_snap.py assets/levels/island.txt
    python3 tools/island_snap.py assets/levels/island.txt --dry-run
"""

import argparse
import os
import sys

from PIL import Image

N = 129
H_MIN, H_RANGE = -64.0, 256.0
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# command -> indices of the tokens that carry a y value
Y_TOKENS = {
    "prop": [3], "npc": [4], "spawn": [2], "boss": [2],
    "block": [2], "collider": [2], "light": [2], "emitter": [3],
    "trigger": [3, 6],
}


class Terrain:
    def __init__(self, base):
        self.h = Image.open(os.path.join(ROOT, "assets", base + "_h.png")).convert("RGBA").load()
        self.cell, self.origin = 1.5, (-96.0, 0.0, -96.0)
        with open(os.path.join(ROOT, "assets", base + ".txt")) as f:
            for line in f:
                line = line.split("#")[0].split()
                if len(line) >= 2 and line[0] == "cell":
                    self.cell = float(line[1])
                elif len(line) >= 4 and line[0] == "origin":
                    self.origin = tuple(float(v) for v in line[1:4])

    def _raw(self, xi, zi):
        xi = 0 if xi < 0 else (N - 1 if xi > N - 1 else xi)
        zi = 0 if zi < 0 else (N - 1 if zi > N - 1 else zi)
        r, g, _b, _a = self.h[xi, zi]
        return ((r << 8) | g) / 65535.0 * H_RANGE + H_MIN

    def height(self, x, z):
        u = (x - self.origin[0]) / self.cell
        v = (z - self.origin[2]) / self.cell
        u = max(0.0, min(N - 1.001, u))
        v = max(0.0, min(N - 1.001, v))
        xi, zi = int(u), int(v)
        fx, fz = u - xi, v - zi
        a = self._raw(xi, zi) + (self._raw(xi + 1, zi) - self._raw(xi, zi)) * fx
        b = self._raw(xi, zi + 1) + (self._raw(xi + 1, zi + 1) - self._raw(xi, zi + 1)) * fx
        return (a + (b - a) * fz) + self.origin[1]


def terrain_base_of(level_path):
    with open(level_path) as f:
        for line in f:
            t = line.split("#")[0].split()
            if len(t) >= 2 and t[0] == "terrain":
                return t[1]
    return None


def xz_for(cmd, toks, yi):
    """The x and z token indices that go with the y at index `yi`."""
    if cmd == "emitter":
        return 2, 4
    if cmd == "npc":
        return 3, 5
    if cmd == "trigger":
        return (2, 4) if yi == 3 else (5, 7)
    if cmd == "prop":
        return 2, 4
    return yi - 1, yi + 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("level")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    path = args.level if os.path.isabs(args.level) else os.path.join(ROOT, args.level)
    base = terrain_base_of(path)
    if not base:
        print("no `terrain` line in %s" % path, file=sys.stderr)
        return 1
    tr = Terrain(base)

    out, changed = [], 0
    for line_no, raw in enumerate(open(path), 1):
        body, _, comment = raw.rstrip("\n").partition("#")
        toks = body.split()
        if not toks or toks[0] not in Y_TOKENS or not any(t.startswith("~") for t in toks):
            out.append(raw)
            continue
        cmd = toks[0]
        for yi in Y_TOKENS[cmd]:
            if yi >= len(toks) or not toks[yi].startswith("~"):
                continue
            xi, zi = xz_for(cmd, toks, yi)
            try:
                x, z = float(toks[xi]), float(toks[zi])
            except (IndexError, ValueError):
                print("%s:%d: cannot read x/z for %s" % (path, line_no, cmd), file=sys.stderr)
                continue
            off = float(toks[yi][1:]) if len(toks[yi]) > 1 else 0.0
            toks[yi] = "%.2f" % (tr.height(x, z) + off)
            changed += 1
        # keep the original column alignment roughly by re-joining with single spaces
        out.append(" ".join(toks) + ("  #" + comment if comment else "") + "\n")

    if args.dry_run:
        sys.stdout.write("".join(out))
    else:
        with open(path, "w") as f:
            f.write("".join(out))
    print("snapped %d y values in %s (terrain %s)" % (changed, os.path.relpath(path, ROOT), base))
    return 0


if __name__ == "__main__":
    sys.exit(main())
