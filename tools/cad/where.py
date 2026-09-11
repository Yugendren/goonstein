#!/usr/bin/env python3
"""Where is a kit piece's opening? Print each piece's bounds, per material, in game axes.

    python3 tools/cad/where.py                 # every piece
    python3 tools/cad/where.py window_large

Placing a window means lining its opening up with the black rectangle already painted on the
wall, and the opening is the piece's `glass` or `dark` material. This prints both the whole
piece's box and each material's box, plus the height of the opening's centre above the piece's
base -- which is the number you subtract from the wall rectangle's centre to get the piece's y.

Needs nothing but the standard library: it reads the OBJ that is already in the tree, so it
tells you about the mesh the game actually loads rather than about the CAD that made it.
"""

import os
import sys

ARCH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..",
                    "assets", "models", "own", "arch")


def bounds(path):
    """-> (whole box, {material: box}) where a box is (lo, hi) in the OBJ's own axes."""
    verts, per_mat, cur = [], {}, "?"
    used = {}
    for line in open(path):
        if line.startswith("v "):
            verts.append([float(t) for t in line.split()[1:4]])
        elif line.startswith("usemtl "):
            cur = line.split(None, 1)[1].strip()
        elif line.startswith("f "):
            for tok in line.split()[1:]:
                i = int(tok.split("/")[0])
                used.setdefault(cur, set()).add(i - 1 if i > 0 else len(verts) + i)
    def box(idx):
        lo = [min(verts[i][k] for i in idx) for k in range(3)]
        hi = [max(verts[i][k] for i in idx) for k in range(3)]
        return lo, hi
    allidx = range(len(verts))
    for m, idx in used.items():
        per_mat[m] = box(idx)
    return box(allidx), per_mat


def fmt(b):
    lo, hi = b
    return "x %6.2f..%6.2f  y %6.2f..%6.2f  z %6.2f..%6.2f   (%.2f x %.2f x %.2f)" % (
        lo[0], hi[0], lo[1], hi[1], lo[2], hi[2],
        hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2])


def main():
    names = sys.argv[1:]
    files = sorted(f for f in os.listdir(ARCH) if f.endswith(".obj"))
    if names:
        files = [n + ".obj" for n in names]
    for f in files:
        whole, mats = bounds(os.path.join(ARCH, f))
        print("%s\n  whole   %s" % (f[:-4], fmt(whole)))
        for m in sorted(mats):
            print("  %-7s %s" % (m, fmt(mats[m])))
            if m in ("glass", "dark"):
                lo, hi = mats[m]
                print("          opening centre %.3f m above the piece's base" % ((lo[1] + hi[1]) / 2))
        print()


main()
