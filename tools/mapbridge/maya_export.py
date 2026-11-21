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
import json, os, sys, time
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

def settings():
    """Map-wide keys from the optional gs_settings node: gs_wad (default dod_caen.wad), gs_skyname, gs_message (worldspawn keys)."""
    import maya.cmds as cmds
    out = {"wad": WAD, "extra": {}}
    if cmds.objExists("gs_settings"):
        for a, k in (("gs_wad", "wad"), ("gs_skyname", "skyname"), ("gs_message", "message")):
            if cmds.attributeQuery(a, node="gs_settings", exists=True):
                v = cmds.getAttr("gs_settings." + a)
                if v: (out.__setitem__("wad", v) if k == "wad" else out["extra"].__setitem__(k, v))
    return out

def validate():
    """Pre-flight: [(level, message)] with level 'error' (the game will not work) or 'warn'. Run before every export."""
    import maya.cmds as cmds
    msgs = []
    classes = {}
    for node in cmds.ls("*", transforms=True):
        if cmds.attributeQuery("gs_classname", node=node, exists=True) and cmds.getAttr(node + ".visibility"):
            classes.setdefault(cmds.getAttr(node + ".gs_classname"), []).append(node)
    if not classes.get("info_player_allies"): msgs.append(("error", "No info_player_allies spawn: Allies cannot spawn"))
    if not classes.get("info_player_axis"): msgs.append(("error", "No info_player_axis spawn: Axis cannot spawn"))
    if not classes.get("info_doddetect"): msgs.append(("warn", "No info_doddetect entity: DoD game rules will not start (use Object Manager > Control Points)"))
    roots = [g for g in cmds.ls("gs_world*", transforms=True) if cmds.getAttr(g + ".visibility")]
    if not roots: msgs.append(("error", "No gs_world group: nothing to export. Put your brushes under gs_world"))
    try:
        idx = json.load(open(os.path.expanduser("~/.cache/gsbridge/tex/index.json")))
    except Exception: idx = None
    bad = set(); nonconvex = []
    for root in roots:
        for m in cmds.listRelatives(root, allDescendents=True, type="mesh", fullPath=True) or []:
            t = cmds.listRelatives(m, parent=True, fullPath=True)[0]
            tex = _tex(m, cmds)
            if idx is not None and tex not in idx: bad.add(tex)
            if not _is_convex(t, cmds): nonconvex.append(t.split("|")[-1])
    for t in sorted(bad): msgs.append(("warn", "Texture %r is not in the extracted WADs: it will compile only if its WAD is in the map's wad list, and shows as the pink/black checker if the game cannot load it" % t))
    if nonconvex: msgs.append(("error", "Not convex (split into convex brushes): " + ", ".join(nonconvex[:5]) + (" ..." if len(nonconvex) > 5 else "")))
    return msgs

def _is_convex(t, cmds, tol=0.05):
    """A brush mesh is convex when every vertex lies behind (or on) every face plane."""
    try:
        verts = [cmds.pointPosition("%s.vtx[%d]" % (t, i), world=True) for i in range(cmds.polyEvaluate(t, vertex=True))]
        for i in range(cmds.polyEvaluate(t, face=True)):
            info = cmds.polyInfo("%s.f[%d]" % (t, i), faceNormals=True)[0].split()[2:5]; n = [float(v) for v in info]
            f0 = cmds.polyInfo("%s.f[%d]" % (t, i), faceToVertex=True)[0].split(":")[1].split()[0]
            p0 = cmds.pointPosition("%s.vtx[%s]" % (t, f0), world=True)
            for v in verts:
                if (v[0]-p0[0])*n[0] + (v[1]-p0[1])*n[1] + (v[2]-p0[2])*n[2] > tol: return False
        return True
    except Exception: return True      # never block an export because the test itself failed

def export_now(path=None):
    import maya.cmds as cmds
    yup = cmds.upAxis(q=True, axis=True) == "y"
    try: os.makedirs(WORKDIR, exist_ok=True)
    except OSError as e: raise RuntimeError("work dir %s is not writable: %s" % (WORKDIR, e))
    path = path or os.path.join(WORKDIR, MAPNAME + ".map")
    msgs = validate()
    try: json.dump([{"level": l, "msg": m} for l, m in msgs], open(os.path.join(WORKDIR, "validation.json"), "w"))
    except OSError: pass
    errs = [m for l, m in msgs if l == "error"]
    for l, m in msgs: cmds.warning("[gsbridge] %s: %s" % (l, m)) if l == "warn" else None
    if errs: raise RuntimeError("; ".join(errs) + "  (nothing pushed: the game keeps the previous map)")
    world = []
    roots = [g for g in cmds.ls("gs_world*", transforms=True) if cmds.getAttr(g + ".visibility")]   # gs_world, gs_world_test, ...
    for root in roots:
        for m in cmds.listRelatives(root, allDescendents=True, type="mesh", fullPath=True) or []:
            t = cmds.listRelatives(m, parent=True, fullPath=True)[0]
            if not cmds.getAttr(t + ".visibility"): continue            # hidden = not exported
            world.append((_polys(t, cmds, yup), _tex(m, cmds)))
    ents = []
    for node in cmds.ls("*", transforms=True, long=True):
        if not cmds.attributeQuery("gs_classname", node=node, exists=True): continue
        if not cmds.getAttr(node + ".visibility"): continue
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
    # z-fighting: faces of different brushes that share a plane and overlap.  Overlay brush entities are pushed 0.5 unit
    # in front of what they cover; coplanar WORLD faces are reported (merge the brushes or move one face).
    # world items are (polys, texture); entity brushes are (polys, texture[, grow])
    groups = [("world:%d" % i, w[0]) for i, w in enumerate(world)] + [("entity:%s" % e["keys"]["classname"], [pp for b in e["brushes"] for pp in b[0]]) for e in ents if e.get("brushes")]
    fighting = gsmap.coplanar_overlaps(groups)
    for la, lb, same in fighting:
        ent_side = [x for x in (la, lb) if x.startswith("entity:")]
        if ent_side:
            for e in ents:
                if e.get("brushes") and "entity:%s" % e["keys"]["classname"] in ent_side:
                    e["brushes"] = [(b[0], b[1], 0.5) for b in e["brushes"]]
        elif same: cmds.warning("[gsbridge] coplanar overlapping faces (will z-fight): %s and %s" % (la, lb))
    st = settings()
    gsmap.write_map(path, world, ents, st["wad"], extra_world=st["extra"])
    # keep a per-map copy (named after the map) so release builds do not depend on which scene is open or which Maya version saved it
    try:
        nm = st["extra"].get("message") or os.path.splitext(os.path.basename(cmds.file(q=True, sceneName=True) or ""))[0]
        if nm:
            mdir = os.path.expanduser("~/gsbridge/maps"); os.makedirs(mdir, exist_ok=True)
            import shutil; shutil.copyfile(path, os.path.join(mdir, nm + ".map"))
    except Exception: pass
    print("[gsbridge] wrote %s (%d world brushes, %d entities) %s" % (path, len(world), len(ents), time.strftime("%H:%M:%S")))
    return path

def _safe_export():
    try: export_now()
    except Exception as e:      # a failed export must never raise inside Maya's save
        import maya.cmds as cmds
        cmds.warning("[gsbridge] export failed: %s (the game keeps the previous map)" % e)

def install():
    """Re-export on every scene save (watcher on ceres picks the .map up). The job id lives in __main__ so reloading this module
    cannot leak a second job."""
    import maya.cmds as cmds, __main__
    old = getattr(__main__, "_gsbridge_export_job", None)
    if old is not None:
        try: cmds.scriptJob(kill=old, force=True)
        except Exception: pass
    __main__._gsbridge_export_job = cmds.scriptJob(event=["SceneSaved", _safe_export], protected=False)
    global _job
    _job = __main__._gsbridge_export_job
    print("[gsbridge] installed: save the scene to push the map. workdir=%s" % WORKDIR)
