#!/usr/bin/env python3
"""Object Manager catalog: stock DoD classnames (as used by the 22 stock maps' entity lumps)
grouped into tabs, with generated icons. Counts come from the real BSPs, so the list is
what the game actually ships with.   python3 objmgr_catalog.py   -> ~/.cache/gsbridge/objmgr/"""
import collections, glob, json, os, re, struct
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

def icon(name, tab, path):
    im = Image.new("RGBA", (64, 64), (0, 0, 0, 0)); dr = ImageDraw.Draw(im)
    dr.rounded_rectangle((2, 2, 61, 61), 8, fill=COLORS[tab], outline="#ddd", width=2)
    base = name.split("_", 1)[-1] if "_" in name else name
    words = [w for w in name.split("_") if w not in ("info", "dod", "env", "weapon")] or [name]
    f = ImageFont.load_default(size=17)
    for i, w in enumerate(words[-2:]):
        dr.text((32, 22 + i * 20 if len(words) > 1 else 32), w[:6].upper(), fill="white", anchor="mm", font=f)
    if "allies" in name: dr.rectangle((6, 52, 57, 57), fill="#7fc4ff")
    if "axis" in name: dr.rectangle((6, 52, 57, 57), fill="#d0d0d0")
    im.save(path)

def main():
    os.makedirs(OUT + "/icons", exist_ok=True); cnt, models = scan(); cat = []
    for tab, names in TABS.items():
        for n in names:
            icon(n, tab, "%s/icons/%s.png" % (OUT, n))
            cat.append({"tab": tab, "class": n, "brush": BRUSH(n), "stock_count": cnt.get(n, 0), "keys": DEFAULTS.get(n, {})})
    for i, (mdl, c) in enumerate(models.most_common(60)):   # Props: the models stock maps really use
        nm = "env_model: " + os.path.basename(mdl)
        icon("env_model", "Props", "%s/icons/m%d.png" % (OUT, i))
        cat.append({"tab": "Props", "class": "env_model", "label": os.path.basename(mdl), "icon": "m%d" % i, "brush": False,
                    "stock_count": c, "keys": {"model": mdl}})
    json.dump(cat, open(OUT + "/catalog.json", "w"), indent=0)
    print(len(cat), "entries;", len(models), "distinct env_model models; stock classes seen:", len(cnt))
if __name__ == "__main__": main()
