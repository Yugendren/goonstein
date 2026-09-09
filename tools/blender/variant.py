# Headless Blender: make a proportion variant of a KayKit character.
#   blender -b --python tools/blender/variant.py -- IN.glb OUT.glb [--height 1.15] [--width 0.9] [--head 1.3] [--arms 1.1] [--legs 1.0]
# Scales bones in rest pose (weights and every animation clip are kept), then re-exports glTF with
# the same clips. Run from the repo root. Requires Blender 4.x/5.x.
import bpy, sys, math
argv = sys.argv[sys.argv.index("--") + 1:]
src, dst = argv[0], argv[1]
opts = {"height": 1.0, "width": 1.0, "head": 1.0, "arms": 1.0, "legs": 1.0}
for i in range(2, len(argv), 2):
    k = argv[i].lstrip("-"); opts[k] = float(argv[i + 1])

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")

def scale_bones(names, s):
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="POSE")
    for b in arm.pose.bones:
        if b.name in names:
            b.scale = (b.scale[0] * s[0], b.scale[1] * s[1], b.scale[2] * s[2])
    bpy.ops.object.mode_set(mode="OBJECT")

# Overall height and width act on the root of the deform chain; head, arms and legs on their bones.
# KayKit bones: hips spine chest head, upperarm/lowerarm/hand .l/.r, upperleg/lowerleg/foot .l/.r
if opts["height"] != 1.0 or opts["width"] != 1.0:
    scale_bones({"hips"}, (opts["width"], opts["height"], opts["width"]))
if opts["head"] != 1.0:
    scale_bones({"head"}, (opts["head"],) * 3)
if opts["arms"] != 1.0:
    scale_bones({"upperarm.l", "upperarm.r"}, (1.0, opts["arms"], 1.0))
if opts["legs"] != 1.0:
    scale_bones({"upperleg.l", "upperleg.r"}, (1.0, opts["legs"], 1.0))

# Bake the pose scale into the rest pose so clips play on top of the new proportions.
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode="POSE")
bpy.ops.pose.armature_apply(selected=False)
bpy.ops.object.mode_set(mode="OBJECT")

bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB", export_animations=True, export_skins=True,
                          export_apply=False, export_yup=True, export_texcoords=True, export_normals=True,
                          export_materials="EXPORT", export_image_format="AUTO")
print("variant written:", dst)
