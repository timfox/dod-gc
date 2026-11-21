"""DoD Object Manager for Maya (LucasArts-style): floating window, Filter box with
Add/Delete Filter, Preferences + Utilities menus, category tabs, grid of icon buttons.
Click an icon = place that entity at the selection (or the view-center ground point).

    import maya_objmgr; maya_objmgr.show()
Catalog: objmgr_catalog.py (stock DoD classnames + env_model models from the shipped maps).
Placed objects carry gs_classname / gs_key_* so maya_export writes them straight to the .map.
"""
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import maya.cmds as cmds
from maya_safe import safe, cb, Progress
WCTL = "gsObjMgrCtl"; CAT = os.path.expanduser("~/.cache/gsbridge/objmgr")
COLS = 5; _ui = {}; _cat = []; _filters = None

def _load():
    global _cat
    if not os.path.exists(CAT + "/catalog.json"):
        import objmgr_catalog; objmgr_catalog.main()
    _cat = json.load(open(CAT + "/catalog.json"))

def _ground_point():
    """World point under the center of the active 3D view on the floor plane (z=0 or y=0)."""
    try:
        import maya.OpenMayaUI as omui
        v = omui.M3dView.active3dView(); pt, vec = v.viewToWorld(v.portWidth() // 2, v.portHeight() // 2)
        ax = 2 if cmds.upAxis(q=True, axis=True) == "z" else 1
        if abs(vec[ax]) > 1e-6:
            t = -pt[ax] / vec[ax]
            if t > 0:
                p = [pt[0] + vec[0]*t, pt[1] + vec[1]*t, pt[2] + vec[2]*t]; p[ax] = 0.0; return p
    except Exception: pass
    return [0, 0, 0]

def place(entry, *_):
    sel = cmds.ls(sl=True, transforms=True)
    pos = cmds.xform(sel[0], q=True, ws=True, t=True) if sel else _ground_point()
    cls = entry["class"]
    if entry["brush"]:
        if not cmds.objExists("gs_world"): cmds.group(empty=True, name="gs_world")
        o = cmds.polyCube(name=cls + "#", w=64, h=64, d=64)[0]; z = 32
        cmds.move(pos[0], pos[1], pos[2] + z, o, absolute=True)
        if cls.startswith(("trigger_", "dod_capture", "dod_trigger", "func_ladder")):   # invisible tool texture, as stock maps use
            try:
                import maya_materials as mm; mm.assign(o, "NULL")
            except Exception: pass
    else:
        o = cmds.spaceLocator(name=cls + "#")[0]; cmds.move(*pos, o, absolute=True)
        cmds.setAttr(o + ".localScaleX", 16); cmds.setAttr(o + ".localScaleY", 16); cmds.setAttr(o + ".localScaleZ", 16)
    cmds.addAttr(o, ln="gs_classname", dt="string"); cmds.setAttr(o + ".gs_classname", cls, type="string")
    if not entry["brush"]:
        cmds.addAttr(o, ln="gs_yaw", at="long")
    for k, v in entry.get("keys", {}).items():
        a = "gs_key_" + k; cmds.addAttr(o, ln=a, dt="string"); cmds.setAttr(o + "." + a, v, type="string")
    if entry.get("keys", {}).get("model"):          # viewport-only preview of the real .mdl (never exported)
        try:
            import maya_modelpreview as mp; mp.build(o)
        except Exception as ex: cmds.warning("model preview: %s" % ex)
    cmds.select(o); print("[objmgr] placed", entry.get("label", cls), "at", [round(c) for c in pos])

def _filters_get():
    return [f for f in (cmds.optionVar(q="gsObjMgrFilters") if cmds.optionVar(exists="gsObjMgrFilters") else "").split("|") if f]

def _fill(*_):
    q = cmds.textField(_ui["filter"], q=True, text=True).strip().lower()
    for tab, grid in _ui["grids"].items():
        for ch in cmds.gridLayout(grid, q=True, childArray=True) or []: cmds.deleteUI(ch)
        cmds.setParent(grid); n = 0
        for e in _cat:
            if e["tab"] != tab: continue
            label = e.get("label", e["class"])
            if q and q not in label.lower() and q not in e["class"].lower(): continue
            ic = "%s/icons/%s.png" % (CAT, e.get("icon", e["class"]))
            cmds.iconTextButton(style="iconAndTextVertical", image1=ic, label=label.replace(".mdl", ""),
                                annotation="%s  (stock maps use it %dx)" % (e["class"], e["stock_count"]),
                                command=lambda *_a, e=e: place(e), width=88, height=84, font="smallPlainLabelFont")
            n += 1
        cmds.tabLayout(_ui["tabs"], e=True, tabLabel=(_ui["scrolls"][tab], "%s (%d)" % (tab, n)))

def _add_filter(*_):
    t = cmds.textField(_ui["filter"], q=True, text=True).strip()
    if not t: return
    fs = _filters_get()
    if t not in fs: fs.append(t)
    cmds.optionVar(sv=("gsObjMgrFilters", "|".join(fs))); _menu_filters()

def _del_filter(*_):
    t = cmds.textField(_ui["filter"], q=True, text=True).strip(); fs = [f for f in _filters_get() if f != t]
    cmds.optionVar(sv=("gsObjMgrFilters", "|".join(fs))); _menu_filters()

def _menu_filters():
    cmds.popupMenu(_ui["pop"], e=True, deleteAllItems=True)
    for f in _filters_get():
        cmds.menuItem(label=f, parent=_ui["pop"], command=lambda *_a, f=f: (cmds.textField(_ui["filter"], e=True, text=f), _fill()))
    cmds.menuItem(label="(clear)", parent=_ui["pop"], command=lambda *_a: (cmds.textField(_ui["filter"], e=True, text=""), _fill()))

def _build():
    """Build the manager UI into the current parent layout (a workspaceControl when docked)."""
    cmds.menuBarLayout()
    cmds.menu(label="Preferences")
    cmds.menuItem(label="Rebuild icons/catalog from stock maps", command=lambda *_a: (__import__("objmgr_catalog").main(), _load(), _fill()))
    cmds.menu(label="Utilities")
    cmds.menuItem(label="Select all entities", command=lambda *_a: cmds.select([n for n in cmds.ls(transforms=True) if cmds.attributeQuery("gs_classname", node=n, exists=True)]))
    cmds.menuItem(label="Refresh model previews", command=lambda *_a: __import__("maya_modelpreview").refresh_all())
    cmds.menuItem(label="Export + Push", command=lambda *_a: __import__("maya_export").export_now())
    cmds.columnLayout(adjustableColumn=True)
    cmds.rowLayout(numberOfColumns=4, adjustableColumn=2)
    cmds.text(label=" Filter: ")
    _ui["filter"] = cmds.textField(textChangedCommand=_fill, enterCommand=_fill)
    _ui["pop"] = cmds.popupMenu(parent=_ui["filter"])
    cmds.button(label="Add Filter", command=_add_filter); cmds.button(label="Delete Filter", command=_del_filter)
    cmds.setParent("..")
    _ui["tabs"] = cmds.tabLayout(innerMarginWidth=4, innerMarginHeight=4)
    _ui["grids"] = {}; _ui["scrolls"] = {}
    for tab in dict.fromkeys(e["tab"] for e in _cat):
        sc = cmds.scrollLayout(childResizable=True, horizontalScrollBarThickness=0); _ui["scrolls"][tab] = sc
        _ui["grids"][tab] = cmds.gridLayout(numberOfColumns=COLS, cellWidth=88, cellHeight=84)
        cmds.setParent(_ui["tabs"])
    for tab, sc in _ui["scrolls"].items(): cmds.tabLayout(_ui["tabs"], e=True, tabLabel=(sc, tab))
    _menu_filters(); _fill()

def _ui_script():
    """workspaceControl uiScript: also what Maya calls to restore the panel on relaunch."""
    _load(); _build()

def show(*_):
    """Open (or raise) the dockable Object Manager. Tabs next to the GoldSrc Bridge panel if it is open."""
    if cmds.workspaceControl(WCTL, q=True, exists=True):
        cmds.workspaceControl(WCTL, e=True, restore=True); return WCTL
    kw = dict(label="Object Manager", retain=False, initialWidth=430, minimumWidth=300,
              uiScript="import maya_objmgr; maya_objmgr._ui_script()")
    kw["tabToControl"] = ("gsBridgeWin" if cmds.workspaceControl("gsBridgeWin", q=True, exists=True) else "AttributeEditor", -1)
    cmds.workspaceControl(WCTL, **kw); return WCTL


# every entry point is guarded: an error becomes a Maya warning, never an exception in Maya's event loop
for _n in ['place', '_fill', '_add_filter', '_del_filter', '_build', '_ui_script', 'show']:
    globals()[_n] = safe(globals()[_n])
