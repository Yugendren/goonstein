"""The shared rules every architecture kit piece obeys.

A piece is a Python function that returns solids. `build_kit.py` turns each one into

    tools/cad/out/NAME.step          the editable CAD body (Z up, real millimetres of intent
                                     expressed in metres), coloured per material
    assets/models/own/arch/NAME.obj  the game mesh (Y up, metres), one material per colour
    assets/models/own/arch/NAME.mtl  flat Kd colours, no maps

THE FRAME, once and for all
---------------------------
Model in CAD convention, the way the user's CAD tool wants it:

    +Z is up.
    +X is along the width of the piece, and the piece is centred on X = 0.
    +Y is outward -- away from the wall it hangs on. The mounting plane is Y = 0
      and the body of the piece lives at Y >= 0.
    The base of the piece is Z = 0. Nothing may dip below it.

`build_kit.py` converts to the game's axes with a single -90 degrees about X:

    game (x, y, z)  =  cad (x, z, -y)

so a piece mounted on a wall face protrudes toward game -Z, which is the direction every
building on this island faces. Place it at the wall's coordinates and it is already right.

The Z = 0 rule is not cosmetic. src/model.c rebases an OBJ's minimum Y to zero on load, so a
piece that floats above its own origin in CAD silently drops onto it in game. build_kit.py
refuses to export a piece whose base is not 0.

COLOUR
------
An OBJ prop is loaded as a single white-textured mesh (model_from_triangles), so the OBJ's
UVs are never sampled -- which is why nothing here generates any. Colour reaches the game two
ways instead, and both multiply:

  * the MTL's Kd becomes the vertex colour. `paint` is deliberately the same 0.78 grey as
    assets/models/shapes/paint.mtl, so every tint number already written in a .part file
    means exactly what it meant on a box.
  * `tex NAME TILE` on the .part line projects a world texture in world space at TILE repeats
    per metre, ignoring the mesh entirely. A piece's size therefore never stretches its stone.

So: use `paint` for anything the level should be free to tint and texture, and a named colour
only where the colour is the point (dark glass in a recess, gold on the temple).

BUDGET
------
Under ~2000 triangles a piece, under 30000 for the kit. Curves are tessellated to `tol` and
`ang` and then decimated if they overshoot. A balustrade is not a place for a 64-sided baluster.
"""

import math

import cadquery as cq

# ---------------------------------------------------------------- materials
# name -> sRGB Kd written into the .mtl and into the STEP's colours.
MATERIALS = {
    "paint": (0.78, 0.78, 0.78),   # neutral: the .part's tint and tex decide what this is
    "trim":  (0.88, 0.88, 0.86),   # a shade brighter than paint, for mouldings caught by the sun
    "dark":  (0.09, 0.10, 0.12),   # the inside of a recess; reads as an opening, never as a wall
    "glass": (0.13, 0.16, 0.20),   # the same near-black the hand-placed window rectangles use
    "wood":  (0.42, 0.31, 0.20),   # teak shutters, pergola beams, dock timber
    "gold":  (0.95, 0.78, 0.32),   # the temple only
    "blue":  (0.18, 0.36, 0.74),   # the temple's cobalt stripe
    "metal": (0.35, 0.36, 0.40),   # brackets, rails, the cistern cap
}

# Tessellation defaults. A flat-sided piece ignores them; a dome does not.
TOL = 0.004      # metres of chordal deviation
ANG = 0.35       # radians of angular deviation

# ---------------------------------------------------------------- the registry
# name -> (function, triangle budget, one-line note for the README table, free-standing?)
PIECES = {}


def piece(name, tris=2000, note="", free=False):
    """Register a kit piece.

    The decorated function takes only keyword parameters with defaults -- the defaults are what
    the kit ships, the parameters are what the user changes -- and returns either a single solid
    or a dict of {material name: solid or list of solids}.

    `free=True` marks a free-standing piece -- a column, a baluster, a dome, a finial -- whose
    axis belongs at the origin rather than on a wall. It is then centred on Y as well as X, and
    you place it at the thing's centre instead of at a wall face. Everything else is mounted:
    Y = 0 is the wall and the body stands in front of it.
    """
    def deco(fn):
        if name in PIECES:
            raise KeyError("two kit pieces called %r" % name)
        PIECES[name] = (fn, tris, note or (fn.__doc__ or "").strip().split("\n")[0], free)
        return fn
    return deco


def normalise(result):
    """Whatever a piece function returned -> {material: [cq.Shape, ...]}."""
    if not isinstance(result, dict):
        result = {"paint": result}
    out = {}
    for mat, val in result.items():
        if mat not in MATERIALS:
            raise KeyError("unknown material %r (have %s)" % (mat, ", ".join(sorted(MATERIALS))))
        items = val if isinstance(val, (list, tuple)) else [val]
        shapes = []
        for it in items:
            if it is None:
                continue
            if isinstance(it, cq.Workplane):
                shapes.extend(s for s in it.vals() if isinstance(s, cq.Shape))
            elif isinstance(it, cq.Shape):
                shapes.append(it)
            else:
                raise TypeError("a piece may only return Workplanes and Shapes, got %r" % type(it))
        if shapes:
            out[mat] = shapes
    if not out:
        raise ValueError("piece produced no solids")
    return out


# ---------------------------------------------------------------- small helpers pieces share
def box(w, d, h, centre_x=True, y0=0.0, z0=0.0):
    """A box w wide (X), d deep (Y), h tall (Z), sitting at y0/z0, centred across X by default."""
    wp = cq.Workplane("XY").box(w, d, h, centered=(centre_x, False, False))
    return wp.translate((0, y0, z0))


def moulding(profile, length, axis="X"):
    """Sweep a 2D profile of (y, z) points along `length`, centred on the axis.

    The profile is drawn in the YZ plane -- y outward, z up -- which is how a cornice, a sill,
    a coping or a roof edge is drawn in any CAD tool: you draw the section and pull it along.
    """
    pts = [(p[0], p[1]) for p in profile]
    wp = cq.Workplane("YZ").polyline(pts).close().extrude(length)
    # extrude off the YZ plane runs along -X; recentre it on X = 0.
    solid = wp.val()
    bb = solid.BoundingBox()
    return cq.Workplane(obj=solid).translate((-(bb.xmin + bb.xmax) / 2.0, 0, 0))


def arch_profile(width, pier, height, rise, depth):
    """A flat plate with a round-headed opening cut out of it: the shape of an arched wall.

    width  overall, pier   the solid each side of the opening, height  to the springing,
    rise   the half-round above it, depth  the thickness in Y.
    """
    opening = width - 2 * pier
    plate = box(width, depth, height + rise + pier)
    cut = (cq.Workplane("XZ").moveTo(-opening / 2, 0).lineTo(-opening / 2, height)
           .threePointArc((0, height + rise), (opening / 2, height))
           .lineTo(opening / 2, 0).close()
           .extrude(depth * 3).translate((0, -depth, 0)))
    return plate.cut(cut)


def revolve(section, angle=360.0):
    """Revolve a list of (radius, z) points about the Z axis. The profile is closed for you."""
    pts = [(p[0], p[1]) for p in section]
    return (cq.Workplane("XZ").polyline(pts).close()
            .revolve(angle, (0, 0, 0), (0, 1, 0)))


def ring(n, radius, start=0.0):
    """n evenly spaced (x, y) points on a circle -- balusters, columns, finial stubs."""
    return [(radius * math.cos(start + 2 * math.pi * i / n),
             radius * math.sin(start + 2 * math.pi * i / n)) for i in range(n)]


def row(n, pitch):
    """n x-coordinates at `pitch` apart, centred on 0."""
    return [(i - (n - 1) / 2.0) * pitch for i in range(n)]
