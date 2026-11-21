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
from maya_safe import safe, cb, Progress
import maya_export as me, maya_materials as mm

WORK = os.path.expanduser(os.environ.get("GSBRIDGE_WORK", "~/.cache/gsbridge"))
ISO = os.environ.get("GSBRIDGE_ISO", "/home/tim/gsbridge/live_sync.iso")
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
    stg = s.get("stages", {}); parts = []
    for k, lab in (("compile", "compile"), ("bake", "bake"), ("patch", "patch")):
        if k in stg: parts.append("%s %.2fs" % (lab, stg[k]))
    timing = " · ".join(parts)
    game = ""
    if s.get("game_connected"): game = "  |  game linked"
    if st == "game_reloading": line, col = "RELOADING IN GAME…  (%s)" % timing, (0.7, 0.55, 0.15)
    elif st == "game_reloaded" or (st == "pushed" and s.get("reload_seconds")):
        line, col = "GAME RELOADED in %.1fs  (%s)%s" % (s.get("reload_seconds", 0), timing, "  player kept" if s.get("player_restored") else ""), (0.2, 0.55, 0.25)
    elif st == "pushed":
        line, col = "PUSHED #%s  (%s)  waiting for the game…%s" % (s.get("serial"), timing, game), (0.25, 0.45, 0.6)
    elif st == "unchanged": line, col = "No change to the map: nothing pushed  (%s)" % timing, (0.4, 0.4, 0.4)
    elif st == "error": line, col = "COMPILE ERROR - game keeps previous map: " + s.get("error", "")[:160], (0.7, 0.2, 0.2)
    elif st in ("watching", "compiling"): line, col = st.upper() + "  iso=" + os.path.basename(s.get("iso", "")) + game, (0.3, 0.4, 0.6)
    else: line, col = st, (0.4, 0.4, 0.4)
    cmds.text(_ui["status"], e=True, label=line, backgroundColor=col)
    if s.get("game_connected") is False:
        note = "Game not linked: start Dolphin with live_dolphin.sh so the panel can see reloads"
    else: note = "Reload is polled by gc_livemap about 3x a second; the player keeps position and view" + ("  · textures unchanged, pack reused" if s.get("wt_reused") else "")
    cmds.text(_ui["reload"], e=True, label=note)

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

def _write_opts(*_):
    try:
        os.makedirs(WORK, exist_ok=True)
        json.dump({"rad": cmds.checkBox(_ui["rad"], q=True, value=True), "vis": cmds.checkBox(_ui["vis"], q=True, value=True)}, open(os.path.join(WORK, "options.json"), "w"))
    except Exception as e: cmds.warning("[gsbridge] options not saved: %s" % e)

def _start_session(*_):
    """gsbridge up: watcher + Dolphin on the live disc (starts only what is not already running)."""
    g = os.path.join(HERE, "gsbridge")
    if not os.path.exists(g): cmds.warning("gsbridge launcher not found at %s" % g); return
    env = {k: v for k, v in os.environ.items() if not k.startswith(("PYTHON", "MAYA", "LD_LIBRARY"))}
    subprocess.Popen([g, "up"], stdout=open(os.path.join(os.path.expanduser("~/.cache/gsbridge"), "up.log"), "w"), stderr=subprocess.STDOUT, env=env, start_new_session=True)
    cmds.inViewMessage(amg="Starting the watcher and Dolphin (see ~/.cache/gsbridge/up.log)", pos="topCenter", fade=True)

def _hw_disc(*_):
    g = os.path.join(HERE, "gsbridge")
    log = os.path.join(os.path.expanduser("~/.cache/gsbridge"), "hw.log")
    env = {k: v for k, v in os.environ.items() if not k.startswith(("PYTHON", "MAYA", "LD_LIBRARY"))}
    me.export_now()
    name = "dod_live"
    if cmds.objExists("gs_settings") and cmds.attributeQuery("gs_message", node="gs_settings", exists=True): name = cmds.getAttr("gs_settings.gs_message") or name
    subprocess.Popen([g, "hw", name], stdout=open(log, "w"), stderr=subprocess.STDOUT, env=env, start_new_session=True)
    cmds.inViewMessage(amg="Building ~/gsbridge/out/%s.iso (log: %s)" % (name, log), pos="topCenter", fade=True)

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
        if not os.path.exists(iso):
            cmds.warning("disc image %s does not exist; build one with build_slot_disc.sh (see DESIGN.md)" % iso)
            cmds.checkBox(_ui["watcher_cb"], e=True, value=False) if "watcher_cb" in _ui else None; return
        os.makedirs(WORK, exist_ok=True)
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
    _ui["watcher_cb"] = cmds.checkBox(label="Watcher", changeCommand=lambda v: _watcher(v), annotation="start/stop ceres-side compile+patch")
    cmds.button(label="Refresh", command=refresh); cmds.setParent("..")
    cmds.rowLayout(numberOfColumns=4, adjustableColumn=1)
    cmds.button(label="Start session (watcher + Dolphin)", command=_start_session, backgroundColor=(0.25, 0.4, 0.55))
    _ui["rad"] = cmds.checkBox(label="Lit", value=True, changeCommand=_write_opts, annotation="compile with lighting (RAD, ~0.4 s)")
    _ui["vis"] = cmds.checkBox(label="VIS", value=False, changeCommand=_write_opts, annotation="visibility data; off by default (it made the engine draw white)")
    cmds.button(label="Hardware disc", command=_hw_disc, annotation="build a real playable ISO from this map"); cmds.setParent("..")
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


# every entry point is guarded: an error becomes a Maya warning, never an exception in Maya's event loop
for _n in ['refresh', '_tick', '_filter', '_preview', '_load_wad', '_assign', '_brush', '_entity', '_push', '_auto', '_watcher', '_build', '_ui_script', 'show']:
    globals()[_n] = safe(globals()[_n])
