"""The Music Room: a gold dome on an octagonal drum, and the little gilded bird that used to
sit on every ridge of the place before the roof budget ran out and left just the two big statues.

The joke of the room is that someone got a permit for "a small octagonal music room" and then
spent the ballroom's budget on it anyway, so everything here is a size too grand for a 9 m cube:
a moulded drum, a proper stilted dome (not a bowl -- see temple_dome's docstring), a lantern with
its own finial. Keep it stately. Do not make it funnier than the client already made it.
"""

import math

import cadquery as cq

from kitlib import piece, revolve, ring

# A regular octagon's across-the-flats width is 2 * r * cos(pi/8), r being the circumradius --
# the radius kitlib.ring wants. Both drum pieces below are built from stacks of these rings, so
# it is worth having the two conversions once.
_COS8 = math.cos(math.pi / 8)


def _oct_r(flats):
    """Circumradius of a regular octagon whose flat-to-flat width is `flats`."""
    return flats / (2 * _COS8)


def _oct_pts(flats):
    """Vertices of a regular octagon of the given across-flats width, one face normal on +X,
    +Y, -X and -Y each -- so a window cut for the +Y face rotates cleanly onto all eight."""
    return ring(8, _oct_r(flats), start=math.pi / 8)


def _arch_wedge(width, z0, spring, rise, extent):
    """A round-headed arch cross-section standing in the XZ plane, pushed out along -Y.

    Used both as the cutter that punches a window through the drum wall and, shrunk a little,
    as the dark pane that fills it -- so the two always share the same round head.
    """
    z1 = z0 + spring
    return (cq.Workplane("XZ").moveTo(-width / 2, z0).lineTo(-width / 2, z1)
            .threePointArc((0, z1 + rise), (width / 2, z1))
            .lineTo(width / 2, z0).close()
            .extrude(extent))


@piece("temple_drum", tris=1600, free=True,
       note="octagonal drum for the temple dome, moulded plinth and cornice, 8 windows")
def temple_drum(flats=5.20, base_flats=5.60, base_h=0.26, wall_h=1.30,
                 cornice_flats=5.60, cornice_h=0.34,
                 win_width=0.50, win_spring=0.70, win_rise=0.25, win_base=0.25,
                 band_h=0.08, band_stand=0.03):
    """The octagonal drum the dome sits on: a stepped plinth, a windowed wall, a cornice.

    Every dimension here is "across the flats" -- the distance between two opposite sides of the
    octagon -- because that is what a mason measures and what has to line up with the 10.4 m
    parapet opening it stands inside. The plinth and cornice both flare out to `base_flats` /
    `cornice_flats`, wider than the wall, which is what makes them read as mouldings rather than
    the wall just stopping.
    """
    pts_wall = _oct_pts(flats)

    # Plinth: a slab, a narrower ledge (the step), then a taper back in to meet the wall -- the
    # stepped-and-tapered profile of a real classical base, built as three cheap flat-sided
    # prisms/lofts instead of one curved moulding.
    step1_h, step2_h = base_h * 0.38, base_h * 0.23
    taper_h = base_h - step1_h - step2_h
    ledge_flats = base_flats - (base_flats - flats) * 0.5
    pts_ledge = _oct_pts(ledge_flats)
    plinth_block = cq.Workplane("XY").polyline(_oct_pts(base_flats)).close().extrude(step1_h)
    plinth_ledge = (cq.Workplane("XY").workplane(offset=step1_h)
                    .polyline(pts_ledge).close().extrude(step2_h))
    plinth_taper = (cq.Workplane("XY").workplane(offset=step1_h + step2_h)
                    .polyline(pts_ledge).close()
                    .workplane(offset=taper_h).polyline(pts_wall).close().loft())

    wall_top = base_h + wall_h
    wall = cq.Workplane("XY").workplane(offset=base_h).polyline(pts_wall).close().extrude(wall_h)

    # Eight round-headed windows, one per face. Build the cutter once for the face whose normal
    # is +Y and rotate it by every multiple of 45 degrees -- which, because the octagon's start
    # angle was chosen above, lands exactly on the other seven face normals too.
    apothem = flats / 2.0
    z0 = base_h + win_base
    margin, inset = 0.10, 0.50  # outside margin for a clean boolean, inward depth for the reveal
    cutter = _arch_wedge(win_width, z0, win_spring, win_rise, inset + margin)
    for k in range(8):
        wall = wall.cut(cutter.translate((0, apothem + margin, 0))
                        .rotate((0, 0, 0), (0, 0, 1), k * 45.0))

    # Dark panes set a little inside the outer face, in the same reveal the cutter opened, so
    # the window reads as glazing in a recess rather than a hole straight through the stone.
    pane_depth, pane_setback = 0.04, 0.04
    pane = _arch_wedge(win_width - 0.04, z0 + 0.02, win_spring - 0.02, win_rise - 0.02, pane_depth)
    panes = [pane.translate((0, apothem - pane_setback, 0)).rotate((0, 0, 0), (0, 0, 1), k * 45.0)
             for k in range(8)]

    # A thin gold band standing proud of the wall just below the cornice -- the one bit of
    # colour on the drum itself, so the eye has somewhere to land before the gold dome above it.
    band = (cq.Workplane("XY").workplane(offset=wall_top - band_h)
            .polyline(_oct_pts(flats + 2 * band_stand)).close().extrude(band_h))

    # Cornice: taper out past the wall (the cyma), a flat projecting corona at the full
    # `cornice_flats` width, then a small taper back in (the cap) that hands off to the dome.
    rng = cornice_flats - flats
    cyma_h, corona_h = cornice_h * 0.29, cornice_h * 0.47
    cap_h = cornice_h - cyma_h - corona_h
    cornice_cyma = (cq.Workplane("XY").workplane(offset=wall_top).polyline(pts_wall).close()
                    .workplane(offset=cyma_h).polyline(_oct_pts(flats + 0.75 * rng)).close().loft())
    cornice_corona = (cq.Workplane("XY").workplane(offset=wall_top + cyma_h)
                      .polyline(_oct_pts(cornice_flats)).close().extrude(corona_h))
    cornice_cap = (cq.Workplane("XY").workplane(offset=wall_top + cyma_h + corona_h)
                  .polyline(_oct_pts(cornice_flats)).close()
                  .workplane(offset=cap_h).polyline(_oct_pts(cornice_flats - 0.5 * rng)).close()
                  .loft())

    return {
        "paint": [plinth_block, plinth_ledge, plinth_taper, wall,
                  cornice_cyma, cornice_corona, cornice_cap],
        "gold": [band],
        "dark": panes,
    }


@piece("temple_dome", tris=2000, free=True,
       note="gilded stilted dome with ribs, lantern and ball-and-spike finial")
def temple_dome(dia=5.20, rise=2.90, thickness=0.08, ribs=12, rib_width=0.10, rib_stand=0.06,
                 lantern_dia=0.90, lantern_h=0.55, finial_h=0.70):
    """A stilted dome: a short vertical drum at the springing, then a true hemisphere on top.

    A plain hemisphere of this diameter would rise only `dia`/2 = 2.60 m, and it would look like
    an upturned bowl, not a dome -- domes read as domes because they stand up on something before
    they curve. So the rise budget (2.90 m) splits into a 0.30 m stilt plus a 2.60 m hemisphere,
    which is the cheapest possible way to get that "stood up" silhouette: one extra cylinder
    segment in the revolve profile, no extra curvature.
    """
    r0 = dia / 2.0
    stilt_h = rise - r0

    # Five points around the quarter-circle from the springing to the crown is little enough to
    # keep this, the tightest-budget piece in the kit, cheap, and still smooths convincingly --
    # the angle between neighbouring chords (22.5 degrees) is well under obj_stage's 34-degree
    # hard-edge threshold.
    phis = [math.radians(a) for a in (0, 22.5, 45, 67.5, 90)]
    outer = [(r0 * math.cos(p), stilt_h + r0 * math.sin(p)) for p in phis]
    r_in = r0 - thickness
    inner = [(r_in * math.cos(p), stilt_h + r_in * math.sin(p)) for p in reversed(phis)]
    # Outer path up from the springing to the crown, inner path back down -- kitlib.revolve
    # closes the loop, which draws the flat annular rim where the shell meets the drum.
    shell = revolve([(r0, 0.0)] + outer + inner[1:] + [(r_in, 0.0)])

    # Ribs share the shell's own meridian curve, offset out by `rib_stand`, and are cut down to
    # a wedge of just enough angle to read as `rib_width` metres at the springing -- a rib is a
    # revolve too, just one that stops after a couple of degrees instead of going all the way
    # round, so it costs almost nothing next to the shell it rides on.
    rib_deg = math.degrees(rib_width / r0)
    rib_outer = [(r + rib_stand * math.cos(p), z + rib_stand * math.sin(p))
                 for (r, z), p in zip(outer, phis)]
    rib = revolve(outer + list(reversed(rib_outer)), angle=rib_deg)
    rib_pieces = [rib.rotate((0, 0, 0), (0, 0, 1), k * 360.0 / ribs) for k in range(ribs)]

    # The lantern: a squat drum, a small capping taper, six little openings, and on top the
    # finial that was too small to be worth its own piece. All riding on the crown, where the
    # ribs converge, which hides the join.
    crown = rise
    l_r = lantern_dia / 2.0
    body_h, cap_h = lantern_h * 0.73, lantern_h * 0.27
    lantern_body = cq.Workplane("XY").workplane(offset=crown).circle(l_r).extrude(body_h)
    lantern_cap = (cq.Workplane("XY").workplane(offset=crown + body_h).circle(l_r)
                   .workplane(offset=cap_h).circle(l_r * 0.55).loft())

    open_w, open_h = 0.12, 0.20
    oz0 = crown + (body_h - open_h) / 2.0
    opening = (cq.Workplane("XZ").moveTo(-open_w / 2, oz0).lineTo(-open_w / 2, oz0 + open_h)
               .lineTo(open_w / 2, oz0 + open_h).lineTo(open_w / 2, oz0).close()
               .extrude(0.4).translate((0, l_r + 0.2, 0)))
    for k in range(6):
        lantern_body = lantern_body.cut(opening.rotate((0, 0, 0), (0, 0, 1), k * 60.0))
    pane = (cq.Workplane("XZ").moveTo(-open_w / 2 + 0.015, oz0 + 0.015)
            .lineTo(-open_w / 2 + 0.015, oz0 + open_h - 0.015)
            .lineTo(open_w / 2 - 0.015, oz0 + open_h - 0.015)
            .lineTo(open_w / 2 - 0.015, oz0 + 0.015).close()
            .extrude(0.03).translate((0, l_r - 0.02, 0)))
    panes = [pane.rotate((0, 0, 0), (0, 0, 1), k * 60.0) for k in range(6)]

    ball_dia = 0.18
    ball_z = crown + lantern_h + ball_dia / 2.0
    ball = cq.Workplane("XY").sphere(ball_dia / 2.0).translate((0, 0, ball_z))
    spike_h = finial_h - ball_dia
    spike = (cq.Workplane("XY").workplane(offset=ball_z + ball_dia / 2.0).circle(0.045)
             .workplane(offset=spike_h).circle(0.004).loft())

    return {
        "gold": [shell] + rib_pieces + [lantern_body, lantern_cap, ball, spike],
        "dark": panes,
    }


# A plane whose normal is +Y rather than the usual +Z, so a `.workplane(offset=d)` chain moves
# forward by `d` instead of up by it -- the natural way to loft a body that lies along its length
# rather than stands on its base. Local u is still global X; local v happens to be global -Z.
_LENGTHWISE = cq.Plane(origin=(0, 0, 0), xDir=(1, 0, 0), normal=(0, 1, 0))


def _wing_rect(cx, z, hw, hh):
    """A small rectangle for a wing loft, centred at (cx, z) in the drawing's own (x, -z) axes."""
    cv = -z
    return [(cx - hw, cv - hh), (cx + hw, cv - hh), (cx + hw, cv + hh), (cx - hw, cv + hh)]


@piece("bird_finial", tris=700, free=True,
       note="small gilded bird finial: base, spike, ball, and a stylised perched bird")
def bird_finial(base=0.26, base_h=0.12, spike_lo=0.10, spike_hi=0.05, spike_h=0.30,
                 ball_dia=0.16, body_h=0.28, wing_sweep=0.28, beak_len=0.09):
    """A miniature cousin of the two big roof statues -- the kind of finial a mason throws in
    for free on every other ridge once he has already carved the expensive ones.

    Built the same way a real finial is assembled: a square moulded base, a tapering spike, a
    ball, and then whatever perches on the ball -- here a stylised bird standing upright with
    its head up and its wings swept back along its sides, rather than lying flat, so the piece
    reads from below instead of from directly above.
    """
    z1 = base_h
    z2 = z1 + spike_h
    plinth = cq.Workplane("XY").box(base, base, base_h, centered=(True, True, False))
    spike = (cq.Workplane("XY").workplane(offset=z1).rect(spike_lo, spike_lo)
             .workplane(offset=spike_h).rect(spike_hi, spike_hi).loft())

    ball_r = ball_dia / 2.0
    ball_z = z2 + ball_r
    ball = cq.Workplane("XY").sphere(ball_r).translate((0, 0, ball_z))

    # The bird itself: body, head, beak and wings. Its own bounding box is not symmetric about
    # Y (the beak reaches further forward than the wingtips reach back), so it is built once,
    # measured, and re-centred as a group -- everything else on the piece already sits on the
    # Z axis, so this one shift is enough to satisfy the free-standing frame rule.
    z_b0 = ball_z + ball_r * 0.7  # rides a little into the top of the ball, not balanced on it
    body = (cq.Workplane("XY").workplane(offset=z_b0).circle(0.05)
            .workplane(offset=body_h * 0.5).circle(0.09)
            .workplane(offset=body_h * 0.5).circle(0.05)
            .loft())
    z_neck = z_b0 + body_h

    head_r = 0.045
    y_head, z_head = 0.08, z_neck + head_r
    head = cq.Workplane("XY").sphere(head_r).translate((0, y_head, z_head))

    beak = (cq.Workplane(_LENGTHWISE).workplane(offset=y_head + head_r * 0.6)
            .moveTo(0, -(z_head - 0.005)).circle(0.018)
            .workplane(offset=beak_len).moveTo(0, -(z_head - 0.02)).circle(0.002)
            .loft())

    def wing(sign):
        z0 = z_b0 + body_h * 0.55
        root = _wing_rect(sign * 0.06, z0, 0.05, 0.012)
        tip = _wing_rect(sign * (0.06 + wing_sweep * 0.75), z0 - wing_sweep * 0.35, 0.012, 0.006)
        return (cq.Workplane(_LENGTHWISE).polyline(root).close()
                .workplane(offset=-wing_sweep).polyline(tip).close().loft())

    bird = [body, head, beak, wing(1), wing(-1)]
    lo = min(p.val().BoundingBox().ymin for p in bird)
    hi = max(p.val().BoundingBox().ymax for p in bird)
    shift = -(lo + hi) / 2.0
    bird = [p.translate((0, shift, 0)) for p in bird]

    return {"gold": [plinth, spike, ball] + bird}
