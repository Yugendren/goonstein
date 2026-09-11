"""The reference piece. Read this one before writing another.

It shows the whole contract: keyword parameters with the shipped values as defaults, CAD axes
(Z up, X across, +Y outward, base at Z=0, centred on X), and a return value of
{material: solid} so one OBJ carries more than one flat colour at the cost of one draw call.
"""

import cadquery as cq

from kitlib import piece


@piece("wall_column", tris=900, free=True, note="square veranda column, 4.6 m, moulded base and capital")
def wall_column(height=4.6, shaft=0.46, base=0.62, base_h=0.20,
                cap=0.66, cap_h=0.26, neck=0.06, entasis=0.02):
    """A square column with a stepped base and a flared capital.

    `entasis` is the classical swell: the shaft is `entasis` wider at a third of its height than
    at the top, which is what stops a straight prism from looking like scaffolding. Keep it small.
    """
    h_shaft = height - base_h - cap_h

    # Base: a plinth and a torus-ish chamfered step.
    plinth = cq.Workplane("XY").box(base, base, base_h * 0.6, centered=(True, True, False))
    step = (cq.Workplane("XY").workplane(offset=base_h * 0.6)
            .rect(base, base).workplane(offset=base_h * 0.4).rect(shaft + 0.06, shaft + 0.06)
            .loft())

    # Shaft: bottom, swell, top -- a loft through three squares.
    swell = shaft + entasis
    body = (cq.Workplane("XY").workplane(offset=base_h)
            .rect(shaft + 0.02, shaft + 0.02)
            .workplane(offset=h_shaft * 0.33).rect(swell, swell)
            .workplane(offset=h_shaft * 0.67).rect(shaft - 0.01, shaft - 0.01)
            .loft())

    # Neck ring and a capital that flares out to carry the beam above it.
    z = base_h + h_shaft
    collar = (cq.Workplane("XY").workplane(offset=z - neck)
              .box(shaft + 0.05, shaft + 0.05, neck, centered=(True, True, False)))
    flare = (cq.Workplane("XY").workplane(offset=z)
             .rect(shaft + 0.02, shaft + 0.02).workplane(offset=cap_h * 0.65).rect(cap, cap).loft())
    abacus = (cq.Workplane("XY").workplane(offset=z + cap_h * 0.65)
              .box(cap, cap, cap_h * 0.35, centered=(True, True, False)))

    # free=True: the column's axis is the origin, so it is placed at the column's centre rather
    # than at a wall face. Nothing is offset in Y.
    solids = [plinth, step, body, collar, flare, abacus]
    return {"paint": solids}
