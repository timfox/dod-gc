"""Map Manager: the game's levels, sub-levels, progression and missions, driven from Maya (dockable panel).

    import maya_mapmgr; maya_mapmgr.show()
Tree = levels with their sub-levels. Select a map to edit its title, Maya scene, BSP, next map, rotation flag and objectives.
Double-click: a map with a Maya scene opens it (asks before discarding changes); a stock map (or a sub-level that has none)
loads as a locked reference beside your scene. Export writes the game's rotation: mapcycle.txt, maps.lst, progression.json
(~/gsbridge/export). The project file is ~/gsbridge/project.json (maya_mapmgr/mapproject).
"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import maya.cmds as cmds
from maya_safe import safe, cb, Progress
import mapproject as mp

WCTL = "gsMapMgrCtl"; _ui = {}; _d = None; _sel = None

def _data():
    global _d
    if _d is None:
        _d = mp.load()
        if not _d["maps"]: _d = mp.seed(); mp.save(_d)
        if not any(m["bsp"] == "dod_anzio" or m["name"] == "dod_anzio" for m in _d["maps"]): mp.preload_stock(_d); mp.save(_d)   # first run: all stock maps
    return _d

def _label(m):
    d = _data(); bits = [m["title"] or m["name"]]
    cur = cmds.file(q=True, sceneName=True)
    if m["scene"] and cur and os.path.abspath(cur) == os.path.abspath(m["scene"]): bits.append("● open")
    if cmds.objExists("ref_" + (m["bsp"] or m["name"])): bits.append("◌ ref")
    if m["next"]: bits.append("→ " + m["next"])
    if not m["rotation"]: bits.append("(not in rotation)")
    return "  ".join(bits) + "   [%s]" % m["name"]

def _fill_tree():
    d = _data(); tv = _ui["tree"]; cmds.treeView(tv, e=True, removeAll=True)
    def add(m, parent):
        cmds.treeView(tv, e=True, addItem=(m["name"], parent)); cmds.treeView(tv, e=True, displayLabel=(m["name"], _label(m)))
        for c in mp.children(d, m["name"]): add(c, m["name"])
    for n in mp.order(d): add(mp.get(d, n), "")
    for m in d["maps"]:      # parents missing from the project: show at top level so nothing disappears
        if m["parent"] and not mp.get(d, m["parent"]) and not cmds.treeView(tv, q=True, itemExists=m["name"]): add(m, "")
    if _sel and cmds.treeView(tv, q=True, itemExists=_sel): cmds.treeView(tv, e=True, selectItem=(_sel, True))
    _problems()

def _problems():
    p = mp.problems(_data())
    cmds.text(_ui["problems"], e=True, label=("⚠ " + "; ".join(p[:3]) + (" …" if len(p) > 3 else "")) if p else "Project OK", backgroundColor=(0.55, 0.35, 0.1) if p else (0.2, 0.3, 0.2))

def _select(item, state):
    global _sel
    if not state: return
    _sel = item; m = mp.get(_data(), item)
    if not m: return
    cmds.textField(_ui["name"], e=True, text=m["name"], enable=False)
    cmds.textField(_ui["title"], e=True, text=m["title"]); cmds.textField(_ui["scene"], e=True, text=m["scene"])
    cmds.textField(_ui["bsp"], e=True, text=m["bsp"]); cmds.checkBox(_ui["rot"], e=True, value=m["rotation"])
    cmds.scrollField(_ui["obj"], e=True, text="\n".join(m["objectives"])); cmds.scrollField(_ui["notes"], e=True, text=m["notes"])
    _fill_next(m)

def _fill_next(m):
    om = _ui["next"]
    for i in cmds.optionMenu(om, q=True, itemListLong=True) or []: cmds.deleteUI(i)
    cmds.menuItem(label="(end)", parent=om)
    for x in _data()["maps"]:
        if x["name"] != m["name"]: cmds.menuItem(label=x["name"], parent=om)
    cmds.optionMenu(om, e=True, value=m["next"] or "(end)")

def _apply(*_):
    """Write the form back into the selected map and save the project."""
    m = mp.get(_data(), _sel)
    if not m: return
    m["title"] = cmds.textField(_ui["title"], q=True, text=True); m["scene"] = cmds.textField(_ui["scene"], q=True, text=True)
    m["bsp"] = cmds.textField(_ui["bsp"], q=True, text=True); m["rotation"] = cmds.checkBox(_ui["rot"], q=True, value=True)
    nx = cmds.optionMenu(_ui["next"], q=True, value=True); m["next"] = "" if nx == "(end)" else nx
    m["objectives"] = [l.strip() for l in cmds.scrollField(_ui["obj"], q=True, text=True).splitlines() if l.strip()]
    m["notes"] = cmds.scrollField(_ui["notes"], q=True, text=True)
    mp.save(_data()); _fill_tree()

def _add(sub=False):
    r = cmds.promptDialog(title="New %s" % ("sub-level" if sub else "map"), message="Map name (e.g. dod_myvalley):", button=["Add", "Cancel"], defaultButton="Add", cancelButton="Cancel")
    if r != "Add": return
    name = cmds.promptDialog(q=True, text=True).strip()
    if not name: return
    try: mp.add(_data(), name, parent=_sel if (sub and _sel) else "", bsp=name, title=name)
    except ValueError as e: cmds.warning(str(e)); return
    mp.save(_data()); globals()["_sel"] = name; _fill_tree()

def _remove(*_):
    if _sel and cmds.confirmDialog(title="Remove", message="Remove %s from the project? (the scene file is not deleted)" % _sel, button=["Remove", "Cancel"]) == "Remove":
        mp.remove(_data(), _sel); mp.save(_data()); _fill_tree()

def _browse(*_):
    f = cmds.fileDialog2(fileMode=1, caption="Maya scene for this map", fileFilter="Maya (*.mb *.ma)")
    if f: cmds.textField(_ui["scene"], e=True, text=f[0]); _apply()

_busy = [False]
def _open_entry(name):
    if _busy[0]: cmds.warning("a map is already loading"); return
    _busy[0] = True
    try: _open_entry_impl(name)
    finally: _busy[0] = False

def _open_entry_impl(name):
    m = mp.get(_data(), name)
    if not m: return
    if m["scene"] and os.path.exists(m["scene"]):
        cur = cmds.file(q=True, sceneName=True)
        if cur and os.path.abspath(cur) == os.path.abspath(m["scene"]): cmds.inViewMessage(amg="%s is already open" % name, pos="topCenter", fade=True); return
        if cmds.file(q=True, modified=True):
            c = cmds.confirmDialog(title="Open %s" % name, message="The current scene has unsaved changes.", button=["Save and open", "Discard and open", "Cancel"])
            if c == "Cancel": return
            if c == "Save and open": cmds.file(save=True)
        cmds.file(m["scene"], open=True, force=True); _fill_tree(); return
    bsp = m["bsp"] or m["name"]                                   # no scene: bring in the stock BSP as a locked reference
    import maya_refmap
    if not os.path.exists(os.path.expanduser("~/.cache/gsbridge/ref/%s.json" % bsp)):
        cmds.warning("%s is not baked: run  python3 %s %s" % (bsp, os.path.join(HERE, "bsp_bake.py"), bsp)); return
    maya_refmap.load(bsp); _fill_tree()

def _reference(*_):
    m = mp.get(_data(), _sel)
    if m and m["scene"] and os.path.exists(m["scene"]): cmds.file(m["scene"], reference=True, namespace=m["name"]); _fill_tree()
    else: _open_entry(_sel)

def _export(*_):
    out, ords = mp.export(_data()); cmds.inViewMessage(amg="Exported rotation (%s) to %s" % (", ".join(ords), out), pos="topCenter", fade=True)

def _unref(*_):
    m = mp.get(_data(), _sel)
    if m: __import__("maya_refmap").unload(m["bsp"] or m["name"]); _fill_tree()

def _build():
    cmds.columnLayout(adjustableColumn=True, rowSpacing=4)
    _ui["problems"] = cmds.text(label="", align="left", height=22, backgroundColor=(0.2, 0.3, 0.2))
    _ui["tree"] = cmds.treeView(height=170, numberOfButtons=0, allowReparenting=False, selectCommand=_select, itemDblClickCommand=lambda item, *a: _open_entry(item))
    cmds.rowLayout(numberOfColumns=5, adjustableColumn=1)
    cmds.button(label="Open / load", command=lambda *_a: _open_entry(_sel)); cmds.button(label="Reference scene", command=_reference)
    cmds.button(label="Unload ref", command=_unref); cmds.button(label="+ Map", command=lambda *_a: _add(False)); cmds.button(label="+ Sub", command=lambda *_a: _add(True))
    cmds.setParent("..")
    cmds.frameLayout(label="Selected map", collapsable=False, marginWidth=6)
    cmds.rowColumnLayout(numberOfColumns=2, columnWidth=[(1, 70), (2, 240)], columnAttach=[(1, "right", 4)])
    cmds.text(label="Name"); _ui["name"] = cmds.textField(enable=False)
    cmds.text(label="Title"); _ui["title"] = cmds.textField(changeCommand=_apply)
    cmds.text(label="Scene"); cmds.rowLayout(numberOfColumns=2, adjustableColumn=1); _ui["scene"] = cmds.textField(changeCommand=_apply); cmds.button(label="…", width=26, command=_browse); cmds.setParent("..")
    cmds.text(label="BSP"); _ui["bsp"] = cmds.textField(changeCommand=_apply, annotation="stock BSP name (maps/<name>.bsp); dod_live for the Maya-built map")
    cmds.text(label="Next map"); _ui["next"] = cmds.optionMenu(changeCommand=_apply)
    cmds.text(label=""); _ui["rot"] = cmds.checkBox(label="In the map rotation", value=True, changeCommand=_apply)
    cmds.setParent("..")
    cmds.text(label="Mission objectives (one per line)", align="left"); _ui["obj"] = cmds.scrollField(height=60, wordWrap=True, changeCommand=_apply)
    cmds.text(label="Notes", align="left"); _ui["notes"] = cmds.scrollField(height=40, wordWrap=True, changeCommand=_apply)
    cmds.setParent("..")
    cmds.rowLayout(numberOfColumns=3, adjustableColumn=1)
    cmds.button(label="Export rotation (mapcycle / maps.lst / progression)", command=_export, backgroundColor=(0.25, 0.45, 0.3))
    cmds.button(label="Remove", command=_remove); cmds.button(label="Refresh", command=lambda *_a: (_reload(), _fill_tree()))
    cmds.setParent("..")
    _fill_tree()

def _reload():
    global _d
    _d = None

def _ui_script(): _reload(); _build()

def show(*_):
    if cmds.workspaceControl(WCTL, q=True, exists=True): cmds.workspaceControl(WCTL, e=True, restore=True); return WCTL
    tab = next((c for c in ("gsObjMgrCtl", "gsBridgeWin", "AttributeEditor") if cmds.workspaceControl(c, q=True, exists=True)), "AttributeEditor")
    cmds.workspaceControl(WCTL, label="Map Manager", retain=False, initialWidth=380, minimumWidth=300, tabToControl=(tab, -1),
                          uiScript="import maya_mapmgr; maya_mapmgr._ui_script()")
    return WCTL


# every entry point is guarded: an error becomes a Maya warning, never an exception in Maya's event loop
for _n in ['_fill_tree', '_select', '_apply', '_add', '_remove', '_browse', '_open_entry', '_reference', '_export', '_unref', '_build', '_ui_script', 'show']:
    globals()[_n] = safe(globals()[_n])
