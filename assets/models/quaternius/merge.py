# Headless Blender: fuse a Quaternius base body + a modular outfit + the Universal Animation
# Library clips onto one armature and write a single GLB the engine can load.
#   blender -b --python merge.py -- OUT.glb BODY.gltf OUTFIT.gltf ANIM1.glb [ANIM2.glb ...]
import bpy, sys, os

argv = sys.argv[sys.argv.index("--") + 1:]
out, body, outfit = argv[0], argv[1], argv[2]
anims = argv[3:]
TEX = 1024

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

# 4. one NLA track per clip so the exporter emits one glTF animation per clip
for name in clips:
    act = bpy.data.actions[name]
    tr = rig.animation_data.nla_tracks.new()
    tr.name = name
    tr.strips.new(name, int(act.frame_range[0]), act)
    tr.mute = True

# 5. the engine only reads base colour: unhook normal/ORM maps and shrink what is left
keep = set()
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
    inp = bsdf.inputs.get("Base Color")
    for l in (inp.links if inp else []):
        if l.from_node.type == "TEX_IMAGE" and l.from_node.image: keep.add(l.from_node.image)
for img in bpy.data.images:
    if img in keep and max(img.size) > TEX:
        img.scale(TEX, TEX)

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
