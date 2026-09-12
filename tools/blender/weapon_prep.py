# Headless Blender: turn a Poly Haven-style photoscan glTF (several loose meshes, arbitrary
# orientation, full PBR texture set) into one hand-held-weapon GLB in the engine's canonical frame.
#   blender -b --python tools/blender/weapon_prep.py -- IN.gltf OUT.glb \
#       [--keep NAME ...] [--drop NAME ...] --aim +X|-X|+Y|-Y|+Z|-Z --up +X|-X|+Y|-Y|+Z|-Z \
#       [--origin X Y Z] [--tex 512] [--decimate N] [--scale S]
# --keep/--drop match mesh object names by substring (as imported); --keep wins if both given.
# --aim/--up say where the weapon's business end (muzzle/barrel tip/jaw) and its "up" point in the
# imported Blender frame; geometry is rotated so aim -> Blender -Y and up -> Blender +Z. That is
# the canonical frame: glTF export (+Y up) turns Blender -Y into glTF +Z and Blender +Z into glTF
# +Y, so the exported model points its business end down glTF +Z with up = glTF +Y. --origin is a
# point in the ORIGINAL imported coordinates (the grip) that is translated onto (0,0,0), applied
# before the rotation. --tex shrinks every image to at most N px on its long side and drops
# everything but each material's base colour map (normal/ORM included) -- the engine reads one
# texture per material, see src/model.c.
import bpy, sys, os, mathutils

argv = sys.argv[sys.argv.index("--") + 1:]
src, dst = argv[0], argv[1]
opts = {"keep": [], "drop": [], "aim": None, "up": None, "origin": (0.0, 0.0, 0.0),
        "tex": 512, "decimate": 0, "scale": 1.0}
i = 2
while i < len(argv):
    k = argv[i].lstrip("-")
    if k in ("keep", "drop"):
        vals = []
        i += 1
        while i < len(argv) and not argv[i].startswith("--"):
            vals.append(argv[i]); i += 1
        opts[k] = vals
    elif k == "origin":
        opts[k] = (float(argv[i + 1]), float(argv[i + 2]), float(argv[i + 3])); i += 4
    elif k in ("aim", "up"):
        opts[k] = argv[i + 1]; i += 2
    elif k in ("tex", "decimate"):
        opts[k] = int(argv[i + 1]); i += 2
    elif k == "scale":
        opts[k] = float(argv[i + 1]); i += 2
    else:
        i += 1
assert opts["aim"] and opts["up"], "--aim and --up are required"

AXES = {"+X": mathutils.Vector((1, 0, 0)), "-X": mathutils.Vector((-1, 0, 0)),
        "+Y": mathutils.Vector((0, 1, 0)), "-Y": mathutils.Vector((0, -1, 0)),
        "+Z": mathutils.Vector((0, 0, 1)), "-Z": mathutils.Vector((0, 0, -1))}

# --- import, strip to mesh objects, keep/drop by name ---
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
for o in list(bpy.data.objects):
    if o.type != "MESH":
        bpy.data.objects.remove(o, do_unlink=True)

meshes = [o for o in bpy.data.objects if o.type == "MESH"]
if opts["keep"]:
    meshes = [o for o in meshes if any(s in o.name for s in opts["keep"])]
if opts["drop"]:
    meshes = [o for o in meshes if not any(s in o.name for s in opts["drop"])]
assert meshes, "no meshes left after --keep/--drop"
kept_names = [o.name for o in meshes]
for o in [o for o in bpy.data.objects if o.type == "MESH" and o not in meshes]:
    bpy.data.objects.remove(o, do_unlink=True)

# --- apply world transform, join into one object ---
bpy.ops.object.select_all(action="DESELECT")
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
if len(meshes) > 1:
    bpy.ops.object.join()
obj = bpy.context.view_layer.objects.active
mesh = obj.data

# --- geometry-level scale, origin translation (in the ORIGINAL frame), then re-orientation ---
if opts["scale"] != 1.0:
    mesh.transform(mathutils.Matrix.Scale(opts["scale"], 4))

ox, oy, oz = opts["origin"]
origin_pt = mathutils.Vector((ox, oy, oz)) * opts["scale"]
if origin_pt.length_squared > 0:
    mesh.transform(mathutils.Matrix.Translation(-origin_pt))

aim_n = AXES[opts["aim"]].normalized()
up_in = AXES[opts["up"]].normalized()
assert abs(aim_n.dot(up_in)) < 0.999, "--aim and --up must not be parallel"
right_n = aim_n.cross(up_in).normalized()
up_n = right_n.cross(aim_n).normalized()  # orthogonalize up against aim

target_aim = mathutils.Vector((0, -1, 0))   # Blender -Y -> glTF +Z
target_up = mathutils.Vector((0, 0, 1))     # Blender +Z -> glTF +Y
target_right = target_aim.cross(target_up).normalized()

src_basis = mathutils.Matrix((right_n, aim_n, up_n)).transposed()
dst_basis = mathutils.Matrix((target_right, target_aim, target_up)).transposed()
rot3 = dst_basis @ src_basis.transposed()  # src_basis is orthonormal, so inverse == transpose
mesh.transform(rot3.to_4x4())
mesh.update()

# --- optional decimate down to at most N triangles ---
mesh.calc_loop_triangles()
tri_count = len(mesh.loop_triangles)
if opts["decimate"] > 0 and tri_count > opts["decimate"]:
    ratio = opts["decimate"] / tri_count
    bpy.context.view_layer.objects.active = obj
    m = obj.modifiers.new("dec", "DECIMATE")
    m.ratio = ratio
    bpy.ops.object.modifier_apply(modifier="dec")
    mesh = obj.data
    mesh.calc_loop_triangles()
    tri_count = len(mesh.loop_triangles)

# --- textures: keep only each material's base colour image, capped at --tex px ---
base_images = set()
for mat in obj.data.materials:
    if not mat or not mat.use_nodes:
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

# --- export ---
bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB", export_yup=True,
                           export_animations=False, export_cameras=False, export_lights=False,
                           export_materials="EXPORT", export_image_format="AUTO", export_apply=True)

# --- report: final bounds/size in glTF axes (X, Y=up, Z=aim) ---
bl_min = mathutils.Vector((min(v.co[a] for v in mesh.vertices) for a in range(3)))
bl_max = mathutils.Vector((max(v.co[a] for v in mesh.vertices) for a in range(3)))
gl_min = mathutils.Vector((bl_min.x, bl_min.z, -bl_max.y))
gl_max = mathutils.Vector((bl_max.x, bl_max.z, -bl_min.y))
size = gl_max - gl_min
print("kept meshes:", kept_names)
print(f"triangles: {tri_count}")
print(f"BOUNDS_MIN {gl_min.x:.4f} {gl_min.y:.4f} {gl_min.z:.4f}")
print(f"BOUNDS_MAX {gl_max.x:.4f} {gl_max.y:.4f} {gl_max.z:.4f}")
print(f"SIZE {size.x:.4f} {size.y:.4f} {size.z:.4f}")
print(f"output size: {os.path.getsize(dst)} bytes -> {dst}")
