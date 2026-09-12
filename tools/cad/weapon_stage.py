"""The Blender half of the weapon build: STL in, GLB out. Runs inside headless Blender.

    blender -b --python tools/cad/weapon_stage.py -- OUT.glb TRIS mat:hexcolour:file.stl [...]

Modelled on tools/cad/obj_stage.py, which does the same job for the architecture kit -- weld,
smooth, budget -- but that stage writes an OBJ+MTL for a piece that hangs in a .part alongside
a dozen others. A weapon is carried on its own, so it ships as one self-contained GLB instead:
one flat Base Color material per colour, no UVs (there is no texture to sample), no vertex
colours, no skin.

CadQuery tessellates each material's solids to its own STL in the "gunsmith" frame
build_weapons.py builds in: +Z up, +X the width, +Y forward -- towards the muzzle. That frame is
convenient to model in (it is the sense tools/cad/kitlib.py calls "outward" on a wall piece) but
backwards for the game, whose viewmodel and hand-attachment code expects the muzzle at -Y with
the grip still at the origin. So after welding and shading, this stage turns the whole mesh 180
degrees about Z -- the one rotation the file gets -- and bakes it into the vertex data before
export, rather than leaving it as a node transform a loader could drop.

That flip is also why an ejection port cut into the gunsmith frame's +X face (the shooter's
right when you are behind the gun looking down the barrel, facing +Y) ends up on -X in the game
frame: with the muzzle now at -Y and up still at +Z, facing the direction the gun fires puts your
right hand at -X, and that is exactly where the flip put the cut. A single global rotation is
what keeps that consistent for every gun built this way, so a second weapon function in
build_weapons.py needs no per-part sign-flipping to get its handedness right.
"""

import math
import sys

import bpy

SMOOTH_ANGLE = math.radians(34.0)   # above this an edge stays hard: boxes crisp, barrels round
WELD = 1e-5                          # metres; CadQuery's STL vertices are exactly coincident


def clear():
    bpy.ops.wm.read_factory_settings(use_empty=True)


# No colour conversion anywhere, same rule tools/cad/obj_stage.py states for the OBJ path: an
# untextured glTF material in src/model.c is read straight off base_color_factor and multiplied
# by 255 with no gamma step of its own (there is no equivalent of the OBJ path's "raise Kd to
# 2.2"), and nothing downstream treats the resulting 1x1 texture as sRGB either. So the number
# that goes in here has to already be the sRGB number the brief states -- writing a linearised
# version would wash the gun out exactly the way writing linear Kd would.
def import_stl(path, name, rgb):
    before = set(bpy.data.objects)
    bpy.ops.wm.stl_import(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs["Base Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
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
    stage(argv[0], int(argv[1]), argv[2:])


def stage(out_glb, budget, specs):
    """One GLB out of one or more `material:hexcolour:file.stl` specs. Returns (raw, final)."""
    clear()
    objs = []
    for spec in specs:
        name, rgb_hex, path = spec.split(":", 2)
        rgb = tuple(int(rgb_hex[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
        objs += import_stl(path, name, rgb)
    if not objs:
        raise SystemExit("weapon_stage: nothing imported")

    # One object, so the whole gun is one mesh and one draw call.
    for o in bpy.data.objects:
        o.select_set(o in objs)
    bpy.context.view_layer.objects.active = objs[0]
    if len(objs) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = out_glb.rsplit("/", 1)[-1].rsplit(".", 1)[0]

    # Weld: STL ships every triangle standalone.
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.remove_doubles(threshold=WELD)
    bpy.ops.object.mode_set(mode="OBJECT")

    raw = tri_count(ob)

    # Budget. Collapse keeps the silhouette better than un-subdivide on these shapes -- see
    # tools/cad/obj_stage.py, which makes the same trade for the same reason.
    if raw > budget:
        m = ob.modifiers.new("cut", "DECIMATE")
        m.decimate_type = "COLLAPSE"
        m.ratio = budget / float(raw)
        bpy.ops.object.modifier_apply(modifier=m.name)

    # Crisp where it is flat (the receiver, the guard), round where it is turned (the barrel,
    # the tube) -- shade-auto-smooth by angle, same threshold the kit uses.
    bpy.ops.object.shade_smooth()
    ob.data.polygons.foreach_set("use_smooth", [True] * len(ob.data.polygons))
    try:
        bpy.ops.object.shade_smooth_by_angle(angle=SMOOTH_ANGLE)
    except Exception:
        for e in ob.data.edges:
            e.use_edge_sharp = False

    # The canonical rotation, once: gunsmith frame (+Y towards the muzzle) -> game frame
    # (-Y towards the muzzle). Baked into the mesh so the glTF's own vertex data is already in
    # the frame every viewmodel and hand-attachment expects, not just the node transform.
    ob.rotation_euler = (0, 0, math.pi)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)

    final = tri_count(ob)

    # No UVs, no vertex colours, no skin: a weapon built here carries exactly one thing per
    # material, its Base Color, which is all model.c's glTF path reads off a textureless material.
    bpy.ops.export_scene.gltf(
        filepath=out_glb, export_format="GLB", use_selection=True,
        export_apply=True, export_normals=True, export_texcoords=False,
        export_materials="EXPORT", export_image_format="NONE",
        export_yup=True, export_animations=False, export_skins=False,
        export_cameras=False, export_lights=False)

    lo = [min(v.co[i] for v in ob.data.vertices) for i in range(3)]
    hi = [max(v.co[i] for v in ob.data.vertices) for i in range(3)]
    # Blender (x, y, z) -> glTF (x, z, -y): the exporter's own +Y-up convention, applied here
    # only so the printed bounds are in the axes the GLB actually ships, for build_weapons.py
    # to echo back as a self-check.
    gmin = (lo[0], lo[2], -hi[1])
    gmax = (hi[0], hi[2], -lo[1])
    print("WEAPONSTAGE %s raw=%d final=%d" % (out_glb, raw, final))
    print("BOUNDS_MIN %.4f %.4f %.4f" % gmin)
    print("BOUNDS_MAX %.4f %.4f %.4f" % gmax)
    return raw, final


if __name__ == "__main__":
    main()
