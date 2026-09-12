# Headless Blender + MPFB2: build one realistic human body from a recipe and write a rigged GLB.
#
#   blender -b --python tools/blender/makegoon.py -- RECIPE.json OUT.glb [--tex 512] [--tris N]
#
# MPFB2 is the MakeHuman Plugin For Blender. It generates the MakeHuman base mesh, shapes it with
# the macro sliders (gender, age, muscle, weight, height, race), fits clothes/hair/eyes to it and
# rigs it. Everything it ships and everything it emits is CC0 -- the plugin's own code is GPL but
# the code is not what we ship. It is not installed in this repo (the plugin is 42 MB and the
# asset pack 280 MB); see README "Characters" for the two downloads and the one environment
# variable, BLENDER_USER_EXTENSIONS, that point this script at them.
#
# What comes out is a body in MakeHuman's A-pose on MakeHuman's "game engine" skeleton. That
# skeleton is the Unreal mannequin's: 53 bones called root/pelvis/spine_01..03/clavicle_l/
# upperarm_l/.../thigh_l/calf_l/foot_l/ball_l -- the same 53 names, in the same hierarchy, as the
# Quaternius universal rig that carries our 84 animation clips. tools/blender/retarget.py takes it
# from here and puts the clips on it.
#
# The recipe is a JSON file under assets/characters/mh/. Every key is optional except phenotype:
#
#   {
#     "name":      "dez",
#     "phenotype": { "gender":0.95, "age":0.55, "muscle":0.72, "weight":0.70,
#                    "proportions":0.45, "height":0.72,
#                    "race": {"asian":0.15, "caucasian":0.60, "african":0.25} },
#     "proxy":     "male1591/male1591.proxy",          # the body mesh; the raw base mesh is 26k tris
#     "skin":      "young_caucasian_male/young_caucasian_male.mhmat",
#     "eyes":      "low-poly/low-poly.mhclo",
#     "eyebrows":  "eyebrow007/eyebrow007.mhclo",
#     "hair":      "short02/short02.mhclo",
#     "clothes":   ["male_casualsuit01/male_casualsuit01.mhclo", "shoes04/shoes04.mhclo"],
#     "tint":      { "male_casualsuit01": [1.30, 0.55, 0.30, 0.55] },   # r g b [saturation]
#     "budget":    { "male_casualsuit01": 7000, "short02": 2000 }       # per-mesh triangle caps
#   }
#
# "tint" repaints a mesh's base-colour map the way assets/models/quaternius/merge.py does: pull
# saturation towards grey, then multiply by r g b. The four goons wear four of MakeHuman's six
# casual suits, and the tint is what stops the two that share a cut from being the same man.
#
# Faces are generic by construction: the shape is the macro sliders plus MakeHuman's own symmetric
# base mesh, and the skin is one of MakeHuman's generic CC0 skin maps. No scan, no likeness, no
# photograph of anybody.
import bpy, sys, os, json, math

argv = sys.argv[sys.argv.index("--") + 1:]
TEX, pos = 512, []
i = 0
while i < len(argv):
    if argv[i] == "--tex": TEX = int(argv[i + 1]); i += 2
    else: pos.append(argv[i]); i += 1
recipe_path, out = pos[0], pos[1]
recipe = json.load(open(recipe_path))

bpy.ops.wm.read_factory_settings(use_empty=True)
try:
    bpy.ops.preferences.addon_enable(module="bl_ext.user_default.mpfb")
except Exception as e:
    raise SystemExit("MPFB2 is not installed for this Blender. See README, \"Characters\". (%s)" % e)
from bl_ext.user_default.mpfb.services.humanservice import HumanService

# 1. build the human ------------------------------------------------------------------------
# detailed_helpers and extra_vertex_groups have to stay on: MakeHuman fits its skeleton to the
# base mesh through the helper geometry, and without it the rig comes out default-proportioned,
# hip-centred and a foot short of the body it is supposed to be inside.
info = HumanService._create_default_human_info_dict()
info["name"] = recipe.get("name", os.path.splitext(os.path.basename(recipe_path))[0])
info["phenotype"] = dict(HumanService._create_default_human_info_dict()["phenotype"])
info["phenotype"].update(recipe["phenotype"])
info["rig"] = "game_engine"
for key in ("eyes", "eyebrows", "eyelashes", "hair", "proxy", "teeth", "tongue"):
    if recipe.get(key): info[key] = recipe[key]
info["clothes"] = list(recipe.get("clothes", []))
if recipe.get("skin"):
    info["skin_mhmat"] = recipe["skin"]
    info["skin_material_type"] = "GAMEENGINE"

ds = HumanService.get_default_deserialization_settings()
ds["subdiv_levels"] = 0                 # a game body is not a render; no subdivision
ds["scale"] = 0.1                       # MakeHuman works in decimetres
ds["override_skin_model"] = "GAMEENGINE"    # one image into Base Color, which is all src/model.c reads
ds["override_clothes_model"] = "MAKESKIN"
ds["override_eyes_model"] = "MAKESKIN"
basemesh = HumanService.deserialize_from_dict(info, ds)
name = info["name"]

rig = next(o for o in bpy.data.objects if o.type == "ARMATURE")
rig.name = name

def sel(objs, active=None):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs: o.select_set(True)
    bpy.context.view_layer.objects.active = active or (objs[0] if objs else None)

# 2. flatten every mesh: apply the masks and the shape keys, drop what is left empty -----------
# The base mesh carries the 30-odd MakeHuman targets as shape keys and four mask modifiers that
# hide the helpers and whatever the clothes cover. Baking them here means retarget.py and the
# engine both see plain geometry.
dg = bpy.context.evaluated_depsgraph_get()
meshes = []
for o in [o for o in bpy.data.objects if o.type == "MESH"]:
    if o.data.shape_keys:                                     # bake the targets into the mesh
        # Removing the last shape key leaves the mesh on the BASIS shape, not on the mix, so the
        # mix has to be copied into the vertices before the keys go.
        sel([o], o)
        mixed = o.shape_key_add(name="baked", from_mix=True)
        co = [0.0] * (len(o.data.vertices) * 3)
        mixed.data.foreach_get("co", co)
        while o.data.shape_keys:
            o.shape_key_remove(o.data.shape_keys.key_blocks[0])
        o.data.vertices.foreach_set("co", co)
        o.data.update()
    sel([o], o)
    for m in list(o.modifiers):
        if m.type != "ARMATURE":
            try: bpy.ops.object.modifier_apply(modifier=m.name)
            except RuntimeError: o.modifiers.remove(m)
    # the base mesh under a proxy masks down to nothing, and MPFB leaves one unparented
    # icosphere in the scene; neither belongs in a character file
    if len(o.data.polygons) == 0 or not o.data.materials or o.parent is not rig:
        bpy.data.objects.remove(o, do_unlink=True)
        continue
    meshes.append(o)

# 3. triangle budgets -------------------------------------------------------------------------
budget = recipe.get("budget", {})
for o in meshes:
    cap = None
    for key, n in budget.items():
        if key in o.name: cap = n
    if not cap: continue
    o.data.calc_loop_triangles()
    tris = len(o.data.loop_triangles)
    if tris <= cap: continue
    sel([o], o)
    m = o.modifiers.new("dec", "DECIMATE")
    m.decimate_type, m.ratio = "COLLAPSE", cap / float(tris)
    bpy.ops.object.modifier_apply(modifier=m.name)

# 4. one base-colour image per material, at most TEX px ---------------------------------------
# src/model.c reads a single texture per material. MakeHuman ships 2048 px diffuse, AO and normal
# maps; the AO and normal maps are most of the file for something nothing ever samples. MPFB's
# materials hide their image nodes inside node groups, so this recurses and sorts the images out
# by name -- walking the links would mean walking group interfaces for no extra certainty.
SECOND = ("normal", "_nor", "bump", "_ao", "ambient", "occlusion", "rough", "spec",
          "metal", "displ", "height", "sss", "subsurf", "transmission", "_disp")
def image_nodes(tree, got=None):
    got = [] if got is None else got
    for n in tree.nodes:
        if n.type == "TEX_IMAGE" and n.image: got.append((tree, n))
        elif n.type == "GROUP" and n.node_tree: image_nodes(n.node_tree, got)
    return got

mat_images = {}
for mat in bpy.data.materials:
    if not mat.use_nodes: continue
    base = set()
    for tree, node in image_nodes(mat.node_tree):
        low = node.image.name.lower()
        if any(s in low for s in SECOND): tree.nodes.remove(node)
        else: base.add(node.image)
    mat_images[mat.name] = base
keep = {img for imgs in mat_images.values() for img in imgs}

# Alpha is the mesh's real shape for hair, eyebrows and eyelashes -- shaders/lit.frag discards
# below 0.5 and a hair cap without its cut-out is a helmet. Everywhere else it is a trap:
# MakeHuman's cloth and skin maps carry an alpha channel with soft patches in it, and alpha
# testing those punches holes in a trouser leg. So: opaque everywhere except the cut-outs.
cutout = {os.path.basename(os.path.dirname(recipe[k])) for k in ("hair", "eyebrows", "eyelashes")
          if recipe.get(k)}
import numpy as np
for mat in bpy.data.materials:
    if not mat.use_nodes: continue
    if any(c and c in mat.name for c in cutout): continue
    bsdf = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
    if bsdf and bsdf.inputs.get("Alpha"):
        for l in list(bsdf.inputs["Alpha"].links): mat.node_tree.links.remove(l)
        bsdf.inputs["Alpha"].default_value = 1.0
    for img in mat_images.get(mat.name, ()):
        if img.channels < 4: continue
        buf = np.empty(len(img.pixels), dtype=np.float32)
        img.pixels.foreach_get(buf)
        buf = buf.reshape(-1, 4)
        if buf[:, 3].min() >= 0.999: continue
        buf[:, 3] = 1.0
        img.pixels.foreach_set(buf.reshape(-1)); img.update()
        print("OPAQUE %s (%s)" % (mat.name, img.name))
for img in list(bpy.data.images):
    if img not in keep:
        if img.users == 0: bpy.data.images.remove(img)
        continue
    if max(img.size) > TEX:
        s = TEX / float(max(img.size))
        img.scale(max(1, int(img.size[0] * s)), max(1, int(img.size[1] * s)))

# 5. tint ------------------------------------------------------------------------------------
# Same idea as merge.py --tint: saturation towards grey, then a multiply. MakeHuman's six casual
# suits are six cuts in three or four colours; this is what makes four goons out of them. It runs
# after the shrink, so it repaints 512 px and not 2048.
painted = set()
for key, rule in recipe.get("tint", {}).items():
    rgb = rule[:3]
    sat = rule[3] if len(rule) > 3 else 1.0
    for mname, imgs in mat_images.items():
        if key not in mname: continue
        for img in imgs:
            if img.name in painted: continue
            painted.add(img.name)
            buf = np.empty(len(img.pixels), dtype=np.float32)
            img.pixels.foreach_get(buf)
            buf = buf.reshape(-1, 4)
            lum = buf[:, 0] * 0.299 + buf[:, 1] * 0.587 + buf[:, 2] * 0.114
            for c in range(3):
                buf[:, c] = np.clip((lum + (buf[:, c] - lum) * sat) * rgb[c], 0.0, 1.0)
            img.pixels.foreach_set(buf.reshape(-1))
            img.update()
            print("TINT %s (%s) rgb %s sat %s" % (key, img.name, rgb, sat))

# 6. name things after the goon, and export ---------------------------------------------------
for o in meshes:
    o.name = o.name.replace(name + ".", "").replace(name, "body")
for mat in bpy.data.materials:
    mat.name = mat.name.replace(name + ".", "")

# whatever else ended up in the scene is not this character (MPFB leaves a stray icosphere)
for o in [o for o in bpy.data.objects if o is not rig and o not in meshes]:
    bpy.data.objects.remove(o, do_unlink=True)

bpy.ops.object.select_all(action="SELECT")
bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", export_yup=True,
                          export_apply=False, export_animations=False,
                          export_skins=True, export_morph=False,
                          export_cameras=False, export_lights=False)

tot = 0
for o in meshes:
    o.data.calc_loop_triangles(); tot += len(o.data.loop_triangles)
    print("MESH %-28s tris %6d" % (o.name, len(o.data.loop_triangles)))
zs = [(o.matrix_world @ v.co).z for o in meshes for v in o.data.vertices]
print("BONES %d  TRIS %d  HEIGHT %.3f m  IMAGES %s" % (
    len(rig.data.bones), tot, max(zs) - min(zs),
    [(i.name, i.size[0], i.size[1]) for i in bpy.data.images]))
print("WROTE %s  %.2f MB" % (out, os.path.getsize(out) / 1e6))
