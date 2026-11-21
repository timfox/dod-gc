"""GoldSrc .mdl -> plain mesh groups (numpy only; used by Maya previews). Bind pose, body 0 of
every bodypart, skin family selectable. Textures written as PNG into ~/.cache/gsbridge/mdltex."""
import os, re
import numpy as np
from PIL import Image
CONTENT = ["/home/tim/Desktop/dod-gc/content/dod", "/home/tim/Desktop/dod-gc/content/valve"]
TEXDIR = os.path.expanduser("~/.cache/gsbridge/mdltex")
_src = open("/home/tim/Desktop/dod-gc/tools/goldsrc_to_usd.py").read().replace("from pxr import Gf, Sdf, Usd, UsdGeom, UsdShade, UsdSkel, Vt", "")
_ns = {"__name__": "gs2usd"}; exec(compile(_src.split("def main():")[0], "goldsrc_to_usd", "exec"), _ns)
Mdl = _ns["Mdl"]

def resolve(rel):
    for c in CONTENT:
        p = os.path.join(c, rel)
        if os.path.exists(p): return p
    return None

def load(rel, skin=0):
    """-> list of groups {tex, png, verts[N,3], uvs[N,2], tris[M,3]} (GoldSrc units, Z up)."""
    p = resolve(rel)
    if not p: raise FileNotFoundError(rel)
    m = Mdl(p); texs = m.textures(); skins = m.skins()
    fam = skins[skin if skin < len(skins) else 0] if skins else []
    os.makedirs(TEXDIR, exist_ok=True)
    pngs = []
    for i, t in enumerate(texs):
        pal = np.frombuffer(t["pal"], np.uint8).reshape(-1, 3); idx = np.frombuffer(t["pix"], np.uint8).reshape(t["h"], t["w"])
        a = np.where((idx == 255) & ((t["flags"] & 0x40) != 0), 0, 255).astype(np.uint8)   # MASKED: index 255 is transparent
        if t["flags"] & 0x20: a = pal[idx].max(axis=1).astype(np.uint8)                       # ADDITIVE: brightness ~ opacity (preview approximation)
        rgb = pal[idx].astype(np.float32); opaque = a > 0
        if (~opaque).any() and opaque.any():     # bleed colour into masked texels so filtering never shows the key colour (red/blue fringe)
            for _ in range(6):
                acc = np.zeros_like(rgb); cnt = np.zeros(opaque.shape, np.float32)
                for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    o = np.roll(opaque, (dy, dx), (0, 1)); c = np.roll(rgb, (dy, dx), (0, 1))
                    acc += c * o[..., None]; cnt += o
                fill = (~opaque) & (cnt > 0)
                rgb[fill] = acc[fill] / cnt[fill, None]; opaque = opaque | fill
            if not opaque.all(): rgb[~opaque] = rgb[opaque].mean(axis=0)
        rgba = np.dstack([rgb.astype(np.uint8), a])
        name = "%s_%d.png" % (re.sub(r"[^A-Za-z0-9]", "_", os.path.splitext(rel)[0]), i)
        out = os.path.join(TEXDIR, name)
        if not os.path.exists(out): Image.fromarray(rgba, "RGBA").save(out)
        pngs.append((out, t["w"], t["h"], t["name"]))
    acc = {}
    for bp in m.bodyparts():
        mod = bp["models"][0]; V = mod["verts"]
        for me in mod["meshes"]:
            ti = fam[me["skinref"]] if me["skinref"] < len(fam) else 0
            png, w, h, tname = pngs[ti] if ti < len(pngs) else (None, 1, 1, "none")
            g = acc.setdefault(ti, {"tex": tname, "png": png, "verts": [], "uvs": []})
            for tr in me["tris"]:
                for t in tr:
                    g["verts"].append(V[int(t[0])]); g["uvs"].append((t[2] / w, 1.0 - t[3] / h))
    out = []
    for g in acc.values():
        v = np.array(g["verts"]); n = len(v)
        out.append({"tex": g["tex"], "png": g["png"], "verts": v, "uvs": np.array(g["uvs"]), "tris": np.arange(n).reshape(-1, 3)})
    return out, m.numskinfam

def bake_json(rel, skin=0):
    """Write ~/.cache/gsbridge/mdltex/<name>_s<skin>.json (plain lists) so Maya's Python (no PIL) can read it."""
    import json
    groups, nskins = load(rel, skin)
    out = os.path.join(TEXDIR, "%s_s%d.json" % (re.sub(r"[^A-Za-z0-9]", "_", os.path.splitext(rel)[0]), skin))
    json.dump({"nskins": nskins, "groups": [{"tex": g["tex"], "png": g["png"], "verts": g["verts"].round(3).tolist(),
               "uvs": g["uvs"].round(5).tolist(), "tris": g["tris"].tolist()} for g in groups]}, open(out, "w"))
    return out

if __name__ == "__main__":
    import sys
    print(bake_json(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 0))

def bake_all(root=None):
    """Pre-bake every .mdl under content/dod/models (all skin families, max 8) for Maya."""
    root = root or CONTENT[0] + "/models"; n = 0; bad = []
    for dp, _, fs in os.walk(root):
        for f in fs:
            if not f.lower().endswith(".mdl") or re.search(r"(T|\d\d)\.mdl$", f) and not f.lower().startswith(("p_", "w_")) and f[:-4].endswith("T"):
                continue
            rel = os.path.relpath(os.path.join(dp, f), CONTENT[0])
            try:
                _, ns = load(rel, 0)
                for sk in range(min(ns, 8)): bake_json(rel, sk)
                n += 1
            except Exception as e: bad.append((rel, str(e)[:60]))
    print("baked", n, "models;", len(bad), "skipped"); return bad
