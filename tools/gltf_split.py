#!/usr/bin/env python3
"""Split a Poly Haven "set" glTF into one file per variant, sharing the original .bin.

Poly Haven ships several scans as a set: `rock_moss_set_02` is seven rocks, `grass_bermuda_01`
is twenty-one tufts, laid out side by side in one file. The engine draws a prop as a whole model
(src/props.c), so placing one of those puts all seven rocks on the ground at once and costs seven
draw calls. Splitting turns the set into `rock_moss_set_02_a.gltf` .. `_g.gltf`, one rock each.

The split is textual and lossless: each output is the same JSON with every node but one stripped
of its `mesh`, so it still points at the same .bin and the same textures on disk. src/model.c
walks the nodes and skips the ones without a mesh, so exactly one variant is loaded. Nothing is
duplicated on disk except a few kB of JSON.

    python3 tools/gltf_split.py --list assets/models/polyhaven/rock_moss_set_02/*.gltf
    python3 tools/gltf_split.py assets/models/polyhaven/rock_moss_set_02/*.gltf
    python3 tools/gltf_split.py --min-tris 200 assets/models/polyhaven/grass_bermuda_01/*.gltf

`--list` prints each node's name, translation and triangle count so you can tell a set of variants
(spread out along an axis) from one object built of parts (all at the origin) before splitting it.
"""
import argparse, copy, json, os, string, sys


def mesh_tris(d, mi):
    return sum(d["accessors"][p["indices"]]["count"] // 3 for p in d["meshes"][mi]["primitives"])


def mesh_bounds(d, mi):
    lo = [1e30] * 3; hi = [-1e30] * 3
    for p in d["meshes"][mi]["primitives"]:
        a = d["accessors"][p["attributes"]["POSITION"]]
        for i in range(3):
            lo[i] = min(lo[i], a["min"][i]); hi[i] = max(hi[i], a["max"][i])
    return lo, hi


def groups(d):
    """Nodes grouped by where they stand: one group is one variant of the set.

    Poly Haven lays a set out along X, one variant per metre, so the node translation is the
    variant id. A tree that ships as bark + leaves has two nodes on the same spot: they belong
    together and come out as one file."""
    g = {}
    for i, n in enumerate(d.get("nodes", [])):
        if "mesh" not in n: continue
        t = tuple(round(v, 3) for v in n.get("translation", [0, 0, 0]))
        g.setdefault(t, []).append(i)
    return [g[k] for k in sorted(g, key=lambda t: (t[0], t[2]))]


def do_list(path):
    d = json.load(open(path))
    print(f"== {path}")
    for k, grp in enumerate(groups(d)):
        tris = sum(mesh_tris(d, d["nodes"][i]["mesh"]) for i in grp)
        lo = [1e30] * 3; hi = [-1e30] * 3
        for i in grp:
            a, b = mesh_bounds(d, d["nodes"][i]["mesh"])
            for c in range(3): lo[c] = min(lo[c], a[c]); hi[c] = max(hi[c], b[c])
        nm = (d["nodes"][grp[0]].get("name") or "")[:30]
        print("  %-3s %-30s %5.2f x %5.2f x %5.2f m  %7d tris  %d node(s)"
              % (string.ascii_lowercase[k] if k < 26 else k, nm,
                 hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], tris, len(grp)))


def do_split(path, min_tris, dry):
    d = json.load(open(path))
    grps = [g for g in groups(d) if sum(mesh_tris(d, d["nodes"][i]["mesh"]) for i in g) >= min_tris]
    if len(grps) < 2:
        print(f"{path}: {len(grps)} variant(s), nothing to split"); return []
    base, ext = os.path.splitext(path)
    # The set's own resolution suffix ("_1k") is noise once a file is one rock: rock_moss_set_02_a.
    stem = base[:-3] if base.endswith(("_1k", "_2k", "_4k", "_8k")) else base
    out = []
    for k, grp in enumerate(grps):
        suffix = string.ascii_lowercase[k] if k < 26 else f"{k:02d}"
        dst = f"{stem}_{suffix}{ext}"
        tris = sum(mesh_tris(d, d["nodes"][i]["mesh"]) for i in grp)
        if not dry:
            e = copy.deepcopy(d)
            for j, n in enumerate(e["nodes"]):
                if j in grp:
                    # Stand the variant at the origin: the set spreads them along X, and a prop
                    # line places its file at x y z, not "wherever the scan happened to sit".
                    n.pop("translation", None)
                else:
                    n.pop("mesh", None)
            for sc in e.get("scenes", []): sc["nodes"] = list(grp)
            json.dump(e, open(dst, "w"), separators=(",", ":"))
        out.append(dst)
        print("  %-72s %7d tris" % (dst, tris))
    return out


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--min-tris", type=int, default=0, help="drop variants smaller than this")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    for f in a.files:
        if a.list: do_list(f)
        else: do_split(f, a.min_tris, a.dry_run)
