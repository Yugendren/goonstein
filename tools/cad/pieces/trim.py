"""Running trim: the mouldings that repeat along a wall or a rail rather than standing alone.

Four of the five pieces here are 1 m runs -- cornice_run, roof_edge_run, pool_coping_run,
balustrade_run -- meant to be placed shoulder to shoulder so a wall reads as one continuous
moulding instead of five separate boxes. Tileability is the whole point of a run, so every one
of them is exactly 1.000 m along X with flat vertical end faces at x = -0.5 and x = +0.5, and
any repeating detail inside it (a tile, a baluster) sits on a pitch that still lines up once the
next run starts. balustrade_post is the one exception, a free-standing newel for the ends and
corners a run of balusters needs to land against.
"""

import math

import cadquery as cq

from kitlib import box, moulding, piece, revolve, row


@piece("cornice_run", tris=400,
       note="1 m of eaves cornice: bed mould, corona and a cyma recta cap")
def cornice_run(length=1.0, height=0.34, proj=0.26, bed_h=0.10, bed_proj=0.16,
                 corona_top=0.23, cyma_top=0.32, cap_proj=0.08):
    """A classical cornice, drawn once as a section in the YZ plane and swept the run's length.

    The three parts read bottom to top the way a real cornice does: the bed mould rises from
    the wall and gives the eye somewhere to land, the corona is the flat slab that actually
    throws the shadow (so it has to reach the full `proj`), and the cyma recta above curls back
    toward the wall the way plaster crown moulding always does, finished with a thin fillet so
    the top edge isn't a knife.
    """
    profile = [
        (0.00, 0.00),
        (0.05, 0.00),
        (0.05, 0.03),
        (bed_proj * 0.7, bed_h * 0.6),
        (bed_proj, bed_h),
        (proj, bed_h),
        (proj, corona_top),
        (proj - 0.04, corona_top),
        (proj - 0.04, corona_top + 0.03),
        (cap_proj + 0.06, cyma_top - 0.03),
        (cap_proj, cyma_top),
        (cap_proj - 0.04, height),
        (0.00, height),
    ]
    return moulding(profile, length)


@piece("roof_edge_run", tris=800,
       note="1 m of pantile eaves: fascia, tilting fillet and five half-round barrel tiles")
def roof_edge_run(length=1.0, fascia_h=0.10, fascia_t=0.03, fillet_rise=0.03,
                   tile_r=0.085, tile_pitch=0.20, n_tiles=5, tile_proj=0.30):
    """A pantile eaves course: the fascia board the rafter tails land on, the small tilting
    fillet that cants the first course of tiles up off the fascia, and the tiles themselves.

    Five tiles at `tile_pitch` sit at x = -0.40 .. 0.40 (see kitlib.row), which is a half-pitch
    in from each edge of the 1 m run -- so the last tile of one run and the first of the next,
    a metre away, are still `tile_pitch` apart and the course reads as continuous.
    """
    fascia_profile = [
        (0.0, 0.0),
        (fascia_t, 0.0),
        (fascia_t, fascia_h),
        (fascia_t * 0.4, fascia_h + fillet_rise),
        (0.0, fascia_h),
    ]
    fascia = moulding(fascia_profile, length)

    # Each tile is a half-round barrel lying along Y: flat underside so it beds on the fillet,
    # rounded top, and a hemispherical drip cap so the projecting end isn't a raw open pipe.
    tile_z = fascia_h + fillet_rise
    barrel_len = tile_proj - tile_r
    tiles = []
    for x in row(n_tiles, tile_pitch):
        barrel = (cq.Solid.makeCylinder(tile_r, barrel_len, cq.Vector(0, 0, 0), cq.Vector(0, 1, 0), 180)
                  .rotate((0, 0, 0), (0, 1, 0), -90)
                  .translate((x, 0.0, tile_z)))
        drip = cq.Solid.makeSphere(tile_r, cq.Vector(x, barrel_len, tile_z))
        tiles.append(barrel)
        tiles.append(drip)

    return {"paint": [fascia] + tiles}


@piece("pool_coping_run", tris=300,
       note="1 m of bullnose pool coping with an underside drip groove")
def pool_coping_run(length=1.0, depth=0.45, thick=0.12, bullnose_r=0.06,
                     groove_w=0.02, groove_d=0.02):
    """A poured-stone coping slab, its pool-side edge rolled to a full bullnose.

    A "full" bullnose rounds the whole thickness of the slab into one half-circle rather than
    just chamfering a corner, which is why `bullnose_r` is half of `thick` -- the roll consumes
    the last `bullnose_r` of the slab's depth, so the drip groove has to sit just inboard of
    where the flat underside meets the curve; any further out and it would have to cut into the
    roll itself.
    """
    flat_end = depth - bullnose_r
    groove_x1 = flat_end
    groove_x0 = flat_end - groove_w

    cx, cz = flat_end, thick / 2.0
    arc = [(cx + bullnose_r * math.cos(a), cz + bullnose_r * math.sin(a))
           for a in (i * math.pi / 8 for i in range(-4, 5))]

    profile = (
        [(0.0, 0.0), (groove_x0, 0.0), (groove_x0, groove_d), (groove_x1, groove_d)]
        + arc
        + [(0.0, thick)]
    )
    return moulding(profile, length)


@piece("balustrade_run", tris=1400,
       note="1 m of turned-baluster balustrade: bottom rail, four balusters, top rail")
def balustrade_run(length=1.0, height=0.95, rail_lo_h=0.10, rail_lo_d=0.20,
                    rail_hi_h=0.13, rail_hi_d=0.22, n_balusters=4, pitch=0.25,
                    belly_r=0.058):
    """A run of balustrade: a moulded bottom rail, four turned balusters, a moulded top rail.

    The balusters sit on a 0.25 m `pitch` starting a half-pitch in from each end of the run (see
    kitlib.row), so butting this run against another leaves exactly one more 0.25 m gap across
    the seam rather than a wider or narrower one -- the rail reads as one continuous run.

    Each baluster is turned with kitlib.revolve from a profile that starts and ends on the axis
    (radius 0) so the sweep closes into a solid rather than a napkin ring: a square-ish plinth,
    a belly blown out about a third of the way up, a narrow neck, and a small turned cap.
    """
    lo_rail = box(length, rail_lo_d, rail_lo_h * 0.75)
    lo_cap = box(length, rail_lo_d - 0.04, rail_lo_h * 0.25, y0=0.02, z0=rail_lo_h * 0.75)

    hi_z = height - rail_hi_h
    hi_rail = box(length, rail_hi_d, rail_hi_h * 0.75, z0=hi_z)
    hi_cap = box(length, rail_hi_d - 0.04, rail_hi_h * 0.25, y0=0.02, z0=hi_z + rail_hi_h * 0.75)

    # Balusters are centred in Y on whichever rail is shallower, so they never poke out the back.
    rail_y = min(rail_lo_d, rail_hi_d) / 2.0
    baluster_h = hi_z - rail_lo_h
    section = [
        (0.000, 0.00), (0.055, 0.00), (0.055, 0.07), (0.040, 0.10),
        (belly_r, baluster_h * 0.33), (0.045, baluster_h * 0.47),
        (0.028, baluster_h * 0.76), (0.035, baluster_h * 0.93), (0.000, baluster_h),
    ]
    balusters = [revolve(section).translate((x, rail_y, rail_lo_h))
                 for x in row(n_balusters, pitch)]

    return {"paint": [lo_rail, lo_cap, hi_rail, hi_cap] + balusters}


@piece("balustrade_post", tris=500, free=True,
       note="newel post: square shaft, moulded plinth and cap, ball finial")
def balustrade_post(height=1.15, shaft=0.30, plinth=0.38, plinth_h=0.16,
                     cap=0.42, cap_h=0.14, finial_r=0.09):
    """The newel that ends or turns a run of balustrade_run.

    `height` is the whole post, base to finial tip, the same way wall_column's `height` is --
    the shaft is whatever is left once the plinth, cap and finial have taken their share, so
    changing any one of those still hands you the post height a level expects.
    """
    shaft_h = height - plinth_h - cap_h - 2 * finial_r

    plinth_block = cq.Workplane("XY").box(plinth, plinth, plinth_h, centered=(True, True, False))
    step = (cq.Workplane("XY").workplane(offset=plinth_h)
            .rect(plinth, plinth).workplane(offset=0.04).rect(shaft, shaft).loft())

    z0 = plinth_h + 0.04
    body = (cq.Workplane("XY").workplane(offset=z0)
            .box(shaft, shaft, shaft_h - 0.04, centered=(True, True, False)))

    z1 = z0 + shaft_h - 0.04
    flare = (cq.Workplane("XY").workplane(offset=z1)
             .rect(shaft, shaft).workplane(offset=cap_h * 0.55).rect(cap, cap).loft())
    abacus = (cq.Workplane("XY").workplane(offset=z1 + cap_h * 0.55)
              .box(cap, cap, cap_h * 0.45, centered=(True, True, False)))

    # A full sphere, not the upper-hemisphere default: a ball finial sits whole on top of the cap.
    finial = cq.Solid.makeSphere(finial_r, cq.Vector(0, 0, z1 + cap_h + finial_r), angleDegrees1=-90)

    return {"paint": [plinth_block, step, body, flare, abacus, finial]}
