"""Cabana Row: the four guest cabanas along the east side of the pool.

This is the reference shell -- the smallest building on the island and the one to read first if
you are writing another. Everything the brief asks for is here in twenty lines of geometry: a
plinth with a chamfer, walls with a real 0.28 m thickness, a door and two windows CUT through
them with a rebate the kit's frames sit down into, a porch you can stand on, and a pyramid roof
that overhangs its walls and stops at a beaded tile course instead of at nothing.
"""

from shelllib import (Walls, eave_course, hip_roof, plinth, post, ridge_tile, shell, skin, slab,
                      steps, veranda)

W, D, H = 6.00, 4.50, 2.70      # the footprint and wall height cabana.part has always had
T = 0.28                        # wall thickness
BASE = 0.12                     # the plinth the walls stand on
EAVE = 0.60
RISE = 1.30

SKINS = {
    "floor": skin("paving_stone", 0.5, (1.73, 1.91, 2.20)),
    "wall":  skin("plaster_smooth", 0.8, (1.67, 1.73, 1.83)),
    "trim":  skin("plaster_smooth", 1.1, (1.80, 1.87, 1.98)),
    "roof":  skin("roof_tile", 1.2, (0.42, 1.53, 1.97)),
    "wood":  skin("planks_weathered", 2.5, (2.00, 1.70, 1.20)),
    "dark":  skin(mat="dark"),
}


@shell("cabana", tris=3000, skins=SKINS,
       note="guest cabana: 6 x 4.5, porch on two posts, pyramid roof with a 0.6 m eave")
def cabana():
    """One of Cabana Row. Front (+Y in CAD) becomes game -Z, which faces the pool."""
    g = {k: [] for k in SKINS}

    # -- the ground it stands on: a terrace, the plinth, and a step down off the porch
    g["floor"].append(plinth(W + 0.9, D + 0.9, 0.08))
    g["floor"].append(plinth(W + 0.4, D + 0.4, BASE, chamfer=0.03))
    g["floor"] += steps(1.6, count=1, going=0.36, rise=0.09, y0=D / 2 + 1.30, z0=0.0)

    # -- walls, with the openings the kit's frames are sized for
    w = Walls(W, D, H, t=T, z0=BASE, lined=False)  # lined=False: the room behind is real, so an opening shows it
    w.kit("plain_door", "front", u=0.0, z0=0.15, tint=(1.47, 1.25, 0.88), tex="planks_weathered",
          tile=2.5)
    for u in (-1.85, 1.85):
        w.kit("window_small", "front", u=u, z0=1.10, scale=0.9, tint=(1.67, 1.73, 1.83),
              tex="plaster_smooth", tile=0.8)
    w.kit("window_small", "back", u=0.0, z0=1.10, scale=0.9, tint=(1.67, 1.73, 1.83),
          tex="plaster_smooth", tile=0.8)
    w.kit("window_small", "left", u=0.0, z0=1.10, scale=0.9, tint=(1.67, 1.73, 1.83),
          tex="plaster_smooth", tile=0.8)
    g["wall"].append(w.solid())
    g["dark"] += w.linings()
    g["floor"].append(w.floor())

    # -- the porch: a real slab on two posts, with a head beam over them
    porch_d = 1.30
    g["floor"].append(veranda(3.90, porch_d, t=BASE, z0=BASE, y=D / 2 + porch_d / 2))
    for x in (-1.55, 1.55):
        g["wood"].append(post(x, D / 2 + porch_d - 0.25, 0.09, H - BASE, z0=BASE, sides=8))
    g["wood"].append(slab(3.70, 0.13, 0.22, z0=H - 0.22, y=D / 2 + porch_d - 0.25))

    # -- roof: one hip, carried forward over the porch on those two posts, which is what a
    # bungalow veranda actually is -- a deep front eave, not a separate lean-to with a gap of
    # sky between it and the house. The footprint is pushed toward the front by half the porch
    # so the ridge stays over the middle of the roof rather than the middle of the walls.
    over = porch_d - 0.15
    y_roof = over / 2.0
    g["roof"].append(hip_roof(W, D + over, RISE, eave=EAVE, ridge=W - D - over, z0=H)
                     .translate((0, y_roof, 0)))
    g["trim"].append(eave_course(W, D + over, EAVE, h=0.18, depth=0.26, z0=H)
                     .translate((0, y_roof, 0)))
    g["roof"].append(ridge_tile(max(0.6, W - D - over) + 0.5, "X", w=0.30, h=0.14,
                                z0=H + RISE - 0.05).translate((0, y_roof, 0)))

    return g, w.fits
