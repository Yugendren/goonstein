"""Service buildings: the bunkhouse (staff quarters), the two blue utility sheds by the
helipad, and the half-buried service shed / tunnel mouth. The plainest architecture on the
island on purpose -- these are the buildings nobody is meant to admire -- but still walls with
real thickness and openings cut through them, not painted-on rectangles.
"""

import cadquery as cq

from shelllib import (Walls, band, eave_course, gable_roof, opening_lintel, parapet, plinth,
                      ridge_tile, shed_roof, shell, skin, slab, veranda)

# ---------------------------------------------------------------- local helpers
def _fascia(w, y, ztop, outward, h=0.18, depth=0.24):
    """A single fascia batten along X, its outer-bottom edge beaded for a shadow line: the
    trim a single-pitch (`shed_roof`) eave gets on its high and low sides, where `eave_course`'s
    symmetric ring does not fit a roof whose two long edges sit at different heights.

    `y` is the batten's centreline and `ztop` where its top meets the roof's underside there;
    `outward` is +1 for the high (front) eave and -1 for the low (back) one, which is only which
    way the bead faces.
    """
    s = slab(w, depth, h, z0=ztop - h, y=y)
    sel = "<Z and >Y" if outward > 0 else "<Z and <Y"
    return s.edges(sel).chamfer(min(h * 0.4, depth * 0.4))


def _berm(x0, sign, run, h, d, z0=0.0):
    """A sloping earth berm against an end wall: a right-triangular prism, full height `h` at
    the wall (x0) and back down to ground `run` metres out from it, `d` deep."""
    prof = (cq.Workplane("XZ").moveTo(0, 0).lineTo(0, h).lineTo(sign * run, 0).close()
            .extrude(d))
    return prof.translate((x0, d / 2.0, z0))


# ================================================================== THE BUNKHOUSE
W, D, H = 16.0, 6.0, 3.0        # footprint and wall height bunkhouse.part has always had
T = 0.22                        # thin block wall -- the cheapest building on the island
BASE = 0.4                      # the plinth the walls stand on (also the plinth's own height)
EAVE = 0.8                      # front/back overhang -- generous, so the eave throws real shade
OVER = 0.5                      # gable-end overhang
RISE = 1.1                      # shallow fall over the 6 m + 2*EAVE depth

BUNKHOUSE_SKINS = {
    # The plinth and walkway were painted_wall on the old box, which reads as green moss on a
    # slab you are meant to take for poured concrete; the rest of the palette is unchanged. The
    # roof is lifted about a third of a stop from the old 0.98/1.22/1.45: a single shallow plane
    # catches far less of a dusk sun than the two wedges it replaces, and at the old number it
    # read as a black rectangle from every angle above it.
    "floor": skin("concrete_rough", 0.5, (1.24, 1.25, 1.28)),
    "wall":  skin("plaster_smooth", 0.9, (1.67, 1.73, 1.83)),
    "roof":  skin("corrugated", 1.0, (1.42, 1.66, 1.92)),
    "trim":  skin("corrugated", 1.0, (1.18, 1.38, 1.62)),
    "dark":  skin(mat="dark"),
}


@shell("bunkhouse", tris=7000, skins=BUNKHOUSE_SKINS,
       note="staff quarters: 16 x 6, plain block, single-pitch roof, four doors, front veranda")
def bunkhouse():
    """Staff quarters -- deliberately the cheapest building on the island: no cornice, no
    pilasters, just a plain rendered block under a corrugated shed roof."""
    g = {k: [] for k in BUNKHOUSE_SKINS}

    # -- plinth and the walkway slab out front
    g["floor"].append(plinth(W + 0.2, D + 0.2, BASE))
    g["floor"].append(veranda(17.0, 2.0, t=0.18, z0=BASE, y=D / 2.0 + 0.1 + 1.0))

    # -- walls: four plain doors, a small window beside each, and one on each end wall
    w = Walls(W, D, H, t=T, z0=BASE, lined=False)  # lined=False: the room behind is real, so an opening shows it
    for x in (-6.0, -2.0, 2.0, 6.0):
        w.kit("plain_door", "front", u=x, z0=0.15, tint=(1.47, 1.25, 0.88),
              tex="planks_weathered", tile=2.5)
        w.kit("window_small", "front", u=x + 1.0, z0=1.10, scale=0.62,
              tint=(1.67, 1.73, 1.83), tex="plaster_smooth", tile=0.8)
    for face in ("left", "right"):
        w.kit("window_small", face, u=0.0, z0=1.15, scale=0.65,
              tint=(1.67, 1.73, 1.83), tex="plaster_smooth", tile=0.8)
    g["wall"].append(w.solid())
    g["dark"] += w.linings()
    g["floor"].append(w.floor())

    # -- roof: one plane falling front (+Y, the -Z game face) to back, a real eave both ends
    g["roof"].append(shed_roof(W, D, RISE, eave=EAVE, over=OVER, z0=BASE + H))
    Droof, Wroof = D + 2 * EAVE, W + 2 * OVER
    g["trim"].append(_fascia(Wroof, Droof / 2.0, BASE + H + RISE, +1))   # high (front) eave
    g["trim"].append(_fascia(Wroof, -Droof / 2.0, BASE + H, -1))        # low (back) eave

    return g, w.fits


# ================================================================== SHED_BLUE
SB_W, SB_D, SB_H = 4.5, 3.5, 2.3
SB_T = 0.25
SB_BASE = 0.12
SB_EAVE = 0.35                  # equal all round, so eave_course's uniform ring fits gable_roof
SB_RISE = 0.65

SHED_BLUE_SKINS = {
    "floor": skin("concrete_rough", 0.7, (0.86, 0.86, 0.93)),
    "wall":  skin("plaster_smooth", 0.9, (1.67, 1.73, 1.83)),
    "roof":  skin("corrugated", 1.0, (0.46, 1.60, 1.82)),
    "trim":  skin("corrugated", 1.0, (0.35, 1.22, 1.42)),
    "metal": skin("concrete_rough", 0.8, (0.58, 0.59, 0.65)),
    "dark":  skin(mat="dark"),
}


@shell("shed_blue", tris=2500, skins=SHED_BLUE_SKINS,
       note="blue-roofed utility shed by the helipad: 4.5 x 3.5, gable roof, real eave")
def shed_blue():
    """One of the pair beside the helipad, placed at yaw 150 and 205 with a 4.6/3.2 collider."""
    g = {k: [] for k in SHED_BLUE_SKINS}

    g["floor"].append(plinth(SB_W + 0.5, SB_D + 0.5, SB_BASE, chamfer=0.03))

    w = Walls(SB_W, SB_D, SB_H, t=SB_T, z0=SB_BASE, lined=False)  # lined=False: the room behind is real, so an opening shows it
    w.kit("plain_door", "front", u=0.0, z0=0.15, tint=(1.47, 1.25, 0.88),
          tex="planks_weathered", tile=2.0)
    w.kit("window_small", "right", u=0.0, z0=1.05, scale=0.75, tint=(1.67, 1.73, 1.83),
          tex="plaster_smooth", tile=0.9)
    g["wall"].append(w.solid())
    g["dark"] += w.linings()
    g["floor"].append(w.floor())

    g["roof"].append(gable_roof(SB_W, SB_D, SB_RISE, eave=SB_EAVE, over=SB_EAVE, z0=SB_BASE + SB_H))
    g["trim"].append(eave_course(SB_W, SB_D, SB_EAVE, h=0.14, depth=0.20, z0=SB_BASE + SB_H))
    g["roof"].append(ridge_tile(SB_W + 2 * SB_EAVE + 0.3, "X", w=0.24, h=0.12,
                                z0=SB_BASE + SB_H + SB_RISE - 0.04))

    # a vent stack on the blind (right) end wall, poking up past the eave
    vx = SB_W / 2.0 + 0.16
    g["metal"].append(slab(0.16, 0.16, 1.05, z0=SB_BASE + SB_H - 0.3, x=vx, y=0.0))
    g["metal"].append(slab(0.24, 0.24, 0.08, z0=SB_BASE + SB_H - 0.3 + 1.05, x=vx, y=0.0))

    return g, w.fits


# ================================================================== SERVICE_SHED
SS_W, SS_D, SS_H = 10.0, 8.0, 4.0     # half-buried concrete block, front faces -Z
SS_T = 0.35
SS_ROOF_T = 0.25
SS_PARA_H, SS_PARA_T = 0.5, 0.20
SS_BERM_RUN = 1.5

SERVICE_SHED_SKINS = {
    "wall": skin("concrete_rough", 0.6, (0.80, 0.80, 0.86)),
    "roof": skin("concrete_rough", 0.6, (0.69, 0.70, 0.77)),
    "trim": skin("concrete_rough", 0.7, (0.58, 0.59, 0.65)),
    "dark": skin(mat="dark"),
}


@shell("service_shed", tris=6000, skins=SERVICE_SHED_SKINS,
       note="half-buried concrete service block, 10 x 8 x 4, vehicle tunnel through the front")
def service_shed():
    """The tunnel entrance: a thick concrete wall ring, a square vehicle opening with a deep
    reveal and a heavy lintel, a door beside it, a flat roof behind a parapet with a real
    overhanging drip edge, and the two earth berms burying its ends."""
    g = {k: [] for k in SERVICE_SHED_SKINS}

    w = Walls(SS_W, SS_D, SS_H, t=SS_T, z0=0.0)
    tunnel = w.hole("front", u=0.0, z0=0.0, w=3.2, h=3.4, reveal=0.5, margin=0.30)
    w.kit("plain_door", "front", u=2.7, z0=0.15, tint=(1.47, 1.25, 0.88),
          tex="planks_weathered", tile=2.0)
    g["wall"].append(w.solid())
    g["dark"] += w.linings()

    # the heavy lintel over the tunnel mouth -- opening_lintel expects an absolute z0, which is
    # what w.hole() returned since this ring's own z0 is 0
    g["trim"].append(opening_lintel(tunnel, SS_W, SS_D, SS_T, proj=0.18, h=0.40, over=0.20))

    # flat roof behind a parapet, with a real overhanging drip edge under the eave
    g["roof"].append(slab(SS_W, SS_D, SS_ROOF_T, z0=SS_H))
    g["trim"].append(band(SS_W, SS_D, 0.10, z0=SS_H - 0.10, over=0.14, chamfer=0.03))
    g["wall"] += parapet(SS_W, SS_D, SS_PARA_H, SS_PARA_T, z0=SS_H + SS_ROOF_T)

    # the two berms burying the ends
    g["trim"].append(_berm(-SS_W / 2.0, -1, SS_BERM_RUN, SS_H, SS_D))
    g["trim"].append(_berm(SS_W / 2.0, 1, SS_BERM_RUN, SS_H, SS_D))

    return g, w.fits
