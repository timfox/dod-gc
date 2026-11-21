#!/usr/bin/env python3
"""Render retail GoldSrc .mdl files to small icons (software rasterizer, no GPU needed).
Reuses the Mdl parser in dod-gc/tools/goldsrc_to_usd.py (pxr import stripped), bind pose,
skin 0, real embedded textures, one fixed 3/4 view, neutral background, auto-framed.

    mdl_icons.py models/mapmodels/sandbags.mdl [...]     # paths relative to content/dod or absolute
    mdl_icons.py --catalog                                # everything the Object Manager lists
Output: ~/.cache/gsbridge/objmgr/icons/mdl_<name>.png (80x80)
"""
import math, os, re, sys
import numpy as np
from PIL import Image
OUT = os.path.expanduser("~/.cache/gsbridge/objmgr/icons"); CONTENT = "/home/tim/Desktop/dod-gc/content/dod"
_src = open("/home/tim/Desktop/dod-gc/tools/goldsrc_to_usd.py").read().replace("from pxr import Gf, Sdf, Usd, UsdGeom, UsdShade, UsdSkel, Vt", "")
_ns = {"__name__": "gs2usd"}; exec(compile(_src.split("def main():")[0], "goldsrc_to_usd", "exec"), _ns)
Mdl = _ns["Mdl"]
S = 192; OUTSZ = 80

def render(path, yaw=40.0, pitch=22.0):
    m = Mdl(path); tex = m.textures(); skin = m.skins()[0] if m.skins() else []
    imgs = []
    for t in tex:
        pal = np.frombuffer(t["pal"], np.uint8).reshape(-1, 3); idx = np.frombuffer(t["pix"], np.uint8).reshape(t["h"], t["w"])
        rgb = pal[idx].astype(np.float32); alpha = (idx != 255) if (t["flags"] & 0x40) else np.ones(idx.shape, bool)
        imgs.append((rgb, alpha, t["w"], t["h"]))
    tris = []  # (3x3 verts, 3x2 uv, texidx, normal)
    for bp in m.bodyparts():
        mod = bp["models"][0]; V = mod["verts"]
        for me in mod["meshes"]:
            ti = skin[me["skinref"]] if me["skinref"] < len(skin) else 0
            for tr in me["tris"]:
                tris.append((V[[int(t[0]) for t in tr]], np.array([[t[2], t[3]] for t in tr], float), ti))
    if not tris: raise ValueError("no geometry")
    # camera: rotate world so view looks down -Y of camera frame; Z up
    ya, pa = math.radians(yaw), math.radians(pitch)
    Rz = np.array([[math.cos(ya), -math.sin(ya), 0], [math.sin(ya), math.cos(ya), 0], [0, 0, 1]])
    Rx = np.array([[1, 0, 0], [0, math.cos(pa), -math.sin(pa)], [0, math.sin(pa), math.cos(pa)]])
    R = Rx @ Rz
    allv = np.concatenate([t[0] for t in tris]) @ R.T            # x right, y depth, z up
    mn, mx = allv.min(0), allv.max(0); c = (mn + mx) / 2; ext = max(mx[0] - mn[0], mx[2] - mn[2], 1e-3)
    sc = S * 0.86 / ext
    col = np.zeros((S, S, 3), np.float32); zb = np.full((S, S), 1e9, np.float32); cov = np.zeros((S, S), bool)
    light = np.array([-0.4, -0.7, 0.6]); light /= np.linalg.norm(light)
    for v, uv, ti in tris:
        p = (v @ R.T - c)
        n = np.cross(p[1] - p[0], p[2] - p[0]); nl = np.linalg.norm(n)
        if nl < 1e-9: continue
        n /= nl
        if n[1] > 0: n = -n                                           # two-sided: face the camera
        shade = 0.45 + 0.55 * max(0.0, float(n @ light))
        x = S / 2 + p[:, 0] * sc; y = S / 2 - p[:, 2] * sc; z = p[:, 1]
        x0, x1 = int(max(0, math.floor(x.min()))), int(min(S - 1, math.ceil(x.max())))
        y0, y1 = int(max(0, math.floor(y.min()))), int(min(S - 1, math.ceil(y.max())))
        if x1 < x0 or y1 < y0: continue
        den = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2])
        if abs(den) < 1e-9: continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + .5, np.arange(y0, y1 + 1) + .5)
        w0 = ((y[1] - y[2]) * (gx - x[2]) + (x[2] - x[1]) * (gy - y[2])) / den
        w1 = ((y[2] - y[0]) * (gx - x[2]) + (x[0] - x[2]) * (gy - y[2])) / den
        w2 = 1 - w0 - w1; inside = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
        if not inside.any(): continue
        zz = w0 * z[0] + w1 * z[1] + w2 * z[2]
        sub = zb[y0:y1 + 1, x0:x1 + 1]; ok = inside & (zz < sub)
        if not ok.any(): continue
        rgb, alpha, tw, th = imgs[ti] if ti < len(imgs) else (np.full((1, 1, 3), 160, np.float32), np.ones((1, 1), bool), 1, 1)
        u = (w0 * uv[0, 0] + w1 * uv[1, 0] + w2 * uv[2, 0]); vv = (w0 * uv[0, 1] + w1 * uv[1, 1] + w2 * uv[2, 1])
        ui = np.clip(np.floor(u).astype(int) % tw, 0, tw - 1); vi = np.clip(np.floor(vv).astype(int) % th, 0, th - 1)
        ok &= alpha[vi, ui]
        sub[ok] = zz[ok]
        col[y0:y1 + 1, x0:x1 + 1][ok] = rgb[vi, ui][ok] * shade
        cov[y0:y1 + 1, x0:x1 + 1] |= ok
    # neutral background gradient, soft shadow-less
    bg = np.linspace(86, 60, S)[:, None, None].repeat(S, 1).repeat(3, 2).astype(np.float32)
    img = np.where(cov[..., None], col, bg).clip(0, 255).astype(np.uint8)
    return Image.fromarray(img).resize((OUTSZ, OUTSZ), Image.LANCZOS)

def icon_for(rel, force=False):
    p = rel if os.path.isabs(rel) else os.path.join(CONTENT, rel)
    name = "mdl_" + re.sub(r"[^A-Za-z0-9]", "_", os.path.splitext(rel)[0])
    out = os.path.join(OUT, name + ".png")
    if os.path.exists(out) and not force: return name
    os.makedirs(OUT, exist_ok=True)
    try: render(p).save(out)
    except Exception as e:
        print("skip", rel, e); return None
    return name

if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    for a in args: print(a, "->", icon_for(a, force=True))
