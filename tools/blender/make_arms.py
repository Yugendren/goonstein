# Headless Blender: trim a goon's full body down to just its two arms (shoulder to fingertip) for
# the first-person weapon viewmodel.
#
#   blender -b --python tools/blender/make_arms.py -- IN.glb OUT.glb [--tex 512] [--keep-clips NAME,NAME,...]
#
# WHY: in first person the camera sits inside the goon's own head. The player's hands and the gun
# are the only part of their body that should ever be on screen, so the viewmodel needs the goon's
# actual skinned arms -- not the placeholder untextured cubes the renderer currently draws instead.
# The head and torso would clip straight through the camera and are cut, along with everything
# below the shoulders (hair, eyebrows, shoes, legs): the same armature is kept whole because every
# clip animates the full 52-bone skeleton and a bone with no geometry left on it is harmless, but
# cutting a bone would break every clip that moves it.
#
# The body (IN.glb) is one of assets/models/characters/goon_[a-d].glb: a MakeHuman body retargeted
# onto the Quaternius universal rig by tools/blender/retarget.py -- 52 joints, the Unreal mannequin
# names (clavicle_l/r, upperarm_l/r, lowerarm_l/r, hand_l/r, three-joint fingers, ...), and however
# many of the 84 Quaternius clips retarget.py left on it. --keep-clips (default: the handful the
# first-person rig actually plays) drops the rest, since a viewmodel has no use for a walk cycle.
#
# What decides "this vertex is an arm": for every vertex, sum its skin weight across the arm-bone
# vertex groups (clavicle/upperarm/lowerarm/hand and every finger bone, matched by name). >= 0.5 of
# its weight on an arm bone means the vertex moves with the arm and is kept; the rest -- torso,
# head, legs -- is deleted. A whole mesh with no vertex weighted to an arm bone at all (eyebrows,
# hair, shoes) is dropped outright rather than trimmed to nothing.
import bpy, bmesh, sys, os

argv = sys.argv[sys.argv.index("--") + 1:]
src, dst = argv[0], argv[1]
DEFAULT_CLIPS = ["Pistol_Idle_Loop", "Pistol_Shoot", "Pistol_Reload", "Pistol_Aim_Up",
                  "Pistol_Aim_Down", "Pistol_Aim_Neutral", "Idle_Loop", "Melee_Hook", "Punch_Jab",
                  "Interact", "OverhandThrow"]
opts = {"tex": 512, "keep_clips": list(DEFAULT_CLIPS), "side": "both"}
i = 2
while i < len(argv):
    k = argv[i].lstrip("-").replace("-", "_")
    if k == "tex":
        opts["tex"] = int(argv[i + 1]); i += 2
    elif k == "keep_clips":
        opts["keep_clips"] = [c for c in argv[i + 1].split(",") if c]; i += 2
    elif k == "side":
        # --side right keeps the trigger arm only. The free animation library this cast is rigged
        # against has one-handed pistol clips and no two-handed one, so a left arm on screen has
        # nothing to hold and sits in open air in the middle of the picture. Doom and Quake drew one
        # hand for exactly this reason. --side both is still there for a rig that grows a rifle clip.
        opts["side"] = argv[i + 1]; i += 2
    else:
        i += 1

# Arm bones: the four arm segments by exact name, plus every finger bone by substring (their names
# carry a joint number too, e.g. index_01_l, so a prefix match would miss them).
ARM_BONES = ("clavicle_l", "clavicle_r", "upperarm_l", "upperarm_r", "lowerarm_l", "lowerarm_r",
             "hand_l", "hand_r")
FINGER_SUBSTR = ("index_", "middle_", "ring_", "pinky_", "thumb_")


def is_arm_bone(name):
    if opts["side"] in ("right", "left"):
        want = "_r" if opts["side"] == "right" else "_l"
        if not name.endswith(want):
            return False
    return name in ARM_BONES or any(s in name for s in FINGER_SUBSTR)


bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)

meshes = [o for o in bpy.data.objects if o.type == "MESH"]
tris_before = 0
for o in meshes:
    o.data.calc_loop_triangles()
    tris_before += len(o.data.loop_triangles)

# --- drop any mesh with no vertex weighted at all to an arm bone (eyebrows, hair, shoes, stray
# marker meshes with no vertex groups at all) ---
kept = []
for o in meshes:
    arm_groups = {g.index for g in o.vertex_groups if is_arm_bone(g.name)}
    has_arm_weight = False
    if arm_groups:
        for v in o.data.vertices:
            if any(ge.group in arm_groups and ge.weight > 0.0 for ge in v.groups):
                has_arm_weight = True
                break
    if has_arm_weight:
        kept.append(o)
    else:
        bpy.data.objects.remove(o, do_unlink=True)
meshes = kept

# --- trim every surviving mesh to just the arm-weighted geometry ---
for o in meshes:
    arm_groups = {g.index for g in o.vertex_groups if is_arm_bone(g.name)}
    bm = bmesh.new()
    bm.from_mesh(o.data)
    dvert_layer = bm.verts.layers.deform.verify()
    bm.verts.ensure_lookup_table()
    non_arm = [v for v in bm.verts
               if sum(w for gi, w in v[dvert_layer].items() if gi in arm_groups) < 0.5]
    bmesh.ops.delete(bm, geom=non_arm, context="VERTS")
    # loose / zero-area leftovers: degenerate faces first (deleting them can leave loose edges and
    # verts behind), then anything with no faces left on it.
    zero_faces = [f for f in bm.faces if f.calc_area() < 1e-10]
    bmesh.ops.delete(bm, geom=zero_faces, context="FACES")
    loose_edges = [e for e in bm.edges if not e.link_faces]
    bmesh.ops.delete(bm, geom=loose_edges, context="EDGES")
    loose_verts = [v for v in bm.verts if not v.link_faces]
    bmesh.ops.delete(bm, geom=loose_verts, context="VERTS")
    bm.to_mesh(o.data)
    bm.free()
    o.data.update()

# a mesh that trimmed down to nothing (e.g. it only just brushed an arm bone) is dead weight
for o in list(meshes):
    if len(o.data.vertices) == 0:
        meshes.remove(o)
        bpy.data.objects.remove(o, do_unlink=True)

tris_after = 0
for o in meshes:
    o.data.calc_loop_triangles()
    tris_after += len(o.data.loop_triangles)

# --- clips: drop every action/NLA strip not in the keep list; leave the armature and all its
# bones alone, since a bone with no geometry on it is still needed to play the clips that move it ---
keep_set = set(opts["keep_clips"])
arm = next((o for o in bpy.data.objects if o.type == "ARMATURE"), None)
if arm and arm.animation_data:
    for tr in list(arm.animation_data.nla_tracks):
        if tr.name not in keep_set:
            arm.animation_data.nla_tracks.remove(tr)
    if arm.animation_data.action and arm.animation_data.action.name not in keep_set:
        arm.animation_data.action = None
for act in list(bpy.data.actions):
    if act.name not in keep_set:
        act.use_fake_user = False
        bpy.data.actions.remove(act, do_unlink=True)
clips = sorted(a.name for a in bpy.data.actions)
missing = keep_set - set(clips)
if missing:
    print("ARMS WARNING %s: missing requested clips: %s" % (os.path.basename(src), ", ".join(sorted(missing))))

# --- textures: keep only each surviving material's base colour image, capped at --tex px. Only
# materials still used by a surviving mesh are touched, so a texture that belonged to a dropped
# mesh (eyebrows, hair, shoes) is never kept just because its material datablock is still lying
# around unused. ---
used_materials = set()
for o in meshes:
    for m in o.data.materials:
        if m:
            used_materials.add(m)

base_images = set()
for mat in used_materials:
    if not mat.use_nodes:
        continue
    nt = mat.node_tree
    bsdf = next((n for n in nt.nodes if n.type == "BSDF_PRINCIPLED"), None)
    out = next((n for n in nt.nodes if n.type == "OUTPUT_MATERIAL"), None)
    if not bsdf or not out:
        continue
    base_input = bsdf.inputs["Base Color"]
    base_img_node = None
    if base_input.is_linked and base_input.links[0].from_node.type == "TEX_IMAGE":
        base_img_node = base_input.links[0].from_node
    keep_nodes = {bsdf, out}
    if base_img_node:
        keep_nodes.add(base_img_node)
        vec_in = base_img_node.inputs.get("Vector")
        if vec_in and vec_in.is_linked:
            keep_nodes.add(vec_in.links[0].from_node)
        base_images.add(base_img_node.image)
    for n in list(nt.nodes):
        if n not in keep_nodes:
            nt.nodes.remove(n)
    if base_img_node:
        nt.links.new(base_img_node.outputs["Color"], bsdf.inputs["Base Color"])
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])

for img in list(bpy.data.images):
    if img not in base_images:
        bpy.data.images.remove(img)
for img in base_images:
    if img is None:
        continue
    long_side = max(img.size[0], img.size[1])
    if long_side > opts["tex"]:
        s = opts["tex"] / long_side
        img.scale(max(1, round(img.size[0] * s)), max(1, round(img.size[1] * s)))
    img.pack()

bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB", export_yup=True,
                           export_animations=True, export_animation_mode="ACTIONS",
                           export_apply=False, export_optimize_animation_size=True,
                           export_skins=True, export_morph=False, export_cameras=False,
                           export_lights=False, export_image_format="AUTO")

print("ARMS in=%s tris_before=%d tris_after=%d meshes=%d clips=%d bytes=%d" %
      (os.path.basename(src), tris_before, tris_after, len(meshes), len(clips),
       os.path.getsize(dst)))
