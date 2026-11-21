"""GoldSrc Bridge dockable panel for Maya.

    import sys; sys.path.insert(0, "/home/tim/Desktop/dod-gc/tools/mapbridge")
    import maya_panel; maya_panel.show()

Sections: status (compile / push / serial / timings / errors from watch.py's status.json),
texture browser (WAD textures with preview + assign), entity placement (stock DoD classnames),
brush helper, Export+Push, auto-push on save, start/stop the ceres-side watcher.
It never starts or stops Dolphin; the reload is done by the game's gc_livemap poll.
"""
import json, os, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import maya.cmds as cmds
import maya_export as me, maya_materials as mm

WORK = os.path.expanduser(os.environ.get("GSBRIDGE_WORK", "~/.cache/gsbridge"))
ISO = os.environ.get("GSBRIDGE_ISO", "/home/tim/gsbridge/live_retail.iso")
CLASSES = ["info_player_allies", "info_player_axis", "info_doddetect", "light", "light_environment",
           "dod_control_point", "dod_control_point_master", "dod_capture_area", "ambient_generic", "info_target"]
WIN = "gsBridgeWin"; _ui = {}; _proc = None; _timer = None

def _status():
    try: return json.load(open(os.path.join(WORK, "status.json")))
    except Exception: return {}

_last = [0.0]
def _tick():
    if time.time() - _last[0] > 1.0: _last[0] = time.time(); refresh()

def refresh(*_):
    s = _status(); st = s.get("state", "watcher not running")
    if _proc and _proc.poll() is not None: st = "watcher exited (rc=%s)" % _proc.returncode
    age = time.time() - s.get("updated", 0) if s else 0
    if st == "pushed":
        t = s.get("times", {}); line = "PUSHED #%s  %.2fs  (%s)  %ds ago" % (s.get("serial"), s.get("total", 0),
            " ".join("%s %.2fs" % kv for kv in t.items()), age)
        col = (0.2, 0.55, 0.25)
    elif st == "error": line, col = "COMPILE ERROR - game keeps previous map: " + s.get("error", "")[:160], (0.7, 0.2, 0.2)
    elif st in ("watching", "compiling"): line, col = st.upper() + "  iso=" + os.path.basename(s.get("iso", "")), (0.3, 0.4, 0.6)
    else: line, col = st, (0.4, 0.4, 0.4)
    cmds.text(_ui["status"], e=True, label=line, backgroundColor=col)
    cmds.text(_ui["reload"], e=True, label="Game reload: polled by gc_livemap about every 1 s after a push (check the Dolphin window)")

def _filter(*_):
    q = cmds.textField(_ui["filter"], q=True, text=True).lower()
    names = sorted(n for n in mm._index() if q in n.lower())
    cmds.textScrollList(_ui["tex"], e=True, removeAll=True, append=names[:400])

def _preview(*_):
    sel = cmds.textScrollList(_ui["tex"], q=True, selectItem=True)
    if not sel: return
    idx = mm._index()[sel[0]]
    cmds.image(_ui["img"], e=True, image=os.path.join(mm.TEX, idx["png"]))
    cmds.text(_ui["texinfo"], e=True, label="%s  %dx%d  (%s)" % (sel[0], idx["w"], idx["h"], idx["wad"]))

def _load_wad(*_):
    wad = cmds.textField(_ui["wad"], q=True, text=True)
    mm.load_wad(wad, names=[])          # extract PNGs only; materials are made on assign
    _filter()

def _assign(*_):
    sel = cmds.textScrollList(_ui["tex"], q=True, selectItem=True); objs = cmds.ls(sl=True, transforms=True)
    if not sel or not objs: cmds.warning("pick a texture and select meshes"); return
    for o in objs: mm.assign(o, sel[0])
    if cmds.checkBox(_ui["fit"], q=True, value=True): mm.fit_uvs()

def _brush(*_):
    if not cmds.objExists("gs_world"): cmds.group(empty=True, name="gs_world")
    c = cmds.polyCube(name="brush#", w=128, h=128, d=128)[0]; cmds.move(0, 0, 64, c)
    cmds.parent(c, "gs_world")
    sel = cmds.textScrollList(_ui["tex"], q=True, selectItem=True)
    if sel: mm.assign(c, sel[0]); mm.fit_uvs()
    cmds.select(c)

def _entity(*_):
    cls = cmds.optionMenu(_ui["cls"], q=True, value=True); sel = cmds.ls(sl=True, transforms=True)
    pos = cmds.xform(sel[0], q=True, ws=True, t=True) if sel else [0, 0, 24]
    l = cmds.spaceLocator(name=cls + "#")[0]; cmds.move(*pos, l, absolute=True)
    cmds.addAttr(l, ln="gs_classname", dt="string"); cmds.setAttr(l + ".gs_classname", cls, type="string")
    cmds.addAttr(l, ln="gs_yaw", at="long"); cmds.select(l)

def _push(*_):
    try: mm.preview_render_modes()
    except Exception as ex: cmds.warning("render-mode preview: %s" % ex)
    me.export_now(); refresh()

def _auto(v):
    if v: me.install()
    else:
        if me._job: cmds.scriptJob(kill=me._job, force=True); me._job = None

def _watcher(on):
    global _proc
    if on and (not _proc or _proc.poll() is not None):
        iso = cmds.textField(_ui["iso"], q=True, text=True)
        _proc = subprocess.Popen([sys.executable, os.path.join(HERE, "watch.py"), "--iso", iso, "--work", WORK],
                                 stdout=open(os.path.join(WORK, "watch.log"), "a"), stderr=subprocess.STDOUT)
    elif not on and _proc and _proc.poll() is None: _proc.terminate()
    refresh()

def _build():
    cmds.setParent(_ui["root"])
    cmds.frameLayout(label="Status", collapsable=False, marginWidth=6)
    _ui["status"] = cmds.text(label="...", align="left", height=40, backgroundColor=(0.4, 0.4, 0.4), wordWrap=True)
    _ui["reload"] = cmds.text(label="", align="left", wordWrap=True, font="smallObliqueLabelFont")
    cmds.rowLayout(numberOfColumns=3, adjustableColumn=1)
    _ui["iso"] = cmds.textField(text=ISO, annotation="slot disc the watcher patches")
    cmds.checkBox(label="Watcher", changeCommand=lambda v: _watcher(v), annotation="start/stop ceres-side compile+patch")
    cmds.button(label="Refresh", command=refresh); cmds.setParent("..")
    cmds.button(label="Export + Compile + Push", height=36, backgroundColor=(0.25, 0.45, 0.3), command=_push)
    cmds.checkBox(label="Auto-push on Save", changeCommand=_auto)
    cmds.setParent(_ui["root"])
    cmds.frameLayout(label="Textures (WAD)", collapsable=True, marginWidth=6)
    cmds.rowLayout(numberOfColumns=3, adjustableColumn=1)
    _ui["wad"] = cmds.textField(text="dod_caen.wad"); cmds.button(label="Load WAD", command=_load_wad)
    _ui["fit"] = cmds.checkBox(label="Fit UVs", value=True); cmds.setParent("..")
    _ui["filter"] = cmds.textField(placeholderText="filter...", changeCommand=_filter, enterCommand=_filter, textChangedCommand=_filter)
    _ui["tex"] = cmds.textScrollList(height=130, selectCommand=_preview)
    cmds.rowLayout(numberOfColumns=2, adjustableColumn=2)
    _ui["img"] = cmds.image(width=96, height=96)
    cmds.columnLayout(adjustableColumn=True)
    _ui["texinfo"] = cmds.text(label="", align="left")
    cmds.button(label="Assign to selection", command=_assign)
    cmds.setParent(_ui["root"])
    cmds.frameLayout(label="Reference maps (locked, never exported)", collapsable=True, collapse=True, marginWidth=6)
    _ui["refmap"] = cmds.optionMenu(label="Stock map")
    for f in sorted(os.listdir(os.path.expanduser("~/.cache/gsbridge/ref"))) if os.path.isdir(os.path.expanduser("~/.cache/gsbridge/ref")) else []:
        if f.endswith(".json"): cmds.menuItem(label=f[:-5])
    cmds.rowLayout(numberOfColumns=2, adjustableColumn=1)
    cmds.button(label="Load beside gs_world", command=lambda *_: __import__("maya_refmap").load(cmds.optionMenu(_ui["refmap"], q=True, value=True)))
    cmds.button(label="Unload", command=lambda *_: __import__("maya_refmap").unload(cmds.optionMenu(_ui["refmap"], q=True, value=True)))
    cmds.setParent(_ui["root"])
    cmds.frameLayout(label="Build", collapsable=True, marginWidth=6)
    cmds.button(label="New 128 brush (gs_world)", command=_brush)
    _ui["cls"] = cmds.optionMenu(label="Entity")
    for c in CLASSES: cmds.menuItem(label=c)
    cmds.button(label="Place entity at selection", command=_entity)

def show(*_):
    global _timer
    if cmds.workspaceControl(WIN, q=True, exists=True):
        cmds.workspaceControl(WIN, e=True, restore=True); return WIN
    cmds.workspaceControl(WIN, label="GoldSrc Bridge", retain=False, initialWidth=300, minimumWidth=240,
                          tabToControl=("AttributeEditor", -1), uiScript="import maya_panel; maya_panel._ui_script()")
    return WIN

def _ui_script():
    global _timer
    _ui["root"] = cmds.columnLayout(adjustableColumn=True, rowSpacing=4)
    _build()
    try: mm._index()
    except Exception: import wad_textures; wad_textures.main(["dod_caen.wad"])
    _filter(); refresh()
    if _timer: cmds.scriptJob(kill=_timer, force=True)
    _timer = cmds.scriptJob(event=["idle", _tick], parent=WIN, protected=True)
