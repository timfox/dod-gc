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
    idx = _index()[name]; sg = "gs_" + safe(name) + "_SG"
    if cmds.objExists(sg): return sg
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
