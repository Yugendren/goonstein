"""Every building shell, one module per part of the island. Importing this registers them all.

Add a building by writing a function in one of these modules (or a new one, named below) and
decorating it with @shell("name", tris=..., skins=...). `build_shells.py --list` then knows about
it, and rebuilding writes its OBJs, its STEP and its .part.

A module that fails to import is reported and skipped rather than taking every other building
down with it, so a half-written villa never stops you rebuilding the cabanas.
"""

import importlib
import traceback

MODULES = ["cabana", "villa", "pool", "service", "utility", "temple"]

BROKEN = {}
for _m in MODULES:
    try:
        importlib.import_module(__name__ + "." + _m)
    except Exception:
        BROKEN[_m] = traceback.format_exc()
        print("shells: %s.py did not import, its buildings are unavailable:\n%s"
              % (_m, BROKEN[_m].rstrip().rsplit("\n", 1)[-1]))
