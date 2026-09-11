"""Utility structures: the cistern tower and the dock house -- both frames, not boxes.

Neither building is a plastered room, so neither uses `Walls`: a lattice tower has no walls to
punch a door through, and an open-sided dock shelter is a frame you can see clean through on
three sides. Both are built the same way as everything else here -- named groups of solids, each
with one skin -- just without `Walls` doing the cutting for them.

LOCAL HELPERS
-------------
`shelllib.post` gives a round prism, but neither building is only posts:

  * `_strut(p0, p1, t)` -- a square-section timber or steel member running straight between any
    two 3-D points. `post`/`slab` only stand straight up; a knee brace, a rafter or a diagonal
    cross-brace runs at an angle in a plane shelllib has no name for, so this turns a plain box
    into whichever line segment is asked for by rotating it -- yaw about Z, then pitch about Y --
    from lying along +X onto (p1 - p0). It is the one building block under both structures below.
"""

import math

import cadquery as cq

from shelllib import gable_roof, ladder, post, ridge_tile, shell, skin, slab, wall_ring


def _strut(p0, p1, t=0.09):
    """A straight t x t member from 3-D point p0 to p1, in the shell's CAD frame (Z up)."""
    x0, y0, z0 = p0
    x1, y1, z1 = p1
    dx, dy, dz = x1 - x0, y1 - y0, z1 - z0
    length = math.sqrt(dx * dx + dy * dy + dz * dz)
    if length < 1e-6:
        raise ValueError("_strut: p0 and p1 coincide")
    horiz = math.hypot(dx, dy)
    yaw = math.degrees(math.atan2(dy, dx))
    pitch = math.degrees(math.atan2(dz, horiz))
    mid = ((x0 + x1) / 2.0, (y0 + y1) / 2.0, (z0 + z1) / 2.0)
    return (cq.Workplane("XY").box(length, t, t, centered=True)
            .rotate((0, 0, 0), (0, 1, 0), -pitch)
            .rotate((0, 0, 0), (0, 0, 1), yaw)
            .translate(mid))


# ======================================================================== the cistern tower
# Four legs to a round platform, a riveted tank standing on it, a ladder up one leg and onto the
# platform, and the downpipe. The 16-gon `post` prisms read round at the distance this is seen
# from and cost a fraction of a tessellated cylinder -- the whole tower is a few hundred
# triangles against its 9000 budget.
LEG_R = 3.00                    # leg centres, on both X and Y -- a square, corners at (+-LEG_R)
LEG_RAD = 0.20                  # leg post radius
PLAT_Z = 7.50                   # platform TOP -- island.txt's collider and the cistern_cap kit
                                 # piece both assume the platform and tank are exactly here
PLAT_R = 4.60                   # 9.2 m across
PLAT_T = 0.28
TANK_R = 2.35                   # matches the cistern_cap kit piece's own base_r of 2.40
TANK_H = 3.65                   # tank stands 3.65 m tall: top lands at 11.15, right where the
                                 # cap (placed by island.txt at +11.20) sits down onto it
SIDES = 16                      # "a 16-sided tank is plenty at this distance" -- the brief

WT_SKINS = {
    "trim":  skin("corrugated", 1.0, (2.02, 2.09, 2.27)),   # legs + platform
    "wall":  skin("corrugated", 1.2, (2.07, 2.15, 2.30)),   # the tank body
    "band":  skin("corrugated", 1.5, (1.58, 1.65, 1.76)),   # the proud rolled hoops
    "metal": skin("corrugated", 2.2, (1.73, 1.74, 1.82)),   # bracing, ladder, handrail
    "dark":  skin(tint=(0.45, 0.28, 0.18)),                 # the downpipe, bare tinted metal
}


def _tower_legs():
    corners = [(sx * LEG_R, sy * LEG_R) for sx in (1, -1) for sy in (1, -1)]
    return [post(x, y, LEG_RAD, PLAT_Z, z0=0.0, sides=8) for x, y in corners]


def _face_point(face, u):
    """A point on one of the square's four sides, `u` along it, at leg radius LEG_R."""
    if face == "front":
        return (u, LEG_R)
    if face == "back":
        return (u, -LEG_R)
    if face == "left":
        return (-LEG_R, u)
    return (LEG_R, u)                # right


def _cross_braces(z_lo, z_hi, t=0.09):
    out = []
    for face in ("front", "back", "left", "right"):
        a0 = _face_point(face, -LEG_R) + (z_lo,)
        a1 = _face_point(face, LEG_R) + (z_hi,)
        b0 = _face_point(face, -LEG_R) + (z_hi,)
        b1 = _face_point(face, LEG_R) + (z_lo,)
        out.append(_strut(a0, a1, t))
        out.append(_strut(b0, b1, t))
    return out


def _hoop(z, over=0.06, h=0.15):
    return post(0.0, 0.0, TANK_R + over, h, z0=z, sides=SIDES)


def _handrail(r, z, gap_face="front", t_post=0.035, t_rail=0.05, rail_h=0.85, n=16):
    """Balusters and a top rail round most of the platform's rim, left open over `gap_face` for
    the ladder to land on."""
    posts_out, rail_pts = [], []
    for i in range(n):
        ang = 2 * math.pi * i / n
        x, y = r * math.cos(ang), r * math.sin(ang)
        # skip the arc in front of the ladder so there is a way onto the platform
        if gap_face == "front" and y > r * 0.55 and abs(x) < r * 0.55:
            continue
        posts_out.append(post(x, y, t_post, rail_h, z0=z, sides=6))
        rail_pts.append((x, y))
    rails = []
    for (x0, y0), (x1, y1) in zip(rail_pts, rail_pts[1:]):
        if math.hypot(x1 - x0, y1 - y0) < r * 0.5:      # only bridge neighbours, not the gap
            rails.append(_strut((x0, y0, z + rail_h), (x1, y1, z + rail_h), t_rail))
    return posts_out + rails


@shell("water_tower", tris=9000, skins=WT_SKINS,
       note="cistern tower: 4-leg lattice, 16-gon riveted tank on a 9.2 m platform, real ladder")
def water_tower():
    """The island's water tower. Front (+Y) carries the ladder up to the platform."""
    g = {k: [] for k in WT_SKINS}

    # -- the lattice: four legs and two levels of X cross-bracing
    g["trim"] += _tower_legs()
    g["metal"] += _cross_braces(1.6, 3.6)
    g["metal"] += _cross_braces(4.4, 6.4)

    # -- the platform the tank stands on
    g["trim"].append(post(0.0, 0.0, PLAT_R, PLAT_T, z0=PLAT_Z - PLAT_T, sides=SIDES))

    # -- the tank: a 16-sided prism, a rolled bottom edge, three proud hoops
    tank = post(0.0, 0.0, TANK_R, TANK_H, z0=PLAT_Z, sides=SIDES)
    tank = tank.edges("<Z").chamfer(0.10)
    g["wall"].append(tank)
    g["band"].append(_hoop(PLAT_Z + 0.75))
    g["band"].append(_hoop(PLAT_Z + 1.85))
    g["band"].append(_hoop(PLAT_Z + 2.95))

    # -- handrail round the platform, open toward the ladder
    g["metal"] += _handrail(PLAT_R - 0.30, PLAT_Z, gap_face="front")

    # -- a real ladder up the front-right leg and a short grab-rail on past the platform edge
    ladder_x, ladder_y = LEG_R, LEG_R + 0.28
    g["metal"] += ladder(ladder_x, ladder_y, PLAT_Z, width=0.42, z0=0.0, pitch=0.30)
    g["metal"] += [slab(0.05, 0.05, 0.9, z0=PLAT_Z, x=ladder_x - 0.21, y=ladder_y),
                   slab(0.05, 0.05, 0.9, z0=PLAT_Z, x=ladder_x + 0.21, y=ladder_y)]

    # -- the downpipe, clear of the ladder side
    g["dark"].append(post(0.0, -LEG_R - 0.35, 0.08, PLAT_Z, z0=0.0, sides=8))

    return g


# ======================================================================== the dock house
# An open timber frame on the stone quay: sill and head beams, four posts, knee braces at every
# corner, purlins and rafters under a corrugated gable roof with a real overhang, a dozen
# individually-gapped deck boards, rails on the three open sides and a boarded back wall. The
# bench, coiled rope, hanging lamp and mooring cleats are not fabric -- they live in
# extras/dock_house.part -- and the kit's own stair_run and lamp_bracket stay in
# arch/dock_house_trim.part, which is not this module's to touch.
DH_W = DH_D = 5.00               # the deck footprint, unchanged from the survey
PX = PY = 2.15                   # post centres, unchanged from the survey
POST_T = 0.24
POST_H = 2.60                    # ground to the top of the head plate, unchanged
SILL_H = 0.16
DECK_T = 0.05
HEAD_H = 0.18
RAIL_Z = 1.00
RISE = 1.30                      # unchanged
EAVE, OVER = 0.45, 0.45          # a real overhang past the frame on every side
FW = 2 * PX + POST_T             # the post-to-post envelope the sill and head beams frame
FD = 2 * PY + POST_T

DH_SKINS = {
    "floor": skin("planks_weathered", 1.2, (1.86, 1.74, 1.39)),   # sill beams + deck boards
    "wood":  skin("planks_weathered", 2.0, (1.47, 1.25, 0.88)),   # posts, head plate, braces,
                                                                   # purlins, rafters, rails
    "wall":  skin("planks_weathered", 2.5, (1.60, 1.51, 1.17)),   # the boarded back wall
    "roof":  skin("corrugated", 1.0, (0.69, 1.39, 1.57)),         # the gable roof
    "trim":  skin("corrugated", 1.0, (0.52, 1.10, 1.26)),         # the ridge cap
}


def _deck_boards(n=12, gap=0.05):
    span = DH_D - 0.20                       # inset a touch from the outer edge
    depth = (span - (n - 1) * gap) / n
    y0 = -span / 2.0 + depth / 2.0
    return [slab(DH_W - 0.20, depth, DECK_T, z0=SILL_H, y=y0 + i * (depth + gap))
            for i in range(n)]


def _knee_braces(run=0.55, drop=0.55, t=0.08):
    z_hi = POST_H - HEAD_H
    z_lo = z_hi - drop
    out = []
    for sx in (1, -1):
        for sy in (1, -1):
            px, py = sx * PX, sy * PY
            out.append(_strut((px, py, z_lo), (px - sx * run, py, z_hi), t))
            out.append(_strut((px, py, z_lo), (px, py - sy * run, z_hi), t))
    return out


def _rafters(n=5, t=0.07):
    """Sloping rafter pairs from the ridge down to the eave, evenly spaced along the width."""
    ridge_z = POST_H + RISE
    eave_y = DH_D / 2.0 + EAVE
    out = []
    span = FW - 0.6
    for i in range(n):
        x = -span / 2.0 + span * i / (n - 1) if n > 1 else 0.0
        out.append(_strut((x, 0.0, ridge_z), (x, eave_y, POST_H), t))
        out.append(_strut((x, 0.0, ridge_z), (x, -eave_y, POST_H), t))
    return out


@shell("dock_house", tris=5000, skins=DH_SKINS,
       note="dock house: real timber frame, corrugated gable roof, planked deck, boarded back")
def dock_house():
    """The dock house on the quay. Front (+Y) is the open side with the step down to the sand."""
    g = {k: [] for k in DH_SKINS}

    # -- the ground frame: sill beams round the post line, a dozen gapped deck boards on top
    g["floor"].append(wall_ring(FW, FD, SILL_H, t=0.18, z0=0.0))
    g["floor"] += _deck_boards()

    # -- posts, head plate, and the knee braces that stop it racking
    for sx in (1, -1):
        for sy in (1, -1):
            g["wood"].append(slab(POST_T, POST_T, POST_H, z0=0.0, x=sx * PX, y=sy * PY))
    g["wood"].append(wall_ring(FW, FD, HEAD_H, t=0.20, z0=POST_H - HEAD_H))
    g["wood"] += _knee_braces()

    # -- purlins (level, along X, one each side of the ridge) and rafters (sloped, under the tin)
    run = math.hypot(DH_D / 2.0 + EAVE, RISE)
    purlin_y = (DH_D / 2.0 + EAVE) * 0.55
    purlin_z = POST_H + RISE * (1.0 - purlin_y / (DH_D / 2.0 + EAVE))
    for sy in (1, -1):
        g["wood"].append(slab(FW + 0.2, 0.08, 0.08, z0=purlin_z - 0.04, y=sy * purlin_y))
    g["wood"] += _rafters()

    # -- rails on the three open sides, with a gap in the front rail for the step
    g["wood"] += [
        slab(0.7, 0.05, 0.05, z0=RAIL_Z, x=-1.15, y=PY),   # front, left of the step
        slab(0.7, 0.05, 0.05, z0=RAIL_Z, x=1.15, y=PY),    # front, right of the step
        slab(0.05, 2 * PY, 0.05, z0=RAIL_Z, x=-PX),        # left side, full run
        slab(0.05, 2 * PY, 0.05, z0=RAIL_Z, x=PX),         # right side, full run
    ]

    # -- the back wall, boarded solid instead of railed
    g["wall"].append(slab(FW, 0.06, POST_H - HEAD_H - SILL_H - DECK_T,
                          z0=SILL_H + DECK_T, y=-PY))

    # -- the corrugated gable roof, a real eave and gable overhang, and its ridge cap
    g["roof"].append(gable_roof(DH_W, DH_D, RISE, eave=EAVE, over=OVER, z0=POST_H))
    g["trim"].append(ridge_tile(DH_W + 2 * OVER + 0.4, "X", w=0.32, h=0.16,
                                z0=POST_H + RISE - 0.05))

    return g
