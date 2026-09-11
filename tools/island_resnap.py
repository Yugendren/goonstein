#!/usr/bin/env python3
"""Move what was standing on the ground when the ground moves under it.

    git show HEAD:assets/levels/island_terrain_h.png > /tmp/old_h.png
    python3 tools/island_resnap.py assets/levels/island.txt --old /tmp/old_h.png --old-cell 3.5
    python3 tools/island_resnap.py assets/levels/island.txt --old /tmp/old_h.png --old-cell 3.5 --write

ISLAND_BUILD.md calls this the one sharp edge of the terrain pipeline: `tools/island_snap.py`
turns `~` into a real number once, after which the level file is ordinary numbers and regenerating
the terrain silently leaves five hundred props hanging in the air or buried to the windows. That
was survivable while the terrain was frozen. It is not survivable once the generator is allowed to
change, which is the whole point of the relief pass.

So: this reads the OLD heightmap and the NEW one, and for every placement in the level it works out
how far the thing sat above the old ground. If that offset says it was resting on, or built into,
the ground -- between `--below` metres under it and `--above` metres over it -- the same offset is
re-applied to the new ground. Anything higher than that was on a roof, a deck or a rope and is left
exactly where the author put it. Nothing moves at all where the ground did not move (`--eps`).

It prints every change it would make and writes nothing without `--write`.

Run it ONCE per terrain change, immediately after the change, against the heightmap as it was
before. It is not idempotent: after a run the offsets are measured against the new ground, so a
second run with the same `--old` moves everything a second time.
"""

import argparse
import os
import sys

from PIL import Image

H_MIN, H_RANGE = -64.0, 256.0
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Which token on a line is the y, per keyword. The same table island_snap.py works from: `trigger`
# has two corners and therefore two of them.
Y_TOKEN = {
    "prop": (3,), "npc": (3,), "spawn": (2,), "boss": (2,), "block": (2,),
    "collider": (2,), "light": (2,), "emitter": (2,), "deck": (2,), "trigger": (3, 6),
}


def load_field(path, cell, origin_x, origin_z):
    im = Image.open(path).convert("RGBA")
    n, m = im.size
    if n != m:
        sys.exit("resnap: %s is %dx%d, not square" % (path, n, m))
    px = im.load()
    h = [[0.0] * n for _ in range(n)]
    for z in range(n):
        for x in range(n):
            r, g, _b, _a = px[x, z]
            h[z][x] = (r * 256 + g) / 65535.0 * H_RANGE + H_MIN

    def sample(wx, wz):
        u = min(max((wx - origin_x) / cell, 0.0), n - 1.001)
        v = min(max((wz - origin_z) / cell, 0.0), n - 1.001)
        xi, zi = int(u), int(v)
        fx, fz = u - xi, v - zi
        a = h[zi][xi] + (h[zi][xi + 1] - h[zi][xi]) * fx
        b = h[zi + 1][xi] + (h[zi + 1][xi + 1] - h[zi + 1][xi]) * fx
        return a + (b - a) * fz

    return sample, n


def sidecar(path):
    cell, origin = 1.5, (-96.0, 0.0, -96.0)
    if os.path.exists(path):
        for line in open(path):
            t = line.split()
            if not t:
                continue
            if t[0] == "cell":
                cell = float(t[1])
            elif t[0] == "origin":
                origin = (float(t[1]), float(t[2]), float(t[3]))
    return cell, origin


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("level")
    ap.add_argument("--old", required=True, help="the previous <level>_terrain_h.png")
    ap.add_argument("--old-cell", type=float, required=True)
    ap.add_argument("--new", default=os.path.join(ROOT, "assets/levels/island_terrain_h.png"))
    ap.add_argument("--new-sidecar", default=os.path.join(ROOT, "assets/levels/island_terrain.txt"))
    ap.add_argument("--origin", nargs=3, type=float, default=[-224.0, 0.0, -224.0],
                    help="the OLD terrain's origin; the new one comes from its sidecar")
    ap.add_argument("--above", type=float, default=2.0, help="the highest offset still counted as ground-built")
    ap.add_argument("--below", type=float, default=1.0)
    ap.add_argument("--keywords", default="prop,npc",
                    help="which line types to move. The default is the two that describe ONE thing "
                         "standing on the ground; a block or a collider is part of a structure the "
                         "author levelled by hand, and moving its pieces by their own local ground "
                         "delta turns a straight wall into a staircase.")
    ap.add_argument("--eps", type=float, default=0.12, help="ignore ground that moved less than this")
    ap.add_argument("--stop-at", default="# ================================================================ vegetation",
                    help="ignore everything from this line down (the scatter regenerates itself)")
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()

    new_cell, new_origin = sidecar(args.new_sidecar)
    old, on = load_field(args.old, args.old_cell, args.origin[0], args.origin[2])
    new, nn = load_field(args.new, new_cell, new_origin[0], new_origin[2])
    print("resnap: old %dx%d cell %g   new %dx%d cell %g" % (on, on, args.old_cell, nn, nn, new_cell))

    want = set(k.strip() for k in args.keywords.split(",") if k.strip())
    lines = open(args.level).read().splitlines()
    stop = len(lines)
    for i, l in enumerate(lines):
        if l.strip() == args.stop_at.strip():
            stop = i
            break

    moved = skipped_high = 0
    out = list(lines)
    for i in range(stop):
        t = lines[i].split()
        if not t or t[0] not in Y_TOKEN or t[0] not in want:
            continue
        # x is always the token before the first y, z the one after
        for yi in Y_TOKEN[t[0]]:
            if len(t) <= yi + 1:
                continue
            try:
                x, y, z = float(t[yi - 1]), float(t[yi]), float(t[yi + 1])
            except ValueError:
                continue
            og, ng = old(x, z), new(x, z)
            if abs(ng - og) < args.eps:
                continue
            off = y - og
            if off > args.above or off < -args.below:
                skipped_high += 1
                continue
            t[yi] = ("%.2f" % (ng + off)).rstrip("0").rstrip(".")
            moved += 1
            print("  %+6.2f  %-9s at %7.1f %7.1f  y %7.2f -> %7s   (%.2f above ground)"
                  % (ng - og, t[0], x, z, y, t[yi], off))
        out[i] = " ".join(t)

    print("resnap: %d placements moved, %d left alone as too high above the old ground" % (moved, skipped_high))
    if args.write:
        with open(args.level, "w") as f:
            f.write("\n".join(out) + "\n")
        print("resnap: wrote " + args.level)
    else:
        print("resnap: dry run, nothing written (pass --write)")


if __name__ == "__main__":
    main()
