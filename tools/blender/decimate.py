# Headless Blender: bring a heavy scan or CAD export down to a game budget and export glTF.
#   blender -b --python tools/blender/decimate.py -- IN.(obj|stl|fbx|glb|gltf) OUT.glb [--ratio 0.1] [--target 20000] [--ground 1]
# ratio: fraction of triangles to keep (Decimate collapse). target: triangle count to aim for
# (overrides ratio). ground 1 drops the model so its lowest point is y = 0.
import bpy, sys, os
argv = sys.argv[sys.argv.index("--") + 1:]
src, dst = argv[0], argv[1]
opts = {"ratio": 0.1, "target": 0, "ground": 1}
for i in range(2, len(argv), 2):
    k = argv[i].lstrip("-"); opts[k] = float(argv[i + 1])
bpy.ops.wm.read_factory_settings(use_empty=True)
ext = os.path.splitext(src)[1].lower()
if ext == ".obj": bpy.ops.wm.obj_import(filepath=src)
elif ext == ".stl": bpy.ops.wm.stl_import(filepath=src)
elif ext == ".fbx": bpy.ops.import_scene.fbx(filepath=src)
else: bpy.ops.import_scene.gltf(filepath=src)
meshes = [o for o in bpy.data.objects if o.type == "MESH"]
total = sum(len(o.data.polygons) for o in meshes)
ratio = opts["ratio"] if opts["target"] <= 0 else min(1.0, opts["target"] / max(total, 1))
for o in meshes:
    bpy.context.view_layer.objects.active = o
    m = o.modifiers.new("dec", "DECIMATE"); m.ratio = ratio
    bpy.ops.object.modifier_apply(modifier="dec")
if opts["ground"]:
    lowest = min((o.matrix_world @ v.co).y if False else (o.matrix_world @ v.co).z for o in meshes for v in o.data.vertices)
    for o in meshes: o.location.z -= lowest
after = sum(len(o.data.polygons) for o in meshes)
bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB", export_yup=True, export_apply=True)
print(f"decimated {total} -> {after} triangles: {dst}")
