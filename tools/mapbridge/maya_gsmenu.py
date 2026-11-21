"""GoldSrc menu + Windows-menu entries + shelf button for the dockable panels.
Called from userSetup.py at Maya start (see install_usersetup), or by hand: maya_gsmenu.install().
Panels are workspaceControls (dock anywhere, saved in Maya's layout, restored on relaunch)."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path: sys.path.insert(0, HERE)
import maya.cmds as cmds, maya.mel as mel

def _open_bridge(*_): import maya_panel; maya_panel.show()
def _open_objmgr(*_): import maya_objmgr; maya_objmgr.show()
def _open_both(*_): _open_bridge(); _open_objmgr()
def _push(*_): import maya_export; maya_export.export_now()

def install():
    main = mel.eval("$tmp=$gMainWindow")
    if cmds.menu("GoldSrcMenu", exists=True): cmds.deleteUI("GoldSrcMenu")
    cmds.menu("GoldSrcMenu", label="GoldSrc", parent=main, tearOff=True)
    for lab, cmd in (("GoldSrc Bridge", _open_bridge), ("Object Manager", _open_objmgr), ("Open both panels", _open_both)):
        cmds.menuItem(label=lab, command=cmd, parent="GoldSrcMenu")
    cmds.menuItem(divider=True, parent="GoldSrcMenu")
    cmds.menuItem(label="Export + Push", command=_push, parent="GoldSrcMenu")
    # Windows menu entries
    try:
        wm = mel.eval("$tmp=$gMainWindowMenu") if mel.eval("exists \"$gMainWindowMenu\"") else "mainWindowMenu"
    except Exception: wm = "mainWindowMenu"
    for name in ("gsWinBridge", "gsWinObjMgr"):
        if cmds.menuItem(name, exists=True): cmds.deleteUI(name)
    try:
        cmds.menuItem("gsWinBridge", label="GoldSrc Bridge", command=_open_bridge, parent=wm)
        cmds.menuItem("gsWinObjMgr", label="GoldSrc Object Manager", command=_open_objmgr, parent=wm)
    except Exception: pass
    # shelf button
    try:
        shelf = mel.eval("$tmp=$gShelfTopLevel"); cur = cmds.tabLayout(shelf, q=True, selectTab=True)
        if not [b for b in cmds.shelfLayout(cur, q=True, childArray=True) or [] if cmds.shelfButton(b, q=True, label=True) == "GoldSrc"]:
            cmds.shelfButton(parent=cur, label="GoldSrc", annotation="Open GoldSrc Bridge + Object Manager", image1="commandButton.png",
                             command="import maya_gsmenu; maya_gsmenu._open_both()", sourceType="python")
    except Exception: pass

BEGIN, END = "# >>> goldsrc-bridge >>>", "# <<< goldsrc-bridge <<<"
def install_usersetup(path=None):
    """Append (once) a guarded block to userSetup.py so the menu exists at every Maya start."""
    path = path or os.path.join(cmds.internalVar(userAppDir=True), "scripts", "userSetup.py")
    old = open(path).read() if os.path.exists(path) else ""
    if BEGIN in old: return path
    os.makedirs(os.path.dirname(path), exist_ok=True)
    block = "\n%s\nimport sys, maya.cmds as _c\nsys.path.insert(0, %r)\n_c.evalDeferred('import maya_gsmenu; maya_gsmenu.install()')\n%s\n" % (BEGIN, HERE, END)
    open(path, "a").write(block); return path
