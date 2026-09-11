"""Windows, doors and the pool house's arched openings.

Every piece here overlays one of the flat black rectangles already painted onto a building, so
each one carries its own dark or glazed pane sitting flush at Y = 0 -- nothing is ever cut out
of the wall itself, the frame just stands proud of the paint.
"""

import cadquery as cq

from kitlib import box, moulding, piece


# ---------------------------------------------------------------- shared construction
def _weathered_slab(width, h, proj, slope=0.015, drip=0.015, groove=0.03, nose=0.02):
    """A window sill or a door threshold: full height at the wall, a sloped top so rain runs
    off instead of pooling, and a drip step recessed into the underside so it can't creep back
    along the slab to the wall. `nose` is the little roll at the leading edge that gives the
    drip step somewhere to be, and it is the deepest single point on the slab.
    """
    profile = [
        (0, 0),
        (0, h),
        (proj + nose, h - slope),
        (proj + nose, h - slope - drip),
        (proj + nose - groove, h - slope - drip),
        (proj + nose - groove, 0),
    ]
    return moulding(profile, width)


def _architrave(opening_w, opening_h, member_w, proud, z0):
    """A rectangular moulded frame around an opening: a box the size of the opening plus the
    member all round, minus the opening itself, standing `proud` out from the wall. Returns the
    frame plus its own outer width/height so callers can line up a sill or a shutter against it
    without recomputing the sum themselves.
    """
    outer_w = opening_w + 2 * member_w
    outer_h = opening_h + 2 * member_w
    outer = box(outer_w, proud, outer_h, y0=0.0, z0=z0)
    inner = box(opening_w, proud * 3, opening_h, y0=-proud, z0=z0 + member_w)
    return outer.cut(inner), outer_w, outer_h


# ---------------------------------------------------------------- windows
def _window(*, opening_w, opening_h, sill_h, sill_proj, sill_overhang, shutter_w,
            cornice_h=0.0, cornice_proj=0.0, cornice_overhang=0.0,
            arch_w=0.10, arch_proud=0.07, shutter_proud=0.05, gap=0.04):
    """The body shared by window_small and window_large: a shuttered opening framed by a
    moulded architrave, standing on a weathered sill, with an optional cornice head for the
    larger size. Only the numbers change between the two -- this is the one place the geometry
    is built.
    """
    z_frame = sill_h                 # the architrave loop rests on top of the sill
    z_opening = sill_h + arch_w      # the glass, and the shutters beside it, start here

    architrave, outer_w, outer_h = _architrave(opening_w, opening_h, arch_w, arch_proud, z_frame)

    sill_w = outer_w + 2 * sill_overhang
    sill = _weathered_slab(sill_w, sill_h, sill_proj)

    trim = [sill, architrave]
    if cornice_h:
        cornice_w = outer_w + 2 * cornice_overhang
        cornice = box(cornice_w, cornice_proj, cornice_h, y0=0.0, z0=z_frame + outer_h)
        trim.append(cornice)

    # Shutters: teak leaves the height of the glass only (a shutter covers the glazing, not the
    # architrave), standing just clear of the frame, each with three louvre grooves cut into the
    # face the way a real louvred shutter is boarded.
    shutter_x = outer_w / 2 + gap + shutter_w / 2
    louvre_h = 0.03
    shutters = []
    for side in (-1, 1):
        leaf = box(shutter_w, shutter_proud, opening_h, y0=0.0, z0=z_opening)
        for i in range(3):
            gz = z_opening + opening_h * (i + 1) / 4.0
            groove = box(shutter_w + 0.02, 0.03, louvre_h, y0=shutter_proud - 0.02, z0=gz)
            leaf = leaf.cut(groove)
        shutters.append(leaf.translate((side * shutter_x, 0, 0)))

    # Glass fills the opening flush on the wall; the glazing bars stand a hair proud of it so
    # they never share a face with the pane.
    glass = box(opening_w, 0.02, opening_h, y0=0.0, z0=z_opening)
    bar_w = 0.04
    vbar = box(bar_w, 0.025, opening_h, y0=0.0, z0=z_opening)
    hbar = box(opening_w, 0.025, bar_w, y0=0.0, z0=z_opening + opening_h / 2 - bar_w / 2)

    return {"trim": trim, "wood": shutters, "glass": [glass], "paint": [vbar, hbar]}


@piece("window_small", tris=700,
       note="shuttered window, moulded sill and architrave, 0.90 x 1.20 opening")
def window_small(opening_w=0.90, opening_h=1.20, sill_h=0.10, sill_proj=0.16, sill_overhang=0.10,
                  shutter_w=0.30, arch_w=0.10, arch_proud=0.07, shutter_proud=0.05, gap=0.04):
    """A small shuttered window: architrave, weathered sill and a pair of louvred shutters."""
    return _window(opening_w=opening_w, opening_h=opening_h, sill_h=sill_h, sill_proj=sill_proj,
                    sill_overhang=sill_overhang, shutter_w=shutter_w, arch_w=arch_w,
                    arch_proud=arch_proud, shutter_proud=shutter_proud, gap=gap)


@piece("window_large", tris=900,
       note="shuttered window with cornice head, 1.20 x 1.90 opening")
def window_large(opening_w=1.20, opening_h=1.90, sill_h=0.15, sill_proj=0.18, sill_overhang=0.10,
                  shutter_w=0.40, cornice_h=0.10, cornice_proj=0.14, cornice_overhang=0.06,
                  arch_w=0.10, arch_proud=0.07, shutter_proud=0.05, gap=0.03):
    """The same window as window_small, scaled up, with a moulded cornice over the head."""
    return _window(opening_w=opening_w, opening_h=opening_h, sill_h=sill_h, sill_proj=sill_proj,
                    sill_overhang=sill_overhang, shutter_w=shutter_w, cornice_h=cornice_h,
                    cornice_proj=cornice_proj, cornice_overhang=cornice_overhang, arch_w=arch_w,
                    arch_proud=arch_proud, shutter_proud=shutter_proud, gap=gap)


# ---------------------------------------------------------------- doors
@piece("french_door", tris=1100,
       note="double glazed door, moulded cornice and threshold, 1.60 x 2.60 opening")
def french_door(opening_w=1.60, opening_h=2.60, arch_w=0.12, arch_proud=0.08,
                 cornice_h=0.08, cornice_proj=0.16, cornice_overhang=0.14,
                 threshold_h=0.08, threshold_proj=0.20, threshold_overhang=0.12,
                 leaf_w=0.78, leaf_thickness=0.06, stile_w=0.10, bar_w=0.04, meeting_w=0.05):
    """A pair of glazed leaves in a moulded surround: architrave, cornice head and threshold,
    each leaf a stile-and-rail frame around a panel of glass divided by bars.
    """
    z_frame = threshold_h
    z_opening = threshold_h + arch_w
    leaf_y0 = 0.02  # the leaves stand clear of the wall by this reveal, per the brief

    architrave, outer_w, outer_h = _architrave(opening_w, opening_h, arch_w, arch_proud, z_frame)

    threshold_w = outer_w + 2 * threshold_overhang
    threshold = _weathered_slab(threshold_w, threshold_h, threshold_proj)

    cornice_w = outer_w + 2 * cornice_overhang
    cornice = box(cornice_w, cornice_proj, cornice_h, y0=0.0, z0=z_frame + outer_h)

    # The dark recess is the fallback: it only shows where the leaves don't reach, but every
    # piece in this file gets one so a reveal never reads as bare wall paint.
    dark = box(opening_w, 0.02, opening_h, y0=0.0, z0=z_opening)

    panel_w = leaf_w - 2 * stile_w
    panel_h = opening_h - 2 * stile_w
    leaves, glass, bars = [], [], []
    for side in (-1, 1):
        outer_leaf = box(leaf_w, leaf_thickness, opening_h, y0=leaf_y0, z0=z_opening)
        panel_hole = box(panel_w, leaf_thickness * 3, panel_h,
                          y0=leaf_y0 - leaf_thickness, z0=z_opening + stile_w)
        frame = outer_leaf.cut(panel_hole)
        pane = box(panel_w, leaf_thickness, panel_h, y0=leaf_y0, z0=z_opening + stile_w)
        vbar = box(bar_w, leaf_thickness + 0.01, panel_h, y0=leaf_y0, z0=z_opening + stile_w)
        hbars = [box(panel_w, leaf_thickness + 0.01, bar_w, y0=leaf_y0,
                      z0=z_opening + stile_w + panel_h * (i + 1) / 4.0 - bar_w / 2)
                 for i in range(3)]

        x0 = side * leaf_w / 2  # the leaves meet at the centre line and hang from their outer jamb
        leaves.append(frame.translate((x0, 0, 0)))
        glass.append(pane.translate((x0, 0, 0)))
        bars.append(vbar.translate((x0, 0, 0)))
        bars.extend(hb.translate((x0, 0, 0)) for hb in hbars)

    meeting_stile = box(meeting_w, leaf_thickness + 0.01, opening_h, y0=leaf_y0, z0=z_opening)
    leaves.append(meeting_stile)

    return {"trim": [threshold, architrave, cornice], "wood": leaves + bars,
            "glass": glass, "dark": [dark]}


@piece("plain_door", tris=800, note="four-panel plain door, 1.00 x 2.20 opening")
def plain_door(opening_w=1.00, opening_h=2.20, arch_w=0.10, arch_proud=0.06,
               head_h=0.03, head_proud=0.08, threshold_h=0.05, threshold_proj=0.16,
               threshold_overhang=0.12, leaf_reveal=0.03, leaf_thickness=0.06,
               stile=0.08, mid_stile=0.06, mid_rail=0.08, panel_recess=0.02):
    """A solid wood leaf with four recessed panels -- two small over two tall -- in a plain
    architrave with a flat head board instead of a moulded cornice, and a small metal handle.
    """
    z_frame = threshold_h
    z_opening = threshold_h + arch_w
    leaf_y0 = 0.02

    architrave, outer_w, outer_h = _architrave(opening_w, opening_h, arch_w, arch_proud, z_frame)

    threshold_w = outer_w + 2 * threshold_overhang
    threshold = _weathered_slab(threshold_w, threshold_h, threshold_proj)

    head = box(outer_w, head_proud, head_h, y0=0.0, z0=z_frame + outer_h)

    dark = box(opening_w, 0.02, opening_h, y0=0.0, z0=z_opening)

    leaf_w = opening_w - 2 * leaf_reveal
    leaf = box(leaf_w, leaf_thickness, opening_h, y0=leaf_y0, z0=z_opening)

    # Four panels, arranged two small over two tall the way a Georgian four-panel door is set
    # out, each a shallow rectangular pocket cut into the leaf's front face.
    usable_h = opening_h - 2 * stile
    top_h = usable_h * 0.32
    bottom_h = usable_h - top_h - mid_rail
    panel_w = (leaf_w - 2 * stile - mid_stile) / 2
    cut_y0 = leaf_y0 + leaf_thickness - panel_recess
    for col in (-1, 1):
        cx = col * (mid_stile / 2 + panel_w / 2)
        z_bottom = z_opening + stile
        z_top = z_bottom + bottom_h + mid_rail
        leaf = leaf.cut(box(panel_w, panel_recess + 0.01, bottom_h,
                             y0=cut_y0, z0=z_bottom).translate((cx, 0, 0)))
        leaf = leaf.cut(box(panel_w, panel_recess + 0.01, top_h,
                             y0=cut_y0, z0=z_top).translate((cx, 0, 0)))

    handle = box(0.03, 0.015, 0.12, y0=leaf_y0 + leaf_thickness, z0=z_opening + 0.95)
    handle = handle.translate((leaf_w / 2 - 0.12, 0, 0))

    return {"trim": [threshold, architrave, head], "wood": [leaf], "metal": [handle],
            "dark": [dark]}


# ---------------------------------------------------------------- arched opening
def _arch_fill(width, height, rise, depth):
    """The solid that exactly fills a round-headed opening: the mirror image of the void that
    a plate needs cut out of it, used both as the piece's own recess panel and, further below,
    as the cutting tool for the surround and the archivolt.
    """
    wp = (cq.Workplane("XZ").moveTo(-width / 2, 0).lineTo(-width / 2, height)
          .threePointArc((0, height + rise), (width / 2, height))
          .lineTo(width / 2, 0).close().extrude(depth))
    solid = wp.val()
    bb = solid.BoundingBox()
    # extrude's direction depends on the workplane's normal; recentre it on Y = 0..depth
    # regardless of which way it came out, the same way kitlib.moulding recentres on X.
    return cq.Workplane(obj=solid).translate((0, -bb.ymin, 0))


def _arch_plate(width, pier, height, rise, depth):
    """A flat plate, `depth` thick, with a round-headed opening cut through it.

    This is kitlib.arch_profile's own shape, rebuilt here: arch_profile's cutting tool comes out
    on the wrong side of Y (it lands entirely behind the plate rather than through it, so its
    .cut() is a no-op -- verified empirically, not something to route around quietly), so the
    plate is cut with _arch_fill's tool instead, which is built the same way but recentred onto
    Y = 0..depth before use.
    """
    opening = width - 2 * pier
    plate = box(width, depth, height + rise + pier)
    void = _arch_fill(opening, height, rise, depth * 3).translate((0, -depth, 0))
    return plate.cut(void)


@piece("arched_opening", tris=900,
       note="round-headed arch surround for the pool house, 1.80 m opening")
def arched_opening(opening_w=1.80, springing=2.00, rise=0.90, pier=0.35,
                    surround_depth=0.18, surround_y0=0.12, band_w=0.14, band_proud=0.08):
    """A semicircular arch surround: a plate with the arch cut through it, an archivolt band
    tracing the curve a step further proud, a keystone at the crown and impost blocks where the
    arch springs from its piers -- and, behind all of it, a dark plate filling the opening
    itself so the arch always reads as a hole even before anyone walks through it.
    """
    surround_w = opening_w + 2 * pier
    surround = _arch_plate(surround_w, pier, springing, rise, surround_depth)
    surround = surround.translate((0, surround_y0, 0))

    # The archivolt is two concentric arched plates cut from one another. Because rise + pier is
    # always half the plate's width, both share exactly the same bounding box, so the only thing
    # the subtraction leaves behind is the band between their two curves.
    rise_out = rise + band_w
    band_half = rise_out + 0.10
    inner_ring = _arch_plate(2 * band_half, band_half - rise, springing, rise, band_proud)
    outer_ring = _arch_plate(2 * band_half, band_half - rise_out, springing, rise_out, band_proud)
    band_y0 = surround_y0 + surround_depth
    archivolt = inner_ring.cut(outer_ring).translate((0, band_y0, 0))

    # Keystone and imposts sit flush with the archivolt's own face rather than standing further
    # out again -- they read as part of the same band, just picked out as separate blocks.
    key_w = pier * 0.7
    key_h = 0.30
    keystone = box(key_w, band_proud, key_h, y0=band_y0, z0=springing + rise - key_h * 0.55)

    imp_w = pier * 0.7
    imp_h = 0.22
    imp_x = opening_w / 2 + pier / 2
    imposts = [box(imp_w, band_proud, imp_h, y0=band_y0,
                    z0=springing - imp_h * 0.6).translate((side * imp_x, 0, 0))
               for side in (-1, 1)]

    dark = _arch_fill(opening_w, springing, rise, 0.02)

    return {"paint": [surround], "trim": [archivolt, keystone] + imposts, "dark": [dark]}
