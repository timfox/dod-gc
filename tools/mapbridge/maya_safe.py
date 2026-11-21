"""Guards for the GoldSrc tools: an error in a button, scriptJob or deferred call must never reach Maya as an exception
(it prints a warning and the tool keeps working), heavy loops can report progress / be cancelled, and orphaned scene nodes
the tools create can be purged.  Import-safe outside Maya (stubs)."""
import functools, time, traceback
try:
    import maya.cmds as cmds, maya.mel as mel
except ImportError:      # unit tests outside Maya
    cmds = mel = None

def safe(fn=None, *, what=None):
    """Decorator/wrapper: call fn, swallow and report any exception (returns None)."""
    def deco(f):
        @functools.wraps(f)
        def w(*a, **k):
            try: return f(*a, **k)
            except Exception as e:      # noqa: BLE001 - this is the point
                msg = "[gsbridge] %s failed: %s" % (what or getattr(f, "__name__", "callback"), e)
                try: cmds.warning(msg); print(traceback.format_exc())
                except Exception: print(msg)
        return w
    return deco(fn) if fn else deco

def cb(fn):
    """Wrap a UI command so it accepts (and ignores) the extra args Maya passes."""
    s = safe(fn)
    return lambda *a: s()

class Progress:
    """Main-window progress bar with cancel (Esc). `with Progress("Loading", n) as p: ... p.step(); if p.cancelled: break`"""
    def __init__(self, label, maxv):
        self.label, self.maxv, self.cancelled = label, max(1, maxv), False
        self._t = time.time()
        try: self.bar = mel.eval("$tmp = $gMainProgressBar")
        except Exception: self.bar = None
    def __enter__(self):
        if self.bar:
            try: cmds.progressBar(self.bar, e=True, beginProgress=True, isInterruptable=True, status=self.label, maxValue=self.maxv)
            except Exception: self.bar = None
        return self
    def step(self, n=1):
        if not self.bar: return
        try:
            if cmds.progressBar(self.bar, q=True, isCancelled=True): self.cancelled = True
            cmds.progressBar(self.bar, e=True, step=n)
            now = time.time()
            if now - self._t > 0.4:      # repaint a few times a second, not per step (a redraw of a big map per step is what froze Maya)
                self._t = now; cmds.refresh(f=False) if False else cmds.refresh()
        except Exception: pass
    def __exit__(self, *a):
        if self.bar:
            try: cmds.progressBar(self.bar, e=True, endProgress=True)
            except Exception: pass
        return False

def purge_unused(prefixes=("refm_", "gs_", "mdlp_", "refsky_", "gsf_")):
    """Delete shading engines of ours that nothing is assigned to, with the shader/file/place2d nodes only they used."""
    n = 0
    for sg in cmds.ls(type="shadingEngine") or []:
        if sg in ("initialShadingGroup", "initialParticleSE") or not sg.startswith(prefixes): continue
        if cmds.sets(sg, q=True): continue                      # still has members
        ups = cmds.listHistory(sg, pruneDagObjects=True) or []
        try: cmds.delete(sg)
        except Exception: continue
        n += 1
        for u in ups:
            if u != sg and cmds.objExists(u):
                try:
                    if not (cmds.listConnections(u, d=True, s=False) or []): cmds.delete(u)
                except Exception: pass
    for node in cmds.ls(type="lambert") or []:
        if node != "lambert1" and node.startswith(prefixes) and not (cmds.listConnections(node, type="shadingEngine") or []):
            try: cmds.delete(node)
            except Exception: pass
    import os
    cache = os.path.expanduser("~/.cache/gsbridge")
    for node in cmds.ls(type="file") or []:                      # texture nodes of ours that no shader uses any more
        try:
            if not cmds.getAttr(node + ".fileTextureName").startswith(cache): continue
            users = [x for x in (cmds.listConnections(node, d=True, s=False) or []) if cmds.nodeType(x) not in ("defaultTextureList", "materialInfo", "place2dTexture", "defaultColorMgtGlobals")]
            if not users: cmds.delete(node); n += 1
        except Exception: pass
    for node in cmds.ls(type="place2dTexture") or []:
        try:
            if not (cmds.listConnections(node, type="file") or []): cmds.delete(node)
        except Exception: pass
    return n

def kill_scriptjobs(prefix_tags=()):
    """Kill scriptJobs that point at our lambdas (leaked by repeated installs)."""
    n = 0
    for j in cmds.scriptJob(listJobs=True) or []:
        if any(t in j for t in prefix_tags):
            try: cmds.scriptJob(kill=int(j.split(":")[0]), force=True); n += 1
            except Exception: pass
    return n
