#!/usr/bin/env python3
"""Object Manager catalog: stock DoD classnames (as used by the 22 stock maps' entity lumps)
grouped into tabs, with generated icons. Counts come from the real BSPs, so the list is
what the game actually ships with.   python3 objmgr_catalog.py   -> ~/.cache/gsbridge/objmgr/"""
import collections, glob, json, math, os, re, struct
from PIL import Image, ImageDraw, ImageFont
OUT = os.path.expanduser("~/.cache/gsbridge/objmgr")
MAPS = "/home/tim/Desktop/dod-gc/content/dod/maps"
TABS = collections.OrderedDict([
 ("Spawns", ["info_player_allies", "info_player_axis", "info_player_start", "info_player_observer",
             "info_initial_player_allies", "info_initial_player_axis"]),
 ("Control Points", ["dod_control_point", "dod_control_point_master", "dod_capture_area", "dod_object",
                     "dod_score_ent", "dod_location", "dod_round_timer", "dod_point_relay", "info_doddetect"]),
 ("Weapons", ["weapon_bazooka", "weapon_pschreck", "weapon_piat"]),
 ("Props", ["env_model", "cycler_sprite", "env_sprite", "env_glow", "infodecal"]),
 ("Lighting", ["light", "light_spot", "light_environment"]),
 ("Sounds", ["ambient_generic", "env_sound"]),
 ("Triggers", ["trigger_multiple", "trigger_hurt", "trigger_push", "trigger_auto", "trigger_relay",
               "trigger_changetarget", "dod_trigger_sandbag", "func_ladder"]),
 ("Brushes", ["func_wall", "func_illusionary", "func_breakable", "func_door", "func_door_rotating",
              "func_water", "func_button", "func_train", "func_wall_toggle", "func_conveyor"]),
 ("Logic", ["multi_manager", "game_text", "game_counter", "game_score", "path_corner", "path_track", "info_target",
            "env_message", "env_explosion", "env_shake", "env_spark", "env_beam", "env_shooter"]),
])
COLORS = {"Spawns": "#3d7a3d", "Control Points": "#a33", "Weapons": "#8a6d2a", "Props": "#4a6a8a", "Lighting": "#b89a20",
          "Sounds": "#6a4a8a", "Triggers": "#2a8a8a", "Brushes": "#666", "Logic": "#555"}
# brush entities need a mesh in Maya; the rest are point entities
BRUSH = lambda c: c.startswith(("func_", "trigger_")) or c in ("dod_capture_area", "dod_trigger_sandbag")
DEFAULTS = {"light": {"_light": "255 255 255 200"}, "light_environment": {"_light": "255 255 255 200", "pitch": "-45"},
            "ambient_generic": {"message": "ambience/", "health": "10", "pitch": "100"}}

def scan():
    cnt = collections.Counter(); models = collections.Counter()
    for f in glob.glob(MAPS + "/*.bsp"):
        d = open(f, "rb").read(); o, l = struct.unpack("<ii", d[4:12]); t = d[o:o+l].decode("latin1")
        for blk in re.findall(r"\{[^{}]*\}", t):
            m = re.search(r'"classname" "([^"]+)"', blk)
            if m:
                cnt[m.group(1)] += 1
                if m.group(1) == "env_model":
                    mm = re.search(r'"model" "([^"]+)"', blk)
                    if mm: models[mm.group(1)] += 1
    return cnt, models

def _tile():
    S = 80; im = Image.new("RGBA", (S, S)); px = im.load()
    for y in range(S):
        for x in range(S): px[x, y] = (int(86 - 26 * y / S),) * 3 + (255,)     # same neutral gradient as the model renders
    return im

def icon(name, tab, path):
    """Clean category icon in the same neutral style: simple white glyph + team/category accent."""
    im = _tile(); dr = ImageDraw.Draw(im); acc = COLORS[tab]; W = "#f2f2f2"
    if "allies" in name: acc = "#6fb2ff"
    if "axis" in name: acc = "#d9d9d9"
    if name.startswith("info_player") or name.startswith("info_initial"):
        dr.ellipse((33, 16, 47, 30), fill=W); dr.polygon([(28, 33), (52, 33), (56, 58), (24, 58)], fill=W)   # person
        if "observer" in name: dr.ellipse((50, 14, 66, 30), outline=W, width=3)
        dr.rectangle((14, 64, 66, 69), fill=acc)
    elif name.startswith("dod_control_point") or name in ("dod_object", "dod_score_ent", "dod_point_relay"):
        dr.line((26, 14, 26, 66), fill=W, width=4); dr.polygon([(28, 16), (60, 26), (28, 38)], fill=acc)    # flag
    elif name == "dod_capture_area":
        dr.rectangle((14, 28, 66, 60), outline=W, width=3); dr.line((24, 34, 56, 54), fill=acc, width=3); dr.line((56, 34, 24, 54), fill=acc, width=3)
    elif name.startswith("trigger_") or name.startswith("dod_trigger") or name in ("func_ladder",):
        dr.rectangle((14, 24, 58, 62), outline=W, width=3); dr.rectangle((22, 16, 66, 54), outline=acc, width=3)  # wire box
    elif name.startswith("light"):
        dr.ellipse((22, 14, 58, 50), fill="#ffe27a"); dr.rectangle((32, 50, 48, 60), fill=W)
        for a_ in range(0, 360, 45):
            c, s_ = math.cos(math.radians(a_)), math.sin(math.radians(a_)); dr.line((40 + 24*c, 32 + 24*s_, 40 + 32*c, 32 + 32*s_), fill="#ffe27a", width=2)
    elif name in ("ambient_generic", "env_sound"):
        dr.polygon([(18, 34), (30, 34), (44, 22), (44, 58), (30, 46), (18, 46)], fill=W)
        for r in (8, 15): dr.arc((40 - r, 40 - r, 48 + r, 40 + r), -50, 50, fill=acc, width=3)
    elif name.startswith("weapon_"):
        dr.polygon([(10, 38), (56, 38), (56, 30), (70, 30), (70, 44), (56, 44), (56, 48), (10, 48)], fill=W); dr.rectangle((10, 54, 66, 58), fill=acc)
    elif name.startswith("func_"):
        dr.rectangle((16, 22, 64, 58), fill="#9a9a9a", outline=W, width=3); dr.line((16, 40, 64, 40), fill=acc, width=3)
    elif name.startswith("env_") or name in ("cycler_sprite", "infodecal"):
        dr.polygon([(40, 14), (46, 32), (64, 32), (50, 43), (56, 62), (40, 50), (24, 62), (30, 43), (16, 32), (34, 32)], fill=acc)  # burst
    else:
        dr.ellipse((20, 20, 60, 60), outline=W, width=4); dr.line((40, 28, 40, 52), fill=acc, width=4); dr.line((28, 40, 52, 40), fill=acc, width=4)
    dr.rounded_rectangle((1, 1, 78, 78), 8, outline="#c8c8c8", width=1)
    im.save(path)

def main():
    os.makedirs(OUT + "/icons", exist_ok=True); cnt, models = scan(); cat = []
    import mdl_icons
    PLAYER = {"info_player_allies": "models/player/us-inf/us-inf.mdl", "info_initial_player_allies": "models/player/us-inf/us-inf.mdl",
              "info_player_axis": "models/player/axis-inf/axis-inf.mdl", "info_initial_player_axis": "models/player/axis-inf/axis-inf.mdl"}
    WEAPON = {"weapon_bazooka": "models/p_bazooka.mdl", "weapon_pschreck": "models/p_pschreck.mdl", "weapon_piat": "models/p_piat.mdl"}
    for tab, names in TABS.items():
        for n in names:
            icon(n, tab, "%s/icons/%s.png" % (OUT, n))
            mp = PLAYER.get(n) or WEAPON.get(n)
            if mp and os.path.exists(mdl_icons.CONTENT + "/" + mp):   # real render replaces the glyph
                ic = mdl_icons.icon_for(mp, force=True)
                if ic: os.replace("%s/icons/%s.png" % (OUT, ic), "%s/icons/%s.png" % (OUT, n))
            cat.append({"tab": tab, "class": n, "brush": BRUSH(n), "stock_count": cnt.get(n, 0), "keys": DEFAULTS.get(n, {})})
    for i, (mdl, c) in enumerate(models.most_common(60)):   # Props: the models stock maps really use
        nm = "env_model: " + os.path.basename(mdl)
        ic = mdl_icons.icon_for(mdl, force=True) if os.path.exists(mdl_icons.CONTENT + "/" + mdl) else None
        if ic: os.replace("%s/icons/%s.png" % (OUT, ic), "%s/icons/m%d.png" % (OUT, i))
        else: icon("env_model", "Props", "%s/icons/m%d.png" % (OUT, i))
        cat.append({"tab": "Props", "class": "env_model", "label": os.path.basename(mdl), "icon": "m%d" % i, "brush": False,
                    "stock_count": c, "keys": {"model": mdl}})
    json.dump(cat, open(OUT + "/catalog.json", "w"), indent=0)
    print(len(cat), "entries;", len(models), "distinct env_model models; stock classes seen:", len(cnt))
if __name__ == "__main__": main()
