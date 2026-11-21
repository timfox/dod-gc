"""Maya side of texture sync. In Maya:
    import maya_materials as mm
    mm.load_wad("dod_caen.wad")          # extracts PNGs (wad_textures.py) + makes one lambert per texture
    mm.assign("floor", "m_floor1")       # material by WAD name
    mm.fit_uvs()                         # box-project UVs of every gs_world mesh exactly like the .map
Shader nodes are named after the texture (Maya-safe form); the true WAD name is stored in
attr gs_texture and is what maya_export writes to the .map, so names round-trip.
UV rule = the one gsmap.py writes: 1 world unit = 1 texel (u = dot(p,uaxis)/w, v = -dot(p,vaxis)/h),
dominant-axis planar, so Maya's view matches the engine's tiling and scale.
"""
import json, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import maya.cmds as cmds
import gsmap
TEX = os.path.expanduser(os.environ.get("GSBRIDGE_TEX", "~/.cache/gsbridge/tex"))

def _index():
    return json.load(open(os.path.join(TEX, "index.json")))

def safe(name):
    return re.sub(r"[^A-Za-z0-9_]", "_", name)

def material(name):
    """Create (or return) the lambert+file texture for a WAD texture name."""
    sg = "gs_" + safe(name) + "_SG"
    if cmds.objExists(sg): return sg
    idx = _index().get(name)
    if idx is None:      # not in any WAD we extracted: show it exactly like the engine does, the pink/black checker (name kept for the .map)
        import missing_tex
        sh = cmds.shadingNode("lambert", asShader=True, name="gs_" + safe(name))
        cmds.addAttr(sh, ln="gs_texture", dt="string"); cmds.setAttr(sh + ".gs_texture", name, type="string")
        f = cmds.shadingNode("file", asTexture=True, isColorManaged=True); cmds.setAttr(f + ".fileTextureName", missing_tex.path(), type="string")
        cmds.setAttr(f + ".filterType", 0); p2 = cmds.shadingNode("place2dTexture", asUtility=True); cmds.connectAttr(p2 + ".outUV", f + ".uvCoord")
        cmds.setAttr(p2 + ".repeatU", 8); cmds.setAttr(p2 + ".repeatV", 8); cmds.connectAttr(f + ".outColor", sh + ".color")
        sgn = cmds.sets(renderable=True, noSurfaceShader=True, empty=True, name=sg); cmds.connectAttr(sh + ".outColor", sgn + ".surfaceShader")
        return sgn
    sh = cmds.shadingNode("lambert", asShader=True, name="gs_" + safe(name))
    cmds.addAttr(sh, ln="gs_texture", dt="string"); cmds.setAttr(sh + ".gs_texture", name, type="string")
    f = cmds.shadingNode("file", asTexture=True, isColorManaged=True, name="gsf_" + safe(name))
    cmds.setAttr(f + ".fileTextureName", os.path.join(TEX, idx["png"]), type="string")
    cmds.setAttr(f + ".colorSpace", "sRGB", type="string")
    cmds.connectAttr(f + ".outColor", sh + ".color")
    if name.startswith("{"): cmds.connectAttr(f + ".outTransparency", sh + ".transparency")
    sgn = cmds.sets(renderable=True, noSurfaceShader=True, empty=True, name=sg)
    cmds.connectAttr(sh + ".outColor", sgn + ".surfaceShader")
    return sgn

def load_wad(wad, names=None):
    import wad_textures; wad_textures.main([wad])
    for n in (names or _index()):
        if _index()[n]["wad"] == os.path.basename(wad): material(n)
    # viewport: textured shading so the stock look is visible
    for p in cmds.getPanel(type="modelPanel") or []:
        cmds.modelEditor(p, e=True, displayTextures=True, displayAppearance="smoothShaded")

def assign(obj, name):
    cmds.sets(obj, e=True, forceElement=material(name))

def fit_uvs(root="gs_world"):
    idx = _index()
    for m in cmds.listRelatives(root, allDescendents=True, type="mesh", fullPath=True) or []:
        sgs = cmds.listConnections(m, type="shadingEngine") or []
        tex = None
        for sg in sgs:
            sh = cmds.listConnections(sg + ".surfaceShader") or []
            if sh and cmds.attributeQuery("gs_texture", node=sh[0], exists=True): tex = cmds.getAttr(sh[0] + ".gs_texture")
        w, h = (idx[tex]["w"], idx[tex]["h"]) if tex in idx else (128, 128)
        t = cmds.listRelatives(m, parent=True, fullPath=True)[0]
        yup = cmds.upAxis(q=True, axis=True) == "y"
        cmds.polyMapCut(t + ".e[*]")   # one UV island per face so each face can take its own projection
        for i in range(cmds.polyEvaluate(t, face=True)):
            vf = cmds.polyListComponentConversion("%s.f[%d]" % (t, i), toVertexFace=True)
            for c in cmds.ls(vf, flatten=True):
                vtx = cmds.polyListComponentConversion(c, toVertex=True)
                p = cmds.pointPosition(vtx[0], world=True)
                x, y, z = (p[0], -p[2], p[1]) if yup else p
                nrm = cmds.polyInfo("%s.f[%d]" % (t, i), faceNormals=True)[0].split()[2:5]
                nx, ny, nz = [float(v) for v in nrm]
                if yup: nx, ny, nz = nx, -nz, ny
                u_ax, v_ax = gsmap._axes((nx, ny, nz))
                u = (x*u_ax[0] + y*u_ax[1] + z*u_ax[2]) / w
                v = -(x*v_ax[0] + y*v_ax[1] + z*v_ax[2]) / h
                cmds.polyEditUV(cmds.polyListComponentConversion(c, toUV=True), u=u, v=v, relative=False)


def preview_render_modes():
    """Viewport preview of brush-entity render modes (never affects the export): each brush entity with
    gs_key_rendermode gets its OWN copy of the shader so shared textures stay opaque on world brushes.
    2/1 translucent (renderamt), 5/3 additive (approximated as 40% transparent), func_water translucent.
    renderamt 0 with mode 1/2/3/5 = invisible in the engine -> shown very faint."""
    n = 0
    for t in cmds.ls("*", transforms=True):
        if not cmds.attributeQuery("gs_classname", node=t, exists=True): continue
        shapes = cmds.listRelatives(t, shapes=True, type="mesh") or []
        if not shapes: continue
        def key(k, d=""): return cmds.getAttr("%s.gs_key_%s" % (t, k)) if cmds.attributeQuery("gs_key_" + k, node=t, exists=True) else d
        mode = int(float(key("rendermode", 0) or 0)); amt = int(float(key("renderamt", 0) or 0)); cls = cmds.getAttr(t + ".gs_classname")
        sgs = cmds.listConnections(shapes[0], type="shadingEngine") or []
        if not sgs: continue
        old = (cmds.listConnections(sgs[0] + ".surfaceShader") or [None])[0]
        if old is None: continue
        if cls == "func_water": tr = 0.5
        elif mode in (1, 2): tr = 1.0 - (amt / 255.0 if amt > 2 else 0.04)
        elif mode in (3, 5): tr = 0.4 if amt > 2 else 0.96
        else: continue
        new = t + "_rm_shader"
        if not cmds.objExists(new):
            dup = cmds.duplicate(old, name=new, upstreamNodes=True)[0]
            sg = cmds.sets(renderable=True, noSurfaceShader=True, empty=True, name=new + "SG"); cmds.connectAttr(dup + ".outColor", sg + ".surfaceShader")
        cmds.sets(t, e=True, forceElement=new + "SG"); cmds.setAttr(new + ".transparency", tr, tr, tr, type="double3"); n += 1
    return n


def style_entities():
    """Viewport colours that mirror what the entity does in game: lights take their _light colour, spawns their team colour,
    control points red.  Pure display (override colours); never exported."""
    n = 0
    for t in cmds.ls("*", transforms=True):
        if not cmds.attributeQuery("gs_classname", node=t, exists=True): continue
        cls = cmds.getAttr(t + ".gs_classname"); rgb = None
        if cls.startswith("light") and cmds.attributeQuery("gs_key__light", node=t, exists=True):
            try:
                v = [float(x) for x in cmds.getAttr(t + ".gs_key__light").split()[:3]]
                rgb = tuple(max(0.05, min(1.0, c / 255.0)) for c in v)
            except ValueError: pass
        elif cls == "info_player_allies" or cls == "info_initial_player_allies": rgb = (0.35, 0.6, 1.0)
        elif cls == "info_player_axis" or cls == "info_initial_player_axis": rgb = (0.85, 0.85, 0.85)
        elif cls.startswith("dod_control_point") or cls == "dod_capture_area": rgb = (1.0, 0.25, 0.2)
        elif cls == "env_model": rgb = (0.4, 0.8, 0.4)
        if rgb is None: continue
        shapes = cmds.listRelatives(t, shapes=True) or [t]
        try:
            cmds.setAttr(shapes[0] + ".overrideEnabled", 1); cmds.setAttr(shapes[0] + ".overrideRGBColors", 1)
            cmds.setAttr(shapes[0] + ".overrideColorRGB", *rgb); n += 1
        except Exception: pass
    return n
