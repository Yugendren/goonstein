#!/usr/bin/env python3
"""Build the island's cheap cut-out bushes in headless Blender.

    blender -b --python tools/palm/make_bush.py -- --variant a --out assets/models/own/bush_a.glb

The scatter's 480 bushes were photoscans: `shrub_02` is 27254 triangles and `pachira_aquatica` is
76914, for a thing that is a metre across and, at the distance you usually see it, forty pixels
tall. Worse than the cost, they are the wrong plant -- scanned temperate scrub reads as a handful
of red twigs on a Caribbean cay, which is exactly what the before screenshots show.

A bush here is a CLUSTER OF CARDS: six to ten quads, each carrying one patch of the leaf atlas
(`tools/palm/make_leafpatch.py`), spread over a squashed hemisphere with random azimuth, tilt and
size so the silhouette is round from every angle rather than an X from above. Each card is written
twice with opposite windings so it is visible from both sides without turning off back-face
culling, and every vertex on both copies carries the same normal -- outward from the middle of the
bush, tilted up -- so the whole thing lights like one soft mass instead of like a pile of flat
plates. That is thirty-two to forty triangles for a bush.

Variants:
  a  sea grape: broad round leaves, waist high, the default thicket filler
  b  low sprawl: wider than it is tall, the ground-cover version
  c  thatch palm: short palm fronds straight out of the ground, using the palm's own frond texture

The wind weight goes in the vertex-colour alpha exactly as it does for the palm (see make_palm.py):
0.15 at the roots, 0.75 at the top of the crown, so a bush shivers where a palm sways.
"""

import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy                                                    # noqa: E402
from mathutils import Vector                                  # noqa: E402

from make_palm import (Rng, clear_scene, env, image_material,  # noqa: E402
                       export, make_object, triangle_count)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

VARIANTS = {
    # cards, radius (m), height (m), squash, card size, seed
    "a": dict(cards=13, radius=0.70, height=1.15, card=0.80, seed=5,  tex="leafpatch"),
    "b": dict(cards=11, radius=0.95, height=0.62, card=0.78, seed=13, tex="leafpatch"),
    "c": dict(cards=11, radius=0.85, height=1.45, card=1.35, seed=29, tex="frond"),
}


def add_card(verts, faces, uvs, normals, wind, centre, right, up, uv_rect, wind_lo, wind_hi):
    """One quad and its mirror. `uv_rect` is (u0, v0, u1, v1) into the atlas."""
    u0, v0, u1, v1 = uv_rect
    corners = [(-1, -1, u0, v1), (1, -1, u1, v1), (1, 1, u1, v0), (-1, 1, u0, v0)]
    for flip in (False, True):
        base = len(verts)
        for sx, sy, uu, vv in corners:
            verts.append(centre + right * sx + up * sy)
            uvs.append((uu, vv))
            normals.append(Vector((0, 0, 1)))
            wind.append(wind_lo + (wind_hi - wind_lo) * (0.5 + 0.5 * sy))
        q = (base, base + 1, base + 2, base + 3)
        faces.append((q[::-1] if flip else q, 0))


def build_leaf_bush(v, rng):
    verts, faces, uvs, normals, wind = [], [], [], [], []
    cen = Vector((0, 0, v["height"] * 0.5))
    for i in range(v["cards"]):
        az = 2 * math.pi * (i + rng.f(-0.3, 0.3)) / v["cards"]
        # sit each card on a squashed hemisphere, leaning outward
        r = v["radius"] * rng.f(0.15, 0.78)
        hgt = v["height"] * rng.f(0.24, 0.94)
        c = Vector((math.cos(az) * r, math.sin(az) * r, hgt))
        size = v["card"] * rng.f(0.70, 1.15) * 0.5
        # The card's own facing is twisted off the radial direction and leaned well back on some
        # of them. Cards all square to the centre give a bush that is a flat slab from half the
        # angles you can stand at: one card face-on, its neighbour edge-on, nothing in between.
        fa = az + rng.f(-1.1, 1.1)
        tilt = rng.f(-0.95, 0.45)                 # radians the card leans back from vertical
        out = Vector((math.cos(fa), math.sin(fa), 0.0))
        right = Vector((-math.sin(fa), math.cos(fa), 0.0)) * size
        up = (Vector((0, 0, 1)) * math.cos(tilt) - out * math.sin(tilt)) * size
        # one of the atlas's four patches, and sometimes mirrored, so no two cards repeat
        px, py = rng.f() < 0.5, rng.f() < 0.5
        u0, v0 = (0.5 if px else 0.0), (0.5 if py else 0.0)
        rect = (u0, v0, u0 + 0.5, v0 + 0.5)
        if rng.f() < 0.5:
            rect = (rect[2], rect[1], rect[0], rect[3])
        add_card(verts, faces, uvs, normals, wind, c, right, up, rect, 0.15, 0.75)
    apply_canopy_normals(verts, normals, cen)
    return verts, faces, uvs, normals, wind


def build_thatch(v, rng):
    """A thatch palm: short fronds straight out of the ground, sharing the palm's frond texture.
    Each frond is one tapered card rather than the palm's five-segment strip -- at a metre and a
    half nobody reads the arc, and it keeps the whole plant under fifty triangles."""
    verts, faces, uvs, normals, wind = [], [], [], [], []
    cen = Vector((0, 0, v["height"] * 0.45))
    root = Vector((0, 0, 0.12))
    for i in range(v["cards"]):
        az = 2 * math.pi * i / v["cards"] + rng.f(-0.25, 0.25)
        pitch = math.radians(rng.f(34, 78))
        L = v["card"] * rng.f(0.75, 1.15)
        fwd = Vector((math.cos(az) * math.cos(pitch), math.sin(az) * math.cos(pitch), math.sin(pitch)))
        side = Vector((-math.sin(az), math.cos(az), 0.0))
        base = len(verts)
        rows = []
        for k in range(3):
            t = k / 2.0
            p = root + fwd * (L * t) + Vector((0, 0, -0.30 * (t ** 1.9) * L))
            hw = env(t) * L * 0.21
            row = []
            for sgn in (-1.0, 1.0):
                verts.append(p + side * (sgn * hw))
                uvs.append((t, 0.5 + sgn * 0.49 * env(t)))
                normals.append(Vector((0, 0, 1)))
                wind.append(0.20 + 0.60 * t)
                row.append(len(verts) - 1)
            rows.append(row)
        for k in range(2):
            faces.append(((rows[k][0], rows[k][1], rows[k + 1][1], rows[k + 1][0]), 0))
        # the other side: the same six vertices again, wound the other way
        off = len(verts) - base
        for j in range(base, base + off):
            verts.append(verts[j].copy())
            uvs.append(uvs[j])
            normals.append(normals[j])
            wind.append(wind[j])
        for k in range(2):
            faces.append(((rows[k + 1][0] + off, rows[k + 1][1] + off,
                           rows[k][1] + off, rows[k][0] + off), 0))
    apply_canopy_normals(verts, normals, cen)
    return verts, faces, uvs, normals, wind


def apply_canopy_normals(verts, normals, cen):
    for i, p in enumerate(verts):
        d = p - cen
        d.z += 0.30
        if d.length < 1e-4:
            d = Vector((0, 0, 1))
        normals[i] = (d.normalized() * 0.75 + Vector((0, 0, 1)) * 0.65).normalized()


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", default="a", choices=sorted(VARIANTS))
    ap.add_argument("--out", required=True)
    ap.add_argument("--leafpatch", default=os.path.join(ROOT, "assets/textures/leafpatch.png"))
    ap.add_argument("--frond", default=os.path.join(ROOT, "assets/textures/frond.png"))
    args = ap.parse_args(argv)

    v = VARIANTS[args.variant]
    tex = args.leafpatch if v["tex"] == "leafpatch" else args.frond
    if not os.path.exists(tex):
        sys.exit("bush: missing texture %s" % tex)

    clear_scene()
    rng = Rng(v["seed"])
    if v["tex"] == "frond":
        verts, faces, uvs, normals, wind = build_thatch(v, rng)
    else:
        verts, faces, uvs, normals, wind = build_leaf_bush(v, rng)
    mat = image_material("bush_leaf", tex, alpha=True)
    obj = make_object("bush_" + args.variant, verts, faces, uvs, normals, wind, [mat])
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    for vtx in obj.data.vertices:
        for i in range(3):
            lo[i] = min(lo[i], vtx.co[i])
            hi[i] = max(hi[i], vtx.co[i])
    print("bush %s: %d triangles, %.2f x %.2f x %.2f m"
          % (args.variant, triangle_count(obj), hi.x - lo.x, hi.y - lo.y, hi.z - lo.z))
    export(obj, args.out)
    print("bush: wrote %s (%.0f kB)" % (args.out, os.path.getsize(args.out) / 1024.0))


if __name__ == "__main__":
    main()
