"""The building-shell contract: what a whole building is, and the architecture it is made of.

The kit (kitlib.py) makes *pieces* -- a window, a column, a cornice -- that hang on a wall.
This makes the wall. A shell is a whole building: walls with real thickness and openings cut
through them, a roof with eaves, a plinth, a parapet, a veranda. One generator function per
building; `build_shells.py` turns each one into

    tools/cad/out/NAME.step               the editable CAD body, coloured per group
    assets/models/own/shell/NAME_GROUP.obj  one game mesh per material group
    assets/models/own/NAME.part           the .part that places those meshes and skins them

THE FRAME
---------
Same CAD convention as the kit, and for the same reason -- it is what a CAD tool wants:

    +Z is up, and the base of the building is Z = 0.
    +Y is the FRONT. Every building on this island faces game -Z, and the exporter's
      -90 about X turns cad +Y into game -Z, so the face you draw toward +Y is the face
      that looks out over the courtyard.
    +X is the building's width, exactly as in game.

    game (x, y, z) = cad (x, z, -y)

A shell is centred on X *and* on Y -- it is a footprint, not something mounted on a wall --
and its base is Z = 0, because src/model.c rebases an OBJ's lowest vertex to Y = 0 on load.
`build_shells.py` refuses to export a shell that breaks any of those.

GROUPS, AND WHY A SHELL IS SEVERAL OBJs
---------------------------------------
`tex NAME TILE` in a .part is per *piece*, i.e. per model file, so one OBJ can wear exactly one
world texture. A building needs plaster on its walls, tile on its roof and stone underfoot, so a
shell is exported as one OBJ per material group and the .part places them on top of each other
at the origin. Four or five draw calls for a whole villa, against the forty-seven boxes it used
to be, and every copy of a cabana still batches with every other copy.

A generator therefore returns {group name: solids} and declares a SKIN for those groups:

    SKIN = {
        "wall":  skin("plaster_cream", 0.5, (1.57, 1.74, 2.39)),
        "roof":  skin("roof_tile", 1.2, (0.42, 1.53, 1.97)),
        "dark":  skin(mat="dark"),
    }

`skin(tex, tile, tint)` uses kitlib's `paint` material -- the same 0.78 grey as
assets/models/shapes/paint.mtl -- so every tint number already written against a box means
exactly what it meant on a box. Only where the colour is the point (the black of a reveal, the
cobalt of the Music Room) does a group name one of kitlib's other materials instead.

BUDGET
------
Each shell declares its own triangle budget and `build_shells.py` fails if the geometry
overshoots. Nothing is decimated: a collapse modifier eats window reveals and eave lines, which
are the whole reason this exists. If a building is over, take geometry out of it.
"""

import math
from collections import namedtuple

import cadquery as cq

from kitlib import MATERIALS, box  # noqa: F401  (box is re-exported for generators)

# ---------------------------------------------------------------- the registry
Skin = namedtuple("Skin", "mat tex tile tint")

SHELLS = {}


def skin(tex=None, tile=1.0, tint=(1.0, 1.0, 1.0), mat="paint"):
    """How a group is dressed in the .part: a world texture, its repeats per metre, and a tint.

    `mat` is a kitlib material and becomes the OBJ's vertex colour. Leave it at `paint` -- the
    neutral 0.78 grey -- for anything the level should be free to tint and texture, and name a
    real colour only where the colour is the point (`dark` inside a reveal, `blue` on a stripe).
    """
    if mat not in MATERIALS:
        raise KeyError("unknown material %r" % mat)
    return Skin(mat, tex, float(tile), tuple(float(c) for c in tint))


def shell(name, tris=8000, note="", skins=None, collide=None):
    """Register a building shell.

    `skins` maps every group the function returns to a `skin(...)`. `collide` is the collider the
    generated .part declares for itself, as `(half_width, height)` or `(half_width, height, True)`
    for a deck -- most buildings state their collider in island.txt instead, per placement, and
    leave this None.
    """
    def deco(fn):
        if name in SHELLS:
            raise KeyError("two shells called %r" % name)
        SHELLS[name] = dict(fn=fn, tris=tris, skins=skins or {}, collide=collide,
                            note=note or (fn.__doc__ or "").strip().split("\n")[0])
        return fn
    return deco


def normalise(result):
    """Whatever a generator returned -> {group: [cq.Shape, ...]}, with empties dropped."""
    if not isinstance(result, dict):
        raise TypeError("a shell must return {group: solids}, not %r" % type(result))
    out = {}
    for group, val in result.items():
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
                raise TypeError("a shell may only return Workplanes and Shapes, got %r" % type(it))
        if shapes:
            out[group] = shapes
    if not out:
        raise ValueError("shell produced no solids")
    return out


# ---------------------------------------------------------------- footprints and walls
def slab(w, d, h, z0=0.0, x=0.0, y=0.0):
    """A plain slab w (X) by d (Y) by h (Z), centred on x/y, sitting on z0."""
    return cq.Workplane("XY").box(w, d, h, centered=(True, True, False)).translate((x, y, z0))


def plinth(w, d, h, z0=0.0, chamfer=0.0):
    """The stone base a building stands on, optionally chamfered along its top edge."""
    s = slab(w, d, h, z0)
    if chamfer > 0:
        s = s.edges(">Z").chamfer(chamfer)
    return s


def steps(width, count=2, going=0.34, rise=0.16, y0=0.0, z0=0.0):
    """A flight falling away from a doorway at y0 toward the yard in +Y, which is the front.

    Each step is a slab standing its full height from z0 up: the tallest is also the shallowest
    and sits against the wall, so stacking `count` of them is every riser and tread without a
    single boolean -- the trick `stair_run` uses in the kit, walked the other way.
    """
    out = []
    for i in range(1, count + 1):
        depth = (count - i + 1) * going
        out.append(slab(width, depth, i * rise, z0=z0, y=y0 + depth / 2.0))
    return out


def wall_ring(w, d, h, t, z0=0.0):
    """Four walls `t` thick around a w x d footprint: the outer box minus the room inside it.

    This is the thing that stops a building being a box. `t` is a real thickness, so an opening
    cut through it has a reveal you can see down, and a doorway you walk through has jambs.
    """
    outer = slab(w, d, h, z0)
    inner = slab(w - 2 * t, d - 2 * t, h + 2.0, z0=z0 - 1.0)
    return outer.cut(inner)


def floor_slab(w, d, t, z0=0.0):
    """The floor inside a wall ring -- what you stand on once you walk in through the door."""
    return slab(w, d, t, z0)


# ---------------------------------------------------------------- openings
# A face is named from outside, looking at the building. `u` runs along the face, 0 at its
# centre; it increases toward +X on the front, and anticlockwise around the plan after that.
FACES = {
    "front": (0.0, "d"),     # +Y, the side that ends up facing game -Z
    "back": (180.0, "d"),    # -Y
    "left": (90.0, "w"),     # -X
    "right": (-90.0, "w"),   # +X
}

Opening = namedtuple("Opening", "face u z0 w h arch reveal margin")


def hole(face, u, z0, w, h, arch=0.0, reveal=0.07, margin=0.11):
    """One opening in a wall.

    face    "front" | "back" | "left" | "right", named from outside
    u       along the face from its centre
    z0, w, h  the sill height, width and height of the hole itself
    arch    rise of a round head above `h`; 0 for a square head
    reveal  how deep the outer rebate is cut back around the hole, so a frame can sit IN the
            wall instead of on it. 0 leaves a plain hole with a square arris.
    margin  how much wider than the hole that rebate is, all round.
    """
    return Opening(face, u, z0, w, h, arch, reveal, margin)


def _prism(w, h, arch, y0, y1):
    """The cutting tool for one opening, drawn on the front face: a w x h rectangle (plus a
    round head of rise `arch`) swept from y0 to y1. Drawn at x = 0, z = 0.

    The XZ workplane's normal is -Y, so extruding runs from the plane toward -Y; the profile is
    drawn at y = y1 and pulled back to y0, which puts the tool exactly where the caller asked
    for it instead of behind the wall, where cutting with it is a silent no-op.
    """
    wp = cq.Workplane("XZ").moveTo(-w / 2.0, 0).lineTo(-w / 2.0, h)
    if arch > 0:
        wp = wp.threePointArc((0, h + arch), (w / 2.0, h))
    else:
        wp = wp.lineTo(w / 2.0, h)
    return wp.lineTo(w / 2.0, 0).close().extrude(y1 - y0).translate((0, y1, 0))


def opening_cutters(o, w, d, t):
    """The one or two solids that make opening `o` in a w x d building with `t` thick walls:
    the hole itself, and the rebate that gives it a reveal."""
    angle, which = FACES[o.face]
    face_y = (d if which == "d" else w) / 2.0
    tools = [_prism(o.w, o.h, o.arch, face_y - t - 0.05, face_y + 0.05).translate((o.u, 0, o.z0))]
    if o.reveal > 0 and o.margin > 0:
        tools.append(_prism(o.w + 2 * o.margin, o.h + o.margin, o.arch,
                            face_y - o.reveal, face_y + 0.05).translate((o.u, 0, o.z0)))
    return [s.rotate((0, 0, 0), (0, 0, 1), angle) for s in tools]


def cut_openings(solid, openings, w, d, t):
    """Cut every opening through a wall ring. Returns the wall."""
    for o in openings:
        for tool in opening_cutters(o, w, d, t):
            solid = solid.cut(tool)
    return solid


def opening_lining(o, w, d, t, thick=0.05):
    """A dark panel set at the back of an opening: the room beyond, for a building whose inside
    is not modelled. It sits flush with the INNER wall face, so you see the reveal in front of
    it rather than a black rectangle stuck on the wall."""
    angle, which = FACES[o.face]
    face_y = (d if which == "d" else w) / 2.0
    panel = _prism(o.w, o.h, o.arch, face_y - t - thick, face_y - t).translate((o.u, 0, o.z0))
    return panel.rotate((0, 0, 0), (0, 0, 1), angle)


def opening_sill(o, w, d, t, proj=0.07, h=0.09, over=0.10):
    """A weathered sill under a window: projects `proj` past the wall, `over` wider each side."""
    angle, which = FACES[o.face]
    face_y = (d if which == "d" else w) / 2.0
    s = slab(o.w + 2 * o.margin + 2 * over, o.reveal + proj + t * 0.5, h,
             z0=o.z0 - h, y=face_y + proj - (o.reveal + proj + t * 0.5) / 2.0).translate((o.u, 0, 0))
    return s.edges(">Z and >Y").chamfer(h * 0.3).rotate((0, 0, 0), (0, 0, 1), angle)


def opening_lintel(o, w, d, t, proj=0.06, h=0.16, over=0.10):
    """A flat lintel course over a square-headed opening."""
    angle, which = FACES[o.face]
    face_y = (d if which == "d" else w) / 2.0
    s = slab(o.w + 2 * o.margin + 2 * over, o.reveal + proj, h,
             z0=o.z0 + o.h + o.margin, y=face_y + proj - (o.reveal + proj) / 2.0)
    return s.translate((o.u, 0, 0)).rotate((0, 0, 0), (0, 0, 1), angle)


# ---------------------------------------------------------------- roofs
def hip_roof(w, d, rise, eave=0.55, ridge=None, z0=0.0):
    """A hipped roof over a w x d building: four slopes lofted from the eave to a ridge line.

    `eave` is the overhang past the wall on every side -- the thing a stack of wedges never had,
    and the single change that most stops a building reading as a box, because it puts the wall
    in the roof's shadow. `ridge` is the length of the ridge; the default keeps the hips at the
    same pitch as the long slopes.

    The ridge is lofted to a 1 mm sliver rather than a line: CadQuery will not loft to a
    degenerate wire, and 1 mm is invisible under the ridge tile that caps it anyway.
    """
    W, D = w + 2 * eave, d + 2 * eave
    if ridge is None:
        ridge = max(0.02, W - D) if W >= D else 0.02
    return (cq.Workplane("XY").rect(W, D).workplane(offset=rise)
            .rect(max(ridge, 0.002), 0.001).loft().translate((0, 0, z0)))


def gable_roof(w, d, rise, eave=0.45, over=0.35, z0=0.0):
    """Two slopes meeting on a ridge that runs along X: the plainer roof, for a plainer block.

    `eave` overhangs the long walls, `over` overhangs the gable ends.
    """
    W, D = w + 2 * over, d + 2 * eave
    prof = (cq.Workplane("YZ").moveTo(-D / 2.0, 0).lineTo(0, rise).lineTo(D / 2.0, 0)
            .close().extrude(W))
    return cq.Workplane(obj=prof.val()).translate((-W / 2.0, 0, z0))


def shed_roof(w, d, rise, eave=0.40, over=0.30, z0=0.0, thick=0.14):
    """A single plane falling from +Y to -Y: the roof a bunkhouse gets.

    Modelled as a real sloping slab of thickness `thick`, so its underside is a soffit you can
    stand beneath and its lower edge is a proper eave line rather than the arris of a wedge.
    """
    W, D = w + 2 * over, d + 2 * eave
    drop = thick / math.cos(math.atan2(rise, D))
    prof = (cq.Workplane("YZ").moveTo(D / 2.0, rise).lineTo(-D / 2.0, 0)
            .lineTo(-D / 2.0, -drop).lineTo(D / 2.0, rise - drop)
            .close().extrude(W))
    return cq.Workplane(obj=prof.val()).translate((-W / 2.0, 0, z0 + drop))


def eave_course(w, d, eave, h=0.20, depth=0.28, z0=0.0, bead=0.05):
    """The tile course at the edge of a roof: a ring of fascia hanging under the eave, its
    bottom outer edge beaded so it throws a shadow line the length of the building.

    This is what makes a roof stop at something instead of just ending. It is a ring rather than
    per-tile geometry on purpose -- seventy metres of eave at one barrel tile every 400 mm is
    seven thousand triangles for a thing the roof_tile texture already draws.
    """
    W, D = w + 2 * eave, d + 2 * eave
    ring = slab(W, D, h, z0=z0 - h).cut(slab(W - 2 * depth, D - 2 * depth, h + 2.0, z0=z0 - h - 1.0))
    if bead > 0:
        ring = ring.edges("<Z").chamfer(min(bead, h * 0.45, depth * 0.45))
    return ring


def ridge_tile(length, axis="X", w=0.34, h=0.16, z0=0.0):
    """The capping along a ridge: a low prism, chamfered, the width of two tiles."""
    if axis == "X":
        s = slab(length, w, h, z0=z0)
        return s.edges(">Z and |X").chamfer(min(h * 0.6, w * 0.4))
    s = slab(w, length, h, z0=z0)
    return s.edges(">Z and |Y").chamfer(min(h * 0.6, w * 0.4))


def stack(x, y, w, d, h, z0=0.0, cap=0.12, cap_over=0.08):
    """A chimney or vent stack with a capping slab: two solids, returned as a list."""
    return [slab(w, d, h, z0=z0, x=x, y=y),
            slab(w + 2 * cap_over, d + 2 * cap_over, cap, z0=z0 + h, x=x, y=y)
            .edges(">Z").chamfer(cap * 0.4)]


# ---------------------------------------------------------------- verandas, arcades, parapets
def parapet(w, d, h, t, z0=0.0, coping=0.10, over=0.06):
    """A low wall round the edge of a flat roof, with a coping that oversails it."""
    body = wall_ring(w, d, h - coping, t, z0=z0)
    cap = (slab(w + 2 * over, d + 2 * over, coping, z0=z0 + h - coping)
           .cut(slab(w - 2 * t - 2 * over, d - 2 * t - 2 * over, coping + 2.0, z0=z0 + h - coping - 1.0)))
    return [body, cap]


def veranda(w, d, t=0.22, z0=0.0, y=0.0, nose=0.03):
    """A real slab you can stand on, with a nosed edge. Give it a `collide ... deck` in the level
    and the players walk up onto it."""
    s = slab(w, d, t, z0=z0 - t, y=y)
    return s.edges(">Z").chamfer(nose) if nose > 0 else s


def arcade(w, d, h, n, pier, rise, z0=0.0, reveal=0.07, margin=0.11, end_pier=None):
    """A wall of `n` true round-headed arches: piers, springing, semicircular heads, cut through.

    Returns (wall, [Opening, ...]) so the caller can line the arches with a dark panel or leave
    them open. `pier` is the solid between two arches and `end_pier` the solid at each end
    (default: the same). The springing height is h - rise, so `h` is to the crown.
    """
    if end_pier is None:
        end_pier = pier
    span = (w - 2 * end_pier - (n - 1) * pier) / float(n)
    if span <= 0:
        raise ValueError("arcade: %d arches, %.2f piers and %.2f ends do not fit in %.2f" %
                         (n, pier, end_pier, w))
    wall = slab(w, d, h, z0=z0)
    us = [-w / 2.0 + end_pier + span / 2.0 + i * (span + pier) for i in range(n)]
    arches = [hole("front", u, z0, span, h - z0 - rise - (z0 - z0), arch=rise,
                   reveal=reveal, margin=margin) for u in us]
    # The arcade is a single wall, not a ring, so cut against its own depth in Y.
    for o in arches:
        for tool in [_prism(o.w, o.h, o.arch, -d, d).translate((o.u, 0, o.z0))]:
            wall = wall.cut(tool)
        if o.reveal > 0:
            wall = wall.cut(_prism(o.w + 2 * o.margin, o.h + o.margin, o.arch,
                                   d / 2.0 - o.reveal, d).translate((o.u, 0, o.z0)))
            wall = wall.cut(_prism(o.w + 2 * o.margin, o.h + o.margin, o.arch,
                                   -d, -d / 2.0 + o.reveal).translate((o.u, 0, o.z0)))
    return wall, arches


def band(w, d, h, z0=0.0, over=0.04, chamfer=0.02):
    """A string course round a building: a band that oversails the wall and catches the light."""
    s = slab(w + 2 * over, d + 2 * over, h, z0=z0)
    return s.edges("|Z").chamfer(chamfer) if chamfer > 0 else s


def pilaster(x, y, w, d, h, z0=0.0, chamfer=0.02):
    """A flat column standing proud of a wall at a corner."""
    s = slab(w, d, h, z0=z0, x=x, y=y)
    return s.edges("|Z").chamfer(chamfer) if chamfer > 0 else s


def post(x, y, r, h, z0=0.0, sides=10):
    """A turned post: a prism of `sides` sides, which at a veranda's scale reads round and costs
    a fifth of what a tessellated cylinder does."""
    pts = [(r * math.cos(2 * math.pi * i / sides), r * math.sin(2 * math.pi * i / sides))
           for i in range(sides)]
    return (cq.Workplane("XY").polyline(pts).close().extrude(h).translate((x, y, z0)))


def ladder(x, y, h, width=0.42, rail=0.05, rungs=None, pitch=0.32, z0=0.0):
    """A steel ladder up the side of a tank: two stiles and a rung every `pitch`."""
    if rungs is None:
        rungs = max(1, int(h / pitch) - 1)
    out = [slab(rail, rail, h, z0=z0, x=x - width / 2.0, y=y),
           slab(rail, rail, h, z0=z0, x=x + width / 2.0, y=y)]
    for i in range(1, rungs + 1):
        out.append(slab(width, rail * 0.7, rail * 0.7, z0=z0 + i * pitch, x=x, y=y))
    return out


# ---------------------------------------------------------------- fitting the kit into a reveal
# What each kit opening piece actually measures, so a shell can cut a hole the piece fits and a
# rebate the piece's architrave sits down into instead of standing on the wall like a picture
# frame. `base` is how far above the piece's own Z = 0 its opening starts (the sill or threshold
# plus the architrave member); `margin` is the architrave's member width and `reveal` how far it
# projects, which is exactly how deep the rebate has to be for it to finish flush.
KIT = {
    "window_small": dict(w=0.90, h=1.20, base=0.20, margin=0.10, reveal=0.07),
    "window_large": dict(w=1.20, h=1.90, base=0.25, margin=0.10, reveal=0.07),
    "plain_door":   dict(w=1.00, h=2.20, base=0.15, margin=0.10, reveal=0.06),
    "french_door":  dict(w=1.60, h=2.60, base=0.20, margin=0.12, reveal=0.08),
    "arched_opening": dict(w=1.80, h=2.00, base=0.00, margin=0.14, reveal=0.08, arch=0.90),
}

# One kit piece placed on a finished building, in GAME coordinates and already turned to face
# out of its wall. build_shells.py writes these into assets/models/own/arch/NAME_facade.part.
Fit = namedtuple("Fit", "kind x y z yaw scale tint tex tile")

# game yaw that turns a mounted kit piece to look out of each face
FACE_YAW = {"front": 0.0, "back": 180.0, "left": 90.0, "right": -90.0}


class Walls:
    """One ring of walls, the openings cut in it, and the kit pieces that dress them.

    A generator builds a ring, punches holes in it and asks for the solids:

        w = Walls(20.0, 12.0, 5.4, t=0.30, z0=0.20)
        w.kit("french_door", "front", u=0.0, z0=0.0)
        for u in (-7.2, -4.8, -2.4, 2.4, 4.8, 7.2):
            w.kit("window_large", "front", u=u, z0=1.10)
        groups["wall"] += [w.solid()]
        groups["dark"] += w.linings()

    and `w.fits` then carries everything the facade .part has to place, in game coordinates,
    so the frames and the holes they sit in can never drift apart.
    """

    def __init__(self, w, d, h, t=0.30, z0=0.0):
        self.w, self.d, self.h, self.t, self.z0 = w, d, h, t, z0
        self.openings = []
        self.fits = []

    # -- geometry
    def hole(self, face, u, z0, w, h, arch=0.0, reveal=0.07, margin=0.11, lining=True):
        """A plain hole, with its outer rebate. `z0` is measured from the building's base."""
        o = Opening(face, u, z0, w, h, arch, reveal, margin)
        self.openings.append((o, lining))
        return o

    def kit(self, kind, face, u, z0, scale=1.0, tint=(1.0, 1.0, 1.0), tex=None, tile=1.0,
            lining=True, drop=None):
        """A hole sized for a kit opening piece, plus the piece itself.

        `z0` is the height of the glazed or panelled opening above the wall ring's own base --
        the sill line for a window, and for a door the top of its threshold.

        `drop` carries the hole further down than the opening, which is what a door wants: the
        kit's threshold and the bottom member of its architrave stand `base` high, so cutting the
        hole only as far as the opening leaves a knee-high bar of wall across a doorway you are
        meant to walk through. The default drops a door's hole to the floor and leaves a window's
        alone.
        """
        k = KIT[kind]
        if drop is None:
            drop = k["base"] * scale if "door" in kind else 0.0
        o = self.hole(face, u, z0 - drop, k["w"] * scale, k["h"] * scale + drop,
                      k.get("arch", 0.0) * scale,
                      reveal=k["reveal"] * scale, margin=k["margin"] * scale, lining=lining)
        x, y, z = self._game_mount(face, u, self.z0 + z0 - k["base"] * scale,
                                   k["reveal"] * scale)
        self.fits.append(Fit(kind, x, y, z, FACE_YAW[face], scale, tint, tex, tile))
        return o

    def place(self, kind, face, u, z0, scale=1.0, tint=(1.0, 1.0, 1.0), tex=None, tile=1.0,
              inset=0.0):
        """A kit piece on this wall with no hole behind it -- a lamp bracket, a cornice run."""
        x, y, z = self._game_mount(face, u, self.z0 + z0, inset)
        self.fits.append(Fit(kind, x, y, z, FACE_YAW[face], scale, tint, tex, tile))

    def _game_mount(self, face, u, z, inset):
        """Where a piece mounted on `face` at `u` sits in GAME coordinates, pushed `inset` into
        the wall. `z` here is already absolute: measured from the building's base, not the ring's.
        game (x, y, z) = cad (x, z, -y)."""
        half = (self.d if FACES[face][1] == "d" else self.w) / 2.0 - inset
        if face == "front":
            return (u, z, -half)
        if face == "back":
            return (-u, z, half)
        if face == "left":
            return (-half, z, -u)
        return (half, z, u)          # right

    # -- results
    def solid(self, extra_cuts=()):
        """The wall ring with every opening cut through it."""
        s = wall_ring(self.w, self.d, self.h, self.t, z0=self.z0)
        for o, _ in self.openings:
            for tool in opening_cutters(o._replace(z0=o.z0 + self.z0), self.w, self.d, self.t):
                s = s.cut(tool)
        for tool in extra_cuts:
            s = s.cut(tool)
        return s

    def linings(self):
        """A dark panel at the back of every opening that asked for one: the room beyond."""
        return [opening_lining(o._replace(z0=o.z0 + self.z0), self.w, self.d, self.t)
                for o, lining in self.openings if lining]

    def floor(self, t=0.10):
        """The floor of the room the ring encloses, its top flush with the ring's base."""
        return floor_slab(self.w - 2 * self.t, self.d - 2 * self.t, t, z0=self.z0)
