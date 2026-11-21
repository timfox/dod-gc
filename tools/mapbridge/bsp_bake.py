#!/usr/bin/env python3
"""Stock BSP (v30) -> reference mesh JSON for Maya (runs in system python: needs numpy/PIL).
    bsp_bake.py dod_charlie            -> ~/.cache/gsbridge/ref/dod_charlie.json
World + brush-model faces grouped per texture (real UVs from texinfo, real WAD/embedded textures),
tool textures (null/clip/trigger/sky/origin/hint/skip) dropped, plus the entity list."""
import json, os, re, struct, sys
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wad_textures
OUT = os.path.expanduser("~/.cache/gsbridge/ref"); TEX = wad_textures.OUT
CONTENT = ["/home/tim/Desktop/dod-gc/content/dod", "/home/tim/Desktop/dod-gc/content/valve"]
SKIP = re.compile(r"^(null|clip|origin|hint|skip|sky|bevel|aaatrigger|trigger|black_trans|noclip|solidhint)$", re.I)

def lumps(d):
    ver = struct.unpack("<i", d[:4])[0]; assert ver == 30, ver
    return [struct.unpack("<ii", d[4 + i * 8:12 + i * 8]) for i in range(15)]

def parse_ents(txt):
    ents = []
    for blk in re.findall(r"\{([^{}]*)\}", txt):
        ents.append(dict(re.findall(r'"([^"]*)"\s+"([^"]*)"', blk)))
    return ents

def embedded(d, off, name):
    """Decode a miptex embedded in the BSP -> PNG in the texture cache (or None)."""
    nm = d[off:off + 16].split(b"\0")[0].decode("latin1"); w, h = struct.unpack("<II", d[off + 16:off + 24])
    mo = struct.unpack("<I", d[off + 24:off + 28])[0]
    if not mo: return None
    px = np.frombuffer(d, np.uint8, w * h, off + mo)
    po = off + struct.unpack("<I", d[off + 36:off + 40])[0] + (w // 8) * (h // 8) + 2
    pal = np.frombuffer(d, np.uint8, 768, po).reshape(-1, 3)
    idx = px.reshape(h, w); a = np.where((idx == 255) & nm.startswith("{"), 0, 255).astype(np.uint8)
    safe = "emb_" + re.sub(r"[^A-Za-z0-9]", "_", nm) + ".png"
    Image.fromarray(np.dstack([pal[idx], a]), "RGBA").save(os.path.join(TEX, safe))
    return safe, w, h

def bake(mapname):
    path = next(os.path.join(c, "maps", mapname + ".bsp") for c in CONTENT if os.path.exists(os.path.join(c, "maps", mapname + ".bsp")))
    d = open(path, "rb").read(); L = lumps(d)
    ents = parse_ents(d[L[0][0]:L[0][0] + L[0][1]].split(b"\0")[0].decode("latin1"))
    wads = [w.strip().replace("\\", "/").split("/")[-1] for w in ents[0].get("wad", "").split(";") if w.strip()]
    os.makedirs(TEX, exist_ok=True)
    planes = None
    verts = np.frombuffer(d, "<f4", L[3][1] // 4, L[3][0]).reshape(-1, 3)
    # textures
    to, tl = L[2]; nt = struct.unpack("<i", d[to:to + 4])[0]; mt = []
    for i in range(nt):
        off = struct.unpack("<i", d[to + 4 + i * 4:to + 8 + i * 4])[0]
        if off < 0: mt.append(None); continue
        o = to + off; nm = d[o:o + 16].split(b"\0")[0].decode("latin1"); w, h = struct.unpack("<II", d[o + 16:o + 24])
        info = {"name": nm, "w": w, "h": h, "png": None, "wad": None}
        wp = wad_textures.resolve_wad(nm, wads)
        if wp:
            info["wad"] = wp; info["png"], info["w"], info["h"] = wad_textures.export_one(nm, wp)
        else:
            e = embedded(d, o, nm)
            if e: info.update(png=os.path.join(TEX, e[0]), w=e[1], h=e[2])
        mt.append(info)
    ti = np.frombuffer(d, np.dtype([("v", "<f4", (8,)), ("mip", "<i4"), ("flags", "<i4")]), L[6][1] // 40, L[6][0])
    edges = np.frombuffer(d, "<u2", L[12][1] // 2, L[12][0]).reshape(-1, 2).astype(int)
    se = np.frombuffer(d, "<i4", L[13][1] // 4, L[13][0])
    fr = np.frombuffer(d, np.dtype([("plane", "<u2"), ("side", "<i2"), ("fe", "<i4"), ("ne", "<i2"), ("ti", "<i2"), ("st", "u1", 4), ("lo", "<i4")]), L[7][1] // 20, L[7][0])
    # which brush model owns each face, and how its entity renders (rendermode 4 = alpha test on index 255,
    # 2 = translucent, 5/3 = additive; func_water = water)
    mo = np.frombuffer(d, np.dtype([("f", "<f4", (9,)), ("hn", "<i4", (4,)), ("vl", "<i4"), ("ff", "<i4"), ("nf", "<i4")]), L[14][1] // 64, L[14][0])
    face_model = np.zeros(len(fr), int)
    for mi, m in enumerate(mo): face_model[m["ff"]:m["ff"] + m["nf"]] = mi
    ent_by_model = {}
    for e in ents:
        mdl = e.get("model", "")
        if mdl.startswith("*"):
            try: ent_by_model[int(mdl[1:])] = (e.get("classname", ""), int(e.get("rendermode", 0) or 0), int(e.get("renderamt", 0) or 0))
            except ValueError: pass
    groups = {}; nface = 0; wadsrc = {}
    for fi, f in enumerate(fr):
        t = ti[f["ti"]]; m = mt[t["mip"]] if t["mip"] < len(mt) else None
        if m is None or SKIP.match(m["name"]) or m["name"].startswith(("!", "*")) and False: continue
        if m["name"].lower().startswith("sky"): continue
        ids = se[f["fe"]:f["fe"] + f["ne"]]
        vi = [edges[abs(e)][0 if e >= 0 else 1] for e in ids]
        pts = verts[vi]; v = t["v"]
        u = (pts @ v[0:3] + v[3]) / m["w"]; vv = (pts @ v[4:7] + v[7]) / m["h"]
        cls, rmode, ramt = ent_by_model.get(int(face_model[fi]), ("", 0, 255))
        nm = m["name"]; low = nm.lower()
        if rmode in (1, 2, 3, 5) and ramt <= 2 and cls != "func_water": kind = "hidden"   # renderamt 0 = invisible until a trigger/env_render shows it
        elif cls == "func_water" or nm.startswith("!") or "water" in low: kind = "water"
        elif nm.startswith("{") or rmode == 4: kind = "alpha"
        elif rmode == 2 or (rmode == 1): kind = "trans"
        elif rmode in (3, 5): kind = "add"
        else: kind = "opaque"
        png = m["png"]
        if kind == "alpha" and not nm.startswith("{") and m["wad"]:   # alpha test on a normal texture: index 255 becomes transparent
            png = wad_textures.export_one(nm, m["wad"], masked=True)[0]
        key = (nm, kind, ramt if kind in ("trans", "add") else 255)
        mean = None
        if kind == "water" and png and os.path.exists(png):
            mean = np.array(Image.open(png).convert("RGB")).reshape(-1, 3).mean(0).round().tolist()
        g = groups.setdefault(key, {"tex": nm, "png": png, "kind": kind, "amt": key[2], "mean": mean, "polys": []})
        g["polys"].append([[round(float(p[0]), 2), round(float(p[1]), 2), round(float(p[2]), 2), round(float(a), 4), round(float(1 - b), 4)] for p, a, b in zip(pts, u, vv)])
        nface += 1
    allp = np.array([q[:3] for g in groups.values() for pl in g["polys"] for q in pl])
    out = {"map": mapname, "bounds": [allp.min(0).tolist(), allp.max(0).tolist()], "groups": list(groups.values()),
           "ents": [{"class": e.get("classname", "?"), "origin": e.get("origin", ""), "angles": e.get("angles", ""), "model": e.get("model", ""),
                     "target": e.get("targetname", "") or e.get("target", ""), "keys": {k: v for k, v in e.items() if k in ("targetname", "target", "model", "skin", "message", "_light", "wad")}} for e in ents]}
    sky = {}
    sn = ents[0].get("skyname", "")
    for suf in ("ft", "bk", "lf", "rt", "up", "dn"):
        for c in CONTENT:
            src = os.path.join(c, "gfx", "env", sn + suf + ".tga")
            if sn and os.path.exists(src):
                dst = os.path.join(TEX, "sky_%s%s.png" % (sn, suf)); Image.open(src).convert("RGB").save(dst); sky[suf] = dst; break
    out["sky"] = {"name": sn, "faces": sky}
    os.makedirs(OUT, exist_ok=True); o = os.path.join(OUT, mapname + ".json"); json.dump(out, open(o, "w"), separators=(",", ":"))
    print("%s: %d faces, %d textures, %d entities -> %s (%.1f MB)" % (mapname, nface, len(groups), len(ents), o, os.path.getsize(o) / 1e6))
    return o

if __name__ == "__main__": bake(sys.argv[1])
