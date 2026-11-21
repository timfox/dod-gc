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
from maya_safe import Progress, purge_unused
REF = os.path.expanduser("~/.cache/gsbridge/ref")
COLORS = {"info_player": 14, "dod_control_point": 13, "dod_capture_area": 13, "light": 17, "env_model": 6, "ambient_generic": 9, "info_doddetect": 16}

def _bake(mapname):
    out = os.path.join(REF, mapname + ".json")
    if not os.path.exists(out):   # Maya's python has no numpy/PIL: bake outside
        raise RuntimeError("bake first:  python3 %s %s" % (os.path.join(HERE, "bsp_bake.py"), mapname))
    return json.load(open(out))

def unload(mapname):
    for n in ("ref_" + mapname, "REF_" + mapname, "refsky_" + mapname, "refocean_" + mapname):
        if cmds.objExists(n): cmds.delete(n)
    purge_unused(("refm_", "refsky_", "gsf_"))      # shaders no mesh uses any more (they used to pile up: thousands after a few maps)

def _material(g):
    """Shared, cached material for a texture group: the same (texture, kind, amount) is created once across every loaded map."""
    import hashlib
    kind = g.get("kind", "opaque")
    key = hashlib.sha1(("%s|%s|%s|%s" % (g.get("png"), g.get("tex"), kind, g.get("amt"))).encode()).hexdigest()[:10]
    sh = "refm_" + key; sg = sh + "SG"
    if cmds.objExists(sg): return sg
    sh = cmds.shadingNode("lambert", asShader=True, name=sh)
    sg = cmds.sets(renderable=True, noSurfaceShader=True, empty=True, name=sh + "SG"); cmds.connectAttr(sh + ".outColor", sg + ".surfaceShader")
    kind = g.get("kind", "opaque")
    if kind == "water":      # engine water: the (animated in-game) texture, alpha-blended over what is behind it
        if g["png"] and os.path.exists(g["png"]):
            f = cmds.shadingNode("file", asTexture=True, isColorManaged=True)
            cmds.setAttr(f + ".fileTextureName", g["png"], type="string"); cmds.connectAttr(f + ".outColor", sh + ".color")
        else:
            r, gr, b = [c / 255.0 for c in (g.get("mean") or [40, 70, 90])]; cmds.setAttr(sh + ".color", r, gr, b, type="double3")
        cmds.setAttr(sh + ".transparency", 0.4, 0.38, 0.35, type="double3"); cmds.setAttr(sh + ".diffuse", 1.0)
    elif g["png"] and os.path.exists(g["png"]):
        f = cmds.shadingNode("file", asTexture=True, isColorManaged=True)
        cmds.setAttr(f + ".fileTextureName", g["png"], type="string"); cmds.connectAttr(f + ".outColor", sh + ".color")
        if kind == "alpha" or g["tex"].startswith("{"): cmds.connectAttr(f + ".outTransparency", sh + ".transparency")
        elif kind in ("trans", "add"):
            t = 1.0 - (g.get("amt", 255) / 255.0 if kind == "trans" else 0.6); cmds.setAttr(sh + ".transparency", t, t, t, type="double3")
    if not (kind == "water" or (g["png"] and os.path.exists(g["png"]))):   # texture the engine could not load either: the same pink/black checker
        import missing_tex
        f = cmds.shadingNode("file", asTexture=True, isColorManaged=True)
        cmds.setAttr(f + ".fileTextureName", missing_tex.path(), type="string"); cmds.setAttr(f + ".filterType", 0)
        p2 = cmds.shadingNode("place2dTexture", asUtility=True); cmds.connectAttr(p2 + ".outUV", f + ".uvCoord")
        cmds.setAttr(p2 + ".repeatU", 8); cmds.setAttr(p2 + ".repeatV", 8)
        cmds.connectAttr(f + ".outColor", sh + ".color")
    return sg

def load(mapname, at=None):
    data = _bake(mapname); unload(mapname)
    yup = cmds.upAxis(q=True, axis=True) == "y"
    lo, hi = data["bounds"]
    if at is None:   # park it beside gs_world (+X), centred on Y, same floor level
        gx = cmds.exactWorldBoundingBox("gs_world")[3] if cmds.objExists("gs_world") else 0
        at = (gx + 400 - lo[0], -(lo[1] + hi[1]) / 2, 0)
    root = cmds.group(empty=True, name="ref_" + mapname)
    def P(x, y, z): return om.MPoint(x + at[0], z + at[2], -(y + at[1])) if yup else om.MPoint(x + at[0], y + at[1], z + at[2])
    prog = Progress("Loading %s" % mapname, len(data["groups"]) + 1).__enter__()
    for gi, g in enumerate(data["groups"]):
        prog.step()
        if prog.cancelled:
            prog.__exit__(); unload(mapname); cmds.warning("load of %s cancelled" % mapname); return None
        pts, counts, conn, us, vs = [], [], [], [], []
        for poly in g["polys"]:
            counts.append(len(poly))
            for x, y, z, u, v in poly:
                conn.append(len(pts)); pts.append(P(x, y, z)); us.append(u); vs.append(v)
        fn = om.MFnMesh(); obj = fn.create(pts, counts, conn)
        fn.setUVs(us, vs); fn.assignUVs(counts, conn)
        name = cmds.rename(om.MFnDagNode(obj).fullPathName(), "refgeo_%s_%d" % (mapname, gi))
        sg = _material(g); kind = g.get("kind", "opaque")
        cmds.sets(name, e=True, forceElement=sg)
        cmds.addAttr(name, ln="ref_texture", dt="string"); cmds.setAttr(name + ".ref_texture", g["tex"] + ("  [" + kind + "]" if kind != "opaque" else ""), type="string")
        cmds.parent(name, root, relative=True)
        if kind == "hidden": cmds.setAttr(name + ".visibility", 0)   # not visible in the engine at spawn (renderamt 0); toggle to inspect
    prog.__exit__()
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


def skybox(mapname, half=30000.0):
    """Draw the map's skybox (gfx/env/<skyname>*.tga) as a big unlit inward cube centred on the map. Toggle with delete."""
    data = _bake(mapname); sky = data.get("sky", {}); faces = sky.get("faces", {})
    if not faces: cmds.warning("no skybox images for %s (skyname %r)" % (mapname, sky.get("name"))); return None
    lo, hi = data["bounds"]; S = half
    cx, cy = (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2
    yup = cmds.upAxis(q=True, axis=True) == "y"
    at = cmds.xform("ref_" + mapname, q=True, ws=True, t=True) if cmds.objExists("ref_" + mapname) else (0, 0, 0)
    Q = {"ft": [(S, S, -S), (S, -S, -S), (S, -S, S), (S, S, S)], "bk": [(-S, -S, -S), (-S, S, -S), (-S, S, S), (-S, -S, S)],
         "rt": [(S, -S, -S), (-S, -S, -S), (-S, -S, S), (S, -S, S)], "lf": [(-S, S, -S), (S, S, -S), (S, S, S), (-S, S, S)],
         "up": [(-S, -S, S), (-S, S, S), (S, S, S), (S, -S, S)], "dn": [(-S, S, -S), (-S, -S, -S), (S, -S, -S), (S, S, -S)]}
    n = "refsky_" + mapname
    if cmds.objExists(n): cmds.delete(n)
    root = cmds.group(empty=True, name=n)
    for k, quad in Q.items():
        if k not in faces: continue
        pts = [om.MPoint(cx + x + at[0], z, -(cy + y + at[1])) if yup else om.MPoint(cx + x + at[0], cy + y + at[1], z) for x, y, z in quad]
        fn = om.MFnMesh(); obj = fn.create(pts, [4], [0, 1, 2, 3]); fn.setUVs([0, 1, 1, 0], [0, 0, 1, 1]); fn.assignUVs([4], [0, 1, 2, 3])
        nm = cmds.rename(om.MFnDagNode(obj).fullPathName(), "%s_%s" % (n, k))
        sh = cmds.shadingNode("surfaceShader", asShader=True, name="%s_%s_m" % (n, k))      # unlit
        sg = cmds.sets(renderable=True, noSurfaceShader=True, empty=True, name=sh + "SG"); cmds.connectAttr(sh + ".outColor", sg + ".surfaceShader")
        f = cmds.shadingNode("file", asTexture=True, isColorManaged=True); cmds.setAttr(f + ".fileTextureName", faces[k], type="string")
        cmds.connectAttr(f + ".outColor", sh + ".outColor", force=True) if False else cmds.connectAttr(f + ".outColor", sh + ".outColor")
        cmds.sets(nm, e=True, forceElement=sg); cmds.parent(nm, root, relative=True)
    lay = "REF_" + mapname
    if cmds.objExists(lay): cmds.editDisplayLayerMembers(lay, root)
    if not cmds.objExists(lay):
        cmds.setAttr(root + ".overrideEnabled", 1); cmds.setAttr(root + ".overrideDisplayType", 2)
    return root


def ocean(mapname, size=30000.0):
    """The sea beyond the water brush: one big plane at the map's water level using the same translucent water material,
    so the engine's horizon-to-horizon ocean is there to measure against. Delete refocean_<map> to remove."""
    data = _bake(mapname); w = [g for g in data["groups"] if g.get("kind") == "water"]
    if not w: cmds.warning("%s has no water faces" % mapname); return None
    z = max(p[2] for pl in w[0]["polys"] for p in pl)
    meshes = [m for m in cmds.ls("refgeo_%s_*" % mapname, type="transform") if "[water]" in (cmds.getAttr(m + ".ref_texture") or "") or "water" in (cmds.getAttr(m + ".ref_texture") or "").lower()]
    if not meshes: cmds.warning("load(%r) first" % mapname); return None
    bb = cmds.exactWorldBoundingBox(meshes[0]); yup = cmds.upAxis(q=True, axis=True) == "y"
    cx, cy = (bb[0] + bb[3]) / 2, ((bb[2] + bb[5]) / 2 if yup else (bb[1] + bb[4]) / 2); lvl = bb[1] if yup else bb[2]
    n = "refocean_" + mapname
    if cmds.objExists(n): cmds.delete(n)
    S = size
    pts = [om.MPoint(cx - S, lvl - 1, cy - S), om.MPoint(cx + S, lvl - 1, cy - S), om.MPoint(cx + S, lvl - 1, cy + S), om.MPoint(cx - S, lvl - 1, cy + S)] if yup else \
          [om.MPoint(cx - S, cy - S, lvl - 1), om.MPoint(cx + S, cy - S, lvl - 1), om.MPoint(cx + S, cy + S, lvl - 1), om.MPoint(cx - S, cy + S, lvl - 1)]
    fn = om.MFnMesh(); obj = fn.create(pts, [4], [0, 1, 2, 3]); fn.setUVs([0, 40, 40, 0], [0, 0, 40, 40]); fn.assignUVs([4], [0, 1, 2, 3])
    nm = cmds.rename(om.MFnDagNode(obj).fullPathName(), n)
    sg = cmds.listConnections(cmds.listRelatives(meshes[0], shapes=True)[0], type="shadingEngine")[0]
    cmds.sets(nm, e=True, forceElement=sg)
    lay = "REF_" + mapname
    if cmds.objExists(lay): cmds.editDisplayLayerMembers(lay, nm)
    return nm


def sky_preview(skyname, size=None):
    """Skybox of the scene's map (gs_settings.gs_skyname) around gs_world, unlit, in the locked reference layer REF_sky.
    PNGs come from bake_skies.py (run once; Maya has no PIL)."""
    tex = os.path.expanduser("~/.cache/gsbridge/tex")
    faces = {k: os.path.join(tex, "sky_%s%s.png" % (skyname, k)) for k in ("ft", "bk", "lf", "rt", "up", "dn")}
    if not all(os.path.exists(p) for p in faces.values()):
        cmds.warning("sky %r not converted: run python3 %s" % (skyname, os.path.join(HERE, "bake_skies.py"))); return None
    bb = cmds.exactWorldBoundingBox("gs_world") if cmds.objExists("gs_world") else [-512, -512, 0, 512, 512, 256]
    half = max(bb[3] - bb[0], bb[4] - bb[1], bb[5] - bb[2]) * 3 + 20000
    data = {"bounds": [[bb[0], bb[1], bb[2]], [bb[3], bb[4], bb[5]]], "sky": {"name": skyname, "faces": faces}}
    import tempfile, json
    old = _bake
    try:
        globals()["_bake"] = lambda m: data
        n = skybox("scene_sky", half)
    finally: globals()["_bake"] = old
    if n and not cmds.objExists("REF_sky"):
        lay = cmds.createDisplayLayer(name="REF_sky", empty=True); cmds.editDisplayLayerMembers(lay, n); cmds.setAttr(lay + ".displayType", 2)
    return n
