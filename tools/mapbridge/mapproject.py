"""Map project model (pure python, no Maya): the game's levels, sub-levels, progression and missions.

~/gsbridge/project.json:
{ "maps": [ {"name": "dod_live", "title": "Live test hall", "scene": "/path/demo_room.mb", "bsp": "dod_live",
             "parent": "", "next": "dod_caen", "rotation": true, "objectives": ["Capture the flag"], "notes": ""} ... ] }
  parent   = sub-level of this map ("" for a top-level map)
  next     = map the game goes to when this one ends ("" = end of campaign / wraps to the first in the rotation)
  rotation = listed in the generated mapcycle
"""
import json, os
PATH = os.path.expanduser("~/gsbridge/project.json")
STOCK = "/home/tim/Desktop/dod-gc/content/dod/maps"
FIELDS = {"name": "", "title": "", "scene": "", "bsp": "", "parent": "", "next": "", "rotation": True, "objectives": [], "notes": ""}

def load(path=PATH):
    try: d = json.load(open(path))
    except (OSError, ValueError): d = {"maps": []}
    for m in d["maps"]:
        for k, v in FIELDS.items(): m.setdefault(k, list(v) if isinstance(v, list) else v)
    return d

def save(d, path=PATH):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    json.dump(d, open(path + ".tmp", "w"), indent=1); os.replace(path + ".tmp", path)

def get(d, name): return next((m for m in d["maps"] if m["name"] == name), None)

def add(d, name, **kw):
    if get(d, name): raise ValueError("map %r exists" % name)
    m = dict({k: (list(v) if isinstance(v, list) else v) for k, v in FIELDS.items()}, name=name, **kw); d["maps"].append(m); return m

def remove(d, name):
    d["maps"] = [m for m in d["maps"] if m["name"] != name]
    for m in d["maps"]:
        if m["parent"] == name: m["parent"] = ""
        if m["next"] == name: m["next"] = ""

def children(d, parent): return [m for m in d["maps"] if m["parent"] == parent]

def order(d):
    """Progression order: follow `next` from the first top-level map, then any leftover top-level maps in file order."""
    tops = [m for m in d["maps"] if not m["parent"]]; seen = []; names = {m["name"] for m in tops}
    targets = {m["next"] for m in tops if m["next"]}
    starts = [m for m in tops if m["name"] not in targets] or tops[:1]
    for s in starts:
        cur = s
        while cur and cur["name"] not in seen:
            seen.append(cur["name"]); cur = get(d, cur["next"]) if cur["next"] in names else None
    seen += [m["name"] for m in tops if m["name"] not in seen]
    return seen

def problems(d):
    out = []; names = {m["name"] for m in d["maps"]}
    for m in d["maps"]:
        if m["parent"] and m["parent"] not in names: out.append("%s: parent %r is not in the project" % (m["name"], m["parent"]))
        if m["next"] and m["next"] not in names: out.append("%s: next %r is not in the project" % (m["name"], m["next"]))
        if m["name"] != "dod_live" and not os.path.exists(os.path.join(STOCK, (m["bsp"] or m["name"]) + ".bsp")) and not m["scene"]:
            out.append("%s: no stock BSP and no Maya scene" % m["name"])
        if m["scene"] and not os.path.exists(m["scene"]): out.append("%s: scene %s is missing" % (m["name"], m["scene"]))
    return out

def export(d, outdir=os.path.expanduser("~/gsbridge/export")):
    """Write what the game reads: mapcycle.txt (rotation order), maps.lst (all maps), progression.json (next/sub-levels/objectives)."""
    os.makedirs(outdir, exist_ok=True); ords = [n for n in order(d) if get(d, n)["rotation"]]
    open(os.path.join(outdir, "mapcycle.txt"), "w").write("".join((get(d, n)["bsp"] or n) + "\n" for n in ords))
    open(os.path.join(outdir, "maps.lst"), "w").write("".join((m["bsp"] or m["name"]) + "\n" for m in d["maps"]))
    prog = {m["name"]: {"title": m["title"], "next": m["next"], "parent": m["parent"], "objectives": m["objectives"],
                        "sublevels": [c["name"] for c in children(d, m["name"])]} for m in d["maps"]}
    json.dump({"order": order(d), "maps": prog}, open(os.path.join(outdir, "progression.json"), "w"), indent=1)
    return outdir, ords

def seed():
    """First-run project: the live test map plus a short stock chain to try the tree with."""
    d = {"maps": []}
    add(d, "dod_live", title="Live test hall", scene="/home/tim/Desktop/dod-gc-mapbridge/tools/mapbridge/demo_room.mb", bsp="dod_live",
        next="dod_charlie", objectives=["Walk the hall", "Check textures, entities and render modes"])
    add(d, "dod_charlie", title="Charlie (Omaha)", next="dod_caen", objectives=["Capture the beach flags", "Destroy the mortar pits"])
    add(d, "dod_caen", title="Caen", objectives=["Hold the town square"])
    add(d, "dod_charlie_bunker", title="Charlie: bunker interior", parent="dod_charlie", bsp="dod_charlie", rotation=False,
        objectives=["Clear the bunker"], notes="sub-level placeholder: same stock BSP until it gets its own scene")
    return d
