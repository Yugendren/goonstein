"""The game-mesh half of the kit: STL in, OBJ+MTL out. Runs inside headless Blender.

    blender -b --python tools/cad/obj_stage.py -- OUT.obj TRIS mat:file.stl [mat:file.stl ...]

CadQuery tessellates each material's solids to its own STL. Here they are welded (an STL has
every triangle split from its neighbours, which would triple the vertex count and kill every
smooth shade), turned from CAD axes to game axes, smoothed by angle so a dome is round and a
cornice is crisp, decimated if they overshoot the piece's budget, and written as one OBJ with
one material per colour -- which src/model.c reads as a single mesh with per-vertex colour,
so a whole window (frame, sill, shutters, dark pane) costs exactly one draw call.

No UVs are written. An OBJ prop is loaded with a white texture and `tex NAME` projects in world
space, so the mesh's own UVs can never be sampled. See tools/cad/README.md.
"""

import math
import sys

import bpy

SMOOTH_ANGLE = math.radians(34.0)   # above this an edge stays hard: boxes crisp, domes round
WELD = 1e-5                          # metres; CadQuery's STL vertices are exactly coincident


def clear():
    bpy.ops.wm.read_factory_settings(use_empty=True)


# No colour conversion anywhere: src/model.c reads Kd and raises it to 2.2 itself, so the MTL
# must carry the sRGB number. Writing linear here would wash every piece out by a stop.
def import_stl(path, name, rgb):
    before = set(bpy.data.objects)
    bpy.ops.wm.stl_import(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (rgb[0], rgb[1], rgb[2], 1.0)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs["Base Color"].default_value = mat.diffuse_color
        if "Roughness" in bsdf.inputs:
            bsdf.inputs["Roughness"].default_value = 0.8
    for o in new:
        o.data.materials.clear()
        o.data.materials.append(mat)
    return new


def tri_count(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    out_obj, budget, specs = argv[0], int(argv[1]), argv[2:]

    clear()
    objs = []
    for spec in specs:
        name, rgb_hex, path = spec.split(":", 2)
        rgb = tuple(int(rgb_hex[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
        objs += import_stl(path, name, rgb)
    if not objs:
        raise SystemExit("obj_stage: nothing imported")

    # One object, so the whole piece is one mesh and one draw call.
    for o in bpy.data.objects:
        o.select_set(o in objs)
    bpy.context.view_layer.objects.active = objs[0]
    if len(objs) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = out_obj.rsplit("/", 1)[-1][:-4]

    # Weld: STL ships every triangle standalone.
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.remove_doubles(threshold=WELD)
    bpy.ops.mesh.tris_convert_to_quads(face_threshold=0.01, shape_threshold=0.01)
    bpy.ops.object.mode_set(mode="OBJECT")

    raw = tri_count(ob)

    # CAD (Z up, +Y outward) -> game (Y up, piece protrudes toward -Z) is done by the exporter's
    # forward/up axes at the bottom, which is the same -90 about X, applied once.

    # Budget. Collapse keeps the silhouette better than un-subdivide on these shapes.
    if raw > budget:
        m = ob.modifiers.new("cut", "DECIMATE")
        m.decimate_type = "COLLAPSE"
        m.ratio = budget / float(raw)
        bpy.ops.object.modifier_apply(modifier=m.name)

    # Crisp where it is flat, round where it is turned.
    bpy.ops.object.shade_smooth()
    ob.data.polygons.foreach_set("use_smooth", [True] * len(ob.data.polygons))
    try:
        bpy.ops.object.shade_smooth_by_angle(angle=SMOOTH_ANGLE)
    except Exception:
        for e in ob.data.edges:
            e.use_edge_sharp = False

    bpy.ops.wm.obj_export(
        filepath=out_obj,
        export_selected_objects=False,
        export_materials=True,
        export_uv=False,            # never sampled: OBJ props load with a white texture
        export_normals=True,
        export_colors=False,
        export_triangulated_mesh=True,
        apply_modifiers=True,
        forward_axis="NEGATIVE_Z",
        up_axis="Y",
        global_scale=1.0,
        path_mode="AUTO",
    )
    print("KITSTAGE %s raw=%d final=%d" % (out_obj, raw, tri_count(ob)))


main()
