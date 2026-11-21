"""Render-parity testbed, built with the SAME Maya exporter path (run under mayapy, standalone):
    mayapy build_testbed.py <workdir> [features]      (needs a batch mayapy: Maya 2026 on the host is gone and 2027 only runs
    sandboxed, so for now run it through the live Maya session: exec the file with sys.argv set, or use selftest_map.py)
A hall with one station per feature, all visible from the spawn at the near end:
  1 masked '{' fence (alpha test)   2 func_wall rendermode 2 (translucent)   3 func_wall rendermode 5 (additive)
  4 func_water ('!' texture)        5 func_illusionary rendermode 4          6 env_model props (+ lights of 3 colours)
Writes <workdir>/live.map and testbed.mb."""
import sys, os
import maya.standalone; maya.standalone.initialize()
import maya.cmds as cmds
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import maya_export as me, maya_materials as mm
FEAT = (sys.argv[2] if len(sys.argv) > 2 else "fence,glass,glow,water,ghost,props,lights").split(",")
me.UNITS_PER_CM = 1.0; me.WORKDIR = sys.argv[1]; os.makedirs(me.WORKDIR, exist_ok=True)
cmds.upAxis(axis="z", rotateView=False); cmds.currentUnit(linear="cm")
cmds.group(empty=True, name="gs_world_test")
def box(name, mn, mx, tex, parent="gs_world_test"):
    c = cmds.polyCube(name=name, w=mx[0]-mn[0], h=mx[1]-mn[1], d=mx[2]-mn[2])[0]
    cmds.move((mn[0]+mx[0])/2, (mn[1]+mx[1])/2, (mn[2]+mx[2])/2, c); mm.assign(c, tex)
    if parent: cmds.parent(c, parent)
    return c
def attrs(o, cls, **kw):
    cmds.addAttr(o, ln="gs_classname", dt="string"); cmds.setAttr(o + ".gs_classname", cls, type="string")
    for k, v in kw.items(): cmds.addAttr(o, ln="gs_key_" + k, dt="string"); cmds.setAttr(o + ".gs_key_" + k, str(v), type="string")
def ent(name, cls, pos, **kw):
    l = cmds.spaceLocator(name=name)[0]; cmds.move(*pos, l); attrs(l, cls, **kw); return l
# shell
box("floor", (-640, -240, -16), (640, 1100, 0), "m_floor1"); box("ceil", (-640, -240, 272), (640, 1100, 288), "m_whitewash2")
box("wallL", (-656, -240, -16), (-640, 1100, 288), "m_wall11"); box("wallR", (640, -240, -16), (656, 1100, 288), "m_wall11")
box("wallN", (-640, -256, -16), (640, -240, 288), "m_wall11"); box("wallF", (-640, 1100, -16), (640, 1116, 288), "m_wall11")
# backdrop: a bright striped wall behind the stations so transparency is obvious
box("backdrop", (-620, 880, 0), (620, 896, 256), "c2_wood")
X = [-170, -90, 0, 90, 170]; Y = 700   # centre lane (x=0) stays clear: the test player auto-runs +Y from x=0
# 1 masked rail across the whole hall ('{' texture: index 255 transparent). It is solid, so the auto-running test player
#   stops against it (y=384) with every station in view, instead of running into the far wall.
if "fence" in FEAT:
    box("curb", (-640, 500, 0), (640, 510, 20), "m_floor1")                 # low solid stopper: the auto-running player halts at y=484
    box("fence", (-100, 600, 0), (100, 604, 128), "{m_railing1")             # free-standing masked panel, 116 units in front of the stopper
# 2 translucent func_wall (rendermode 2 = texture alpha, renderamt 128)
if "glass" in FEAT:
    b = box("glass", (X[0]-32, Y, 0), (X[0]+32, Y+8, 100), "m_wall11", parent=None); attrs(b, "func_wall", rendermode=2, renderamt=128, rendercolor="0 0 0")
# 3 additive func_wall (rendermode 5, renderamt 200)
if "glow" in FEAT:
    b = box("glow", (X[1]-32, Y, 0), (X[1]+32, Y+8, 100), "c2_wood", parent=None); attrs(b, "func_wall", rendermode=5, renderamt=200, rendercolor="0 0 0")
# 4 water: func_water with a '!' texture
if "water" in FEAT:
    b = box("pool", (X[3]-56, Y-60, 0), (X[3]+56, Y+60, 24), "!K_water1", parent=None); attrs(b, "func_water", WaveHeight="3.2", skin="-3")
# 5 alpha-test func_illusionary on a normal texture (rendermode 4: index 255 pixels vanish)
if "ghost" in FEAT:
    b = box("ghost", (X[4]-32, Y, 0), (X[4]+32, Y+8, 100), "m_wall11", parent=None); attrs(b, "func_illusionary", rendermode=4, renderamt=255, rendercolor="0 0 0", skin="-1")
# props
for i, (mdl, x) in enumerate((("models/mapmodels/sandbags.mdl", -220), ("models/mapmodels/pine_tree.mdl", 220)) if "props" in FEAT else ()):
    ent("prop%d" % i, "env_model", (x, 330, 0), model=mdl)
# lights: red, blue, warm white
if "lights" in FEAT:
    ent("lt_r", "light", (-480, 400, 160), _light="255 40 40 400")
    ent("lt_b", "light", (480, 400, 160), _light="40 80 255 400")
    ent("lt_w", "light", (0, 200, 200), _light="255 240 200 500")
# spawns face +Y (yaw 90); locators are FEET
for n, cls, p, yaw in (("sp_a", "info_player_allies", (0, -150, 0), 90), ("sp_x", "info_player_axis", (0, -120, 0), 90)):
    l = ent(n, cls, p); cmds.addAttr(l, ln="gs_yaw", at="long"); cmds.setAttr(l + ".gs_yaw", yaw)
ent("detect", "info_doddetect", (0, 0, 0))
me.export_now()
cmds.file(rename=os.path.join(me.WORKDIR, "testbed.mb")); cmds.file(save=True, type="mayaBinary")
print("TESTBED OK", me.WORKDIR)
