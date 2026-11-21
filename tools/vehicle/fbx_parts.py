"""Blender (headless): an FBX vehicle -> OBJ with one group per loose part, in world space, for obj_to_mdl.py.

    blender -b --python tools/vehicle/fbx_parts.py -- <in.fbx> <out.obj>

Every mesh object's transforms are applied (the FBX's unit scale and its Y-up axis included, as Blender's FBX
import sets them), then each object's connected pieces become groups part_000, part_001, ... so the MDL
builder can tell the turret, the gun and the hull apart. UVs go out as vt; texture paths are listed as
comments (# texture <path>) for the builder to find the skin.

Blender 5's bpy.ops.import_scene.fbx breaks when driven from Python (AttributeError on 'files'), so the
importer's own load() is called.
"""
import sys

import bmesh
import bpy


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    src, dst = argv[0], argv[1]
    bpy.ops.wm.read_factory_settings(use_empty=True)
    from io_scene_fbx import import_fbx

    class Op:   # what load() reads off its operator
        report = staticmethod(lambda *a, **k: None)
    import_fbx.load(Op(), bpy.context, filepath=src)
    lines, nv, nt = [], 0, 0
    textures = set()
    part = 0
    for ob in [o for o in bpy.context.scene.objects if o.type == "MESH"]:
        for slot in ob.material_slots:
            if slot.material and slot.material.use_nodes:
                for n in slot.material.node_tree.nodes:
                    if n.type == "TEX_IMAGE" and n.image:
                        textures.add(bpy.path.abspath(n.image.filepath))
        bm = bmesh.new()
        bm.from_mesh(ob.data)
        bm.transform(ob.matrix_world)
        bmesh.ops.triangulate(bm, faces=bm.faces[:])
        uv = bm.loops.layers.uv.active
        bm.faces.ensure_lookup_table()
        seen = set()
        for f0 in bm.faces:
            if f0.index in seen:
                continue
            # one connected piece (shared vertices)
            stack, island = [f0], []
            seen.add(f0.index)
            while stack:
                f = stack.pop()
                island.append(f)
                for v in f.verts:
                    for g in v.link_faces:
                        if g.index not in seen:
                            seen.add(g.index)
                            stack.append(g)
            lines.append(f"g part_{part:03d}")
            part += 1
            for f in island:
                idx = []
                for lp in f.loops:
                    co = lp.vert.co
                    u = lp[uv].uv if uv else (0.0, 0.0)
                    lines.append(f"v {co.x:.6f} {co.y:.6f} {co.z:.6f}")
                    lines.append(f"vt {u[0]:.6f} {u[1]:.6f}")
                    nv += 1
                    idx.append(nv)
                lines.append("f " + " ".join(f"{i}/{i}" for i in idx))
                nt += 1
        bm.free()
    with open(dst, "w") as fh:
        fh.write(f"# fbx_parts.py from {src}: {part} parts, {nt} triangles\n")
        for t in sorted(textures):
            fh.write(f"# texture {t}\n")
        fh.write("\n".join(lines) + "\n")
    print(f"fbx_parts: {part} parts, {nt} triangles, textures {sorted(textures)} -> {dst}")


main()
