"""The pool house: a small bathhouse whose front is a true arcade -- three round-headed arches
you walk into and see the depth of, not a cylinder laid across a box.

Front (+Y in CAD) becomes game -Z, which faces the pool. The other three walls are a plain
0.30 m ring with a door and a couple of windows; the front is `shelllib.arcade(...)`, cut into
the same ring after its own front strip is removed, so the colonnade joins the side walls at the
corners instead of sitting apart from them.
"""

from shelllib import (Walls, arcade, band, eave_course, hip_roof, parapet, plinth, ridge_tile,
                      shell, skin, slab, veranda)

W, D, H = 9.00, 5.00, 3.40      # footprint; walls run BASE..BASE+H, i.e. 0.2 to 3.6
T = 0.30                        # wall thickness, arcade included
BASE = 0.20                     # plinth top -- the height assets/levels/island.txt's stair_run
                                 # climbs to; do not change without checking that placement too

N_ARCH = 3
PIER = 1.20                     # solid width between two arches
END_PIER = 0.60                 # solid width from the wall's own end to the first arch
RISE = 0.90                     # arch head radius, so a 1.8 m opening
REVEAL, MARGIN = 0.08, 0.12

PARA_H, PARA_T = 0.40, 0.22
ROOF_RISE, EAVE = 1.00, 0.45

WOOD_TINT = (1.47, 1.25, 0.88)
TRIM_TINT = (1.67, 1.73, 1.83)

SKINS = {
    "floor": skin("paving_stone", 0.5, (1.73, 1.91, 2.20)),
    "wall":  skin("plaster_cream", 0.5, (1.57, 1.74, 2.39)),
    "trim":  skin("plaster_smooth", 0.8, TRIM_TINT),
    "band":  skin("plaster_smooth", 0.8, TRIM_TINT),
    "roof":  skin("roof_tile", 1.2, (0.42, 1.53, 1.97)),
    "dark":  skin(mat="dark"),
}


@shell("pool_house", tris=9000, skins=SKINS,
       note="pool house: 9 x 5, a true 3-arch colonnade on the front, low hip roof behind a "
            "parapet")
def pool_house():
    """The bathhouse beside the pool. Front (+Y in CAD) faces the pool."""
    g = {k: [] for k in SKINS}

    # -- the ground it stands on: a nosed terrace and a chamfered plinth up to the wall base
    g["floor"].append(veranda(W + 1.0, D + 1.0, t=0.15, z0=0.15, nose=0.03))
    g["floor"].append(plinth(W + 0.4, D + 0.4, BASE, chamfer=0.03))
    # island.txt places a stair_run of its own up onto this plinth on the front -- no flight of
    # steps modelled here, so the two don't double up.

    # -- the back and side walls, a normal ring with a door and a couple of windows
    w = Walls(W, D, H, t=T, z0=BASE, lined=False)  # lined=False: the room behind is real, so an opening shows it
    w.kit("plain_door", "back", u=0.0, z0=0.15, tint=WOOD_TINT, tex="planks_weathered", tile=2.5)
    w.kit("window_small", "left", u=0.0, z0=1.10, scale=0.9, tint=TRIM_TINT,
          tex="plaster_smooth", tile=0.8)
    w.kit("window_small", "right", u=0.0, z0=1.10, scale=0.9, tint=TRIM_TINT,
          tex="plaster_smooth", tile=0.8)
    ring = w.solid()

    # -- replace the ring's own front wall with a true arcade: cut its strip away clean (full
    # width, so the corners go with it) and union in a wall built by arcade(), which cuts a
    # reveal on BOTH faces so you can see the depth of the arch as you walk through it. The
    # panel is drawn centred on y = 0 like arcade() always does, then slid out to the front.
    front_y = D / 2.0 - T / 2.0
    ring = ring.cut(slab(W + 0.4, T + 0.06, H + 1.0, z0=BASE - 0.5, y=front_y))
    front_wall, _arches = arcade(W, T, H, N_ARCH, PIER, RISE, z0=BASE, end_pier=END_PIER,
                                 reveal=REVEAL, margin=MARGIN)
    front_wall = front_wall.translate((0.0, front_y, 0.0))
    g["wall"].append(ring.union(front_wall))
    g["dark"] += w.linings()
    g["floor"].append(w.floor())          # the colonnade is a floor you walk into, not a facade

    # -- an impost on each pier at the springing line, so the arches spring from something
    # instead of starting mid-plaster. On the PIERS, not across the whole front: a band that
    # runs over an opening is a ledge hanging in mid-air, which is exactly what an arcade is
    # not. The springing is where arcade() put it: `head` above the wall's base.
    span = (W - 2 * END_PIER - (N_ARCH - 1) * PIER) / float(N_ARCH)
    crown = H - 0.35
    springing = BASE + crown - RISE
    piers = [(-W / 2.0 + END_PIER / 2.0, END_PIER), (W / 2.0 - END_PIER / 2.0, END_PIER)]
    piers += [(-W / 2.0 + END_PIER + span + PIER / 2.0 + i * (span + PIER), PIER)
              for i in range(N_ARCH - 1)]
    for x, pw in piers:
        g["band"].append(band(pw, T, 0.12, z0=springing, over=0.05)
                         .translate((x, front_y, 0.0)))

    # -- parapet on all four sides, then the low hip roof rising behind it
    top = BASE + H
    g["trim"] += parapet(W, D, PARA_H, PARA_T, z0=top)
    g["roof"].append(hip_roof(W, D, ROOF_RISE, eave=EAVE, z0=top))
    g["trim"].append(eave_course(W, D, EAVE, h=0.18, depth=0.26, z0=top))
    g["roof"].append(ridge_tile(max(0.6, W - D) + 0.5, "X", w=0.30, h=0.14,
                                z0=top + ROOF_RISE - 0.05))

    return g, w.fits
