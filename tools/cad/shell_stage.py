"""The game-mesh half of a building shell: one STL per material group in, one OBJ each out.

    blender -b --python shell_stage.py -- OUTDIR PREFIX group:mat:hexcolour:file.stl [...]

writes OUTDIR/PREFIX_GROUP.obj (+ .mtl) for every group, and prints one

    SHELLSTAGE GROUP tris=N

line per group so build_shells.py can total them up.

It is a thin loop over obj_stage.stage(), which is the same weld/smooth/axis/export the kit uses
-- the only differences are that a shell's groups go to separate files, because `tex NAME TILE`
in a .part is per model file and a villa needs plaster on its walls and tile on its roof, and
that nothing is ever decimated. A collapse modifier is fine on a 2000-triangle window and ruinous
on a building: the first thing it eats is the window reveal and the eave line, which are the
entire point. build_shells.py enforces the budget by failing instead.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import obj_stage  # noqa: E402

NO_DECIMATE = 1 << 30


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    outdir, prefix, specs = argv[0], argv[1], argv[2:]
    os.makedirs(outdir, exist_ok=True)
    for spec in specs:
        group, mat, rgb_hex, path = spec.split(":", 3)
        out = os.path.join(outdir, "%s_%s.obj" % (prefix, group))
        _, final = obj_stage.stage(out, NO_DECIMATE, ["%s:%s:%s" % (mat, rgb_hex, path)])
        print("SHELLSTAGE %s tris=%d" % (group, final))


main()
