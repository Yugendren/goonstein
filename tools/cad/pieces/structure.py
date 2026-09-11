"""Stairs, pergolas, lamps, banding and tank fittings -- the pieces that don't belong to a
column, an opening or a moulding run, but still have to obey the same frame.
"""

import cadquery as cq

from kitlib import box, piece, revolve, ring, row


def _bar(a, b, size):
    """A square-section iron bar from point `a` to point `b`.

    Lofting between two workplanes set on the bar's own end points -- exactly the technique
    `wall_column` uses to loft a swelling shaft, just walked along an arbitrary line instead of
    straight up Z -- means there is no rotation matrix to get wrong. Keeping the section square
    (rather than a flat rectangle) also means the loft's own roll around the axis, which is
    otherwise unpredictable, never shows.
    """
    a, b = cq.Vector(*a), cq.Vector(*b)
    length = (b - a).Length
    pl = cq.Plane(origin=a, normal=(b - a))
    return (cq.Workplane(pl).rect(size, size)
            .workplane(offset=length).rect(size, size).loft())


@piece("stair_run", tris=900,
       note="five steps climbing to a doorway, closed strings, half-round nosing")
def stair_run(steps=5, going=0.30, rise=0.18, width=1.60, nose=0.03, string=0.14):
    """A straight flight that climbs toward the wall it serves.

    Y = 0 is the doorway at the head of the flight; the stair runs downhill in +Y to the yard,
    `steps * going` out. Each step is modelled as a solid slab standing the full height of its
    own tread, base to top -- stacking five slabs, tallest at the wall and shortest at the yard,
    already produces every riser and tread without a single Boolean cut, which is the cheapest
    way to get a manifold-looking stair out of five boxes.
    """
    solids = []
    for i in range(1, steps + 1):
        y_front = (steps - i + 1) * going    # this step's nose -- its riser's outward face
        z_top = i * rise                     # this step's tread height
        solids.append(box(width, y_front, z_top))

        # A half-round bullnose on every tread: a semicircular bead whose flat side sits flush
        # on the riser below (so it needs no boolean to attach) and whose curve projects `nose`
        # out past it, catching the light along the front of each step.
        nosing = (cq.Workplane("YZ")
                  .moveTo(y_front, z_top - 2 * nose).lineTo(y_front, z_top)
                  .threePointArc((y_front + nose, z_top - nose), (y_front, z_top - 2 * nose))
                  .close().extrude(width))
        solids.append(nosing.translate((-width / 2.0, 0, 0)))

    # Closed strings: a sloping board down each side whose top edge runs parallel to the line
    # through the nosings but `margin` above it, so the steps read as let into a solid board
    # rather than as an exposed sawtooth. It levels off once the flight reaches the doorway,
    # the way a real stringer squares off at the landing.
    margin = 0.10
    bottom_nose = (steps * going, rise)
    top_nose = (going, steps * rise)
    side = (cq.Workplane("YZ")
            .moveTo(0, 0).lineTo(bottom_nose[0], 0)
            .lineTo(bottom_nose[0], bottom_nose[1] + margin)
            .lineTo(top_nose[0], top_nose[1] + margin)
            .lineTo(0, top_nose[1] + margin)
            .close().extrude(string))
    solids.append(side.translate((width / 2.0, 0, 0)))
    solids.append(side.translate((-width / 2.0 - string, 0, 0)))

    return {"paint": solids}


@piece("pergola_beam_set", tris=800, free=True,
       note="one 3.2 m pergola bay: two posts, a pair of girders, nine rafters")
def pergola_beam_set(span=3.20, post=0.18, post_h=2.55, head_chamfer=0.02,
                      girder_y=0.55, girder_t=0.10, girder_d=0.30, girder_len=3.60,
                      taper=0.30, rafter_n=9, rafter_w=0.08, rafter_d=0.16, rafter_span=3.00):
    """One bay of the trellis that runs the length of the cabana row.

    Everything is drawn about x = 0 so a run of bays tiles by stepping x by `span`, post to
    post. The rafters run the full `rafter_span` while the girders they rest on sit well inboard
    of that at +/- `girder_y` -- the overhang is deliberate, a couple of feet of exposed rafter
    tail is what throws the long slatted shadow a pergola is built for.
    """
    z_girder0 = post_h                  # top of post = underside of girder
    z_girder1 = post_h + girder_d       # top of girder = underside of rafter

    posts = [(cq.Workplane("XY").box(post, post, post_h, centered=(True, True, False))
              .faces(">Z").chamfer(head_chamfer)).translate((x, 0, 0))
             for x in (-span / 2.0, span / 2.0)]

    # A girder's side profile: flat along the top, its underside cut away on a straight taper
    # for the last `taper` metres so it comes to a point at the tip instead of a blunt square
    # end -- the "downward taper" the brief asks for, without needing a curve at all.
    half = girder_len / 2.0
    profile = (cq.Workplane("XZ")
               .moveTo(-half, z_girder1).lineTo(-half + taper, z_girder0)
               .lineTo(half - taper, z_girder0).lineTo(half, z_girder1)
               .close().extrude(girder_t))
    girders = [profile.translate((0, y + girder_t / 2.0, 0)) for y in (-girder_y, girder_y)]

    pitch = rafter_span / (rafter_n - 1) if rafter_n > 1 else 0.0
    rafters = [cq.Workplane("XY").box(rafter_w, rafter_span, rafter_d, centered=(True, True, False))
               .translate((x, 0, z_girder1))
               for x in row(rafter_n, pitch)]

    return {"wood": posts + girders + rafters}


@piece("lamp_bracket", tris=700,
       note="wall lantern on a scrolled iron arm, glass body")
def lamp_bracket(plate_w=0.18, plate_p=0.04, plate_h=0.30,
                  arm=0.035, reach=0.55, glass=0.20, glass_h=0.26, cap=0.26, post=0.025):
    """A lantern slung off a scrolled bracket, the way the veranda lights are hung.

    Z = 0 is the foot of the backplate, which is also the piece's only contact with Z = 0 --
    the arm has to climb well above the plate before it turns down, because the lantern that
    hangs from its hook is not allowed to dip back below the mounting foot.
    """
    backplate = (box(plate_w, plate_p, plate_h)
                 .faces(">Y").chamfer(plate_p * 0.35))

    # The arm: up off the plate, out and further up to a peak past `reach`, then straight down
    # to the hook. Three bars is enough to read as a scroll without sweeping a real curve.
    a0 = (0, plate_p, plate_h * 0.67)
    a1 = (0, reach * 0.55, plate_h * 1.8)
    a2 = (0, reach, plate_h * 2.6)
    a3 = (0, reach, plate_h * 1.8)
    arm_bars = [_bar(a0, a1, arm), _bar(a1, a2, arm), _bar(a2, a3, arm)]

    hook_z = a3[2]
    cap_h = plate_h * 0.13
    cap_solid = box(cap, cap, cap_h, y0=reach - cap / 2.0, z0=hook_z - cap_h)
    glass_z0 = hook_z - cap_h - glass_h - 0.02
    glass_solid = box(glass, glass, glass_h, y0=reach - glass / 2.0, z0=glass_z0)
    post_h = hook_z - cap_h - glass_z0
    posts = [box(post, post, post_h, y0=y0, z0=glass_z0).translate((x, 0, 0))
             for x in (-glass / 2.0 + post / 2.0, glass / 2.0 - post / 2.0)
             for y0 in (reach - glass / 2.0, reach + glass / 2.0 - post)]
    finial = cq.Solid.makeCone(post * 1.2, 0.0, 0.06, cq.Vector(0, reach, glass_z0 - 0.06))

    return {
        "metal": [backplate] + arm_bars + [cap_solid] + posts + [finial],
        "glass": [glass_solid],
    }


@piece("stripe_panel", tris=200,
       note="1 m band course, tiles flush, chamfered top and bottom for a real shadow line")
def stripe_panel(width=1.00, height=0.90, depth=0.07, edge=0.02):
    """A course of the striped pavilion's banding, modelled rather than painted on.

    The ends stay flat verticals at x = +/- width/2 so courses tile edge to edge with nothing to
    hide; only the horizontal edges of the proud face are chamfered, which is what a level can
    never fake with a flat texture: a real shadow line where one course laps the next.
    """
    panel = box(width, depth, height)
    return {"paint": panel.faces(">Y").edges("|X").chamfer(edge)}


@piece("cistern_cap", tris=900, free=True,
       note="conical tank roof with a rolled eave, six standing seams and a vent")
def cistern_cap(base_r=2.40, top_r=0.25, height=1.40, eave_r=2.50, lip_h=0.10,
                 seams=6, seam_size=0.05, vent_r=0.20, vent_h=0.35, hat_h=0.15):
    """The roof and vent of a water tower, sized to a 4.8 m tank.

    The rolled eave is drawn as a small outward kink in the revolve profile rather than an
    actual torus: at this radius a true torus tessellates far more finely than a 0.10 m bead is
    worth, and a two-segment kink reads the same from the ground.
    """
    profile = [(0, 0), (base_r, 0), (eave_r, lip_h / 2.0), (base_r, lip_h),
               (top_r, height), (0, height)]
    body = revolve(profile)

    seam_solids = []
    for (bx, by), (tx, ty) in zip(ring(seams, base_r + seam_size), ring(seams, top_r + seam_size)):
        seam_solids.append(_bar((bx, by, lip_h), (tx, ty, height), seam_size))

    vent = cq.Solid.makeCylinder(vent_r, vent_h, cq.Vector(0, 0, height), cq.Vector(0, 0, 1))
    hat = cq.Solid.makeCone(vent_r, vent_r * 0.1, hat_h,
                             cq.Vector(0, 0, height + vent_h), cq.Vector(0, 0, 1))

    return {"metal": [body] + seam_solids + [vent, hat]}
