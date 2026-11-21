"""Maya side of the GoldSrc bridge.

In Maya's Script Editor (Python):
    import sys; sys.path.insert(0, "/home/tim/Desktop/dod-gc/tools/mapbridge")
    import maya_export; maya_export.install()      # export on every Save + shelf-less
    maya_export.export_now()                        # or call by hand

Scene conventions (kept deliberately small):
  * Every mesh under a group named  gs_world  is one convex world brush
    (non-convex pieces: split them, as in any brush editor).
  * Texture = the mesh's shading-group / shader name (e.g. m_wall11, from
    dod_caen.wad). No shader -> DEFAULT_TEXTURE.
  * Any transform with a string attribute gs_classname is an entity. Extra keys:
    string attributes named gs_key_<name> (e.g. gs_key__light = "255 240 200 300"
    exports "_light"; double underscore after key_ -> leading underscore).
    A mesh with gs_classname becomes a brush entity (func_*); a locator/null
    becomes a point entity at its translate.
  * info_player_* locators mark the player's FEET: export adds 37 units (a spawn at floor+24 puts the
    standing hull inside the floor and the player falls through).
  * Maya units: 1 cm = UNITS_PER_CM GoldSrc units (default 0.3937 = inches...
    use 1.0 if you model in GoldSrc units directly). Y-up scenes are rotated to Z-up.
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gsmap

WORKDIR = os.environ.get("GSBRIDGE_WORK", "/home/tim/.cache/gsbridge")
MAPNAME = "live"
WAD = "dod_caen.wad"
DEFAULT_TEXTURE = "m_wall11"
UNITS_PER_CM = 1.0
PLAYER_ORIGIN_ABOVE_FLOOR = 37   # GoldSrc standing player origin is 36 above his feet; +1 so he is not embedded

def _conv(p, yup):
    x, y, z = (c * UNITS_PER_CM for c in p)
    return (x, -z, y) if yup else (x, y, z)

def _tex(mesh, cmds):
    sgs = cmds.listConnections(mesh, type="shadingEngine") or []
    for sg in sgs:
        sh = cmds.listConnections(sg + ".surfaceShader") or []
        if sh:
            if cmds.attributeQuery("gs_texture", node=sh[0], exists=True): return cmds.getAttr(sh[0] + ".gs_texture")
            return sh[0].split(":")[-1]
    return DEFAULT_TEXTURE

def _polys(mesh, cmds, yup):
    polys = []
    n = cmds.polyEvaluate(mesh, face=True)
    for i in range(n):
        info = cmds.polyInfo("%s.f[%d]" % (mesh, i), faceToVertex=True)[0].split(":")[1].split()
        pts = []
        for v in info:
            p = cmds.pointPosition("%s.vtx[%s]" % (mesh, v), world=True)
            pts.append(_conv(p, yup))
        polys.append(pts)
    return polys

def _keys(node, cmds):
    keys = {"classname": cmds.getAttr(node + ".gs_classname")}
    for a in cmds.listAttr(node, userDefined=True) or []:
        if a.startswith("gs_key_"):
            keys[a[7:]] = cmds.getAttr(node + "." + a)  # gs_key__light -> "_light"
    return keys

def export_now(path=None):
    import maya.cmds as cmds
    yup = cmds.upAxis(q=True, axis=True) == "y"
    os.makedirs(WORKDIR, exist_ok=True)
    path = path or os.path.join(WORKDIR, MAPNAME + ".map")
    world = []
    for m in cmds.listRelatives("gs_world", allDescendents=True, type="mesh", fullPath=True) or [] \
            if cmds.objExists("gs_world") else []:
        t = cmds.listRelatives(m, parent=True, fullPath=True)[0]
        world.append((_polys(t, cmds, yup), _tex(m, cmds)))
    ents = []
    for node in cmds.ls("*", transforms=True, long=True):
        if not cmds.attributeQuery("gs_classname", node=node, exists=True): continue
        keys = _keys(node, cmds)
        shapes = cmds.listRelatives(node, shapes=True, type="mesh", fullPath=True) or []
        if shapes:
            ents.append({"keys": keys, "brushes": [(_polys(node, cmds, yup), _tex(shapes[0], cmds))]})
        else:
            t = cmds.xform(node, q=True, ws=True, t=True)
            o = _conv(t, yup)
            if keys["classname"].startswith("info_player"):
                o = (o[0], o[1], o[2] + PLAYER_ORIGIN_ABOVE_FLOOR)   # locator = feet; hull 1 needs origin >= 36 above the floor
            keys["origin"] = "%d %d %d" % tuple(round(c) for c in o)
            yaw = cmds.getAttr(node + ".gs_yaw") if cmds.attributeQuery("gs_yaw", node=node, exists=True) else 0
            r = cmds.xform(node, q=True, ws=True, ro=True)            # rotating the locator in the viewport = entity angles
            if not yaw: yaw = r[1] if yup else r[2]
            if yaw or "angles" in keys: keys["angles"] = "0 %d 0" % round(yaw)
            ents.append({"keys": keys})
    gsmap.write_map(path, world, ents, WAD)
    print("[gsbridge] wrote %s (%d world brushes, %d entities) %s" % (path, len(world), len(ents), time.strftime("%H:%M:%S")))
    return path

_job = None
def install():
    """Re-export on every scene save (watcher on ceres picks the .map up)."""
    import maya.cmds as cmds
    global _job
    if _job: cmds.scriptJob(kill=_job, force=True)
    _job = cmds.scriptJob(event=["SceneSaved", lambda: export_now()], protected=False)
    print("[gsbridge] installed: save the scene to push the map. workdir=%s" % WORKDIR)
