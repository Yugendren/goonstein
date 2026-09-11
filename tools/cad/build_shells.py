"""Build the building shells: CadQuery solids -> STEP (editable) + OBJ per group + the .part.

    tools/cad/.venv/bin/python tools/cad/build_shells.py            # every building
    tools/cad/.venv/bin/python tools/cad/build_shells.py villa      # just one
    tools/cad/.venv/bin/python tools/cad/build_shells.py --list

Writes, for each shell NAME:

    tools/cad/out/NAME.step                  the editable CAD body, one coloured part per group
    assets/models/own/shell/NAME_SKIN.obj    the game meshes, one per distinct skin
    assets/models/own/NAME.part              the whole building: those meshes, the kit frames
                                             that sit in its reveals, and any hand-placed extras

and prints a triangle table. A shell that breaks the frame rules in shelllib.py, or that goes
over its own triangle budget, fails the build rather than shipping.

THE .part IS GENERATED. Everything about how a building is textured -- which PBR name, at what
tile, under what tint -- lives in the generator's `skins=`, so the geometry and its skin can
never drift apart. Anything hand-placed that belongs to the building but is not part of its
fabric (an air conditioner, a washing line, a bench) goes in

    tools/cad/shells/extras/NAME.part          (the fabric: benches, air conditioners)
    tools/cad/shells/extras/NAME_facade.part   (the dressing: cornice runs, lamps, columns)

both of which are appended verbatim. Edit those, not the generated file: rebuilding overwrites
the .part exactly as it overwrites the OBJ.

A building is ONE .part on purpose. src/props.c caches a .part in the same 192-slot table it
caches a model in, and a second prop line per building to place its facade cost a slot, a draw
and a line in the level for nothing: a .part can place a kit OBJ perfectly well itself.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT_STEP = os.path.join(HERE, "out")
OUT_OBJ = os.path.join(ROOT, "assets", "models", "own", "shell")
OUT_PART = os.path.join(ROOT, "assets", "models", "own")
EXTRAS = os.path.join(HERE, "shells", "extras")
BLENDER = shutil.which("blender") or "/opt/homebrew/bin/blender"

sys.path.insert(0, HERE)

import cadquery as cq                                          # noqa: E402
from kitlib import ANG, MATERIALS, TOL                         # noqa: E402
from shelllib import SHELLS, normalise                         # noqa: E402
import shells                                                  # noqa: E402,F401  (registers them)

# Drawn back to front the way a building is built, so the .part reads like a section through it.
ORDER = ["floor", "step", "wall", "trim", "band", "arch", "roof", "wood", "metal",
         "stripe", "gold", "glass", "dark"]

# Which group name a fused mesh takes when several share one skin: the most structural wins.
HEAD_RANK = ["wall", "roof", "floor", "trim", "wood", "metal", "stripe", "gold", "band",
             "arch", "step", "glass", "dark"]


def hexrgb(rgb):
    return "%02x%02x%02x" % tuple(max(0, min(255, round(c * 255))) for c in rgb)


def order_key(g):
    return (ORDER.index(g) if g in ORDER else len(ORDER), g)


def check_frame(name, groups):
    """The frame rules from shelllib: base at Z = 0, origin at the middle of the footprint.

    Only the Z rule is exact -- src/model.c rebases an OBJ's lowest vertex to Y = 0, so a shell
    that floats above its own origin in CAD silently drops onto it in game. X and Y are a sanity
    net rather than a law: a porch or a veranda legitimately pushes the bounding box off centre,
    but a building drawn two footprints away from its own origin is a mistake that would
    otherwise only show up as a house standing in the wrong place.
    """
    lo = [1e9, 1e9, 1e9]
    hi = [-1e9, -1e9, -1e9]
    for solids in groups.values():
        for s in solids:
            bb = s.BoundingBox()
            lo = [min(lo[0], bb.xmin), min(lo[1], bb.ymin), min(lo[2], bb.zmin)]
            hi = [max(hi[0], bb.xmax), max(hi[1], bb.ymax), max(hi[2], bb.zmax)]
    bad = []
    if abs(lo[2]) > 1e-3:
        bad.append("base is at z=%.4f, must be 0 (src/model.c rebases it anyway)" % lo[2])
    for ax, slack in ((0, 2.5), (1, 3.0)):
        off = (lo[ax] + hi[ax]) / 2.0
        if abs(off) > slack:
            bad.append("its %s is %.2f m off the origin (%.2f .. %.2f); the origin has to be the"
                       " middle of the footprint the level places"
                       % ("width" if ax == 0 else "depth", off, lo[ax], hi[ax]))
    if max(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]) > 45.0:
        bad.append("over 45 m across; model.c would read it as millimetres")
    if bad:
        raise SystemExit("%s: %s" % (name, "; ".join(bad)))
    return lo, hi


def obj_base(path):
    """The lowest Y in a written OBJ.

    src/model.c rebases every OBJ's lowest vertex to Y = 0 as it loads it, and a shell is
    several OBJs -- so a roof exported at 2.7 m arrives sitting on the ground, and a building
    comes apart into a wall, a roof and a floor all stacked at zero. The .part puts each group
    back at the height it was drawn at, which is exactly the number read back here.
    """
    lo = None
    with open(path) as f:
        for line in f:
            if line.startswith("v "):
                y = float(line.split()[2])
                lo = y if lo is None or y < lo else lo
    return lo or 0.0


def write_part(name, groups, skins, spec, size, bases, fits):
    """The .part that places every fused group's OBJ at the building's own origin and skins it."""
    lines = [
        "# %s -- generated by tools/cad/build_shells.py from tools/cad/shells/. DO NOT EDIT." % name,
        "# %s" % spec["note"],
        "# %.1f x %.1f m footprint, %.1f m tall. One piece per material group, all at the",
        "# building's own origin: `tex NAME TILE` is per model file, so a wall and a roof cannot",
        "# share one. The y on each line is the height that group was drawn at, put back: model.c",
        "# rebases every OBJ's lowest vertex to zero, which would otherwise land the roof on the",
        "# ground. Hand-placed extras, if any, follow at the bottom from",
        "# tools/cad/shells/extras/%s.part" % name,
    ]
    lines[2] = lines[2] % (size[0], size[2], size[1])
    if spec["collide"]:
        c = spec["collide"]
        lines.append("collide %.2f %.2f%s" % (c[0], c[1], " deck" if len(c) > 2 and c[2] else ""))
    for g in sorted(groups, key=order_key):
        s = skins[g]
        line = ("piece models/own/shell/%s_%s.obj   0 %.3f 0   1 1 1   0 0 0   %.3f %.3f %.3f"
                % (name, g, bases[g], s.tint[0], s.tint[1], s.tint[2]))
        if s.tex:
            line += "  tex %s %.3g" % (s.tex, s.tile)
        lines.append(line)

    if fits:
        lines += [
            "",
            "# The kit's frames, seated IN the reveals the shell cuts: each piece is pushed back",
            "# into the wall by its own architrave projection, so the moulding finishes flush with",
            "# the plaster instead of standing on it. Generated from the same Walls calls that cut",
            "# the holes, so a frame and its reveal cannot drift apart.",
        ]
        for f in fits:
            line = ("piece models/own/arch/%s.obj   %.3f %.3f %.3f   %.3f %.3f %.3f   %.1f 0.0 0.0"
                    "   %.3f %.3f %.3f" % (f.kind, f.x, f.y, f.z, f.scale, f.scale, f.scale,
                                           f.yaw, f.tint[0], f.tint[1], f.tint[2]))
            if f.tex:
                line += "  tex %s %.3g" % (f.tex, f.tile)
            lines.append(line)

    for suffix in ("", "_facade"):
        extra = os.path.join(EXTRAS, name + suffix + ".part")
        if not os.path.exists(extra):
            continue
        with open(extra) as f:
            body = f.read().rstrip("\n")
        if body:
            lines += ["", "# ---- from tools/cad/shells/extras/%s%s.part ----" % (name, suffix),
                      body]

    path = os.path.join(OUT_PART, name + ".part")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    return path


def fuse(groups, skins):
    """One OBJ per distinct SKIN, not per group name.

    A group only has to be its own mesh because `tex NAME TILE` is per model file. Two groups
    that ask for the same texture, the same tile and the same tint therefore have no reason to
    be two files, and every extra file costs a slot in src/props.c's prop model cache -- which
    this island is close enough to filling that four saved files are worth having. The fused
    group keeps the name of whichever of its members comes first in ORDER, so the .part still
    reads floor, wall, trim, roof.
    """
    by_skin = {}
    for g in sorted(groups, key=order_key):
        by_skin.setdefault(skins[g], []).append(g)
    out, out_skins = {}, {}
    for skin, members in by_skin.items():
        # Name the fused mesh after its most structural member, not its first: a wall and the
        # steps that share its plaster is a `wall`, not a `step`.
        head = min(members, key=lambda g: (HEAD_RANK.index(g) if g in HEAD_RANK else len(HEAD_RANK), g))
        solids = []
        for g in members:
            solids += groups[g]
        out[head] = solids
        out_skins[head] = skin
    return out, out_skins


def stale(name, keep):
    """Delete NAME_GROUP.obj/.mtl left behind by a group that no longer exists, so a rebuild
    never leaves a mesh the .part has stopped placing sitting in the asset tree."""
    gone = []
    for f in sorted(os.listdir(OUT_OBJ)):
        if not f.startswith(name + "_") or not f.endswith((".obj", ".mtl")):
            continue
        group = f[len(name) + 1:-4]
        if group not in keep:
            os.remove(os.path.join(OUT_OBJ, f))
            gone.append(f)
    return gone


def build(name, spec, tmp):
    result = spec["fn"]()
    fits = []
    if isinstance(result, tuple):
        result, fits = result
    groups = normalise(result)
    lo, hi = check_frame(name, groups)
    missing = [g for g in groups if g not in spec["skins"]]
    if missing:
        raise SystemExit("%s: no skin declared for group(s) %s" % (name, ", ".join(sorted(missing))))
    groups, skins = fuse(groups, spec["skins"])
    stale(name, set(groups))

    # STEP: one coloured assembly, which is what the user opens in their CAD tool.
    asm = cq.Assembly(name=name)
    for g in sorted(groups, key=order_key):
        rgb = MATERIALS[skins[g].mat]
        for i, s in enumerate(groups[g]):
            asm.add(s, name="%s_%d" % (g, i), color=cq.Color(rgb[0], rgb[1], rgb[2], 1.0))
    asm.export(os.path.join(OUT_STEP, name + ".step"))

    specs = []
    for g in sorted(groups, key=order_key):
        mat = skins[g].mat
        comp = cq.Compound.makeCompound(groups[g])
        stl = os.path.join(tmp, "%s_%s.stl" % (name, g))
        cq.exporters.export(cq.Workplane(obj=comp), stl, tolerance=TOL, angularTolerance=ANG)
        specs.append("%s:%s:%s:%s" % (g, mat, hexrgb(MATERIALS[mat]), stl))

    r = subprocess.run([BLENDER, "-b", "--python", os.path.join(HERE, "shell_stage.py"), "--",
                        OUT_OBJ, name] + specs, capture_output=True, text=True)
    per = {}
    for line in r.stdout.splitlines():
        if line.startswith("SHELLSTAGE "):
            _, g, t = line.split()
            per[g] = int(t.split("=")[1])
    if len(per) != len(groups):
        sys.stderr.write(r.stdout[-4000:] + r.stderr[-2000:])
        raise SystemExit("%s: the Blender stage failed" % name)

    size = (hi[0] - lo[0], hi[2] - lo[2], hi[1] - lo[1])   # game axes: w, h, depth
    bases = {g: obj_base(os.path.join(OUT_OBJ, "%s_%s.obj" % (name, g))) for g in groups}
    part = write_part(name, groups, skins, spec, size, bases, fits)
    return dict(name=name, per=per, tris=sum(per.values()), budget=spec["tris"],
                size=size, part=part, fits=len(fits))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="*", help="shells to build (default: all)")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--groups", action="store_true", help="print the per-group triangle split")
    a = ap.parse_args()

    if a.list:
        for n in sorted(SHELLS):
            print("%-16s %6d tris  %s" % (n, SHELLS[n]["tris"], SHELLS[n]["note"]))
        return

    for d in (OUT_STEP, OUT_OBJ):
        os.makedirs(d, exist_ok=True)
    names = a.names or sorted(SHELLS)
    for n in names:
        if n not in SHELLS:
            raise SystemExit("no shell called %r (--list to see them)" % n)

    rows, t0, over = [], time.time(), []
    with tempfile.TemporaryDirectory() as tmp:
        for n in names:
            t = time.time()
            rows.append(build(n, SHELLS[n], tmp))
            print("  %-16s %6d tris  %4.1fs" % (n, rows[-1]["tris"], time.time() - t), flush=True)

    print("\n%-16s %7s %7s  %-22s %s" % ("building", "tris", "budget", "w x h x d (m)", "groups"))
    for r in rows:
        flag = "  OVER" if r["tris"] > r["budget"] else ""
        if flag:
            over.append(r["name"])
        print("%-16s %7d %7d  %-22s %s%s" % (
            r["name"], r["tris"], r["budget"], "%.1f x %.1f x %.1f" % r["size"],
            " ".join("%s:%d" % (g, r["per"][g]) for g in sorted(r["per"], key=order_key))
            if a.groups else len(r["per"]), flag))
    print("%-16s %7d          in %.0fs" % ("TOTAL", sum(r["tris"] for r in rows), time.time() - t0))
    if over:
        raise SystemExit("over budget: %s" % ", ".join(over))


if __name__ == "__main__":
    main()
