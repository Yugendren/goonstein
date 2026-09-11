"""The main compound villa and its outbuilding wing.

villa: the hero building, 20 x 12, a hipped turquoise roof over a walled block with a real
colonnaded portico front and back -- the ground-floor columns and the trim's cornice/balustrade
stay hand-placed (see tools/cad/shells/extras/villa_facade.part and the untouched
assets/models/own/arch/villa_trim.part island.txt still places), everything else -- the wall
ring, its openings, the plinth and steps, the pediment, the parapet rail, the pilasters, the roof
and its chimneys -- is real CAD architecture cut and lofted like cabana.py.

villa_wing: the smaller outbuilding, 10 x 8, a plain veranda on four turned posts carrying the
hip roof forward the way cabana.py's porch does.
"""

from shelllib import (Walls, eave_course, hip_roof, pilaster, plinth, post, ridge_tile, shell,
                      skin, slab, stack, steps, veranda)

# ==================================================================== villa
W, D, H = 20.0, 12.0, 5.4       # footprint and wall height villa.part has always had
T = 0.30                        # wall thickness
BASE = 0.20                     # plinth height / the wall ring's own base
TERRACE_H = 0.10                # the terrace pad, a step below the plinth
EAVE = 0.7
RISE = 2.6
COL_Y = 7.4                     # the colonnade's depth, front (+Y) and back (-Y)
COL_SPAN = 21.0                 # stylobate/architrave run, wider than the wall to clear the ends

TRIM = (1.67, 1.73, 1.83)
WALL_TINT = (1.57, 1.74, 2.39)

SKINS = {
    "floor": skin("paving_stone", 0.5, (1.73, 1.91, 2.20)),
    "step":  skin("plaster_cream", 0.5, WALL_TINT),
    "wall":  skin("plaster_cream", 0.5, WALL_TINT),
    "trim":  skin("plaster_smooth", 0.8, TRIM),
    "roof":  skin("roof_tile", 1.2, (0.42, 1.53, 1.97)),
    "dark":  skin(mat="dark"),
}


@shell("villa", tris=15000, skins=SKINS,
       note="the compound's hero house: 20 x 12, front/back colonnades, hipped turquoise roof")
def villa():
    """The villa. Front (+Y in CAD) is the courtyard side, becomes game -Z."""
    g = {k: [] for k in SKINS}

    # -- terrace, chamfered plinth and the flight of steps down from the front door
    g["floor"].append(plinth(24.0, 16.0, TERRACE_H))
    g["step"].append(plinth(20.6, 12.6, BASE, chamfer=0.05))
    g["step"] += steps(3.0, count=2, going=0.4, rise=(BASE - TERRACE_H) / 2.0,
                       y0=12.6 / 2.0, z0=TERRACE_H)

    # -- walls: a french door centred on the front, large windows either side of it and on the
    # back, a small window on each end wall
    w = Walls(W, D, H, t=T, z0=BASE, lined=False)  # lined=False: the room behind is real, so an opening shows it
    w.kit("french_door", "front", u=0.0, z0=0.20, tint=(1.58, 1.62, 1.70), tex="plaster_smooth",
          tile=0.8)
    for u in (-7.2, -4.8, -2.4, 2.4, 4.8, 7.2):
        w.kit("window_large", "front", u=u, z0=1.10, tint=(1.62, 1.68, 1.78),
              tex="plaster_smooth", tile=0.8)
    for u in (-4.0, 4.0):
        w.kit("window_large", "back", u=u, z0=1.10, tint=(1.62, 1.68, 1.78),
              tex="plaster_smooth", tile=0.8)
    for face in ("left", "right"):
        w.kit("window_small", face, u=0.0, z0=1.10, tint=(1.62, 1.68, 1.78),
              tex="plaster_smooth", tile=0.8)
    g["wall"].append(w.solid())
    g["dark"] += w.linings()
    g["floor"].append(w.floor())

    # -- the colonnades, front and back: stylobate, architrave and a parapet rail. The columns
    # keep their existing positions in tools/cad/shells/extras/villa_facade.part, which places
    # the kit's wall_column over this masonry the way the old hand-built villa always did.
    for y in (COL_Y, -COL_Y):
        g["trim"].append(slab(COL_SPAN, 0.9, 0.3, z0=0.2, y=y))     # stylobate
        g["trim"].append(slab(COL_SPAN, 0.9, 0.55, z0=5.1, y=y))    # architrave
        g["trim"].append(slab(COL_SPAN, 0.12, 0.35, z0=5.65, y=y))  # parapet rail

    # -- pediment over the front door
    g["trim"].append(slab(3.2, 0.75, 0.45, z0=5.65, y=7.35))
    g["roof"].append(slab(3.5, 0.85, 0.12, z0=6.10, y=7.35))

    # -- corner pilasters
    for x in (-10.0, 10.0):
        for y in (-6.0, 6.0):
            g["trim"].append(pilaster(x, y, 0.6, 0.6, H, z0=BASE))

    # -- roof: one hip over the wall block (the colonnades are their own flat roof terrace,
    # under the balustrade in villa_trim.part, and stop short of the main roof's eave on purpose)
    g["roof"].append(hip_roof(W, D, RISE, eave=EAVE, ridge=8.6, z0=H))
    g["trim"].append(eave_course(W, D, EAVE, h=0.20, depth=0.28, z0=H))
    g["roof"].append(ridge_tile(8.6, "X", w=0.34, h=0.18, z0=H + RISE - 0.05))

    # -- two chimneys on the ridge
    for x in (5.0, -5.0):
        body, cap = stack(x, 0.0, 0.6, 0.6, 0.8, z0=8.3)
        g["wall"].append(body)
        g["trim"].append(cap)

    return g, w.fits


# ==================================================================== villa_wing
WW, WD, WH = 10.0, 8.0, 3.6     # footprint and wall height villa_wing.part has always had
WT = 0.30
WBASE = 0.15
WEAVE = 0.55
WRISE = 1.7
PORCH_D = 1.6

WSKINS = {
    "floor": skin("paving_stone", 0.5, (1.73, 1.91, 2.20)),
    "wall":  skin("plaster_cream", 0.5, WALL_TINT),
    "trim":  skin("plaster_smooth", 0.8, TRIM),
    "roof":  skin("roof_tile", 1.2, (0.42, 1.53, 1.97)),
    "dark":  skin(mat="dark"),
}


@shell("villa_wing", tris=8000, skins=WSKINS,
       note="the compound's outbuilding wing: 10 x 8, veranda on four posts, hipped roof")
def villa_wing():
    """The wing. Front (+Y in CAD) faces the same way as the villa, becomes game -Z."""
    g = {k: [] for k in WSKINS}

    # -- terrace/plinth (the wing has always had one footprint for both)
    g["floor"].append(plinth(WW + 2.0, WD + 2.0, WBASE, chamfer=0.03))

    # -- walls: a plain door centred on the front, small windows front, back and each end
    w = Walls(WW, WD, WH, t=WT, z0=WBASE, lined=False)  # lined=False: the room behind is real, so an opening shows it
    w.kit("plain_door", "front", u=0.0, z0=0.15, tint=(1.47, 1.25, 0.88), tex="planks_weathered",
          tile=2.5)
    for u in (-3.0, 3.0):
        w.kit("window_small", "front", u=u, z0=1.00, tint=TRIM, tex="plaster_smooth", tile=0.8)
    for face in ("back", "left", "right"):
        w.kit("window_small", face, u=0.0, z0=1.00, tint=TRIM, tex="plaster_smooth", tile=0.8)
    g["wall"].append(w.solid())
    g["dark"] += w.linings()
    g["floor"].append(w.floor())

    # -- corner pilasters
    for x in (-5.0, 5.0):
        for y in (-4.0, 4.0):
            g["trim"].append(pilaster(x, y, 0.5, 0.5, WH, z0=WBASE))

    # -- veranda: a real slab on four turned posts with a head beam, the way cabana.py does it
    post_y = WD / 2.0 + PORCH_D - 0.25
    g["floor"].append(veranda(WW + 0.4, PORCH_D, t=WBASE, z0=WBASE, y=WD / 2.0 + PORCH_D / 2.0))
    for x in (-3.5, -1.2, 1.2, 3.5):
        g["trim"].append(post(x, post_y, 0.14, WH - WBASE, z0=WBASE, sides=8))
    g["trim"].append(slab(WW + 0.4, 0.15, 0.25, z0=WH - 0.25, y=post_y))

    # -- roof, carried forward over the veranda on those four posts
    over = PORCH_D - 0.15
    y_roof = over / 2.0
    g["roof"].append(hip_roof(WW, WD + over, WRISE, eave=WEAVE, ridge=max(0.2, WW - WD - over),
                              z0=WH).translate((0, y_roof, 0)))
    g["trim"].append(eave_course(WW, WD + over, WEAVE, h=0.16, depth=0.22, z0=WH)
                     .translate((0, y_roof, 0)))
    g["roof"].append(ridge_tile(max(0.6, WW - WD - over) + 0.4, "X", w=0.28, h=0.14,
                                z0=WH + WRISE - 0.05).translate((0, y_roof, 0)))

    return g, w.fits
