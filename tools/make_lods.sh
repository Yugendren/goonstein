#!/usr/bin/env bash
# Decimate the island's heaviest scatter models (plants, rocks, crates, ...) into
# cheap far-distance LODs, written beside each original as "<original>.lod.glb".
# The runtime loads the LOD for shadow-map instances and anything small on
# screen; it still draws the original up close, so the LOD must not move or
# change the near silhouette, only shed triangles.
#
# WHY: the island draws ~9.1M triangles a frame, and scatter props (plants,
# rocks, crates) supply the bulk of that -- a single background shrub can be
# 25k-80k triangles. Almost none of that detail is visible past a few metres
# or in a shadow map, so every model above 8000 triangles gets a decimated
# stand-in sized to roughly 1/15th of its original count (with a 800-tri
# floor and a few heavier hand-picked targets for the worst offenders).
#
# Requires:
#   - Blender 5.x on PATH (uses tools/blender/decimate.py, headless).
#   - python3 on PATH (used only to count triangles in glTF/GLB JSON; no
#     external packages, it never touches the binary buffer).
#
# Usage:
#   tools/make_lods.sh            regenerate any LOD whose source is newer
#                                  (or whose .lod.glb is missing)
#   tools/make_lods.sh --force    rebuild every LOD regardless of mtimes
#
# Run from anywhere; paths are resolved relative to this script's location.
set -euo pipefail

# ---------------------------------------------------------------------------
# Repo root, resolved from this script's own location.
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MODELS_DIR="$ROOT/assets/models"
DECIMATE_PY="$SCRIPT_DIR/blender/decimate.py"

FORCE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --force) FORCE=1; shift ;;
        -h|--help)
            sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "error: unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

if ! command -v blender >/dev/null 2>&1; then
    echo "error: blender not found on PATH" >&2
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "error: python3 not found on PATH" >&2
    exit 1
fi
if [[ ! -f "$DECIMATE_PY" ]]; then
    echo "error: $DECIMATE_PY is missing" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# The list: one "<path relative to assets/models/>  <lod triangle target>"
# per line. Edit this table to add/drop/retarget a model; everything else in
# this script is mechanical. The first block is the island's known heaviest
# offenders (hand-picked targets); the second block is every other island
# prop above 8000 triangles, targeted at roughly tris/15 (floor 800), found
# by scanning assets/levels/island.txt.
# ---------------------------------------------------------------------------
read -r -d '' MODEL_LIST <<'EOF' || true
polyhaven/pachira_aquatica_01/pachira_aquatica_01_b.gltf   4000
polyhaven/pachira_aquatica_01/pachira_aquatica_01_c.gltf   4000
polyhaven/shrub_02/shrub_02_a.gltf                         2000
polyhaven/shrub_02/shrub_02_b.gltf                         2000
polyhaven/shrub_02/shrub_02_c.gltf                         2000
polyhaven/shrub_02/shrub_02_d.gltf                         2000
polyhaven/cheiridopsis_succulent/cheiridopsis_succulent_a.gltf  3000
polyhaven/cheiridopsis_succulent/cheiridopsis_succulent_d.gltf  3000
polyhaven/cheiridopsis_succulent/cheiridopsis_succulent_g.gltf  3000
polyhaven/cheiridopsis_succulent/cheiridopsis_succulent_h.gltf  3000
polyhaven/cheiridopsis_succulent/cheiridopsis_succulent_i.gltf  3000
polyhaven/rock_moss_set_01/rock_moss_set_01_a.gltf         2500
polyhaven/rock_moss_set_01/rock_moss_set_01_f.gltf         2500
polyhaven/rock_moss_set_02/rock_moss_set_02_a.gltf         2500
polyhaven/rock_moss_set_02/rock_moss_set_02_c.gltf         2500
polyhaven/rock_moss_set_02/rock_moss_set_02_e.gltf         2500
polyhaven/rock_moss_set_02/rock_moss_set_02_g.gltf         2500
polyhaven/crystalline_iceplant/crystalline_iceplant_a.gltf 1200
polyhaven/crystalline_iceplant/crystalline_iceplant_e.gltf 1200
polyhaven/weed_plant_02/weed_plant_02_a.gltf               1000
polyhaven/anthurium_botany_01/anthurium_botany_01_b.gltf   4477
polyhaven/anthurium_botany_01/anthurium_botany_01_c.gltf   4477
polyhaven/anthurium_botany_01/anthurium_botany_01_d.gltf   4477
polyhaven/rock_moss_set_01/rock_moss_set_01_c.gltf         4208
polyhaven/rock_moss_set_01/rock_moss_set_01_d.gltf         4208
polyhaven/rock_moss_set_02/rock_moss_set_02_b.gltf         3843
polyhaven/rock_moss_set_02/rock_moss_set_02_d.gltf         3843
polyhaven/rock_moss_set_02/rock_moss_set_02_f.gltf         3843
polyhaven/tree_stump_01/tree_stump_01_1k.gltf              2736
polyhaven/namaqualand_boulders_01/namaqualand_boulders_01_a.gltf  2723
polyhaven/namaqualand_boulders_01/namaqualand_boulders_01_b.gltf  2723
polyhaven/Lantern_01/Lantern_01_1k.gltf                    2260
polyhaven/wooden_barrels_01/wooden_barrels_01_1k.gltf      2209
polyhaven/street_lamp_01/street_lamp_01_1k.gltf            2041
polyhaven/portable_generator/portable_generator_1k.gltf    1761
polyhaven/modular_wooden_pier/modular_wooden_pier_1k.glb   1600
polyhaven/wooden_military_crate/wooden_military_crate_1k.gltf  1532
polyhaven/wicker_basket_01/wicker_basket_01_1k.gltf        1485
polyhaven/street_lamp_02/street_lamp_02_1k.gltf            1356
polyhaven/metal_jerrycan/metal_jerrycan_1k.gltf            1335
polyhaven/plastic_crate_01/plastic_crate_01_1k.gltf        1221
polyhaven/dead_tree_trunk/dead_tree_trunk_1k.glb           1200
polyhaven/marble_bust_01/marble_bust_01_1k.gltf            1164
polyhaven/calathea_orbifolia_01/calathea_orbifolia_01_a.gltf  1113
polyhaven/calathea_orbifolia_01/calathea_orbifolia_01_b.gltf  1113
polyhaven/calathea_orbifolia_01/calathea_orbifolia_01_c.gltf  1113
polyhaven/calathea_orbifolia_01/calathea_orbifolia_01_e.gltf  1113
polyhaven/rock_07/rock_07_1k.gltf                          990
polyhaven/boulder_01/boulder_01_1k.glb                     933
polyhaven/hand_truck/hand_truck_1k.gltf                    881
polyhaven/planter_box_03/planter_box_03_1k.gltf            874
polyhaven/lambis_shell/lambis_shell_1k.gltf                833
polyhaven/ocean_buoy/ocean_buoy_1k.gltf                    816
polyhaven/concrete_cat_statue/concrete_cat_statue_1k.gltf  800
polyhaven/wooden_stool_01/wooden_stool_01_1k.gltf          800
polyhaven/planter_box_02/planter_box_02_1k.gltf            800
polyhaven/lifebuoy/lifebuoy_1k.gltf                        800
polyhaven/old_military_crate/old_military_crate_1k.gltf    800  # KNOWN FAILURE (see below)
polyhaven/wooden_picnic_table/wooden_picnic_table_1k.gltf  800
polyhaven/concrete_road_barrier/concrete_road_barrier_1k.glb  800
polyhaven/outdoor_table_chair_set_01/outdoor_table_chair_set_01_1k.gltf  800
polyhaven/life_jacket/life_jacket_1k.gltf                  800
polyhaven/potted_plant_04/potted_plant_04_1k.gltf          800
polyhaven/wooden_ladder/wooden_ladder_1k.gltf              800
polyhaven/wooden_lantern_01/wooden_lantern_01_1k.gltf      800
polyhaven/planter_box_01/planter_box_01_1k.gltf            800
EOF
# old_military_crate_1k.gltf's KNOWN FAILURE: its source geometry has two
# objects sharing one linked mesh datablock (a rope/cloth mesh), so
# tools/blender/decimate.py's bpy.ops.object.modifier_apply raises "Modifiers
# cannot be applied to multi-user data" and no .lod.glb is written. Fixing it
# means adding a make_single_user pass to decimate.py, which this script does
# not own. Only 2 island instances (~21k triangles) are affected, so it is
# left in the list -- it will show up as a reported FAILED line on every run
# rather than being silently dropped.

# ---------------------------------------------------------------------------
# Triangle counter: reads only the JSON chunk/document of a .gltf or .glb (no
# buffer data needed -- triangle counts live in the accessor "count" fields),
# so it works the same on the plain-JSON originals and the exported LOD glbs.
# ---------------------------------------------------------------------------
count_tris() {
    python3 - "$1" <<'PYEOF'
import json, struct, sys

def load(path):
    if path.endswith(".glb"):
        with open(path, "rb") as f:
            f.read(4); f.read(4); f.read(4)  # magic, version, total length
            chunk_len = struct.unpack("<I", f.read(4))[0]
            chunk_type = f.read(4)
            if chunk_type != b"JSON":
                raise ValueError(f"{path}: first GLB chunk is not JSON")
            return json.loads(f.read(chunk_len))
    with open(path) as f:
        return json.load(f)

data = load(sys.argv[1])
total = 0
for mesh in data.get("meshes", []):
    for prim in mesh.get("primitives", []):
        acc_idx = prim.get("indices")
        if acc_idx is None:
            acc_idx = prim.get("attributes", {}).get("POSITION")
        if acc_idx is None:
            continue
        total += data["accessors"][acc_idx].get("count", 0) // 3
print(total)
PYEOF
}

FAILED=0

while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    # shellcheck disable=SC2206
    fields=($line)
    rel="${fields[0]}"
    target="${fields[1]}"
    src="$MODELS_DIR/$rel"
    dst="$src.lod.glb"

    if [[ ! -f "$src" ]]; then
        echo "$rel: FAILED (source not found: $src)" >&2
        FAILED=1
        continue
    fi

    if [[ $FORCE -eq 0 && -f "$dst" && "$dst" -nt "$src" ]]; then
        original_tris="$(count_tris "$src")"
        actual_tris="$(count_tris "$dst")"
        echo "$rel  $original_tris -> $actual_tris tris  (target $target)  [cached]"
        continue
    fi

    original_tris="$(count_tris "$src")"

    if ! blender -b --python "$DECIMATE_PY" -- "$src" "$dst" --target "$target" --ground 0 \
        > /tmp/make_lods_blender.$$.log 2>&1; then
        echo "$rel: FAILED (blender exited non-zero, see /tmp/make_lods_blender.$$.log)" >&2
        cat /tmp/make_lods_blender.$$.log >&2
        rm -f /tmp/make_lods_blender.$$.log
        FAILED=1
        continue
    fi
    rm -f /tmp/make_lods_blender.$$.log

    if [[ ! -f "$dst" ]]; then
        echo "$rel: FAILED (blender did not produce $dst)" >&2
        FAILED=1
        continue
    fi

    actual_tris="$(count_tris "$dst")"
    echo "$rel  $original_tris -> $actual_tris tris  (target $target)"
done <<< "$MODEL_LIST"

if [[ $FAILED -ne 0 ]]; then
    echo "make_lods: one or more models FAILED" >&2
    exit 1
fi

echo "make_lods: ok"
