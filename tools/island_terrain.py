#!/usr/bin/env python3
"""Generate the Goonstein Island heightmap, biome colours and scatter.

Writes, relative to the repo root:

    assets/levels/island_terrain_h.png   129x129 RGBA8, 16-bit height packed as R<<8|G
    assets/levels/island_terrain_c.png   129x129 RGBA8, vertex colours
    assets/levels/island_terrain.txt     sidecar: cell / origin / water

and prints (with --scatter) a block of level `prop` lines for the vegetation and rocks, and
(with --report) the terrain height at every named site so props can be placed on the ground.

The engine's terrain grid is fixed at 129x129 vertices (TERRAIN_N) and indexes as
`height[z * 129 + x]`, i.e. **image column = x index, image row = z index**.
Heights are packed as `h in [-64, 192)` -> `[0, 65535]` (see src/terrain_io.c).

Island axes, once and for all:

    world +Z = island EAST      world -Z = island WEST
    world -X = island NORTH     world +X = island SOUTH

so the long axis of the island runs along Z, the dock is at the north-west, the compound sits
on the western shelf and the striped pavilion stands on the eastern high point.

Usage:
    python3 tools/island_terrain.py                 # write the terrain files
    python3 tools/island_terrain.py --report        # + height at every named site
    python3 tools/island_terrain.py --scatter FILE  # + write prop lines to FILE
    python3 tools/island_terrain.py --preview FILE  # + write a top-down PNG to eyeball
"""

import argparse
import math
import os
import random
import sys

from PIL import Image

N = 129                       # TERRAIN_N
CELL = 3.5                    # metres per cell -> 448 m of world
ORIGIN = (-224.0, 0.0, -224.0)
WATER = 0.0
H_MIN, H_RANGE = -64.0, 256.0

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


# --------------------------------------------------------------------------- noise

def _hash(x):
    x &= 0xFFFFFFFF
    x ^= x >> 16
    x = (x * 0x7FEB352D) & 0xFFFFFFFF
    x ^= x >> 15
    x = (x * 0x846CA68B) & 0xFFFFFFFF
    x ^= x >> 16
    return x


def _h2(seed, x, z):
    return (_hash(_hash((x * 73856093) ^ seed) ^ (z * 19349663)) & 0xFFFFFF) / 16777215.0


def vnoise(seed, x, z):
    xi, zi = math.floor(x), math.floor(z)
    fx, fz = x - xi, z - zi
    fx = fx * fx * (3 - 2 * fx)
    fz = fz * fz * (3 - 2 * fz)
    a, b = _h2(seed, xi, zi), _h2(seed, xi + 1, zi)
    c, d = _h2(seed, xi, zi + 1), _h2(seed, xi + 1, zi + 1)
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fz


def fbm(seed, x, z, oct=4):
    s, amp, f, norm = 0.0, 0.5, 1.0, 0.0
    for i in range(oct):
        s += vnoise(seed + i * 101, x * f, z * f) * amp
        norm += amp
        amp *= 0.5
        f *= 2.03
    return s / norm


# --------------------------------------------------------------------------- helpers

def lerp(a, b, t):
    return a + (b - a) * t


def clamp(v, lo, hi):
    return lo if v < lo else (hi if v > hi else v)


def smoothstep(e0, e1, x):
    if e1 == e0:
        return 0.0 if x < e0 else 1.0
    t = clamp((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def curve(points, z):
    """Piecewise-linear interpolation over a sorted list of (z, value)."""
    if z <= points[0][0]:
        return points[0][1]
    if z >= points[-1][0]:
        return points[-1][1]
    for i in range(len(points) - 1):
        z0, v0 = points[i]
        z1, v1 = points[i + 1]
        if z0 <= z <= z1:
            return lerp(v0, v1, (z - z0) / (z1 - z0))
    return points[-1][1]


# --------------------------------------------------------------------------- island shape

# Centreline of the island, drifting south as it runs east.
SPINE_X = [(-160, -4), (-120, -8), (-80, -10), (-40, -6), (0, 0), (40, 6), (80, 10), (120, 6), (160, 2)]

# Half-width: broad at the west end, pinched in the middle, a second bulge east, tapering to points.
HALF_W = [(-158, 0), (-150, 14), (-140, 26), (-130, 37), (-120, 46), (-110, 52), (-100, 55),
          (-90, 57), (-80, 57), (-70, 56), (-60, 54), (-50, 51), (-40, 48), (-30, 45),
          (-20, 42), (-10, 40), (0, 38), (10, 36), (20, 34), (30, 33), (40, 33),
          (50, 35), (60, 38), (70, 42), (80, 45), (90, 46), (100, 46), (110, 44),
          (120, 40), (130, 34), (140, 25), (148, 12), (156, 0)]

# Crest height along the spine: flat western shelf, a saddle, one hill rising to the east point.
CREST = [(-158, 0.5), (-150, 2), (-135, 5), (-120, 9), (-105, 13), (-95, 16), (-85, 18),
         (-75, 18.6), (-60, 19), (-45, 18.4), (-30, 16.5), (-15, 15), (0, 15), (15, 16),
         (30, 18), (45, 21), (60, 24), (75, 26.5), (90, 28.5), (105, 30), (120, 31.5),
         (128, 32), (136, 28), (144, 16), (152, 4), (158, 0.5)]

# How fast the ground climbs inland from the shoreline (metres per metre): gentle at the west
# end, near-vertical along the eastern hill.
SLOPE = [(-158, 0.18), (-145, 0.16), (-130, 0.26), (-115, 0.36), (-100, 0.44), (-80, 0.46),
         (-60, 0.46), (-40, 0.46), (-20, 0.50), (0, 0.58), (20, 0.72), (40, 0.88),
         (60, 0.98), (80, 1.05), (100, 1.10), (120, 1.15), (135, 0.80), (150, 0.30)]

SEED = 20260910


def spine_x(z):
    return curve(SPINE_X, z) + 6.0 * math.sin((z + 160.0) / 92.0)


def half_width(z, ragged=True):
    w = curve(HALF_W, z)
    if not ragged:
        return w
    return w


def coast_wobble(z, side):
    """Ragged coastline: the windward (east / south) side is chewed up, the west is smooth."""
    east = smoothstep(-40.0, 70.0, z)
    amp = lerp(0.05, 0.17, east) * (1.25 if side > 0 else 1.0)
    n = fbm(SEED + (17 if side > 0 else 41), z / 26.0, side * 3.0, 4) - 0.5
    n2 = fbm(SEED + (91 if side > 0 else 5), z / 9.0, side * 7.0, 3) - 0.5
    return 1.0 + amp * (n * 2.0 + n2 * 0.8)


# Beaches. A beach is a shallow apron of `width` metres inland from the waterline rising at
# `rise` metres per metre; past it the ground goes back to the normal slope. Every shore gets the
# default fringe so there is always a line of sand at the water.
#   (name, minx, maxx, minz, maxz, width, rise)
BEACH_DEFAULT = (3.5, 0.13)
BEACHES = [
    ("slack_tide_cove", -38, 4, -156, -124, 26.0, 0.055),   # the landable west cove
    ("north_beach", -82, -42, -70, -24, 15.0, 0.085),       # the decorative north beach
    ("dock_shelf", -88, -56, -126, -100, 5.0, 0.140),
]

# Cliff regions, where the ground comes out of the water almost vertically:
#   (minx, maxx, minz, maxz, factor)
HARD_SHORE = [
    (-70, -6, 20, 110, 1.85),      # the north cliffs under Windward Point
    (10, 70, 96, 150, 1.45),       # the east point
]


def slope_at(x, z, side):
    s = curve(SLOPE, z)
    s *= 1.30 if side < 0 else 0.88          # the north face is the steep one
    for x0, x1, z0, z1, f in HARD_SHORE:
        if x0 <= x <= x1 and z0 <= z <= z1:
            s *= f
    return s


def beach_at(x, z):
    for _n, x0, x1, z0, z1, w, r in BEACHES:
        if x0 <= x <= x1 and z0 <= z <= z1:
            return w, r
    return BEACH_DEFAULT


def shore_rise(x, z, side, inland):
    """Height above the waterline `inland` metres in from the shore: beach apron, then slope."""
    bw, br = beach_at(x, z)
    if inland <= bw:
        return inland * br
    return bw * br + (inland - bw) * slope_at(x, z, side)


Z_MIN, Z_MAX = HALF_W[0][0], HALF_W[-1][0]

# Great Goonstein, the empty neighbour across the cut: skybox scenery, never landed on.
NEIGHBOUR = (-178.0, -168.0, 62.0, 34.0)      # x, z, radius, peak


def neighbour_height(x, z):
    nx, nz, r, peak = NEIGHBOUR
    d = math.hypot(x - nx, z - nz) / r
    if d >= 1.35:
        return None
    lump = fbm(SEED + 313, x / 21.0, z / 21.0, 4)
    d /= 0.78 + lump * 0.46
    h = peak * (1.0 - smoothstep(0.10, 1.0, d)) - 3.0
    h += (fbm(SEED + 77, x / 24.0, z / 24.0, 3) - 0.5) * 5.0 * smoothstep(0.0, 6.0, h)
    return h


def base_height(x, z):
    xc = spine_x(z)
    side = 1.0 if x >= xc else -1.0
    w = half_width(z) * coast_wobble(z, side)
    if w <= 0.1:
        w = 0.1
    inland = w - abs(x - xc)                  # metres from the waterline, negative offshore
    # past the tips the cross-section is meaningless: fall away in 2D instead
    dz = (Z_MIN - z) if z < Z_MIN else ((z - Z_MAX) if z > Z_MAX else 0.0)
    if dz > 0.0:
        inland = -math.hypot(dz, max(0.0, -inland))
    crest = curve(CREST, z)
    if inland >= 0:
        h = min(crest, shore_rise(x, z, side, inland))
        # rounded shoulders instead of a hard cap where the slope meets the crest
        h = crest - (crest - h) ** 1.0
        detail = (fbm(SEED, x / 34.0, z / 34.0, 4) - 0.5) * 3.2 * smoothstep(1.0, 8.0, h)
        detail += (fbm(SEED + 7, x / 11.0, z / 11.0, 3) - 0.5) * 1.1 * smoothstep(2.0, 12.0, h)
        return h + detail
    # seafloor: a shelf, then away into the dark
    d = -inland
    return -(1.0 - math.exp(-d / 26.0)) * 19.0 - d * 0.03


def island_height(x, z):
    h = base_height(x, z)
    nb = neighbour_height(x, z)
    if nb is not None and nb > h:
        h = nb
    return h


# --------------------------------------------------------------------------- named sites

# name, x, z, radius, height, kind
PADS = [
    ("dock_mole",      -69, -112, 2.5, 1.6, "rect", (-82, -56, -116.5, -108.5)),
    ("dock_ramp",      -52, -104, 8, 5.5, "circle", None),
    ("helipad",        -48,  -92, 6.0, 10.0, "rect", (-62, -34, -104, -80)),
    ("villa_terrace",  -16,  -70, 0, 18.0, "rect", (-34, 2, -84, -56)),
    ("pool_terrace",    20,  -48, 0, 16.6, "rect", (6, 34, -64, -32)),
    ("bunkhouse",      -32,  -32, 13, 15.2, "circle", None),
    ("water_tower",     -2,   -4, 10, 15.0, "circle", None),
    ("lookout",        -22,   46, 9, 20.5, "circle", None),
    ("service_yard",    37,   88, 6.0, 17.0, "rect", (26, 48, 78, 100)),
    # the cutting into the hillside behind the service shed: the placeholder for the descent
    ("tunnel_cut",       0,    0, 3.0, 17.0, "rect", (10, 27, 86.0, 93.0)),
    ("bermed_rect",     32,  113, 6.0, 22.0, "rect", (20, 44, 102, 124)),
    ("temple",           0,  126, 20, 31.0, "circle", None),
    ("cove_beach",     -14, -140, 15, 1.1, "circle", None),
]

# Golf-cart roads. Each is a polyline; the terrain is levelled and painted along it.
PATHS = [
    ("dock_road",   [(-66, -112), (-56, -107), (-46, -98), (-34, -85), (-24, -76), (-17, -71)], 6.0),
    ("helipad_spur", [(-36, -86), (-43, -90), (-47, -92)], 6.0),
    ("cove_road",   [(-26, -80), (-24, -98), (-19, -118), (-14, -134)], 6.0),
    ("pool_road",   [(-14, -66), (-2, -60), (10, -53)], 7.0),
    ("spine_west",  [(14, -48), (8, -30), (0, -14), (-2, -6)], 7.0),
    ("bunk_spur",   [(-4, -18), (-16, -25), (-28, -30)], 6.0),
    ("spine_east",  [(-2, -4), (2, 16), (6, 38), (4, 62), (2, 86), (0, 108), (0, 122)], 7.0),
    ("lookout_spur", [(5, 44), (-6, 45), (-18, 46)], 6.0),
    ("service_spur", [(4, 68), (16, 76), (28, 82), (37, 85)], 6.0),
    ("berm_spur",   [(40, 94), (36, 102), (33, 107)], 6.0),
]


MARKS = [
    ("Slack Tide Cove", -14, -140), ("Pelican Pier", -70, -112), ("Pad One", -48, -92),
    ("Villa Ambergris", -16, -68), ("The Oval", 16, -48), ("Cabana Row", 30, -48),
    ("The Bunkhouse", -32, -32), ("The Cistern", -2, -4), ("Windward Point", -22, 46),
    ("Utility Two", 34, 89), ("the Culvert", 14, 89), ("The Court", 32, 113),
    ("The Music Room", 0, 126), ("Great Goonstein", -178, -168),
]


# --------------------------------------------------------------------------- build

def build():
    h = [[0.0] * N for _ in range(N)]         # h[row=z][col=x]
    for zi in range(N):
        wz = ORIGIN[2] + zi * CELL
        for xi in range(N):
            wx = ORIGIN[0] + xi * CELL
            h[zi][xi] = island_height(wx, wz)

    # --- pads: level ground for everything that is built ---
    for name, px, pz, r, ph, kind, rect in PADS:
        for zi in range(N):
            wz = ORIGIN[2] + zi * CELL
            for xi in range(N):
                wx = ORIGIN[0] + xi * CELL
                if kind == "circle":
                    d = math.hypot(wx - px, wz - pz)
                    t = 1.0 - smoothstep(r * 0.62, r, d)
                else:
                    x0, x1, z0, z1 = rect
                    dx = max(x0 - wx, 0.0, wx - x1)
                    dz = max(z0 - wz, 0.0, wz - z1)
                    d = math.hypot(dx, dz)
                    t = 1.0 - smoothstep(0.0, r if r > 0 else 5.0, d)
                if t > 0:
                    h[zi][xi] = lerp(h[zi][xi], ph, t)

    # --- roads: sample the ground at each node, smooth it, then cut the corridor ---
    def height_at(wx, wz):
        u = clamp((wx - ORIGIN[0]) / CELL, 0, N - 1.001)
        v = clamp((wz - ORIGIN[2]) / CELL, 0, N - 1.001)
        xi, zi = int(u), int(v)
        fx, fz = u - xi, v - zi
        a = lerp(h[zi][xi], h[zi][xi + 1], fx)
        b = lerp(h[zi + 1][xi], h[zi + 1][xi + 1], fx)
        return lerp(a, b, fz)

    road_pts = []        # (x, z, height, halfwidth)
    for _name, pts, width in PATHS:
        dense = []
        for i in range(len(pts) - 1):
            (ax, az), (bx, bz) = pts[i], pts[i + 1]
            seg = max(2, int(math.hypot(bx - ax, bz - az) / 1.5))
            for k in range(seg):
                t = k / seg
                dense.append((lerp(ax, bx, t), lerp(az, bz, t)))
        dense.append(pts[-1])
        hs = [height_at(x, z) for x, z in dense]
        for _ in range(6):                                   # smooth the profile along the road
            hs = [hs[0]] + [(hs[i - 1] + 2 * hs[i] + hs[i + 1]) / 4 for i in range(1, len(hs) - 1)] + [hs[-1]]
        for (x, z), hh in zip(dense, hs):
            road_pts.append((x, z, hh, width * 0.5))

    road_mask = [[0.0] * N for _ in range(N)]
    for zi in range(N):
        wz = ORIGIN[2] + zi * CELL
        for xi in range(N):
            wx = ORIGIN[0] + xi * CELL
            best_t, best_h = 0.0, 0.0
            for rx, rz, rh, hw in road_pts:
                if abs(rx - wx) > hw + 6 or abs(rz - wz) > hw + 6:
                    continue
                d = math.hypot(rx - wx, rz - wz)
                t = 1.0 - smoothstep(hw, hw + 5.0, d)
                if t > best_t:
                    best_t, best_h = t, rh
            if best_t > 0:
                h[zi][xi] = lerp(h[zi][xi], best_h, best_t * 0.92)
                road_mask[zi][xi] = best_t

    return h, road_mask


# --------------------------------------------------------------------------- colour

SAND = (0.86, 0.80, 0.64)
WET_SAND = (0.64, 0.58, 0.46)
SEABED = (0.28, 0.32, 0.30)
DEEP = (0.10, 0.14, 0.18)
SCRUB_LOW = (0.38, 0.50, 0.27)
SCRUB_HIGH = (0.32, 0.42, 0.24)
DRY_GRASS = (0.50, 0.50, 0.30)
ROCK = (0.42, 0.40, 0.36)
DARK_ROCK = (0.30, 0.29, 0.27)
LAWN = (0.33, 0.48, 0.25)
PAVING = (0.70, 0.68, 0.63)
ROAD = (0.60, 0.58, 0.53)


def mix(a, b, t):
    return tuple(lerp(a[i], b[i], t) for i in range(3))


LAWNS = [(-16, -68, 30), (14, -50, 22)]


def colourise(h, road_mask):
    col = [[(0, 0, 0)] * N for _ in range(N)]
    for zi in range(N):
        wz = ORIGIN[2] + zi * CELL
        for xi in range(N):
            wx = ORIGIN[0] + xi * CELL
            hh = h[zi][xi]
            # slope, in metres per metre
            hl = h[zi][max(0, xi - 1)]
            hr = h[zi][min(N - 1, xi + 1)]
            hd = h[max(0, zi - 1)][xi]
            hu = h[min(N - 1, zi + 1)][xi]
            slope = math.hypot(hr - hl, hu - hd) / (2 * CELL)

            if hh < -8:
                c = mix(SEABED, DEEP, smoothstep(-8, -22, hh))
            elif hh < 0:
                c = mix(WET_SAND, SEABED, smoothstep(0, -8, hh))
            elif hh < 1.8:
                c = mix(WET_SAND, SAND, smoothstep(0.0, 0.9, hh))
            else:
                veg = mix(SCRUB_LOW, SCRUB_HIGH, smoothstep(6.0, 26.0, hh))
                dry = fbm(SEED + 3, wx / 40.0, wz / 40.0, 3)
                veg = mix(veg, DRY_GRASS, smoothstep(0.50, 0.80, dry) * 0.40)
                rock = mix(ROCK, DARK_ROCK, smoothstep(0.8, 1.6, slope))
                c = mix(veg, rock, smoothstep(0.42, 0.95, slope))
                c = mix(SAND, c, smoothstep(1.8, 3.2, hh))

            for lx, lz, lr in LAWNS:
                t = 1.0 - smoothstep(lr * 0.55, lr, math.hypot(wx - lx, wz - lz))
                if t > 0 and hh > 2.0:
                    c = mix(c, LAWN, t * 0.85)

            rm = road_mask[zi][xi]
            if rm > 0 and hh > 1.0:
                c = mix(c, ROAD, smoothstep(0.55, 0.95, rm))

            # pale paving on the built pads
            for name, px, pz, r, _ph, kind, rect in PADS:
                if kind != "circle" or name in ("cove_beach",):
                    continue
                t = 1.0 - smoothstep(r * 0.25, r * 0.70, math.hypot(wx - px, wz - pz))
                if t > 0:
                    c = mix(c, PAVING, t * 0.55)

            n = fbm(SEED + 11, wx / 7.0, wz / 7.0, 2) - 0.5
            c = tuple(clamp(v * (1.0 + n * 0.10), 0.0, 1.0) for v in c)
            col[zi][xi] = c
    return col


# --------------------------------------------------------------------------- output

def encode(hv):
    t = (hv - H_MIN) / H_RANGE * 65535.0
    return int(clamp(t + 0.5, 0, 65535))


def write_terrain(h, col):
    him = Image.new("RGBA", (N, N))
    cim = Image.new("RGBA", (N, N))
    hp, cp = him.load(), cim.load()
    for zi in range(N):
        for xi in range(N):
            v = encode(h[zi][xi])
            hp[xi, zi] = (v >> 8, v & 0xFF, 0, 255)
            c = col[zi][xi]
            cp[xi, zi] = (int(c[0] * 255 + 0.5), int(c[1] * 255 + 0.5), int(c[2] * 255 + 0.5), 255)
    him.save(os.path.join(ROOT, "assets/levels/island_terrain_h.png"))
    cim.save(os.path.join(ROOT, "assets/levels/island_terrain_c.png"))
    with open(os.path.join(ROOT, "assets/levels/island_terrain.txt"), "w") as f:
        f.write("# Goonstein Island -- generated by tools/island_terrain.py, do not hand-edit\n")
        f.write("cell %g\n" % CELL)
        f.write("origin %g %g %g\n" % ORIGIN)
        f.write("water %g\n" % WATER)


def write_preview(h, col, path, annotate=True):
    """Hill-shaded top-down map. The engine's camera far plane is 80 m, so this is currently the
    only way to see the whole island at once (see ISLAND_BUILD.md, "Needs code")."""
    S = 5
    W = N * S
    im = Image.new("RGB", (W, W))
    p = im.load()
    for zi in range(N):
        for xi in range(N):
            hh = h[zi][xi]
            c = col[zi][xi]
            if hh < WATER:
                d = clamp(-hh / 20.0, 0, 1)
                c = (lerp(0.20, 0.05, d), lerp(0.44, 0.12, d), lerp(0.58, 0.26, d))
            else:                                     # cheap hillshade
                hl = h[zi][max(0, xi - 1)]
                hu = h[min(N - 1, zi + 1)][xi]
                sh = clamp(0.72 + (hl - hh) * 0.10 + (hu - hh) * 0.06, 0.35, 1.35)
                c = tuple(clamp(v * sh, 0, 1) for v in c)
            rgb = (int(c[0] * 255), int(c[1] * 255), int(c[2] * 255))
            for dz in range(S):
                for dx in range(S):
                    p[xi * S + dx, zi * S + dz] = rgb

    if not annotate:
        im.save(path)
        return

    from PIL import ImageDraw
    d = ImageDraw.Draw(im)

    def px(x, z):
        return ((x - ORIGIN[0]) / CELL * S, (z - ORIGIN[2]) / CELL * S)

    for name, x, z in MARKS:
        cx, cy = px(x, z)
        d.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], fill=(255, 240, 210), outline=(20, 20, 20))
        d.text((cx + 7, cy - 5), name, fill=(255, 255, 255))
        d.text((cx + 8, cy - 4), name, fill=(20, 20, 20))
        d.text((cx + 7, cy - 5), name, fill=(255, 250, 235))

    # 100 m scale bar and a north arrow (island north is world -X, i.e. image left)
    x0, y0 = px(-210, 200)
    d.rectangle([x0, y0, x0 + 100.0 / CELL * S, y0 + 5], fill=(250, 250, 250))
    d.text((x0, y0 - 14), "100 m", fill=(250, 250, 250))
    ax, ay = px(-190, -196)
    d.line([ax, ay, ax - 40, ay], fill=(250, 250, 250), width=3)
    d.polygon([(ax - 52, ay), (ax - 38, ay - 6), (ax - 38, ay + 6)], fill=(250, 250, 250))
    d.text((ax - 60, ay + 8), "N", fill=(250, 250, 250))
    d.text((8, 8), "GOONSTEIN ISLAND   %d x %d m   summit %.0f m   sea level 0"
           % (302, 114, max(max(r) for r in h)), fill=(250, 250, 250))
    im.save(path)


# --------------------------------------------------------------------------- scatter

# What gets sown. The palms are ours (assets/models/own/*.part, retextured with a real bark and
# leaf map); everything else is a photoscanned Poly Haven variant split out of its set by
# tools/gltf_split.py, so one prop line is one bush and not a whole shop display. Each entry is
# (file, low scale, high scale) with the scale range chosen to land the scan on the metre size the
# old stylised piece used to occupy -- a 1.4 m scan of a bush at 1.3 reads as a 1.8 m bush.
PH = "models/polyhaven/"

PALMS_TALL = [("models/own/palm_tall.part", 0.85, 1.22)]
PALMS_BENT = [("models/own/palm_bent.part", 0.85, 1.22)]

# Coastal thicket: sea-grape-scale bushes, 1.0 to 2.2 m as scanned.
BUSHES = [(PH + "shrub_02/shrub_02_a.gltf", 1.05, 1.90),
          (PH + "shrub_02/shrub_02_b.gltf", 1.15, 2.10),
          (PH + "shrub_02/shrub_02_c.gltf", 0.95, 1.70),
          (PH + "shrub_02/shrub_02_d.gltf", 1.20, 2.20),
          (PH + "pachira_aquatica_01/pachira_aquatica_01_b.gltf", 1.60, 2.90),
          (PH + "pachira_aquatica_01/pachira_aquatica_01_c.gltf", 1.30, 2.40)]

# Agave-scale succulents on the dry high ground, where the stylised saguaro used to stand:
# a 0.5 m scan at 3x is a 1.5 m rosette, which is what actually grows on a limestone cay.
AGAVES = [(PH + "cheiridopsis_succulent/cheiridopsis_succulent_a.gltf", 2.2, 4.0),
          (PH + "cheiridopsis_succulent/cheiridopsis_succulent_d.gltf", 2.0, 3.6),
          (PH + "cheiridopsis_succulent/cheiridopsis_succulent_g.gltf", 1.8, 3.2),
          (PH + "cheiridopsis_succulent/cheiridopsis_succulent_i.gltf", 1.8, 3.2),
          (PH + "cheiridopsis_succulent/cheiridopsis_succulent_h.gltf", 2.0, 3.6)]

# Shoreline boulders. The mossy set is tinted back toward bleached limestone: nothing on a
# windward Caribbean shore is that green.
ROCKS = [(PH + "rock_moss_set_02/rock_moss_set_02_a.gltf", 1.0, 2.0),
         (PH + "rock_moss_set_02/rock_moss_set_02_c.gltf", 1.1, 2.2),
         (PH + "rock_moss_set_02/rock_moss_set_02_e.gltf", 1.1, 2.2),
         (PH + "rock_moss_set_02/rock_moss_set_02_g.gltf", 1.0, 1.9),
         (PH + "rock_moss_set_01/rock_moss_set_01_a.gltf", 0.9, 1.7),
         (PH + "rock_moss_set_01/rock_moss_set_01_d.gltf", 0.8, 1.5),
         (PH + "rock_moss_set_01/rock_moss_set_01_f.gltf", 1.0, 2.0),
         (PH + "boulder_01/boulder_01_1k.glb", 1.2, 2.6)]
ROCK_TINT = " tint 0.92 0.88 0.78"

# Ground cover, sown last and small: it exists to break the biome colour up under the player's
# feet and is the first thing the size cull drops (src/props.c, PROP_CULL_SIZE).
GROUND = [(PH + "grass_medium_02/grass_medium_02_b.gltf", 1.4, 2.6),
          (PH + "grass_medium_02/grass_medium_02_d.gltf", 1.2, 2.2),
          (PH + "grass_medium_02/grass_medium_02_e.gltf", 1.0, 1.9),
          (PH + "weed_plant_02/weed_plant_02_a.gltf", 0.9, 1.5),
          (PH + "crystalline_iceplant/crystalline_iceplant_a.gltf", 1.2, 2.2),
          (PH + "crystalline_iceplant/crystalline_iceplant_e.gltf", 1.3, 2.4),
          (PH + "fern_02/fern_02_a.gltf", 0.9, 1.7),
          (PH + "fern_02/fern_02_c.gltf", 0.9, 1.7)]


def sample(h, wx, wz):
    u = clamp((wx - ORIGIN[0]) / CELL, 0, N - 1.001)
    v = clamp((wz - ORIGIN[2]) / CELL, 0, N - 1.001)
    xi, zi = int(u), int(v)
    fx, fz = u - xi, v - zi
    a = lerp(h[zi][xi], h[zi][xi + 1], fx)
    b = lerp(h[zi + 1][xi], h[zi + 1][xi + 1], fx)
    return lerp(a, b, fz)


def slope_sample(h, wx, wz):
    e = CELL
    return math.hypot(sample(h, wx + e, wz) - sample(h, wx - e, wz),
                      sample(h, wx, wz + e) - sample(h, wx, wz - e)) / (2 * e)


def near_road(wx, wz, road_mask):
    u = int(round(clamp((wx - ORIGIN[0]) / CELL, 0, N - 1)))
    v = int(round(clamp((wz - ORIGIN[2]) / CELL, 0, N - 1)))
    return road_mask[v][u] > 0.35


def near_pad(wx, wz, extra=0.0):
    for _name, px, pz, r, _ph, kind, rect in PADS:
        if kind == "circle":
            if math.hypot(wx - px, wz - pz) < r * 0.9 + extra:
                return True
        else:
            x0, x1, z0, z1 = rect
            if x0 - extra < wx < x1 + extra and z0 - extra < wz < z1 + extra:
                return True
    return False


def scatter(h, road_mask):
    rng = random.Random(4242)
    out = []
    placed = []

    def free(x, z, sep):
        # two pieces need the larger of their two clearances between them, so a bush is allowed
        # to grow closer to a palm than two palms are to each other
        for px, pz, ps in placed:
            if math.hypot(px - x, pz - z) < (sep if sep > ps else ps):
                return False
        return True

    def put(model, x, z, yaw, scale, sep, extra=""):
        y = sample(h, x, z)
        placed.append((x, z, sep))
        out.append("prop %s %.2f %.2f %.2f %.1f %.2f%s" % (model, x, y, z, yaw, scale, extra))

    def sow(target, tries, sep, pick, weight):
        """Rejection-sample `target` positions over the island."""
        n = 0
        for _ in range(tries):
            if n >= target:
                break
            x = rng.uniform(-100, 82)
            z = rng.uniform(-170, 172)
            y = sample(h, x, z)
            w = weight(x, z, y)
            if w <= 0 or rng.random() > w:
                continue
            if not free(x, z, sep):
                continue
            model, scale, extra = pick(rng, x, z, y)
            put(model, x, z, rng.uniform(0, 360), scale, sep, extra)
            n += 1
        return n

    def from_pool(pool, extra=""):
        def pick(r, x, z, y):
            f, lo, hi = pool[r.randrange(len(pool))]
            return (f, r.uniform(lo, hi), extra)
        return pick

    # --- coconut palms: the planted rows near the compound, wild stands behind the beaches ---
    def palm_w(x, z, y):
        if y < 1.2 or y > 17:
            return 0.0
        if slope_sample(h, x, z) > 0.5:
            return 0.0
        w = 0.35 + 0.55 * smoothstep(15.0, 2.5, y)
        w *= 0.35 + 0.9 * smoothstep(0.30, 0.70, fbm(SEED + 51, x / 40.0, z / 40.0, 3))
        if near_road(x, z, road_mask):
            w *= 0.30
        if near_pad(x, z, 1.0):
            w *= 0.20
        return w

    def palm_pick(rng, x, z, y):
        bent = y < 5.0 and rng.random() < 0.45
        pool = PALMS_BENT if bent else PALMS_TALL
        f, lo, hi = pool[0]
        return (f, rng.uniform(lo, hi), "")

    n_palm = sow(104, 60000, 6.0, palm_pick, palm_w)

    # --- dry scrub: what actually covers a USVI cay ---
    def scrub_w(x, z, y):
        if y < 1.8 or y > 33:
            return 0.0
        sl = slope_sample(h, x, z)
        if sl > 1.05:
            return 0.0
        w = 0.85 * (1.0 - smoothstep(0.6, 1.05, sl))
        # thickets, not lawn: a moisture field so the bushes clump and leave clearings between
        w *= 0.20 + 1.15 * smoothstep(0.34, 0.72, fbm(SEED + 77, x / 26.0, z / 26.0, 3))
        if near_road(x, z, road_mask):
            w *= 0.10
        if near_pad(x, z, 0.5):
            w *= 0.18
        return w

    # --- agaves on the dry, steep, high ground (where the stylised cactus used to be) ---
    def agave_w(x, z, y):
        if y < 7 or y > 32:
            return 0.0
        sl = slope_sample(h, x, z)
        if sl < 0.22 or sl > 0.95:
            return 0.0
        if near_road(x, z, road_mask) or near_pad(x, z, 1.5):
            return 0.0
        return 0.55

    n_cact = sow(38, 40000, 5.0, from_pool(AGAVES), agave_w)

    # --- boulders on the shoreline and the cliff shoulders ---
    def rock_w(x, z, y):
        if y < -2.2 or y > 30:
            return 0.0
        sl = slope_sample(h, x, z)
        shore = 1.0 - smoothstep(1.2, 6.0, abs(y - 0.4))
        steep = smoothstep(0.7, 1.4, sl)
        w = max(shore * 0.8, steep * 0.5)
        if near_road(x, z, road_mask) or near_pad(x, z, 0.5):
            return 0.0
        return w

    n_rock = sow(70, 50000, 5.5, from_pool(ROCKS, ROCK_TINT), rock_w)

    n_scrub = sow(480, 200000, 2.6, from_pool(BUSHES), scrub_w)

    # --- ground cover: tufts and weeds in the gaps the bushes left ---
    def ground_w(x, z, y):
        if y < 1.5 or y > 30:
            return 0.0
        if slope_sample(h, x, z) > 1.1:
            return 0.0
        w = 0.7
        if near_road(x, z, road_mask):
            w *= 0.25
        if near_pad(x, z, 0.5):
            w *= 0.15
        return w

    n_ground = sow(150, 90000, 1.9, from_pool(GROUND), ground_w)

    print("scatter: %d palms, %d bushes, %d agaves, %d rocks, %d ground cover (%d props)"
          % (n_palm, n_scrub, n_cact, n_rock, n_ground, len(out)))
    return out


# --------------------------------------------------------------------------- main

REPORT_POINTS = [
    ("spawn (mole, landward)", -58, -112), ("dock house (mole end)", -79, -112),
    ("boat mooring", -70, -102), ("mole n edge", -68, -104), ("dock ramp", -52, -104),
    ("helipad centre", -48, -92), ("helipad shed A", -57, -84), ("helipad shed B", -39, -84),
    ("villa centre", -16, -70), ("villa wing W", -33, -66), ("villa wing E", -1, -66),
    ("courtyard", -16, -80), ("villa terrace SE", 0, -58),
    ("pool centre", 16, -48), ("pool house", 8, -58),
    ("cabana 1", 30, -60), ("cabana 2", 30, -52), ("cabana 3", 30, -44), ("cabana 4", 30, -36),
    ("bunkhouse", -32, -32), ("water tower", -2, -4),
    ("lookout", -22, 46), ("lookout edge", -27, 46),
    ("service yard", 38, 88), ("service shed", 30, 89.5), ("tunnel mouth", 26, 89.5),
    ("tunnel inner", 13, 89.5), ("bermed rect centre", 32, 113),
    ("temple centre", 0, 126), ("temple approach", 0, 112),
    ("cove beach", -14, -140), ("cove west", -24, -142),
    ("saddle", -2, -6), ("hill mid", 4, 40), ("hill high", 2, 90),
    ("sea NW", -100, -120), ("sea E", 0, 175),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--scatter", metavar="FILE")
    ap.add_argument("--preview", metavar="FILE")
    args = ap.parse_args()

    h, road_mask = build()
    col = colourise(h, road_mask)
    write_terrain(h, col)

    lo = min(min(r) for r in h)
    hi = max(max(r) for r in h)
    land = sum(1 for r in h for v in r if v > 0)
    print("terrain: cell %g  span %g m  height %.1f .. %.1f  land %d/%d cells (%.0f m2)"
          % (CELL, (N - 1) * CELL, lo, hi, land, N * N, land * CELL * CELL))

    if args.preview:
        write_preview(h, col, args.preview)
        print("preview: " + args.preview)

    if args.report:
        print("\n%-26s %8s %8s %8s" % ("site", "x", "z", "ground y"))
        for name, x, z in REPORT_POINTS:
            print("%-26s %8.1f %8.1f %8.2f" % (name, x, z, sample(h, x, z)))

    if args.scatter:
        lines = scatter(h, road_mask)
        with open(args.scatter, "w") as f:
            f.write("# generated by tools/island_terrain.py --scatter -- do not hand-edit\n")
            f.write("\n".join(lines) + "\n")
        print("scatter: %d props -> %s" % (len(lines), args.scatter))


if __name__ == "__main__":
    sys.exit(main())
