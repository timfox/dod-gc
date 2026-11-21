"""Viewport-only preview of a stock .mdl at an entity locator (env_model etc.).
The mesh is a child of the locator in reference display mode; it is NOT under gs_world and has no
gs_classname, so maya_export never writes it (the engine instances the prop at runtime).
    build(locator)   # reads gs_key_model / gs_key_skin; rebuilt automatically when they change
"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import maya.cmds as cmds
import maya.api.OpenMaya as om
import json, re, subprocess
TEXDIR = os.path.expanduser("~/.cache/gsbridge/mdltex")

def _load(rel, skin):
    """Bake with system python (Maya has no PIL), then read plain JSON."""
    out = os.path.join(TEXDIR, "%s_s%d.json" % (re.sub(r"[^A-Za-z0-9]", "_", os.path.splitext(rel)[0]), skin))
    if not os.path.exists(out):   # Maya runs in a sandbox whose python has no numpy/PIL: bake outside
        raise RuntimeError("model not baked yet; run:  python3 %s %s %d   (all stock models are pre-baked by mdl_mesh.bake_all)" % (os.path.join(HERE, "mdl_mesh.py"), rel, skin))
    d = json.load(open(out))
    for g in d["groups"]:
        g["verts"] = g["verts"]; g["tris"] = g["tris"]
    return d["groups"], d["nskins"]

def _attr(loc, name, default=""):
    return cmds.getAttr(loc + "." + name) if cmds.attributeQuery(name, node=loc, exists=True) else default

_jobs = {}
def clear(loc):
    g = loc + "_mdlpreview"
    if cmds.objExists(g): cmds.delete(g)
    try:
        from maya_safe import purge_unused; purge_unused(("mdlp_",))
    except Exception: pass

def build(loc):
    model = _attr(loc, "gs_key_model")
    clear(loc)
    if not model: return None
    try: skin = int(_attr(loc, "gs_key_skin", 0) or 0)
    except ValueError: skin = 0
    groups, nskins = _load(model, skin)
    yup = cmds.upAxis(q=True, axis=True) == "y"
    root = cmds.group(empty=True, name=loc + "_mdlpreview"); cmds.parent(root, loc, relative=True)
    for i, g in enumerate(groups):
        v = g["verts"]
        pts = [om.MPoint(x, z, -y) if yup else om.MPoint(x, y, z) for x, y, z in v]
        n = len(g["tris"]); conn = [i for t in g["tris"] for i in t]
        fn = om.MFnMesh(); obj = fn.create(pts, [3] * n, conn)
        fn.setUVs([u[0] for u in g["uvs"]], [u[1] for u in g["uvs"]]); fn.assignUVs([3] * n, conn)
        name = cmds.rename(om.MFnDagNode(obj).fullPathName(), "%s_mdl%d" % (loc, i))   # create() returns the new transform
        cmds.polySoftEdge(name, angle=60, ch=False)
        sh = cmds.shadingNode("lambert", asShader=True, name="mdlp_" + g["tex"].replace(".", "_")[:24] + "#")
        sgn = cmds.sets(renderable=True, noSurfaceShader=True, empty=True, name=sh + "SG"); cmds.connectAttr(sh + ".outColor", sgn + ".surfaceShader")
        if g["png"]:
            f = cmds.shadingNode("file", asTexture=True, isColorManaged=True)
            cmds.setAttr(f + ".fileTextureName", g["png"], type="string"); cmds.connectAttr(f + ".outColor", sh + ".color"); cmds.connectAttr(f + ".outTransparency", sh + ".transparency")   # masked ("{") textures
        cmds.sets(name, e=True, forceElement=sgn)
        cmds.parent(name, root, relative=True)
    cmds.setAttr(root + ".overrideEnabled", 1); cmds.setAttr(root + ".overrideDisplayType", 2)   # reference: pick the locator, not the mesh
    watch(loc)
    return root

def watch(loc):
    """Rebuild when the model or skin key changes on the locator."""
    for j in _jobs.pop(loc, []):      # one job per attribute, never stacked by repeated builds
        try: cmds.scriptJob(kill=j, force=True)
        except Exception: pass
    def rebuild(l=loc):
        try:
            if cmds.objExists(l): build(l)
        except Exception as e: cmds.warning("[gsbridge] model preview rebuild failed: %s" % e)
    _jobs[loc] = [cmds.scriptJob(attributeChange=[loc + "." + a, lambda: cmds.evalDeferred(rebuild)], killWithScene=True, protected=False)
                  for a in ("gs_key_model", "gs_key_skin") if cmds.attributeQuery(a, node=loc, exists=True)]

def refresh_all():
    for n in cmds.ls("*", transforms=True):
        if cmds.attributeQuery("gs_key_model", node=n, exists=True) and not cmds.objExists(n + "_mdlpreview"): build(n)
