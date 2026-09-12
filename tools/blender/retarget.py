# Headless Blender: put an existing animation library on a new body, by moving the new body onto
# the library's rest pose rather than by re-recording a single keyframe.
#
#   blender -b --python tools/blender/retarget.py -- BODY.glb CLIPS.glb OUT.glb \
#       [--map THEIRS=OURS ...] [--fit BONE] [--tex 512]
#
# BODY.glb is a rigged mesh with no animation (tools/blender/makegoon.py writes one). CLIPS.glb is
# a rigged mesh that carries the clips (assets/models/quaternius/ranger_male.glb, 84 of them). OUT
# is one GLB with the body's meshes, the body's proportions and every one of the clips, loadable
# through the ordinary `model` and `anim` lines of a character file.
#
# WHY THIS WORKS AT ALL, AND WHY IT IS NOT A BAKE
#
# MakeHuman's "game engine" skeleton and the Quaternius universal skeleton are the same rig: 53
# bones, the Unreal mannequin's names (root, pelvis, spine_01..03, clavicle_l, upperarm_l,
# lowerarm_l, hand_l, the four fingers and thumb in three joints each, neck_01, Head, thigh_l,
# calf_l, foot_l, ball_l, and the same on the right), in the same hierarchy. Only the spelling of
# two of them differs, which is what --map is for.
#
# So there is no bone mapping problem. There is a REST POSE problem: MakeHuman's body stands in an
# A-pose -- the upper arm leaves the shoulder about 48 degrees below horizontal -- and every
# Quaternius clip was authored against a T-pose. A clip stores each bone's rotation *relative to
# its own rest*, so playing a T-pose clip on an A-pose rig leaves the arms 48 degrees low in every
# frame of all 84 of them.
#
# The fix is to move the body, not the animation:
#
#   1. rename the body's bones to the clip rig's spelling, and its vertex groups with them;
#   2. scale the body uniformly so one landmark bone (--fit, the base of the neck by default)
#      sits at the clip rig's height, so that the clips' root and hip translations still land;
#   3. pose every body bone so its orientation in space equals the clip rig's REST orientation for
#      the bone of that name, leaving each bone's head where it already is -- the body is now
#      standing in the clips' T-pose, with its own limb lengths and its own joint positions;
#   4. bake that pose into the meshes (apply the armature modifier) and then make it the rest pose;
#   5. hang the clips' actions on the result, untouched, one NLA track each.
#
# After step 4 the two rigs agree on every bone's rest orientation, so a clip's stored local
# rotations mean exactly what they meant on the rig they were authored for. Nothing is resampled,
# no keyframe is created or moved, and the output GLB carries the same keyframe count as the pack
# did. The body keeps its own bone lengths and joint positions, which is the whole point: a short
# goon has short legs and the clip still plays.
#
# What is approximate: root and hip TRANSLATION keys are in metres and belong to the body they
# were recorded on, so a body with legs of a noticeably different length slides a little against
# them. Step 2 keeps that within a couple of per cent for anything human-shaped. Shoulder skinning
# also has to survive a 48-degree rotation from where MakeHuman weighted it, which MakeHuman's own
# weights do comfortably; it is the same rotation the game asks for whenever anyone reaches up.
import bpy, sys, os
from mathutils import Matrix

argv = sys.argv[sys.argv.index("--") + 1:]
# The two rigs differ by two spellings and nothing else. THEIRS is the name on the body, OURS the
# name the clips use.
rename = {"Root": "root", "head": "Head"}
FIT, TEX, pos = "neck_01", 512, []
i = 0
while i < len(argv):
    if argv[i] == "--map":
        k, v = argv[i + 1].split("=", 1); rename[k] = v; i += 2
    elif argv[i] == "--fit": FIT = argv[i + 1]; i += 2
    elif argv[i] == "--tex": TEX = int(argv[i + 1]); i += 2
    else: pos.append(argv[i]); i += 1
body_path, clips_path, out = pos[0], pos[1], pos[2]

bpy.ops.wm.read_factory_settings(use_empty=True)

def imported(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    return [o for o in bpy.data.objects if o not in before]

def arm_of(objs):
    return next(o for o in objs if o.type == "ARMATURE")

def sel(objs, active=None):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs: o.select_set(True)
    bpy.context.view_layer.objects.active = active or (objs[0] if objs else None)

body_objs = imported(body_path)
rig = arm_of(body_objs)
# A mesh with no material and no vertex groups is not part of the character: MPFB and the
# Quaternius exports both leave a stray unit icosphere behind, and a 2 m ball round a goon's
# waist is exactly the kind of thing that only shows up in a screenshot.
for o in [o for o in body_objs if o.type == "MESH" and (not o.data.materials or not o.vertex_groups)]:
    print("RETARGET dropped stray mesh %s (%d tris, no material or no weights)"
          % (o.name, len(o.data.polygons)))
    body_objs.remove(o); bpy.data.objects.remove(o, do_unlink=True)
meshes = [o for o in body_objs if o.type == "MESH"]

clip_objs = imported(clips_path)
clip_rig = arm_of(clip_objs)
# the clip file is only here for its rest pose and its actions; its meshes go straight away (the
# Quaternius exports carry a stray unit icosphere that would otherwise ride along into the goon)
for o in [o for o in clip_objs if o.type != "ARMATURE"]:
    bpy.data.objects.remove(o, do_unlink=True)

# 1. one spelling ------------------------------------------------------------------------------
for old, new in rename.items():
    b = rig.data.bones.get(old)
    if not b:
        print("RETARGET map %s -> %s: no such bone on the body" % (old, new)); continue
    b.name = new
    for o in meshes:
        g = o.vertex_groups.get(old)
        if g: g.name = new
missing = [b.name for b in clip_rig.data.bones if b.name not in rig.data.bones]
extra = [b.name for b in rig.data.bones if b.name not in clip_rig.data.bones]
print("RETARGET body %d bones, clips %d bones; not on the body: %s; not in the clips: %s"
      % (len(rig.data.bones), len(clip_rig.data.bones), missing or "none", extra or "none"))

# the rest orientation of every clip-rig bone, in world space; this is what the body has to adopt
rest = {}
for b in clip_rig.data.bones:
    rest[b.name] = (clip_rig.matrix_world @ b.matrix_local).to_quaternion().to_matrix().to_4x4()

# 2. one size ----------------------------------------------------------------------------------
# Matched at a landmark rather than at the crown: the clip rig's Head bone ends mid-skull and
# MakeHuman's ends at the top of it, so "total bone span" would quietly shrink every goon.
def land(a, n):
    return (a.matrix_world @ a.data.bones[n].matrix_local).translation.z
if FIT in rig.data.bones and FIT in clip_rig.data.bones:
    k = land(clip_rig, FIT) / land(rig, FIT)
    # Scaled in place -- bone rest positions in edit mode, vertex coordinates in mesh data --
    # rather than through object scale, because the glTF importer leaves the meshes parented to
    # the armature and scaling a parent and its children both is how a body ends up 1.4x too
    # tall. A uniform factor commutes with the importer's Y-up rotation, so doing it in each
    # object's own space is the same as doing it in the world's.
    sel([rig], rig)
    bpy.ops.object.mode_set(mode="EDIT")
    for eb in rig.data.edit_bones:
        eb.head, eb.tail = eb.head * k, eb.tail * k
    bpy.ops.object.mode_set(mode="OBJECT")
    for o in meshes:
        for v in o.data.vertices: v.co = v.co * k
    print("RETARGET fit on %s: scaled body by %.4f" % (FIT, k))

# 3. stand the body in the clips' pose ----------------------------------------------------------
# Bone by bone, parents first: keep the head where the parent has just put it, take the rotation
# from the clip rig's rest. A bone the clips do not have is left alone and follows its parent.
sel([rig], rig)
bpy.ops.object.mode_set(mode="POSE")
order, todo = [], [b for b in rig.data.bones if b.parent is None]
while todo:
    b = todo.pop(0); order.append(b.name); todo.extend(b.children)
inv = rig.matrix_world.inverted()
for name in order:
    if name not in rest: continue
    pb = rig.pose.bones[name]
    bpy.context.view_layer.update()
    here = (rig.matrix_world @ pb.matrix).translation
    pb.matrix = inv @ (Matrix.Translation(here) @ rest[name])
bpy.context.view_layer.update()

# 4. bake it into the meshes, then into the rest pose -------------------------------------------
bpy.ops.object.mode_set(mode="OBJECT")
for o in meshes:
    sel([o], o)
    for m in list(o.modifiers):
        if m.type == "ARMATURE": bpy.ops.object.modifier_apply(modifier=m.name)
sel([rig], rig)
bpy.ops.object.mode_set(mode="POSE")
bpy.ops.pose.armature_apply()
bpy.ops.object.mode_set(mode="OBJECT")
for o in meshes:
    mw = o.matrix_world.copy()
    o.parent = rig; o.matrix_world = mw
    m = o.modifiers.new("Armature", "ARMATURE"); m.object = rig

# 5. the clips, untouched ------------------------------------------------------------------------
rig.animation_data_create()
clips = []
for act in bpy.data.actions:
    name = act.name.split("|")[-1]
    if name.endswith(".001"): name = name[:-4]
    if name == "A_TPose" or name in clips: continue
    act.name = name; act.use_fake_user = True; clips.append(name)
for name in clips:
    tr = rig.animation_data.nla_tracks.new()
    tr.name = name
    tr.strips.new(name, int(bpy.data.actions[name].frame_range[0]), bpy.data.actions[name])
    tr.mute = True
bpy.data.objects.remove(clip_rig, do_unlink=True)
for o in [o for o in bpy.data.objects if o is not rig and o not in meshes]:
    bpy.data.objects.remove(o, do_unlink=True)

# 6. one base colour per material, at most TEX px, same rule as merge.py -------------------------
for mat in bpy.data.materials:
    if not mat.use_nodes: continue
    bsdf = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
    if not bsdf: continue
    for slot in ("Normal", "Metallic", "Roughness", "Specular IOR Level", "Emission Color"):
        inp = bsdf.inputs.get(slot)
        if inp:
            for l in list(inp.links): mat.node_tree.links.remove(l)
    bsdf.inputs["Metallic"].default_value = 0.0
    bsdf.inputs["Roughness"].default_value = 0.9
for img in bpy.data.images:
    if img.users and max(img.size) > TEX:
        s = TEX / float(max(img.size))
        img.scale(max(1, int(img.size[0] * s)), max(1, int(img.size[1] * s)))

# bones nothing is weighted to are dead payload against the engine's 64-joint limit
used = set()
for o in meshes:
    names = [g.name for g in o.vertex_groups]
    for v in o.data.vertices:
        for g in v.groups:
            if g.weight > 0.0001: used.add(names[g.group])
sel([rig], rig)
bpy.ops.object.mode_set(mode="EDIT")
for n in [b.name for b in rig.data.edit_bones if b.name not in used]:
    b = rig.data.edit_bones[n]
    for c in list(b.children): c.parent = b.parent
    rig.data.edit_bones.remove(b)
bpy.ops.object.mode_set(mode="OBJECT")

bpy.ops.object.select_all(action="SELECT")
bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", export_yup=True,
                          export_animation_mode="ACTIONS", export_apply=False,
                          export_optimize_animation_size=True, export_skins=True,
                          export_morph=False, export_cameras=False, export_lights=False,
                          # AUTO, not JPEG: hair and eyebrows are alpha cut-outs and shaders/lit.frag
                          # discards below 0.5, so an alpha channel is the mesh's actual shape
                          export_image_format="AUTO")

tris = 0
for o in meshes:
    o.data.calc_loop_triangles(); tris += len(o.data.loop_triangles)
zs = [(o.matrix_world @ v.co).z for o in meshes for v in o.data.vertices]
print("RETARGET %s: %d meshes, %d tris, %d joints, %d clips, %.3f m tall, %.2f MB"
      % (os.path.basename(out), len(meshes), tris, len(rig.data.bones), len(clips),
         max(zs) - min(zs), os.path.getsize(out) / 1e6))
