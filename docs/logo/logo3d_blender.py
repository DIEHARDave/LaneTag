"""Build the coloured LaneTag logo plaque in Blender and export OBJ+MTL for OrcaSlicer.

Usage:
  blender --background --python logo3d_blender.py -- PARTS.json OUT.blend OUT.obj
PARTS.json comes from logo3d_flatten.py. One Blender unit = 1 mm (the scene is
set to millimetres), and the OBJ is written Z-up in mm, which is what
OrcaSlicer expects. Each colour is its own object and material; the MTL
carries the colours (Kd), which OrcaSlicer reads on import.
"""
import json, sys
import bpy


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def hex_rgb(h):
    h = h.lstrip("#")
    return [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]


def material(hexcol):
    name = "LT_" + hexcol.lstrip("#").upper()
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    lin = [srgb_to_linear(c) for c in hex_rgb(hexcol)] + [1.0]
    bsdf.inputs["Base Color"].default_value = lin
    bsdf.inputs["Roughness"].default_value = 0.6
    mat.diffuse_color = lin
    return mat


def build(parts_json, blend_path, obj_path):
    with open(parts_json) as f:
        data = json.load(f)

    # fresh scene
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 0.001
    scene.unit_settings.length_unit = "MILLIMETERS"

    coll = bpy.data.collections.new("LaneTag_Logo")
    scene.collection.children.link(coll)
    for p in data["parts"]:
        me = bpy.data.meshes.new(p["name"])
        me.from_pydata([tuple(v) for v in p["verts"]], [], [tuple(f) for f in p["faces"]])
        me.validate()
        me.materials.append(material(p["color"]))
        ob = bpy.data.objects.new(p["name"], me)
        coll.objects.link(ob)

    # give the viewport a sensible look/clip range for a 100 mm part
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == "VIEW_3D":
                for sp in area.spaces:
                    if sp.type == "VIEW_3D":
                        sp.clip_end = 10000
                        sp.shading.color_type = "MATERIAL"

    bpy.ops.wm.save_as_mainfile(filepath=blend_path)

    props = bpy.ops.wm.obj_export.get_rna_type().properties
    kw = dict(filepath=obj_path, export_materials=True, export_selected_objects=False,
              apply_modifiers=True, export_normals=True, export_uv=False,
              global_scale=1.0)
    ups = [i.identifier for i in props["up_axis"].enum_items]
    fwds = [i.identifier for i in props["forward_axis"].enum_items]
    kw["up_axis"] = "Z" if "Z" in ups else ups[0]
    kw["forward_axis"] = "Y" if "Y" in fwds else fwds[0]
    if "path_mode" in props:
        kw["path_mode"] = "STRIP"
    bpy.ops.wm.obj_export(**kw)

    # Blender writes Kd in linear space; slicers read it as plain sRGB, so put
    # the exact logo colours back (material names carry the hex code).
    mtl_path = obj_path[:-4] + ".mtl"
    out, cur = [], None
    with open(mtl_path) as f:
        for line in f:
            if line.startswith("newmtl "):
                cur = line.split()[1]
            elif line.startswith("Kd ") and cur and cur.startswith("LT_"):
                line = "Kd %.6f %.6f %.6f\n" % tuple(hex_rgb(cur[3:]))
            elif line.startswith("Ka "):
                line = "Ka 0.000000 0.000000 0.000000\n"
            out.append(line)
    with open(mtl_path, "w") as f:
        f.writelines(out)
    print("saved", blend_path, "and", obj_path)


if __name__ == "__main__" and "--" in sys.argv:
    build(*sys.argv[sys.argv.index("--") + 1:][:3])
