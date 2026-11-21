"""New-map wizard: a playable template (sealed room, 4 spawns per team, doddetect, two control points with capture areas,
sky + lights) with the map's WAD/sky settings, saved under ~/gsbridge/maps/<name>.mb and added to the Map Manager project.

    import maya_newmap; maya_newmap.dialog()          # or maya_newmap.create("dod_myvalley", wad="dod_caen.wad", sky="killertomato")
GoldSrc menu > New Map calls dialog().
"""
import glob, json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import maya.cmds as cmds
from maya_safe import safe
CONTENT = "/home/tim/Desktop/dod-gc/content/dod"
MAPS = os.path.expanduser("~/gsbridge/maps")

def wads():
    return sorted(os.path.basename(p) for p in glob.glob(CONTENT + "/dod_*.wad")) or ["dod_caen.wad"]

def skies():
    return sorted({os.path.basename(p)[:-6] for p in glob.glob(CONTENT + "/gfx/env/*ft.tga")}) or ["desert"]

def _pick_textures(wad):
    """(floor, wall, ceiling) from the textures of this WAD that we have extracted."""
    try: idx = json.load(open(os.path.expanduser("~/.cache/gsbridge/tex/index.json")))
    except Exception: idx = {}
    names = [n for n, v in idx.items() if v.get("wad") == wad and not n.startswith(("{", "!", "-", "+")) and v["w"] >= 64]
    def find(*keys, default=None):
        for k in keys:
            for n in names:
                if k in n.lower(): return n
        return default
    fb = names[0] if names else "m_wall11"
    return find("floor", "grass", "dirt", "sand", default=fb), find("wall", "brick", "cement", default=fb), find("ceil", "white", "roof", default=fb)

def _brush(name, mn, mx, tex, parent):
    import maya_materials as mm
    c = cmds.polyCube(name=name, w=mx[0]-mn[0], h=mx[1]-mn[1], d=mx[2]-mn[2])[0]
    cmds.move((mn[0]+mx[0])/2, (mn[1]+mx[1])/2, (mn[2]+mx[2])/2, c); mm.assign(c, tex); cmds.parent(c, parent); return c

def _ent(name, cls, pos, yaw=None, **keys):
    l = cmds.spaceLocator(name=name)[0]; cmds.move(*pos, l)
    cmds.addAttr(l, ln="gs_classname", dt="string"); cmds.setAttr(l + ".gs_classname", cls, type="string")
    if yaw is not None: cmds.addAttr(l, ln="gs_yaw", at="long"); cmds.setAttr(l + ".gs_yaw", yaw)
    for k, v in keys.items(): cmds.addAttr(l, ln="gs_key_" + k, dt="string"); cmds.setAttr(l + ".gs_key_" + k, str(v), type="string")
    return l

def create(name, wad="dod_caen.wad", sky="", size=1024, height=256, save=True):
    import maya_materials as mm, maya_export as me
    name = name.strip().lower().replace(" ", "_")
    if not name.startswith("dod_"): name = "dod_" + name
    cmds.file(new=True, force=True)
    cmds.upAxis(axis="z", rotateView=True); cmds.currentUnit(linear="cm"); me.UNITS_PER_CM = 1.0
    floor, wall, ceil = _pick_textures(wad)
    for t in (floor, wall, ceil):
        try: mm.material(t)
        except Exception: pass
    st = cmds.group(empty=True, name="gs_settings")
    for a, v in (("gs_wad", wad), ("gs_skyname", sky), ("gs_message", name)):
        cmds.addAttr(st, ln=a, dt="string"); cmds.setAttr(st + "." + a, v, type="string")
    w = cmds.group(empty=True, name="gs_world"); h = size // 2; T = 16
    _brush("floor", (-h-T, -h-T, -T), (h+T, h+T, 0), floor, w); _brush("ceiling", (-h-T, -h-T, height), (h+T, h+T, height+T), ceil, w)
    _brush("wallW", (-h-T, -h-T, 0), (-h, h+T, height), wall, w); _brush("wallE", (h, -h-T, 0), (h+T, h+T, height), wall, w)
    _brush("wallS", (-h, -h-T, 0), (h, -h, height), wall, w); _brush("wallN", (-h, h, 0), (h, h+T, height), wall, w)
    # spawns: feet positions (export adds the player height); Allies west facing east, Axis east facing west
    for i in range(4):
        y = -90 + i * 60
        _ent("allies_spawn%d" % (i + 1), "info_player_allies", (-h + 90, y, 0), yaw=0)
        _ent("axis_spawn%d" % (i + 1), "info_player_axis", (h - 90, y, 0), yaw=180)
    _ent("detect", "info_doddetect", (0, 0, 0))
    _ent("cp_master", "dod_control_point_master", (0, 0, 8), targetname="the_cp_master", scoring_type="0")
    for i, x in enumerate((-h // 3, h // 3)):
        _ent("cp%d" % (i + 1), "dod_control_point", (x, 0, 0), targetname="cp%d" % (i + 1), point_name="Flag %d" % (i + 1), point_index=i)
        a = cmds.polyCube(name="capture_area%d" % (i + 1), w=96, h=96, d=96)[0]; cmds.move(x, 0, 48, a)
        mm.assign(a, "NULL") if True else None
        cmds.addAttr(a, ln="gs_classname", dt="string"); cmds.setAttr(a + ".gs_classname", "dod_capture_area", type="string")
        for k, v in (("area_allies_cancap", "1"), ("area_axis_cancap", "1"), ("area_time_to_cap", "5"), ("target", "cp%d" % (i + 1))):
            cmds.addAttr(a, ln="gs_key_" + k, dt="string"); cmds.setAttr(a + ".gs_key_" + k, v, type="string")
    _ent("sun", "light_environment", (0, 0, height - 8), _light="255 245 220 180", pitch="-60")
    _ent("lamp1", "light", (-h // 2, 0, height - 32), _light="255 240 200 250"); _ent("lamp2", "light", (h // 2, 0, height - 32), _light="255 240 200 250")
    cmds.select(clear=True)
    if save:
        os.makedirs(MAPS, exist_ok=True); path = os.path.join(MAPS, name + ".mb")
        cmds.file(rename=path); cmds.file(save=True, type="mayaBinary")
        try:
            import mapproject as mp
            d = mp.load()
            if not mp.get(d, name): mp.add(d, name, title=name, scene=path, bsp=name, notes="Created with the New Map wizard (wad %s, sky %s)" % (wad, sky or "default"))
            else: mp.get(d, name)["scene"] = path
            mp.save(d)
        except Exception as e: cmds.warning("[gsbridge] could not add %s to the Map Manager project: %s" % (name, e))
        me.install()
    try: cmds.viewFit(allObjects=True)
    except Exception: pass      # no viewport (batch mode)
    print("[gsbridge] new map %s created (%s, sky %s); save pushes it to the game" % (name, wad, sky or "default"))
    return name

@safe
def dialog(*_):
    if cmds.file(q=True, modified=True):
        if cmds.confirmDialog(title="New map", message="The current scene has unsaved changes. Discard them and start a new map?", button=["Discard and continue", "Cancel"], cancelButton="Cancel") == "Cancel": return
    if cmds.window("gsNewMapWin", exists=True): cmds.deleteUI("gsNewMapWin")
    w = cmds.window("gsNewMapWin", title="New DoD map", sizeable=False, widthHeight=(340, 190))
    cmds.columnLayout(adjustableColumn=True, rowSpacing=6, columnOffset=("both", 10))
    cmds.separator(height=6, style="none")
    nm = cmds.textFieldGrp(label="Map name", text="dod_myvalley", columnWidth2=(80, 220))
    wd = cmds.optionMenuGrp(label="Texture WAD", columnWidth2=(80, 220))
    for x in wads(): cmds.menuItem(label=x)
    cmds.optionMenuGrp(wd, e=True, value="dod_caen.wad" if "dod_caen.wad" in wads() else wads()[0])
    sk = cmds.optionMenuGrp(label="Sky", columnWidth2=(80, 220))
    cmds.menuItem(label="(default)")
    for x in skies(): cmds.menuItem(label=x)
    def go(*_):
        n = cmds.textFieldGrp(nm, q=True, text=True); wad = cmds.optionMenuGrp(wd, q=True, value=True); s = cmds.optionMenuGrp(sk, q=True, value=True)
        cmds.deleteUI(w); create(n, wad, "" if s == "(default)" else s)
    cmds.rowLayout(numberOfColumns=2, adjustableColumn=1); cmds.button(label="Create map", command=go, height=30, backgroundColor=(0.25, 0.45, 0.3)); cmds.button(label="Cancel", command=lambda *_: cmds.deleteUI(w))
    cmds.showWindow(w)
