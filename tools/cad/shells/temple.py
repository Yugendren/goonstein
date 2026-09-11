"""The Music Room: the striped pavilion on the island's eastern high point.

A stone podium of two real steps; a peristyle of twelve piers -- base, shaft and capital --
carrying a real entablature; an 11 x 11 masonry cell with a real 0.45 m wall you can stand
inside; ten courses of banding on the cell's outside face, the cobalt ones standing proud with a
chamfered edge so they throw a real shadow line instead of reading as paint; a doorway and four
window slots cut clean through the wall with gold jambs, lintels and sills; and a hollow parapet
with a coping, open in the middle where the dome used to sit. The drum, the finials and the fallen
dome are hand-placed kit pieces in extras/temple_facade.part, unchanged fabric-wise from the
building's survey.
"""

from shelllib import (FACES, Walls, band, opening_cutters, opening_lintel, opening_sill,
                      parapet, plinth, post, shell, skin, slab, wall_ring)

# ---------------------------------------------------------------- the podium
PODIUM_W1, PODIUM_H1 = 21.0, 0.30      # the outer step
PODIUM_W2, PODIUM_H2 = 19.0, 0.20      # the inner step
PODIUM_TOP = PODIUM_H1 + PODIUM_H2      # 0.50 -- where everything above stands

# ---------------------------------------------------------------- the peristyle
PIER_Z0 = PODIUM_TOP
PIER_BASE_W, PIER_BASE_H = 0.85, 0.22
PIER_SHAFT_W, PIER_SHAFT_H = 0.55, 3.76
PIER_CAP_H = 0.22
PIER_TOP = PIER_Z0 + PIER_BASE_H + PIER_SHAFT_H + PIER_CAP_H   # 4.70

# twelve piers on the 9 m grid: four corners plus two more down each side -- keep this grid at
# +/-9 and +/-3, and the cell at 11 x 11 below, so island.txt's five colliders still land where
# they always have.
PIER_POS = ([(x, y) for x in (-9.0, 9.0) for y in (-9.0, 9.0)] +      # the four corners
            [(x, y) for x in (-3.0, 3.0) for y in (-9.0, 9.0)] +      # front and back
            [(x, y) for x in (-9.0, 9.0) for y in (-3.0, 3.0)])       # left and right

ENT_W = ENT_D = 19.0
ENT_T = 0.80
ARCH_Z0, ARCH_H = PIER_TOP, 0.22
FRIEZE_Z0, FRIEZE_H = ARCH_Z0 + ARCH_H, 0.36
CORNICE_Z0, CORNICE_H = FRIEZE_Z0 + FRIEZE_H, 0.22
CORNICE_OVER, CORNICE_CHAMFER = 0.25, 0.05

# ---------------------------------------------------------------- the cell
CELL_W = CELL_D = 11.0
CELL_T = 0.45
CELL_Z0 = PODIUM_TOP
CELL_H = 9.00
COURSE_H = CELL_H / 10.0               # ten courses, alternating white and cobalt
STRIPE_PROUD = 0.075                   # how far the cobalt courses stand off the white wall
STRIPE_T = 0.14                        # the course's own radial thickness (partly embedded)
STRIPE_CHAMFER = 0.03

DOOR = dict(face="front", u=0.0, z0=0.0, w=2.6, h=4.2, reveal=0.28, margin=0.20)
WINDOWS = [
    dict(face="left", u=0.0, z0=3.0, w=0.9, h=2.4, reveal=0.18, margin=0.14),
    dict(face="right", u=0.0, z0=3.0, w=0.9, h=2.4, reveal=0.18, margin=0.14),
    dict(face="back", u=-2.5, z0=3.0, w=0.9, h=2.4, reveal=0.18, margin=0.14),
    dict(face="back", u=2.5, z0=3.0, w=0.9, h=2.4, reveal=0.18, margin=0.14),
]

# ---------------------------------------------------------------- the parapet and the well
PARAPET_W = PARAPET_D = 11.6
PARAPET_T = 0.6
PARAPET_Z0 = CELL_Z0 + CELL_H           # 9.50
PARAPET_H = 0.9
PARAPET_COPING = 0.12
PARAPET_OVER = 0.10
DECK_SIZE = PARAPET_W - 2 * PARAPET_T   # 10.4, the dark floor the dome used to stand on
DECK_H = 0.15
STUB_POS = (-4.6, 4.6)                  # under the two gold birds island.txt places
STUB_Y = 4.4
STUB_R = 0.28

SKINS = {
    "step":   skin("paving_stone", 0.4, (1.77, 1.96, 2.26)),
    "floor":  skin("paving_stone", 0.4, (1.77, 1.96, 2.26)),
    "wall":   skin("plaster_smooth", 0.7, (1.67, 1.73, 1.83)),
    "trim":   skin("plaster_smooth", 0.7, (1.80, 1.87, 1.98)),
    "band":   skin("plaster_smooth", 0.7, (1.67, 1.73, 1.83)),
    "stripe": skin("plaster_smooth", 0.7, (0.23, 0.57, 1.29)),
    "gold":   skin(mat="gold"),
    "dark":   skin(mat="dark"),
}


def _pier(x, y):
    """One peristyle column: a flared base, a plain square shaft and a flared capital -- what
    shelllib doesn't have a helper for, so a shaft reads as a column rather than a fence post."""
    return [
        plinth(PIER_BASE_W, PIER_BASE_W, PIER_BASE_H, z0=PIER_Z0, chamfer=0.05).translate((x, y, 0)),
        slab(PIER_SHAFT_W, PIER_SHAFT_W, PIER_SHAFT_H, z0=PIER_Z0 + PIER_BASE_H, x=x, y=y),
        plinth(PIER_BASE_W, PIER_BASE_W, PIER_CAP_H,
              z0=PIER_Z0 + PIER_BASE_H + PIER_SHAFT_H, chamfer=0.05).translate((x, y, 0)),
    ]


def _ring_band(w, d, h, z0, t, over=0.0, chamfer=0.0):
    """A shelllib.band() course hollowed into a ring, so an oversailing cornice wraps the open
    colonnade instead of roofing it over -- band() on its own is a solid slab, and this building's
    entablature has nothing but sky above it."""
    b = band(w, d, h, z0=z0, over=over, chamfer=chamfer)
    return b.cut(slab(w - 2 * t, d - 2 * t, h + 2.0, z0=z0 - 1.0))


def _jambs(o, w, d, t, proj=0.06):
    """Two vertical posts down the sides of an opening, standing `proj` proud of the wall and
    filling the same margin the rebate cuts -- shelllib has a sill and a lintel but no jamb, and
    this building's gold surrounds need one on every door and window."""
    angle, which = FACES[o.face]
    face_y = (d if which == "d" else w) / 2.0
    h = o.h + o.margin
    depth = o.reveal + proj
    out = []
    for side in (-1, 1):
        u = o.u + side * (o.w / 2.0 + o.margin / 2.0)
        s = slab(o.margin, depth, h, z0=o.z0, y=face_y + proj - depth / 2.0)
        out.append(s.translate((u, 0, 0)).rotate((0, 0, 0), (0, 0, 1), angle))
    return out


@shell("temple", tris=12000, skins=SKINS,
       note="the striped pavilion: podium, 12-column peristyle, striped 11x11 cell, open parapet")
def temple():
    """The Music Room. Front (+Y in CAD) is the doorway face, becomes game -Z."""
    g = {k: [] for k in SKINS}

    # -- the podium: two real steps, each nosed with a chamfer
    g["step"].append(plinth(PODIUM_W1, PODIUM_W1, PODIUM_H1, z0=0.0, chamfer=0.05))
    g["step"].append(plinth(PODIUM_W2, PODIUM_W2, PODIUM_H2, z0=PODIUM_H1, chamfer=0.04))

    # -- the peristyle: twelve columns and the entablature they carry
    for x, y in PIER_POS:
        g["wall"] += _pier(x, y)
    g["band"].append(wall_ring(ENT_W, ENT_D, ARCH_H, ENT_T, z0=ARCH_Z0))
    g["band"].append(wall_ring(ENT_W, ENT_D, FRIEZE_H, ENT_T, z0=FRIEZE_Z0))
    g["band"].append(_ring_band(ENT_W, ENT_D, CORNICE_H, CORNICE_Z0, ENT_T,
                                over=CORNICE_OVER, chamfer=CORNICE_CHAMFER))

    # -- the cell: real walls you can stand inside, with a doorway and four windows cut through
    w = Walls(CELL_W, CELL_D, CELL_H, t=CELL_T, z0=CELL_Z0, lined=False)  # lined=False: the room behind is real, so an opening shows it
    door = w.hole(**DOOR)
    windows = [w.hole(**win) for win in WINDOWS]
    g["wall"].append(w.solid())
    g["dark"] += w.linings()
    g["floor"].append(w.floor(t=0.12))

    # gold surrounds -- built by hand rather than Walls.kit(), because this building's frames are
    # temple jambs and lintels, not one of the kit's window/door pieces. opening_* and _jambs()
    # want an Opening with an ABSOLUTE z0, so put the wall ring's own z0 back onto each one first.
    door_abs = door._replace(z0=door.z0 + CELL_Z0)
    win_abs = [o._replace(z0=o.z0 + CELL_Z0) for o in windows]
    g["gold"] += _jambs(door_abs, CELL_W, CELL_D, CELL_T, proj=0.08)
    g["gold"].append(opening_lintel(door_abs, CELL_W, CELL_D, CELL_T, proj=0.08, h=0.22, over=0.05))
    for o in win_abs:
        g["gold"] += _jambs(o, CELL_W, CELL_D, CELL_T, proj=0.06)
        g["gold"].append(opening_lintel(o, CELL_W, CELL_D, CELL_T, proj=0.06, h=0.14, over=0.06))
        g["gold"].append(opening_sill(o, CELL_W, CELL_D, CELL_T, proj=0.08, h=0.09, over=0.06))

    # -- ten courses on the outside of the cell wall: white flush with the plaster (no geometry of
    # its own -- it IS the wall), cobalt standing STRIPE_PROUD off it with a chamfer top and
    # bottom so each course throws a real line onto the one below.
    for i in range(10):
        if i % 2 == 0:
            continue
        z = CELL_Z0 + i * COURSE_H
        course = wall_ring(CELL_W + 2 * STRIPE_PROUD, CELL_D + 2 * STRIPE_PROUD, COURSE_H,
                           STRIPE_T, z0=z)
        course = course.edges(">Z").chamfer(STRIPE_CHAMFER)
        course = course.edges("<Z").chamfer(STRIPE_CHAMFER)
        # The courses are rings, and a ring runs straight across a doorway unless you take the
        # openings out of it -- which is exactly what was happening: a gold surround with blue
        # and white banding where the way in should be. Cut each opening out of every course at
        # its architrave size, so the banding stops clear of the gold rather than butting it.
        for o in [door_abs] + win_abs:
            clear = o._replace(reveal=0.0, w=o.w + 2 * o.margin, h=o.h + o.margin)
            for tool in opening_cutters(clear, CELL_W + 2 * STRIPE_PROUD,
                                        CELL_D + 2 * STRIPE_PROUD, STRIPE_T):
                course = course.cut(tool)
        g["stripe"].append(course)

    # -- the parapet: hollow, so the well the dome used to sit in is still open, with a coping
    body, cap = parapet(PARAPET_W, PARAPET_D, PARAPET_H, PARAPET_T, z0=PARAPET_Z0,
                        coping=PARAPET_COPING, over=PARAPET_OVER)
    g["wall"].append(body)
    g["trim"].append(cap)
    g["dark"].append(slab(DECK_SIZE, DECK_SIZE, DECK_H, z0=PARAPET_Z0))

    # -- the two gold stubs the birds in island.txt perch on
    stub_h = (PARAPET_Z0 + PARAPET_H) - (PARAPET_Z0 + DECK_H)
    for x in STUB_POS:
        g["gold"].append(post(x, STUB_Y, STUB_R, stub_h, z0=PARAPET_Z0 + DECK_H, sides=8))

    return g, w.fits
