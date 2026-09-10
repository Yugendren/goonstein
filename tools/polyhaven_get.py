#!/usr/bin/env python3
"""Fetch a Poly Haven asset (CC0) into assets/ at a chosen resolution.

    python3 tools/polyhaven_get.py model NAME [--res 1k] [--out assets/models/polyhaven]
    python3 tools/polyhaven_get.py texture NAME [--res 1k] [--out assets/textures] [--as NAME]

Models land in OUT/NAME/ as the .gltf, its .bin and textures/. Only the base-colour ("diff")
maps are kept: the renderer reads nothing else (see src/model.c, one texture per material), so
the normal/ARM/rough maps would be a third of the repo for nothing.

Textures land in OUT/NAME.jpg -- one diffuse map, which is what a `tex NAME` block or prop line
in a level asks for.

Downloads go through a cache under /tmp and are checked against the md5 the API publishes; what
lands in assets/ is a copy, except that a model's diffuse maps are re-encoded down to --shrink
pixels (512 by default) because props load their textures at 512 anyway (README, "Performance
budget"). Downloading 1k and storing 512 is the difference between 102 MB and 50 MB of scans for
no visible change; block textures (the `texture` kind) keep their full 1k, since those are
sampled across a whole wall.
"""
import argparse, hashlib, json, os, shutil, subprocess, sys, urllib.parse

API = "https://api.polyhaven.com"
UA = "goonstein-asset-tool/1 (+https://polyhaven.com CC0 assets)"

CACHE = "/tmp/ph_cache"

def fetch(url, path):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    r = subprocess.run(["curl", "-sS", "-L", "-A", UA, url, "-o", path])
    if r.returncode != 0: raise SystemExit(f"download failed: {url}")

def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""): h.update(b)
    return h.hexdigest()

def get_json(name):
    p = f"/tmp/ph_files_{name}.json"
    if not os.path.exists(p) or os.path.getsize(p) < 50: fetch(f"{API}/files/{name}", p)
    return json.load(open(p))

def cached(url, want_md5):
    """The file as Poly Haven published it, downloaded once into CACHE and md5-checked."""
    path = os.path.join(CACHE, hashlib.sha1(url.encode()).hexdigest()[:16] + "_" +
                        os.path.basename(urllib.parse.urlparse(url).path))
    if not (os.path.exists(path) and want_md5 and md5(path) == want_md5):
        fetch(url, path)
        if want_md5 and md5(path) != want_md5: raise SystemExit(f"md5 mismatch: {url}")
    return path

def install(src, dst, shrink=0):
    """Copy src to dst, downsampling a JPEG to `shrink` pixels on its long side if it is bigger."""
    os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
    if shrink and dst.lower().endswith((".jpg", ".jpeg")):
        from PIL import Image
        im = Image.open(src)
        if max(im.size) > shrink:
            k = shrink / max(im.size)
            im = im.convert("RGB").resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)
            im.save(dst, "JPEG", quality=90, optimize=True)
            return os.path.getsize(dst)
    shutil.copyfile(src, dst)
    return os.path.getsize(dst)

def want(path, url, want_md5, shrink=0):
    return install(cached(url, want_md5), path, shrink)

def do_model(name, res, out, keep_all, shrink):
    d = get_json(name)
    g = d.get("gltf", {}).get(res, {}).get("gltf")
    if not g: raise SystemExit(f"{name}: no gltf at {res}")
    dest = os.path.join(out, name)
    total = 0
    gltf_path = os.path.join(dest, os.path.basename(urllib.parse.urlparse(g["url"]).path))
    total += want(gltf_path, g["url"], g.get("md5"))
    for rel, f in g["include"].items():
        if not keep_all and "/textures/" in "/" + rel and "_diff" not in rel: continue
        total += want(os.path.join(dest, rel), f["url"], f.get("md5"), shrink)
    # Drop maps the renderer never reads, so a re-run after a policy change cleans up.
    tdir = os.path.join(dest, "textures")
    if not keep_all and os.path.isdir(tdir):
        for f in os.listdir(tdir):
            if "_diff" not in f: os.remove(os.path.join(tdir, f))
    size = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(dest) for f in fs)
    print(f"model {name:<28} {res}  {size/1e6:6.2f} MB  -> {dest}")

def do_texture(name, res, out, alias):
    d = get_json(name)
    # Poly Haven texture sets: "Diffuse" is the base colour; some only ship "Color"/"albedo".
    for key in ("Diffuse", "diffuse", "Color", "col", "albedo"):
        m = d.get(key)
        if m: break
    else: raise SystemExit(f"{name}: no diffuse map ({list(d)})")
    entry = m.get(res)
    if not entry: raise SystemExit(f"{name}: no {res} diffuse")
    f = entry.get("jpg") or entry.get("png") or next(iter(entry.values()))
    ext = os.path.splitext(urllib.parse.urlparse(f["url"]).path)[1]
    path = os.path.join(out, (alias or name) + ext)
    want(path, f["url"], f.get("md5"))
    print(f"texture {name:<26} {res}  {os.path.getsize(path)/1e6:6.2f} MB  -> {path}")

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("kind", choices=["model", "texture"])
    ap.add_argument("names", nargs="+")
    ap.add_argument("--res", default="1k")
    ap.add_argument("--out", default=None)
    ap.add_argument("--as", dest="alias", default=None)
    ap.add_argument("--keep-all-maps", action="store_true")
    ap.add_argument("--shrink", type=int, default=512, help="max pixels on a model texture's long side (0 = keep)")
    a = ap.parse_args()
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    out = a.out or (os.path.join(root, "assets/models/polyhaven") if a.kind == "model"
                    else os.path.join(root, "assets/textures"))
    for n in a.names:
        if a.kind == "model": do_model(n, a.res, out, a.keep_all_maps, a.shrink)
        else: do_texture(n, a.res, out, a.alias if len(a.names) == 1 else None)
