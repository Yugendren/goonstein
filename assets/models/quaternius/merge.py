# Headless Blender: fuse a Quaternius base body + a modular outfit + the Universal Animation
# Library clips onto one armature and write a single GLB the engine can load.
#   blender -b --python merge.py -- OUT.glb BODY.gltf OUTFIT.gltf ANIM1.glb [ANIM2.glb ...]
#       [--add HAIR.gltf ...] [--tex N] [--alias NEW=CLIP ...] [--tint PREFIX R G B [SAT] ...]
#   --add    another mesh rigged to the same universal skeleton (hair, beard, eyebrows); its
#            meshes are re-bound to the one armature and its own armature thrown away
#   --tex    longest texture edge to keep (default 1024)
#   --alias  export CLIP a second time under the name NEW, for files that name a clip the rig
#            does not have (assets/enemies/*.txt call the boss's moves by clip name)
#   --tint   repaint the base colour of the materials (or the meshes) whose name starts with
#            PREFIX, "*" for all: pull saturation to SAT (1 = leave it, 0 = grey) and then
#            multiply by R G B. The packs ship one atlas per outfit, so this is how two
#            characters in the same outfit end up different people. The game's lighting mostly
#            normalises brightness away, so hue and saturation are the levers that read on screen.
#            One material is repainted by the first rule that names it; if a later rule wants an
#            atlas an earlier one already touched, it gets its own copy (the warden's red cloth
#            and iron pauldrons come out of the one ranger map that way).
import bpy, sys, os

argv = sys.argv[sys.argv.index("--") + 1:]
adds, aliases, tints = [], [], []
TEX = 1024
pos = []
i = 0
def num(s):
    try: float(s); return True
    except (TypeError, ValueError): return False
while i < len(argv):
    a = argv[i]
    if a == "--add": adds.append(argv[i + 1]); i += 2
    elif a == "--tex": TEX = int(argv[i + 1]); i += 2
    elif a == "--alias": aliases.append(argv[i + 1].split("=", 1)); i += 2
    elif a == "--tint":
        has_sat = i + 5 < len(argv) and num(argv[i + 5])
        tints.append((argv[i + 1], [float(x) for x in argv[i + 2:i + 5]],
                      float(argv[i + 5]) if has_sat else 1.0))
        i += 6 if has_sat else 5
    else: pos.append(a); i += 1
out, body, outfit = pos[0], pos[1], pos[2]
anims = pos[3:]

bpy.ops.wm.read_factory_settings(use_empty=True)

def imported(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    return [o for o in bpy.data.objects if o not in before]

def arm_of(objs):
    return next(o for o in objs if o.type == "ARMATURE")

# 1. outfit first: its armature is the one everything ends up on
outfit_objs = imported(outfit)
rig = arm_of(outfit_objs)
rig.name = "Armature"
rig.animation_data_create()

# 2. base body (head, hands, eyes): re-bind its meshes to the outfit rig, drop its armature
body_objs = imported(body)
body_rig = arm_of(body_objs)
body_meshes = [o for o in body_objs if o.type == "MESH"]
for o in body_meshes:
    mw = o.matrix_world.copy()
    o.parent = rig; o.matrix_world = mw
    for m in o.modifiers:
        if m.type == "ARMATURE": m.object = rig
bpy.data.objects.remove(body_rig, do_unlink=True)

# The outfit already supplies torso, arms (with hands), legs and feet. Keep only the head off the
# base body, or the bare mannequin pushes straight through the clothes.
import bmesh
HEAD = {"Head", "neck_01"}
skin = max(body_meshes, key=lambda o: len(o.data.vertices))
names = [g.name for g in skin.vertex_groups]
bm = bmesh.new(); bm.from_mesh(skin.data); bm.verts.ensure_lookup_table()
kill = []
for i, v in enumerate(skin.data.vertices):
    w = sum(g.weight for g in v.groups if names[g.group] in HEAD)
    if w < 0.5: kill.append(bm.verts[i])
bmesh.ops.delete(bm, geom=kill, context="VERTS")
bm.to_mesh(skin.data); bm.free()
print("MERGE base body %s trimmed to head: %d verts" % (skin.name, len(skin.data.vertices)))

# 2b. extra pieces on the same skeleton (hair, beard, eyebrows): same re-bind, nothing to trim
for path in adds:
    objs = imported(path)
    extra_rig = arm_of(objs)
    for o in [o for o in objs if o.type == "MESH"]:
        mw = o.matrix_world.copy()
        o.parent = rig; o.matrix_world = mw
        for m in o.modifiers:
            if m.type == "ARMATURE": m.object = rig
    bpy.data.objects.remove(extra_rig, do_unlink=True)

# 3. animation libraries: keep the actions, throw the mannequins away
clips = []
for a in anims:
    before = set(bpy.data.actions)
    objs = imported(a)
    for act in bpy.data.actions:
        if act in before: continue
        name = act.name.split("|")[-1]
        if name.endswith(".001"): name = name[:-4]
        if name == "A_TPose" or name in clips:
            bpy.data.actions.remove(act); continue
        act.name = name; act.use_fake_user = True; clips.append(name)
    for o in objs: bpy.data.objects.remove(o, do_unlink=True)

# 3b. second names for clips the enemy files ask for by name (the boss's moves)
for new, src in aliases:
    if new in clips or src not in bpy.data.actions:
        print("MERGE alias %s <- %s SKIPPED" % (new, src)); continue
    cp = bpy.data.actions[src].copy()
    cp.name = new; cp.use_fake_user = True; clips.append(new)

# 4. one NLA track per clip so the exporter emits one glTF animation per clip
for name in clips:
    act = bpy.data.actions[name]
    tr = rig.animation_data.nla_tracks.new()
    tr.name = name
    tr.strips.new(name, int(act.frame_range[0]), act)
    tr.mute = True

# 5. the engine only reads base colour: unhook normal/ORM maps and shrink what is left
def base_images(mat):
    # the packs feed Base Color straight from a texture on some materials and through a mix or a
    # colour node on others, so walk upstream instead of only looking at the direct link
    bsdf = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
    inp = bsdf.inputs.get("Base Color") if bsdf else None
    seen, todo, got = set(), [l.from_node for l in inp.links] if inp else [], set()
    while todo:
        n = todo.pop()
        if n in seen: continue
        seen.add(n)
        if n.type == "TEX_IMAGE" and n.image: got.add(n.image)
        for i in n.inputs:
            for l in i.links: todo.append(l.from_node)
    return got

keep, mat_images = set(), {}
for mat in bpy.data.materials:
    if not mat.use_nodes: continue
    bsdf = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
    if not bsdf: continue
    for slot in ("Normal", "Metallic", "Roughness", "Specular IOR Level", "Emission Color"):
        inp = bsdf.inputs.get(slot)
        if inp and inp.links:
            for l in list(inp.links): mat.node_tree.links.remove(l)
    bsdf.inputs["Metallic"].default_value = 0.0
    bsdf.inputs["Roughness"].default_value = 0.9
    mat_images[mat.name] = base_images(mat)
    keep |= mat_images[mat.name]
for img in bpy.data.images:
    if img in keep and max(img.size) > TEX:
        img.scale(TEX, TEX)

# 5b. --tint: one atlas per outfit means every peasant is the same peasant. Repaint the base
# colour of the materials under a prefix so the elder's linen, the smith's leather and the apprentice's
# smock come out of the one texture as different cloth. The engine's own `recolor` shifts single
# palette entries; this shifts the whole map, which is what these gradient atlases need.
def mats_of(prefix):
    # prefix names a material (MI_Peasant, MI_Ranger.001) or a mesh (Male_Ranger_Body), or "*"
    got = [n for n in mat_images if prefix == "*" or n.startswith(prefix)]
    for o in bpy.data.objects:
        if o.type != "MESH" or not o.name.startswith(prefix): continue
        for s in o.material_slots:
            if s.material and s.material.name in mat_images: got.append(s.material.name)
    return got

if tints:
    import numpy as np
    painted_mats, painted_imgs = set(), set()
    for prefix, rgb, sat in tints:
        for mname in mats_of(prefix):
            if mname in painted_mats: continue
            painted_mats.add(mname)
            mat = bpy.data.materials[mname]
            for img in list(mat_images[mname]):
                if img in painted_imgs:   # an earlier rule owns this atlas: work on a copy
                    dup = img.copy()
                    for n in mat.node_tree.nodes:
                        if n.type == "TEX_IMAGE" and n.image == img: n.image = dup
                    mat_images[mname].discard(img); mat_images[mname].add(dup)
                    keep.add(dup); img = dup
                painted_imgs.add(img)
                buf = np.empty(len(img.pixels), dtype=np.float32)
                img.pixels.foreach_get(buf)
                buf = buf.reshape(-1, 4)
                lum = buf[:, 0] * 0.299 + buf[:, 1] * 0.587 + buf[:, 2] * 0.114
                for c in range(3):
                    buf[:, c] = np.clip((lum + (buf[:, c] - lum) * sat) * rgb[c], 0.0, 1.0)
                img.pixels.foreach_set(buf.reshape(-1))
                img.update()
                print("MERGE tint %s (%s) rgb %s sat %s -> %s" % (prefix, mname, rgb, sat, img.name))

# 6. the rig is 65 bones and the engine skins at most 64. The fingertip and toe-tip "leaf"
# bones carry a sliver of weight each; fold them into their parents to get back under.
parent = {b.name: (b.parent.name if b.parent else None) for b in rig.data.bones}
for o in bpy.data.objects:
    if o.type != "MESH": continue
    for g in [g for g in o.vertex_groups if "_leaf" in g.name]:
        up = parent.get(g.name)
        if not up: continue
        dst_g = o.vertex_groups.get(up) or o.vertex_groups.new(name=up)
        idx = g.index
        for v in o.data.vertices:
            w = next((e.weight for e in v.groups if e.group == idx), 0.0)
            if w > 0: dst_g.add([v.index], w, "ADD")
        o.vertex_groups.remove(g)

# bones with no vertex weights are dead payload against the engine's 64-joint skin limit
used = set()
for o in bpy.data.objects:
    if o.type != "MESH": continue
    names = [g.name for g in o.vertex_groups]
    for v in o.data.vertices:
        for g in v.groups:
            if g.weight > 0.0001: used.add(names[g.group])
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode="EDIT")
drop = [b.name for b in rig.data.edit_bones if b.name not in used]
for n in drop:
    b = rig.data.edit_bones[n]
    for c in list(b.children): c.parent = b.parent
    rig.data.edit_bones.remove(b)
bpy.ops.object.mode_set(mode="OBJECT")
print("MERGE dropped %d unweighted bones, %d left" % (len(drop), len(rig.data.bones)))

bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", export_yup=True,
                          export_animation_mode="ACTIONS", export_apply=False,
                          export_optimize_animation_size=True, export_skins=True,
                          export_image_format="JPEG", export_jpeg_quality=90)
print("MERGE clips:", len(clips))
