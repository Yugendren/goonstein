"""Build the architecture kit: CadQuery solids -> STEP (editable) + OBJ (game).

    tools/cad/.venv/bin/python tools/cad/build_kit.py            # everything
    tools/cad/.venv/bin/python tools/cad/build_kit.py wall_column arched_opening

Writes tools/cad/out/NAME.step and assets/models/own/arch/NAME.obj + .mtl, and prints a
triangle table. Nothing here is fetched or borrowed; every solid is ours.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT_STEP = os.path.join(HERE, "out")
OUT_OBJ = os.path.join(ROOT, "assets", "models", "own", "arch")
BLENDER = shutil.which("blender") or "/opt/homebrew/bin/blender"

sys.path.insert(0, HERE)

import cadquery as cq                                     # noqa: E402
from kitlib import ANG, MATERIALS, PIECES, TOL, normalise  # noqa: E402
import pieces                                              # noqa: E402,F401  (registers everything)


def hexrgb(rgb):
    return "%02x%02x%02x" % tuple(max(0, min(255, round(c * 255))) for c in rgb)


def check_frame(name, shapes, free):
    """The frame rules from kitlib: base at Z = 0, centred on X, body at Y >= 0 (or centred on
    Y too, for a free-standing piece)."""
    lo = [1e9, 1e9, 1e9]
    hi = [-1e9, -1e9, -1e9]
    for group in shapes.values():
        for s in group:
            bb = s.BoundingBox()
            lo = [min(lo[0], bb.xmin), min(lo[1], bb.ymin), min(lo[2], bb.zmin)]
            hi = [max(hi[0], bb.xmax), max(hi[1], bb.ymax), max(hi[2], bb.zmax)]
    problems = []
    if abs(lo[2]) > 1e-4:
        problems.append("base is at z=%.4f, must be 0 (src/model.c rebases it anyway)" % lo[2])
    if abs(lo[0] + hi[0]) > 2e-3:
        problems.append("not centred on x: %.3f .. %.3f" % (lo[0], hi[0]))
    if free:
        if abs(lo[1] + hi[1]) > 2e-3:
            problems.append("free-standing but not centred on y: %.3f .. %.3f" % (lo[1], hi[1]))
    elif lo[1] < -1e-4:
        problems.append("reaches behind the mounting plane: y=%.4f (free=True if it is a column)" % lo[1])
    if max(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]) > 45.0:
        problems.append("over 45 m across; model.c would read it as millimetres")
    if problems:
        raise SystemExit("%s: %s" % (name, "; ".join(problems)))
    return lo, hi


def build(name, fn, budget, free, tmp):
    shapes = normalise(fn())
    lo, hi = check_frame(name, shapes, free)

    # STEP: one coloured assembly, which is what the user opens in their CAD tool.
    asm = cq.Assembly(name=name)
    for mat, group in shapes.items():
        rgb = MATERIALS[mat]
        for i, s in enumerate(group):
            asm.add(s, name="%s_%d" % (mat, i), color=cq.Color(rgb[0], rgb[1], rgb[2], 1.0))
    step = os.path.join(OUT_STEP, name + ".step")
    asm.export(step)

    # STL per material, so the OBJ keeps one flat colour per part of the piece.
    specs = []
    for mat, group in shapes.items():
        comp = cq.Compound.makeCompound(group)
        stl = os.path.join(tmp, "%s_%s.stl" % (name, mat))
        cq.exporters.export(cq.Workplane(obj=comp), stl, tolerance=TOL, angularTolerance=ANG)
        specs.append("%s:%s:%s" % (mat, hexrgb(MATERIALS[mat]), stl))

    obj = os.path.join(OUT_OBJ, name + ".obj")
    r = subprocess.run([BLENDER, "-b", "--python", os.path.join(HERE, "obj_stage.py"), "--",
                        obj, str(budget)] + specs,
                       capture_output=True, text=True)
    line = [l for l in r.stdout.splitlines() if l.startswith("KITSTAGE ")]
    if not line:
        sys.stderr.write(r.stdout[-3000:] + r.stderr[-2000:])
        raise SystemExit("%s: the Blender stage failed" % name)
    raw, final = (int(t.split("=")[1]) for t in line[0].split()[2:4])

    # The exported .mtl sits beside the .obj and is referenced by name; make sure it is there.
    if not os.path.exists(obj[:-4] + ".mtl"):
        raise SystemExit("%s: no .mtl was written" % name)
    size = [hi[i] - lo[i] for i in range(3)]
    return dict(name=name, raw=raw, tris=final, budget=budget,
                size=(size[0], size[2], size[1]),  # reported in game axes: w, h, depth
                mats=len(shapes), step=step, obj=obj)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="*", help="pieces to build (default: all)")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()

    if a.list:
        for n in sorted(PIECES):
            print("%-22s %-9s %s" % (n, "standing" if PIECES[n][3] else "mounted", PIECES[n][2]))
        return

    os.makedirs(OUT_STEP, exist_ok=True)
    os.makedirs(OUT_OBJ, exist_ok=True)
    names = a.names or sorted(PIECES)
    for n in names:
        if n not in PIECES:
            raise SystemExit("no kit piece called %r (--list to see them)" % n)

    rows, t0 = [], time.time()
    with tempfile.TemporaryDirectory() as tmp:
        for n in names:
            fn, budget, _, free = PIECES[n]
            t = time.time()
            rows.append(build(n, fn, budget, free, tmp))
            print("  %-22s %5d tris  %4.1fs" % (n, rows[-1]["tris"], time.time() - t), flush=True)

    print("\n%-22s %7s %7s  %-22s %s" % ("piece", "tris", "budget", "w x h x d (m)", "colours"))
    for r in rows:
        flag = "  OVER" if r["tris"] > r["budget"] else ""
        print("%-22s %7d %7d  %-22s %d%s" % (
            r["name"], r["tris"], r["budget"],
            "%.2f x %.2f x %.2f" % r["size"], r["mats"], flag))
    total = sum(r["tris"] for r in rows)
    print("%-22s %7d          in %.0fs" % ("TOTAL", total, time.time() - t0))
    if len(names) == len(PIECES) and total > 30000:
        raise SystemExit("the kit is over its 30k triangle budget")


if __name__ == "__main__":
    main()
