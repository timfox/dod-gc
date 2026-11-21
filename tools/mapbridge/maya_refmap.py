"""Open a stock DoD map alongside your own scene as locked reference geometry.

    import maya_refmap
    maya_refmap.load("dod_charlie")                    # offset next to gs_world
    maya_refmap.load("dod_caen", at=(3000, 0, 0))
    maya_refmap.unload("dod_charlie")

Geometry: real BSP faces, real UVs and WAD textures (baked by bsp_bake.py in system python).
Entities: locators (spawns, control points, lights, props...) with attrs ref_classname/ref_targetname.
Everything sits under group ref_<map> in display layer REF_<map> in reference mode (unselectable) and
has no gs_classname / is not under gs_world, so maya_export never writes it to the .map.
"""
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import maya.cmds as cmds
import maya.api.OpenMaya as om
REF = os.path.expanduser("~/.cache/gsbridge/ref")
COLORS = {"info_player": 14, "dod_control_point": 13, "dod_capture_area": 13, "light": 17, "env_model": 6, "ambient_generic": 9, "info_doddetect": 16}

def _bake(mapname):
    out = os.path.join(REF, mapname + ".json")
    if not os.path.exists(out):   # Maya's python has no numpy/PIL: bake outside
        raise RuntimeError("bake first:  python3 %s %s" % (os.path.join(HERE, "bsp_bake.py"), mapname))
    return json.load(open(out))

def unload(mapname):
    for n in ("ref_" + mapname, "REF_" + mapname):
        if cmds.objExists(n): cmds.delete(n)

def load(mapname, at=None):
    data = _bake(mapname); unload(mapname)
    yup = cmds.upAxis(q=True, axis=True) == "y"
    lo, hi = data["bounds"]
    if at is None:   # park it beside gs_world (+X), centred on Y, same floor level
        gx = cmds.exactWorldBoundingBox("gs_world")[3] if cmds.objExists("gs_world") else 0
        at = (gx + 400 - lo[0], -(lo[1] + hi[1]) / 2, 0)
    root = cmds.group(empty=True, name="ref_" + mapname)
    def P(x, y, z): return om.MPoint(x + at[0], z + at[2], -(y + at[1])) if yup else om.MPoint(x + at[0], y + at[1], z + at[2])
    for gi, g in enumerate(data["groups"]):
        pts, counts, conn, us, vs = [], [], [], [], []
        for poly in g["polys"]:
            counts.append(len(poly))
            for x, y, z, u, v in poly:
                conn.append(len(pts)); pts.append(P(x, y, z)); us.append(u); vs.append(v)
        fn = om.MFnMesh(); obj = fn.create(pts, counts, conn)
        fn.setUVs(us, vs); fn.assignUVs(counts, conn)
        name = cmds.rename(om.MFnDagNode(obj).fullPathName(), "refgeo_%s_%d" % (mapname, gi))
        sh = cmds.shadingNode("lambert", asShader=True, name="refm_%s_%d" % (mapname, gi))
        sg = cmds.sets(renderable=True, noSurfaceShader=True, empty=True, name=sh + "SG"); cmds.connectAttr(sh + ".outColor", sg + ".surfaceShader")
        if g["png"] and os.path.exists(g["png"]):
            f = cmds.shadingNode("file", asTexture=True, isColorManaged=True)
            cmds.setAttr(f + ".fileTextureName", g["png"], type="string"); cmds.connectAttr(f + ".outColor", sh + ".color")
            if g["tex"].startswith("{"): cmds.connectAttr(f + ".outTransparency", sh + ".transparency")
        else:   # texture not in any WAD we have (retail HL wads not shipped here): flat neutral, named colours honoured
            named = {"yellow": (0.9, 0.8, 0.1), "black": (0.04, 0.04, 0.04), "red": (0.7, 0.1, 0.1), "white": (0.9, 0.9, 0.9)}
            cmds.setAttr(sh + ".color", *named.get(g["tex"].lower(), (0.55, 0.55, 0.55)), type="double3")
        cmds.sets(name, e=True, forceElement=sg)
        cmds.addAttr(name, ln="ref_texture", dt="string"); cmds.setAttr(name + ".ref_texture", g["tex"], type="string")
        cmds.parent(name, root, relative=True)
    eg = cmds.group(empty=True, name="ref_%s_entities" % mapname); cmds.parent(eg, root, relative=True)
    n = 0
    for e in data["ents"]:
        if not e["origin"] or e["model"].startswith("*") or e["class"] == "worldspawn": continue
        try: x, y, z = [float(v) for v in e["origin"].split()]
        except ValueError: continue
        l = cmds.spaceLocator(name="ref_%s_#" % e["class"])[0]
        p = P(x, y, z); cmds.move(p.x, p.y, p.z, l, absolute=True)
        for k in ("localScaleX", "localScaleY", "localScaleZ"): cmds.setAttr(l + "." + k, 12)
        for k, v in (("ref_classname", e["class"]), ("ref_targetname", e["target"]), ("ref_model", e["model"]), ("ref_angles", e["angles"])):
            cmds.addAttr(l, ln=k, dt="string"); cmds.setAttr(l + "." + k, v, type="string")
        col = next((c for pre, c in COLORS.items() if e["class"].startswith(pre)), None)
        if col: cmds.setAttr(l + ".overrideEnabled", 1); cmds.setAttr(l + ".overrideColor", col)
        cmds.parent(l, eg, relative=True); n += 1
    lay = cmds.createDisplayLayer(name="REF_" + mapname, empty=True); cmds.editDisplayLayerMembers(lay, root)
    cmds.setAttr(lay + ".displayType", 2)          # reference: visible, not selectable, not editable
    cmds.select(clear=True)
    print("[refmap] %s: %d meshes, %d locators, offset %s" % (mapname, len(data["groups"]), n, [round(a) for a in at]))
    return root
