#!/usr/bin/env python3
"""WAD3 -> PNG for Maya. Same names, same pixels the engine/compiler use.

    wad_textures.py dod_caen.wad [more.wad ...]   # -> ~/.cache/gsbridge/tex/<name>.png + index.json
Transparent ('{' prefix) textures: palette index 255 -> alpha 0, as the engine does.
Names are kept verbatim (case preserved); Maya shader name == texture name == what the
.map and compiled BSP reference. index.json holds width/height so the exporter and the
Maya UV tool use the same pixels-per-unit (1 unit = 1 texel in the .map, scale 1).
"""
import json, os, re, struct, sys
from PIL import Image
CONTENT = ["/home/tim/Desktop/dod-gc/content/dod", "/home/tim/Desktop/dod-gc/content/valve"]
OUT = os.path.expanduser(os.environ.get("GSBRIDGE_TEX", "~/.cache/gsbridge/tex"))

def read_wad(path):
    d = open(path, "rb").read()
    magic, n, off = struct.unpack("<4sii", d[:12])
    assert magic == b"WAD3", path
    for i in range(n):
        fo, dsz, sz, typ, comp, _, nm = struct.unpack("<iiibbh16s", d[off+i*32:off+i*32+32])
        if typ != 0x43 or comp: continue            # 0x43 = mip texture
        name = nm.split(b"\0")[0].decode("latin1")
        w, h = struct.unpack("<II", d[fo+16:fo+24]); mip0 = struct.unpack("<I", d[fo+24:fo+28])[0]
        px = d[fo+mip0:fo+mip0+w*h]
        # palette follows the 4 mips (+2 byte count)
        pal_off = fo + struct.unpack("<I", d[fo+36:fo+40])[0] + (w//8)*(h//8) + 2
        pal = d[pal_off:pal_off+768]
        yield name, w, h, px, pal

def _bleed(im):
    """Fill transparent texels with nearby opaque colour so filtering never shows the blue key colour."""
    import numpy as np
    a = np.array(im); rgb = a[..., :3].astype(np.float32); op = a[..., 3] > 0
    if op.all() or not op.any(): return im
    for _ in range(6):
        acc = np.zeros_like(rgb); cnt = np.zeros(op.shape, np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            o = np.roll(op, (dy, dx), (0, 1)); acc += np.roll(rgb, (dy, dx), (0, 1)) * o[..., None]; cnt += o
        f = (~op) & (cnt > 0); rgb[f] = acc[f] / cnt[f, None]; op = op | f
    if not op.all(): rgb[~op] = rgb[op].mean(0)
    a[..., :3] = rgb.astype(np.uint8); return Image.fromarray(a, "RGBA")

def masked_variant(name, wadfile):
    """PNG of texture `name` with palette index 255 transparent (GoldSrc rendermode 4 / alpha test), colour-bled."""
    p = wadfile if os.path.isabs(wadfile) else next((os.path.join(c, wadfile) for c in CONTENT if os.path.exists(os.path.join(c, wadfile))), wadfile)
    for nm, w, h, px, pal in read_wad(p):
        if nm == name:
            im = Image.frombytes("P", (w, h), px); im.putpalette(pal); rgba = im.convert("RGBA")
            rgba.putalpha(Image.frombytes("L", (w, h), bytes(0 if b == 255 else 255 for b in px)))
            out = os.path.join(OUT, "mask_" + name.replace("{", "_brace_").replace("-", "_m_").replace("+", "_p_").replace("!", "_w_") + ".png")
            _bleed(rgba).save(out); return out
    return None

_dirs = {}
def wad_dir(path):
    """{texture name: file offset} for one WAD3 (cached)."""
    if path not in _dirs:
        d = open(path, "rb").read(); n, o = struct.unpack("<ii", d[4:12]); ent = {}
        for i in range(n):
            fo, dsz, sz, typ, comp, _, nm = struct.unpack("<iiibbh16s", d[o + i * 32:o + i * 32 + 32])
            if typ == 0x43 and not comp: ent.setdefault(nm.split(b"\0")[0].decode("latin1"), fo)
        _dirs[path] = (d, ent)
    return _dirs[path]

def resolve_wad(name, wadnames):
    """First WAD in the map's own wad order that has the texture (engine behaviour); None if absent."""
    for w in wadnames:
        p = next((os.path.join(c, w) for c in CONTENT if os.path.exists(os.path.join(c, w))), None)
        if p and name in wad_dir(p)[1]: return p
    return None

def export_one(name, wadpath, masked=False):
    d, ent = wad_dir(wadpath); fo = ent[name]
    w, h = struct.unpack("<II", d[fo + 16:fo + 24]); mip0 = struct.unpack("<I", d[fo + 24:fo + 28])[0]
    px = d[fo + mip0:fo + mip0 + w * h]
    pal_off = fo + struct.unpack("<I", d[fo + 36:fo + 40])[0] + (w // 8) * (h // 8) + 2
    im = Image.frombytes("P", (w, h), px); im.putpalette(d[pal_off:pal_off + 768]); rgba = im.convert("RGBA")
    if masked or name.startswith("{"):
        rgba.putalpha(Image.frombytes("L", (w, h), bytes(0 if b == 255 else 255 for b in px))); rgba = _bleed(rgba)
    safe = re.sub(r"[^A-Za-z0-9]", "_", name)
    out = os.path.join(OUT, "%s__%s%s.png" % (os.path.splitext(os.path.basename(wadpath))[0], safe, "_a" if (masked or name.startswith("{")) else ""))
    if not os.path.exists(out): rgba.save(out)
    return out, w, h

def main(wads):
    os.makedirs(OUT, exist_ok=True); idx = {}
    for wad in wads:
        p = wad if os.path.isabs(wad) else next((os.path.join(c, wad) for c in CONTENT if os.path.exists(os.path.join(c, wad))), wad)
        for name, w, h, px, pal in read_wad(p):
            im = Image.frombytes("P", (w, h), px); im.putpalette(pal)
            rgba = im.convert("RGBA")
            if name.startswith("{"):
                a = Image.frombytes("L", (w, h), bytes(0 if b == 255 else 255 for b in px)); rgba.putalpha(a)
                rgba = _bleed(rgba)
            safe = name.replace("{", "_brace_").replace("-", "_m_").replace("+", "_p_")
            rgba.save(os.path.join(OUT, safe + ".png")); idx[name] = {"w": w, "h": h, "png": safe + ".png", "wad": os.path.basename(p)}
    old = {}
    try: old = json.load(open(os.path.join(OUT, "index.json")))
    except Exception: pass
    old.update(idx); json.dump(old, open(os.path.join(OUT, "index.json"), "w"), indent=0)
    print("%d textures -> %s" % (len(idx), OUT))
if __name__ == "__main__": main(sys.argv[1:] or ["dod_caen.wad"])
