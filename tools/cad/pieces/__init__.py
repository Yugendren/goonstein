"""Every kit piece, grouped by what it is for. Importing this registers them all.

Add a piece by writing a function in one of these modules (or a new one, named below) and
decorating it with @piece("name", tris=...). `build_kit.py --list` then knows about it.

A module that fails to import is reported and skipped rather than taking the kit down with it,
so a half-written piece never stops you rebuilding the other seventeen.
"""

import importlib
import traceback

MODULES = ["column", "openings", "trim", "structure", "temple"]

BROKEN = {}
for _m in MODULES:
    try:
        importlib.import_module(__name__ + "." + _m)
    except Exception:
        BROKEN[_m] = traceback.format_exc()
        print("kit: pieces/%s.py did not import, its pieces are unavailable:\n%s"
              % (_m, BROKEN[_m].rstrip().rsplit("\n", 1)[-1]))
