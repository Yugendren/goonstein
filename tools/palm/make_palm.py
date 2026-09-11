#!/usr/bin/env python3
"""Build a coconut palm in headless Blender and export it as one glTF file.

    blender -b --python tools/palm/make_palm.py -- --variant a --out assets/models/own/palm_a.glb

Three variants (`--variant a|b|c`) differ in height, lean, crown droop and frond count. Each is
ONE mesh file with exactly two materials -- `palm_bark` on the trunk, `palm_frond` on the crown --
so the engine's prop instancing collapses a hundred palms into two batches, which a `.part`
assembly of thirty boxes could never do.

What is in it, and why:

**Trunk.** A tapering tube swept along a leaning curve: nine rings by eight radial segments, the
rings packed toward the base where the bole flares and the curve is tightest. The UV wraps once
around (u) and runs up the trunk in metres (v), so the bark map tiles along the length without
stretching, and every ring is nudged +/-2% in radius so the silhouette has the faint step of ring
scars rather than a machined cone.

**Fronds.** Twelve to sixteen, each a bent card strip of five segments carrying the cut-out frond
texture (`tools/palm/make_frond.py`). The strip is not flat: at every node it has five vertices --
rib, two mid-wing, two edge -- folded into a shallow drooping V, so the frond has a cross-section
and catches light differently on its two wings. The card's half width at t follows `env(t)`, the
same leaflet envelope the texture is drawn with, and the UVs are mapped to the matching band of
the texture, so the card's outline IS the frond's outline: no wasted transparent margin, no
rectangular shadow.

**Two-sided lighting without a two-sided material.** Every frond is written twice, once with each
winding, and BOTH copies carry the same normal: a canopy normal that points away from the centre
of the crown rather than off the face of the card. The underside of a frond is therefore lit like
the top of a dome instead of like a floor, which is what foliage does, and it costs nothing at
draw time because the engine's back-face culling never has to be turned off.

**Wind.** The wind weight is written into the mesh's vertex-colour ALPHA as `1 - weight`, which
the engine's vertex shader reads (see shaders/world.vert): alpha 1 -- every other asset in the
game, and glTF's default when the attribute is absent -- is rigid, and alpha 0 sways the most. The
trunk reaches 0.30 at the crown, a frond runs from 0.30 at its base to 1.0 at its tip, so the
whole tree leans a little and the tips flutter. Nothing else in the pipeline has to know.

`--far` instead renders the finished palm to a texture from four sides, keeps the best one, and
writes a two-crossed-cards imposter of eight triangles for scenery distance.
"""

import argparse
import math
import os
import sys

import bpy
import bmesh
from mathutils import Vector

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


# --------------------------------------------------------------------------- maths

def smoothstep(e0, e1, x):
    if e1 == e0:
        return 0.0 if x < e0 else 1.0
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def env(t):
    """Leaflet envelope: how far the leaflets reach from the rib, 0..1, as a fraction of the
    frond's maximum half width. Identical to the one tools/palm/make_frond.py draws with -- the
    card's edge and the texture's edge are the same line, so change one and you change both."""
    a = smoothstep(0.14, 0.40, t)
    b = 1.0 - smoothstep(0.70, 1.00, t)
    return 0.04 + 0.96 * (a * b) ** 0.65


def lerp(a, b, t):
    return a + (b - a) * t


class Rng:
    """A tiny deterministic LCG, so a variant is the same mesh on every machine and every Blender."""

    def __init__(self, seed):
        self.s = (seed * 2654435761 + 1013904223) & 0xFFFFFFFF

    def f(self, lo=0.0, hi=1.0):
        self.s = (self.s * 1664525 + 1013904223) & 0xFFFFFFFF
        return lo + (hi - lo) * (self.s / 4294967296.0)


# --------------------------------------------------------------------------- variants

VARIANTS = {
    # height, lean (m of horizontal drift at the crown), trunk base/top radius,
    # frond count, frond length, droop (degrees the rib falls over its length), seed
    "a": dict(height=8.0,  lean=0.55, r_base=0.44, r_top=0.190, fronds=18, flen=3.30, droop=86,  seed=11,
              pitch_hi=48, pitch_lo=-18, coconuts=3),
    "b": dict(height=6.1,  lean=2.35, r_base=0.46, r_top=0.200, fronds=17, flen=3.45, droop=104, seed=23,
              pitch_hi=40, pitch_lo=-32, coconuts=4),
    "c": dict(height=10.4, lean=0.95, r_base=0.41, r_top=0.175, fronds=19, flen=3.30, droop=72,  seed=37,
              pitch_hi=56, pitch_lo=-8,  coconuts=0),
}

NRING = 10          # rings up the trunk
NRAD = 8            # radial segments
NSEG = 5            # segments along a frond rib
FROND_HALF_W = 0.37 # metres, the widest half width of a frond card


# --------------------------------------------------------------------------- trunk

def trunk_point(v, s):
    """Centreline of the trunk at s in [0,1]: a lean that accumulates with height plus a slight
    S so it is not a machine-drawn arc. Blender is Z-up here; the exporter converts."""
    h = v["height"] * s
    x = v["lean"] * (s ** 2.0)
    y = 0.10 * math.sin(s * 3.1) * v["height"] * 0.045
    return Vector((x, y, h))


def trunk_radius(v, s):
    flare = v["r_top"] + (v["r_base"] - v["r_top"]) * math.exp(-s / 0.10)
    return flare * (1.0 - 0.16 * s) * (1.0 + 0.022 * math.sin(s * 47.0))


def build_trunk(v, verts, faces, uvs, normals, wind, mats):
    """Append the trunk to the mesh buffers. Returns the crown origin and its tangent."""
    ss = [(i / (NRING - 1)) ** 1.25 for i in range(NRING)]
    base = len(verts)
    arclen = 0.0
    prev = trunk_point(v, ss[0])
    rings = []
    for i, s in enumerate(ss):
        p = trunk_point(v, s)
        arclen += (p - prev).length
        prev = p
        # local frame: tangent up the trunk, and two perpendiculars
        eps = 1e-3
        t = (trunk_point(v, min(1.0, s + eps)) - trunk_point(v, max(0.0, s - eps))).normalized()
        a = Vector((0, 1, 0)) if abs(t.z) > 0.9 else Vector((0, 0, 1))
        u_ax = t.cross(a).normalized()
        v_ax = t.cross(u_ax).normalized()
        r = trunk_radius(v, s)
        row = []
        for j in range(NRAD + 1):          # +1 duplicates the seam so u can reach 1
            ang = 2 * math.pi * j / NRAD
            n = (u_ax * math.cos(ang) + v_ax * math.sin(ang))
            verts.append(p + n * r)
            normals.append(n)
            # 0.62 repeats per metre up the trunk: a 512 px bark map (props cap model textures
            # there) then puts about 320 pixels across the 1.6 m of trunk you are standing next
            # to, instead of 160, and palm ring scars are 10 to 20 cm apart anyway. One wrap
            # around the circumference makes the texels roughly square at the base.
            uvs.append((j / NRAD, arclen * 0.62))
            wind.append(0.30 * s * s)      # the trunk leans a little, most of it near the top
            row.append(base + i * (NRAD + 1) + j)
        rings.append(row)
    for i in range(NRING - 1):
        for j in range(NRAD):
            a, b = rings[i][j], rings[i][j + 1]
            c, d = rings[i + 1][j + 1], rings[i + 1][j]
            faces.append(((a, b, c, d), 0))
    # a flat cap so the top is not an open pipe when a frond swings aside
    top = rings[-1]
    cen = len(verts)
    p = trunk_point(v, 1.0)
    verts.append(p)
    normals.append(Vector((0, 0, 1)))
    uvs.append((0.5, 0.5))
    wind.append(0.30)
    for j in range(NRAD):
        faces.append(((top[j], top[j + 1], cen), 0))
    eps = 1e-3
    tan = (trunk_point(v, 1.0) - trunk_point(v, 1.0 - eps)).normalized()
    mats.add(0)
    return p, tan


# --------------------------------------------------------------------------- crown

def build_fronds(v, crown, tan, verts, faces, uvs, normals, wind, rng):
    """One card strip per frond, folded into a shallow V and bent over by gravity."""
    n = v["fronds"]
    # the crown's fronds are ordered oldest (hanging) to youngest (standing), but they are not
    # laid out around the trunk in that order -- an interleave keeps the droopers from all
    # ending up on one side
    order = []
    for k in range(n):
        order.append((k * 7) % n)
    for slot, k in enumerate(order):
        frac = k / max(1, n - 1)
        az = 2 * math.pi * slot / n + rng.f(-0.16, 0.16)
        pitch0 = math.radians(lerp(v["pitch_hi"], v["pitch_lo"], frac) + rng.f(-6, 6))
        droop = math.radians(v["droop"] * rng.f(0.85, 1.15))
        length = v["flen"] * rng.f(0.86, 1.10)
        width = FROND_HALF_W * rng.f(0.88, 1.10)
        build_one_frond(crown, tan, az, pitch0, droop, length, width, rng,
                        verts, faces, uvs, normals, wind)


def build_one_frond(crown, tan, az, pitch0, droop, length, width, rng,
                    verts, faces, uvs, normals, wind):
    fwd = Vector((math.cos(az), math.sin(az), 0.0))
    side = Vector((-math.sin(az), math.cos(az), 0.0))
    up = Vector((0, 0, 1))
    # the rib: walk out in NSEG steps, pitching down further with every step
    nodes = []
    p = crown + tan * 0.12 + fwd * 0.10
    for i in range(NSEG + 1):
        t = i / NSEG
        nodes.append((t, p.copy()))
        if i == NSEG:
            break
        # the pitch at the middle of this step
        tm = (i + 0.5) / NSEG
        pitch = pitch0 - droop * (tm ** 1.35)
        d = (fwd * math.cos(pitch) + up * math.sin(pitch)).normalized()
        p = p + d * (length / NSEG)
    # centre of the crown, for the canopy normals
    base = len(verts)
    rows = []
    for t, p in nodes:
        e = env(t)
        hw = e * width
        # the wings fold up out of the rib near the base and flatten out toward the tip
        fold_mid = math.radians(lerp(16.0, 30.0, t))
        fold_edge = math.radians(lerp(-4.0, -26.0, t))     # the outer half sags back down
        row = []
        for sgn in (-1.0, 1.0):
            pass
        # order: edgeL, midL, rib, midR, edgeR
        offs = [(-1.0, 1.0, fold_edge), (-1.0, 0.5, fold_mid), (0.0, 0.0, 0.0),
                (1.0, 0.5, fold_mid), (1.0, 1.0, fold_edge)]
        for sgn, frac, fold in offs:
            lat = side * (sgn * hw * frac)
            vert = up * (math.sin(fold) * hw * frac)
            q = p + lat + vert
            verts.append(q)
            # UV: u along the rib, v across the SAME band of the texture the card's edge follows
            uvs.append((t, 0.5 + sgn * frac * 0.49 * e))
            wind.append(0.30 + 0.70 * (t ** 1.5))
            normals.append(Vector((0, 0, 1)))      # replaced below with the canopy normal
            row.append(len(verts) - 1)
        rows.append(row)
    # canopy normals: away from the crown's centre, tilted up, the same on both copies
    for row in rows:
        for vi in row:
            d = (verts[vi] - crown)
            d.z += 0.35
            if d.length < 1e-4:
                d = Vector((0, 0, 1))
            normals[vi] = (d.normalized() * 0.80 + Vector((0, 0, 1)) * 0.60).normalized()
    for i in range(NSEG):
        for j in range(4):
            a, b = rows[i][j], rows[i][j + 1]
            c, d = rows[i + 1][j + 1], rows[i + 1][j]
            faces.append(((a, b, c, d), 1))
    # the other side: the same vertices, the same normals, the opposite winding
    off = len(verts) - base
    for i in range(base, base + off):
        verts.append(verts[i].copy())
        uvs.append(uvs[i])
        normals.append(normals[i])
        wind.append(wind[i])
    for i in range(NSEG):
        for j in range(4):
            a, b = rows[i][j] + off, rows[i][j + 1] + off
            c, d = rows[i + 1][j + 1] + off, rows[i + 1][j] + off
            faces.append(((d, c, b, a), 1))


def build_coconuts(v, crown, tan, verts, faces, uvs, normals, wind, rng):
    """A few nuts tucked under the crown. Six segments by four rings each: they read as round at
    the two metres you ever see them from and cost 36 triangles."""
    for k in range(v["coconuts"]):
        az = rng.f(0, 2 * math.pi)
        r = rng.f(0.16, 0.34)
        c = crown + Vector((math.cos(az) * r, math.sin(az) * r, -rng.f(0.10, 0.30)))
        rad = rng.f(0.115, 0.145)
        base = len(verts)
        S, R = 6, 4
        for i in range(R + 1):
            phi = math.pi * i / R
            for j in range(S + 1):
                th = 2 * math.pi * j / S
                n = Vector((math.sin(phi) * math.cos(th), math.sin(phi) * math.sin(th), math.cos(phi)))
                verts.append(c + n * rad)
                normals.append(n)
                uvs.append((j / S * 0.5, i / R * 0.5))     # a small patch of the bark map
                wind.append(0.30)
        for i in range(R):
            for j in range(S):
                a = base + i * (S + 1) + j
                b = a + 1
                d = base + (i + 1) * (S + 1) + j
                c2 = d + 1
                faces.append(((a, b, c2, d), 0))


# --------------------------------------------------------------------------- blender glue

def clear_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def image_material(name, image_path, alpha):
    img = bpy.data.images.load(image_path, check_existing=True)
    img.pack()
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 0.85
    if "Specular IOR Level" in bsdf.inputs:
        bsdf.inputs["Specular IOR Level"].default_value = 0.2
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = img
    tex.interpolation = "Linear"
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    if alpha:
        nt.links.new(tex.outputs["Alpha"], bsdf.inputs["Alpha"])
        try:
            mat.surface_render_method = "DITHERED"
        except AttributeError:
            mat.blend_method = "CLIP"
    return mat


def make_object(name, verts, faces, uvs, normals, wind, mats):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata([tuple(v) for v in verts], [], [f[0] for f in faces])
    mesh.update()
    # material slots: 0 = bark, 1 = frond
    for poly, (_idx, mi) in zip(mesh.polygons, faces):
        poly.material_index = mi
        poly.use_smooth = True
    uv = mesh.uv_layers.new(name="UVMap")
    for loop in mesh.loops:
        uv.data[loop.index].uv = uvs[loop.vertex_index]
    col = mesh.color_attributes.new(name="Color", type="FLOAT_COLOR", domain="POINT")
    for i, w in enumerate(wind):
        col.data[i].color = (1.0, 1.0, 1.0, max(0.0, min(1.0, 1.0 - w)))
    mesh.color_attributes.active_color_index = 0
    mesh.color_attributes.render_color_index = 0
    try:
        mesh.normals_split_custom_set_from_vertices([tuple(n) for n in normals])
    except Exception as e:                                    # noqa: BLE001
        print("palm: custom normals not set (%s); falling back to smooth shading" % e)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    for m in mats:
        obj.data.materials.append(m)
    return obj


def triangle_count(obj):
    me = obj.data
    return sum(len(p.vertices) - 2 for p in me.polygons)


def export(obj, out):
    os.makedirs(os.path.dirname(out), exist_ok=True)
    for o in bpy.context.scene.objects:
        o.select_set(o is obj)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.gltf(
        filepath=out, export_format="GLB", use_selection=True,
        export_apply=True, export_normals=True, export_texcoords=True,
        export_vertex_color="ACTIVE", export_all_vertex_colors=False,
        export_materials="EXPORT", export_image_format="AUTO",
        export_yup=True, export_animations=False, export_skins=False,
        export_cameras=False, export_lights=False)


# --------------------------------------------------------------------------- the palm

def build_palm(name, vkey, frond_tex, bark_tex):
    v = VARIANTS[vkey]
    rng = Rng(v["seed"])
    verts, faces, uvs, normals, wind = [], [], [], [], []
    mats = set()
    crown, tan = build_trunk(v, verts, faces, uvs, normals, wind, mats)
    build_fronds(v, crown, tan, verts, faces, uvs, normals, wind, rng)
    if v["coconuts"]:
        build_coconuts(v, crown, tan, verts, faces, uvs, normals, wind, rng)
    bark = image_material("palm_bark", bark_tex, alpha=False)
    frond = image_material("palm_frond", frond_tex, alpha=True)
    obj = make_object(name, verts, faces, uvs, normals, wind, [bark, frond])
    return obj


# --------------------------------------------------------------------------- the far imposter

def render_imposter(obj, out_png, size=512):
    """Flat albedo of the whole palm on transparent film, rendered with Cycles on the CPU so it
    needs no GPU and no display: every material is swapped for an emission of its own base colour,
    which is exactly the albedo the engine wants to light itself."""
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 16
    scene.render.film_transparent = True
    scene.render.resolution_x = size
    scene.render.resolution_y = size
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.view_settings.view_transform = "Standard"
    # emission versions of the two materials
    for slot in obj.material_slots:
        mat = slot.material
        nt = mat.node_tree
        bsdf = nt.nodes.get("Principled BSDF")
        out_node = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
        tex = next((n for n in nt.nodes if n.type == "TEX_IMAGE"), None)
        emis = nt.nodes.new("ShaderNodeEmission")
        if tex:
            nt.links.new(tex.outputs["Color"], emis.inputs["Color"])
        mix = nt.nodes.new("ShaderNodeMixShader")
        trans = nt.nodes.new("ShaderNodeBsdfTransparent")
        if tex:
            nt.links.new(tex.outputs["Alpha"], mix.inputs["Fac"])
        else:
            mix.inputs["Fac"].default_value = 1.0
        nt.links.new(trans.outputs["BSDF"], mix.inputs[1])
        nt.links.new(emis.outputs["Emission"], mix.inputs[2])
        nt.links.new(mix.outputs["Shader"], out_node.inputs["Surface"])
        if bsdf:
            nt.nodes.remove(bsdf)
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    for vtx in obj.data.vertices:
        for i in range(3):
            lo[i] = min(lo[i], vtx.co[i])
            hi[i] = max(hi[i], vtx.co[i])
    cen = (lo + hi) * 0.5
    extent = max((hi - lo).x, (hi - lo).y, (hi - lo).z)
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = extent * 1.04
    cam = bpy.data.objects.new("cam", cam_data)
    bpy.context.collection.objects.link(cam)
    dist = extent * 3
    cam.location = (cen.x, cen.y - dist, cen.z)
    cam.rotation_euler = (math.radians(90), 0, 0)
    scene.camera = cam
    scene.render.filepath = out_png
    bpy.ops.render.render(write_still=True)
    return lo, hi, cen, extent


def build_imposter_mesh(name, lo, hi, cen, tex_png):
    """Two crossed cards, each drawn from both sides: eight triangles, the whole palm."""
    w = max(hi.x - lo.x, hi.y - lo.y)
    h = hi.z - lo.z
    # the render is square and fitted to `extent`, so the card has to be square too or the
    # picture stretches; the empty margin is transparent
    s = max(w, h) * 1.04
    half = s * 0.5
    z0 = cen.z - half
    verts, faces, uvs, normals, wind = [], [], [], [], []
    for k, ang in enumerate((0.0, math.pi / 2)):
        dx, dy = math.cos(ang), math.sin(ang)
        # the back face gets its own four vertices: two faces on one set of corners is a mesh
        # Blender calls invalid and exports wrongly
        for back in (False, True):
            base = len(verts)
            for (sgn, vv) in ((-1, 0.0), (1, 0.0), (1, 1.0), (-1, 1.0)):
                verts.append(Vector((cen.x + dx * half * sgn, cen.y + dy * half * sgn, z0 + s * vv)))
                uvs.append((0.5 + 0.5 * sgn, vv))
                normals.append(Vector((0.0, 0.0, 1.0)))
                wind.append(0.15 + 0.45 * vv)
            q = (base, base + 1, base + 2, base + 3)
            faces.append((q[::-1] if back else q, 0))
    for i, vtx in enumerate(verts):
        d = Vector((vtx.x - cen.x, vtx.y - cen.y, 0.0))
        if d.length < 1e-4:
            d = Vector((1.0, 0.0, 0.0))
        normals[i] = (d.normalized() * 0.55 + Vector((0, 0, 1)) * 0.85).normalized()
    mat = image_material("palm_far", tex_png, alpha=True)
    return make_object(name, verts, faces, uvs, normals, wind, [mat])


# --------------------------------------------------------------------------- main

def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", default="a", choices=sorted(VARIANTS))
    ap.add_argument("--out", required=True)
    ap.add_argument("--frond", default=os.path.join(ROOT, "assets/textures/frond.png"))
    ap.add_argument("--bark", default=os.path.join(ROOT, "assets/textures/palm_bark_lit.png"))
    ap.add_argument("--far", action="store_true", help="also write the imposter card and its texture")
    ap.add_argument("--far-out", default=os.path.join(ROOT, "assets/models/own/palm_far.glb"))
    ap.add_argument("--far-tex", default=os.path.join(ROOT, "assets/textures/palm_far.png"))
    args = ap.parse_args(argv)

    for p in (args.frond, args.bark):
        if not os.path.exists(p):
            sys.exit("palm: missing texture %s (run tools/palm/make_frond.py and make_bark.py first)" % p)

    clear_scene()
    obj = build_palm("palm_" + args.variant, args.variant, args.frond, args.bark)
    tris = triangle_count(obj)
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    for vtx in obj.data.vertices:
        for i in range(3):
            lo[i] = min(lo[i], vtx.co[i])
            hi[i] = max(hi[i], vtx.co[i])
    print("palm %s: %d triangles, %d vertices, %.2f m tall, %.2f x %.2f m crown"
          % (args.variant, tris, len(obj.data.vertices), hi.z - lo.z, hi.x - lo.x, hi.y - lo.y))
    if tris > 2600:
        print("palm: WARNING %d triangles is over the 2.5k budget" % tris)
    export(obj, args.out)
    print("palm: wrote %s (%.0f kB)" % (args.out, os.path.getsize(args.out) / 1024.0))

    if args.far:
        lo, hi, cen, extent = render_imposter(obj, args.far_tex)
        print("palm: imposter texture -> %s" % args.far_tex)
        obj.hide_render = True
        for o in list(bpy.context.scene.objects):
            if o is not obj and o.type == "MESH":
                bpy.data.objects.remove(o)
        card = build_imposter_mesh("palm_far", lo, hi, cen, args.far_tex)
        bpy.data.objects.remove(obj)
        print("palm far: %d triangles" % triangle_count(card))
        export(card, args.far_out)
        print("palm: wrote %s (%.0f kB)" % (args.far_out, os.path.getsize(args.far_out) / 1024.0))


if __name__ == "__main__":
    main()
