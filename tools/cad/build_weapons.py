"""Build the weapon kit: CadQuery solids -> STEP (editable) + GLB (game).

    tools/cad/.venv/bin/python tools/cad/build_weapons.py            # everything
    tools/cad/.venv/bin/python tools/cad/build_weapons.py shotgun
    tools/cad/.venv/bin/python tools/cad/build_weapons.py --list

Writes tools/cad/out/NAME.step and assets/models/own/NAME.glb, and prints a triangle table.
Modelled on tools/cad/build_kit.py, which does the same job for the architecture kit -- see
tools/cad/README.md for the frame and colour rules that both obey. The difference is the output:
a weapon is one small prop carried in a hand, so it ships as a single self-contained GLB (one
flat material per colour, no texture, no UVs) rather than an OBJ+MTL pair sitting in a .part.

Why CAD at all: the other three weapons (pistol, bat, wrench) are CC0 Poly Haven photoscans.
No CC0 photoscan of a pump shotgun exists without going through a login-walled source, so this
one is ours, built the same way the buildings are -- a real parametric solid, not a stack of
boxes, that a second person could open in FreeCAD and change a number in.

THE FRAME
---------
Modelled in the "gunsmith" frame, the way you would rack a shotgun on a bench: +Z up, +X the
width (centred on 0), +Y forward -- the direction the muzzle points -- exactly the sense
tools/cad/kitlib.py gives "outward" on a wall piece. The origin (0, 0, 0) is the grip point:
the wrist of the stock, where the trigger hand closes. Every number below is that grip's own
distance forward (+Y) or aft (-Y) of it, which is why the wrist and buttstock go negative and
the barrel goes positive.

tools/cad/weapon_stage.py turns this into the game's frame with the one rotation the whole file
gets: 180 degrees about Z, because the game's viewmodel frame has the muzzle pointing -Y, not
+Y. See that file for why that flip is also what puts a right-handed ejection port on the
correct side once you are looking down the sights.
"""

import argparse
import math
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT_STEP = os.path.join(HERE, "out")
OUT_GLB = os.path.join(ROOT, "assets", "models", "own")
BLENDER = shutil.which("blender") or "/opt/homebrew/bin/blender"

sys.path.insert(0, HERE)

import cadquery as cq  # noqa: E402

# Tessellation defaults, same numbers the architecture kit uses (tools/cad/kitlib.py): a flat
# side ignores them, a cylinder does not. 16-20 sided barrels are plenty at viewmodel distance.
TOL = 0.004      # metres of chordal deviation
ANG = 0.35       # radians of angular deviation

# sRGB Kd, written into the STEP's colours and passed to weapon_stage.py as the glTF base colour.
# Flat and few on purpose -- see the brief: silhouette carries a weapon seen at arm's length or
# from a few metres away, not surface detail, so there is no texture and no second channel.
MATERIALS = {
    "steel":  (0.10, 0.11, 0.12),   # blued steel: receiver, barrel, mag tube, guard, bead
    "walnut": (0.29, 0.17, 0.09),   # forend, wrist, buttstock
    "rubber": (0.05, 0.05, 0.05),   # butt pad, near-black
}

# name -> (function, triangle budget, one-line note for the printed table)
WEAPONS = {}


def weapon(name, tris=3000, note=""):
    """Register a weapon. The decorated function takes no arguments and returns a dict of
    {material name: solid or list of solids}, same contract as kitlib.piece but simpler --
    a weapon has no wall to mount on and nothing free-standing to centre, so there is no frame
    check beyond the triangle budget."""
    def deco(fn):
        if name in WEAPONS:
            raise KeyError("two weapons called %r" % name)
        WEAPONS[name] = (fn, tris, note or (fn.__doc__ or "").strip().split("\n")[0])
        return fn
    return deco


def normalise(result):
    """{material: solid | [solid, ...] | Workplane} -> {material: [cq.Shape, ...]}."""
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
                raise TypeError("a weapon may only return Workplanes and Shapes, got %r" % type(it))
        if shapes:
            out[mat] = shapes
    if not out:
        raise ValueError("weapon produced no solids")
    return out


# ------------------------------------------------------------------------------- small helpers
def box(x0, x1, y0, y1, z0, z1):
    """An axis-aligned box, given explicitly by its span on each axis. Every other shape in
    this file is quoted in the brief the same way -- "f 0.22 .. 0.80" -- so a literal span is
    easier to check against it than a centre-and-size box would be."""
    return (cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=(False, False, False))
            .translate((x0, y0, z0)))


def cyl_fwd(r, y0, y1, x=0.0, z=0.0):
    """A cylinder on the forward axis (+Y): the barrel, the magazine tube."""
    return cq.Workplane(obj=cq.Solid.makeCylinder(r, y1 - y0, cq.Vector(x, y0, z), cq.Vector(0, 1, 0)))


def ellipse_at(y, zc, rx, rz):
    """One loft cross-section: an ellipse rx wide (X) by rz tall (Z), on the plane forward-
    distance y, centred zc up. Building the wrist and the buttstock as a loft between three or
    four of these is what gives a CAD box-and-cylinder gun the curve of a stock instead of a
    plank -- a straight-sided wrist is the single fastest way to stop this reading as a shotgun."""
    return cq.Workplane("XZ", origin=(0, y, 0)).ellipse(rx, rz).val().translate((0, 0, zc))


# ------------------------------------------------------------------------------------ the guns
@weapon("shotgun", tris=3000, note="pump-action, Remington-870-ish; blued steel + walnut, no texture")
def build_shotgun():
    """A pump shotgun, built the way the brief lays it out: a boxy receiver, a barrel over a
    magazine tube, a pump forend wrapped around the tube, a trigger guard hanging under the
    receiver, and a stock whose wrist passes through the grip origin and whose comb and toe give
    the silhouette its curve.

    Two changes from the brief's starting numbers, both to hit the ~1.02 m overall length it
    asks for while the barrel + stock numbers as given added up to 1.16 m:
      * barrel shortened from f 0.22..0.80 to f 0.22..0.66 (a 0.58 m barrel to a 0.44 m one --
        closer to a "tactical" short-barrelled pump gun than a 28" field gun, which also reads
        better at viewmodel distance where a long barrel would poke out of frame).
      * magazine tube shortened to match, f 0.22..0.60, so its cap sits under the barrel rather
        than past it.
    Everything else is the brief's own numbers.
    """
    steel, walnut = [], []

    # Receiver: the boxy magazine/action housing. Its own top (z 0.095) sits proud of the
    # barrel's top (z 0.0905) by a few millimetres -- the 870's own humpbacked receiver line,
    # not an error -- and the fillet is what stops it reading as a cardboard carton.
    receiver = box(-0.021, 0.021, 0.00, 0.22, 0.015, 0.095).edges("|Z").fillet(0.003)
    # Ejection port: a blind pocket, not a through-hole, cut into the shooter's right (+X in
    # this frame; weapon_stage.py's 180-about-Z flip carries it to -X, the correct side once
    # the gun is up at the shoulder pointing -Y).
    port = box(0.017, 0.030, 0.09, 0.17, 0.04, 0.075)
    receiver = receiver.cut(port)
    steel.append(receiver)

    # Barrel and magazine tube: two parallel cylinders on the forward axis. The tube sits lower
    # and thicker-looking (r 13 mm vs 10.5 mm) than the barrel, which is what reads as "shotgun"
    # in silhouette before anything else does.
    steel.append(cyl_fwd(0.0105, 0.22, 0.66, z=0.080))
    steel.append(cyl_fwd(0.0130, 0.22, 0.60, z=0.046))

    # Front bead: a bead sitting on the barrel a little short of the muzzle, not hanging past it.
    steel.append(cq.Workplane("XY").sphere(0.003).translate((0, 0.655, 0.0935)))

    # Trigger guard: an outer loop with an inner loop subtracted out of it -- a picture frame,
    # 5 mm of wall left between them -- rather than a single filleted rectangle, because a solid
    # blob under the receiver reads as a fender, not a guard you could put a finger through.
    gy0, gy1, gz0, gz1, wall, thick = -0.01, 0.10, -0.018, 0.015, 0.005, 0.008
    guard_outer = (cq.Workplane("YZ").moveTo((gy0 + gy1) / 2, (gz0 + gz1) / 2)
                   .rect(gy1 - gy0, gz1 - gz0).extrude(thick / 2.0, both=True)
                   .edges("|X").fillet(0.008))
    guard_inner = (cq.Workplane("YZ").moveTo((gy0 + gy1) / 2, (gz0 + gz1) / 2)
                   .rect(gy1 - gy0 - 2 * wall, gz1 - gz0 - 2 * wall).extrude(thick, both=True)
                   .edges("|X").fillet(max(0.001, 0.008 - wall)))
    steel.append(guard_outer.cut(guard_inner))

    # Trigger: a small blade inside the guard's own opening.
    steel.append(box(-0.0015, 0.0015, 0.020, 0.035, -0.006, 0.010))

    # Forend / pump: a rounded box wrapping the magazine tube, with grip grooves cut straight
    # across the top and bottom faces -- a real pump's checkering read from three metres away.
    fx0, fx1, fy0, fy1, fz0, fz1 = -0.027, 0.027, 0.40, 0.60, 0.022, 0.070
    forend = box(fx0, fx1, fy0, fy1, fz0, fz1).edges("|Y").fillet(0.008)
    for gy in (0.44, 0.48, 0.52, 0.56):
        forend = forend.cut(box(fx0 - 0.004, fx1 + 0.004, gy - 0.003, gy + 0.003, fz1 - 0.006, fz1 + 0.005))
        forend = forend.cut(box(fx0 - 0.004, fx1 + 0.004, gy - 0.003, gy + 0.003, fz0 - 0.005, fz0 + 0.006))
    walnut.append(forend)

    # Wrist: the stock's neck, a loft through three elliptical stations. The middle one is
    # centred exactly on the origin, which is both the grip point the brief requires and the
    # simplest way to guarantee the solid actually passes through it.
    wrist_wires = [ellipse_at(0.02, 0.010, 0.019, 0.024),
                   ellipse_at(0.00, 0.000, 0.017, 0.022),
                   ellipse_at(-0.11, -0.055, 0.017, 0.022)]
    walnut.append(cq.Workplane(obj=cq.Solid.makeLoft(wrist_wires, ruled=False)))

    # Buttstock: from the wrist's own last station back to the pad, the cross-section flaring
    # from the wrist's tight ellipse into the tall, narrow oval a comb-and-toe profile makes in
    # side view -- comb up at z 0.045, toe down at z -0.075 -- while its width keeps narrowing.
    stock_wires = [ellipse_at(-0.11, -0.055, 0.017, 0.022),
                   ellipse_at(-0.22, -0.020, 0.016, 0.050),
                   ellipse_at(-0.345, -0.015, 0.015, 0.060)]
    walnut.append(cq.Workplane(obj=cq.Solid.makeLoft(stock_wires, ruled=False)))

    # Butt pad: the last 15 mm, a straight cap on the stock's own final section so the two
    # never gap or overlap.
    pad = (cq.Workplane("XZ", origin=(0, -0.345, 0)).ellipse(0.015, 0.060).extrude(0.015)
           .translate((0, 0, -0.015)))

    return {"steel": steel, "walnut": walnut, "rubber": [pad]}


# ------------------------------------------------------------------------------------- the build
def hexrgb(rgb):
    return "%02x%02x%02x" % tuple(max(0, min(255, round(c * 255))) for c in rgb)


def build(name, fn, budget, tmp):
    shapes = normalise(fn())

    # STEP: one coloured assembly, which is what a second person opens in their own CAD tool.
    asm = cq.Assembly(name=name)
    for mat, group in shapes.items():
        rgb = MATERIALS[mat]
        for i, s in enumerate(group):
            asm.add(s, name="%s_%d" % (mat, i), color=cq.Color(rgb[0], rgb[1], rgb[2], 1.0))
    step = os.path.join(OUT_STEP, name + ".step")
    asm.export(step)

    # STL per material, so the GLB keeps one flat colour per part of the gun.
    specs = []
    for mat, group in shapes.items():
        comp = cq.Compound.makeCompound(group)
        stl = os.path.join(tmp, "%s_%s.stl" % (name, mat))
        cq.exporters.export(cq.Workplane(obj=comp), stl, tolerance=TOL, angularTolerance=ANG)
        specs.append("%s:%s:%s" % (mat, hexrgb(MATERIALS[mat]), stl))

    glb = os.path.join(OUT_GLB, name + ".glb")
    r = subprocess.run([BLENDER, "-b", "--python", os.path.join(HERE, "weapon_stage.py"), "--",
                        glb, str(budget)] + specs,
                       capture_output=True, text=True)
    line = [l for l in r.stdout.splitlines() if l.startswith("WEAPONSTAGE ")]
    if not line:
        sys.stderr.write(r.stdout[-3000:] + r.stderr[-2000:])
        raise SystemExit("%s: the Blender stage failed" % name)
    raw, final = (int(t.split("=")[1]) for t in line[0].split()[2:4])
    bounds = [l for l in r.stdout.splitlines() if l.startswith("BOUNDS_")]
    for l in bounds:
        print("  " + l)

    if not os.path.exists(glb):
        raise SystemExit("%s: no GLB was written" % name)
    size = os.path.getsize(glb)

    # The frame check the architecture kit does at build time (check_frame in build_kit.py)
    # does not apply here -- a weapon is not mounted on a wall and its stock legitimately
    # reaches behind its own origin -- so the one hard rule left is the triangle budget.
    if final > budget:
        raise SystemExit("%s: %d triangles is over its %d budget" % (name, final, budget))

    return dict(name=name, raw=raw, tris=final, budget=budget, glb=glb, glb_bytes=size, step=step)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="*", help="weapons to build (default: all)")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()

    if a.list:
        for n in sorted(WEAPONS):
            print("%-12s %s" % (n, WEAPONS[n][2]))
        return

    os.makedirs(OUT_STEP, exist_ok=True)
    os.makedirs(OUT_GLB, exist_ok=True)
    names = a.names or sorted(WEAPONS)
    for n in names:
        if n not in WEAPONS:
            raise SystemExit("no weapon called %r (--list to see them)" % n)

    rows, t0 = [], time.time()
    with tempfile.TemporaryDirectory() as tmp:
        for n in names:
            fn, budget, _ = WEAPONS[n]
            t = time.time()
            rows.append(build(n, fn, budget, tmp))
            print("  %-12s %5d tris (%5d raw)  %6.1f kB  %4.1fs"
                  % (n, rows[-1]["tris"], rows[-1]["raw"], rows[-1]["glb_bytes"] / 1024.0, time.time() - t), flush=True)

    print("\n%-12s %7s %7s %7s  %9s" % ("weapon", "raw", "tris", "budget", "glb kB"))
    for r in rows:
        flag = "  OVER" if r["tris"] > r["budget"] else ""
        print("%-12s %7d %7d %7d  %9.1f%s"
              % (r["name"], r["raw"], r["tris"], r["budget"], r["glb_bytes"] / 1024.0, flag))
    total = sum(r["tris"] for r in rows)
    print("%-12s %7d          in %.0fs" % ("TOTAL", total, time.time() - t0))


if __name__ == "__main__":
    main()
