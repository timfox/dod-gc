"""Scout views: what the camera sees in the real map, rendered on the host from the map's BSP and WADs.

The storyboards sketch a shot from the bot waypoints, which is enough for framing on an open beach but not in a
town, where walls, roofs and windows decide what the lens sees. This draws the actual level geometry with its
textures and baked lightmaps, so a camera can be placed and checked without booting the game:

    tools/film/scout.py view dod_avalanche 0 0 200 --angles 10 90 0 --fov 70 -o view.png
    tools/film/scout.py shots film/episodes/ep02-salerno.json            # A/B frames of every shot -> contact sheet
    tools/film/scout.py top dod_avalanche -o top.png                     # overhead with a coordinate grid

It is a scouting aid, not the film: brush geometry only (no studio models, sprites, decals, fog or players),
lightmap style 0, and no sky box (sky faces are painted a flat grey). The maps and textures are read from
$FILM_BSP_DIR or content/dod/maps, and WADs from $FILM_WAD_DIR or content/dod (retail files, not in the repo).
Needs numpy; numba makes it about fifty times faster.
"""
import argparse
import math
import os
import struct
import sys
from pathlib import Path

import numpy as np

try:
    from numba import njit
except ImportError:  # slow but works
    def njit(*a, **k):
        if a and callable(a[0]):
            return a[0]
        return lambda f: f

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    __package__ = "tools.film"

from . import campath  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
SKY = (150, 156, 160)
SKIP = ("aaatrigger", "clip", "origin", "null", "hint", "skip", "bevel")
TRIGGERS = ("trigger_", "func_ladder", "dod_", "func_buyzone", "func_bomb_target", "env_bubbles",
            "func_illusionary_skip")


def _wad_textures(paths):
    tex = {}
    for p in paths:
        try:
            data = Path(p).read_bytes()
        except OSError:
            continue
        if data[:4] not in (b"WAD3", b"WAD2"):
            continue
        n, ofs = struct.unpack_from("<ii", data, 4)
        for i in range(n):
            pos, disk, size, typ, comp = struct.unpack_from("<iiibb", data, ofs + 32 * i)
            name = data[ofs + 32 * i + 16: ofs + 32 * i + 32].split(b"\0")[0].decode("latin-1").lower()
            if typ == 0x43 and name not in tex:
                tex[name] = (data, pos)
    return tex


def _miptex(data, pos):
    name = data[pos:pos + 16].split(b"\0")[0].decode("latin-1").lower()
    w, h, o0, o1, o2, o3 = struct.unpack_from("<6I", data, pos + 16)
    if o0 == 0:
        return name, w, h, None
    pix = np.frombuffer(data, np.uint8, w * h, pos + o0)
    pal_at = pos + o3 + (w * h) // 64 + 2
    pal = np.frombuffer(data, np.uint8, 768, pal_at).reshape(256, 3)
    rgb = np.empty((h, w, 4), np.uint8)
    rgb[..., :3] = pal[pix].reshape(h, w, 3)
    rgb[..., 3] = 255
    if name.startswith("{"):          # masked (fences, foliage): palette index 255 is see-through
        rgb[..., 3] = np.where(pix.reshape(h, w) == 255, 0, 255)
    return name, w, h, rgb


class Level:
    """Faces of the world and the visible brush entities, with textures and lightmaps, packed for the renderer."""

    def __init__(self, mapname, bsp_dir=None, wad_dirs=None):
        bsp_path = find_file(f"{mapname}.bsp", bsp_dir_list(bsp_dir))
        data = bsp_path.read_bytes()
        if struct.unpack_from("<i", data, 0)[0] != 30:
            raise ValueError(f"{bsp_path}: not a GoldSrc BSP")
        lumps = [struct.unpack_from("<ii", data, 4 + 8 * i) for i in range(15)]

        def lump(i):
            off, n = lumps[i]
            return data[off:off + n]
        verts = np.frombuffer(lump(3), np.float32).reshape(-1, 3)
        planes = np.frombuffer(lump(1), dtype=np.dtype([("n", "<f4", 3), ("d", "<f4"), ("t", "<i4")]))
        edges = np.frombuffer(lump(12), np.uint16).reshape(-1, 2)
        surfedges = np.frombuffer(lump(13), np.int32)
        ti = np.frombuffer(lump(6), dtype=np.dtype([("v", "<f4", (2, 4)), ("mip", "<i4"), ("fl", "<i4")]))
        fdt = np.dtype([("plane", "<u2"), ("side", "<u2"), ("fe", "<i4"), ("ne", "<u2"), ("ti", "<u2"),
                        ("st", "u1", 4), ("lo", "<i4")])
        faces = np.frombuffer(lump(7), dtype=fdt)
        light = np.frombuffer(lump(8), np.uint8)
        mdt = np.dtype([("mins", "<f4", 3), ("maxs", "<f4", 3), ("org", "<f4", 3), ("head", "<i4", 4),
                        ("vis", "<i4"), ("ff", "<i4"), ("nf", "<i4")])
        models = np.frombuffer(lump(14), dtype=mdt)
        from .clearance import _entities
        self.entities = _entities(lump(0).split(b"\0")[0].decode("latin-1"))
        self.name = mapname

        # textures: embedded or from the WADs
        tl = lump(2)
        ntex = struct.unpack_from("<i", tl, 0)[0]
        offs = struct.unpack_from(f"<{ntex}i", tl, 4)
        wad_paths = []
        for d in wad_dir_list(wad_dirs):
            wad_paths += sorted(Path(d).glob("*.wad")) if Path(d).is_dir() else []
        wads = None
        texs = []
        for o in offs:
            if o < 0:
                texs.append(("", 16, 16, None))
                continue
            name, w, h, rgb = _miptex(tl, o)
            if rgb is None:
                if wads is None:
                    wads = _wad_textures(wad_paths)
                if name in wads:
                    _, w, h, rgb = _miptex(*wads[name])
            texs.append((name, w, h, rgb))
        self.missing = sorted({t[0] for t in texs if t[3] is None and t[0]})

        atlas, tex_off, tex_wh = [], [], []
        at = 0
        for name, w, h, rgb in texs:
            if rgb is None:
                rgb = np.full((max(h, 1), max(w, 1), 4), 128, np.uint8)
                rgb[..., 3] = 255
            tex_off.append(at)
            tex_wh.append((rgb.shape[1], rgb.shape[0]))
            atlas.append(rgb.reshape(-1, 4))
            at += rgb.shape[0] * rgb.shape[1]
        self.atlas = np.concatenate(atlas).astype(np.uint8) if atlas else np.zeros((1, 4), np.uint8)

        # which models are drawn: 0 plus brush entities that are not triggers or clip
        draw = [(0, np.zeros(3, np.float32))]
        for e in self.entities:
            m = e.get("model", "")
            cls = e.get("classname", "")
            if not m.startswith("*") or cls.startswith(TRIGGERS):
                continue
            if e.get("rendermode", "0") not in ("", "0") and float(e.get("renderamt") or 0) <= 0:
                continue    # rendered at zero amount: invisible (clip walls, light shafts set off)
            o = np.array([float(v) for v in e.get("origin", "0 0 0").split()], np.float32)
            draw.append((int(m[1:]), o))

        poly_start, poly_n, pverts = [], [], []
        f_sv, f_tv, f_tex, f_lm, f_lmw, f_lmh, f_lms, f_lmt, f_flag = [], [], [], [], [], [], [], [], []
        for mi, org in draw:
            if mi >= len(models):
                continue
            m = models[mi]
            for fi in range(m["ff"], m["ff"] + m["nf"]):
                f = faces[fi]
                tinfo = ti[f["ti"]]
                tname = texs[tinfo["mip"]][0] if 0 <= tinfo["mip"] < len(texs) else ""
                if any(tname.startswith(s) for s in SKIP):
                    continue
                se = surfedges[f["fe"]:f["fe"] + f["ne"]]
                idx = np.where(se >= 0, edges[np.abs(se), 0], edges[np.abs(se), 1])
                vs = verts[idx] + org
                poly_start.append(len(pverts))
                poly_n.append(len(vs))
                pverts.extend(vs.tolist())
                sv, tv = tinfo["v"][0].copy(), tinfo["v"][1].copy()
                # brush entities are moved by origin: texture/lightmap coords follow the unmoved brush
                sv[3] -= float(np.dot(sv[:3], org))
                tv[3] -= float(np.dot(tv[:3], org))
                s = vs @ sv[:3] + sv[3]
                t = vs @ tv[:3] + tv[3]
                smin, tmin = math.floor(s.min() / 16), math.floor(t.min() / 16)
                smax, tmax = math.ceil(s.max() / 16), math.ceil(t.max() / 16)
                lw, lh = smax - smin + 1, tmax - tmin + 1
                special = tinfo["fl"] & 1
                flag = 1 if tname.startswith("sky") else (2 if special or f["lo"] < 0 else 0)
                f_sv.append(sv)
                f_tv.append(tv)
                f_tex.append(tinfo["mip"] if 0 <= tinfo["mip"] < len(texs) else 0)
                f_lm.append(f["lo"] if f["lo"] >= 0 and f["st"][0] != 255 else -1)
                f_lmw.append(lw)
                f_lmh.append(lh)
                f_lms.append(smin)
                f_lmt.append(tmin)
                f_flag.append(flag)
        self.poly_start = np.array(poly_start, np.int32)
        self.poly_n = np.array(poly_n, np.int32)
        self.pverts = np.array(pverts, np.float64)
        self.f_sv = np.array(f_sv, np.float64)
        self.f_tv = np.array(f_tv, np.float64)
        tex_off = np.array(tex_off, np.int64)
        tex_wh = np.array(tex_wh, np.int32)
        self.f_toff = tex_off[f_tex]
        self.f_tw = tex_wh[f_tex, 0].copy()
        self.f_th = tex_wh[f_tex, 1].copy()
        self.f_lm = np.array(f_lm, np.int64)
        self.f_lmw = np.array(f_lmw, np.int32)
        self.f_lmh = np.array(f_lmh, np.int32)
        self.f_lms = np.array(f_lms, np.float64)
        self.f_lmt = np.array(f_lmt, np.float64)
        self.f_flag = np.array(f_flag, np.int32)
        self.light = light if len(light) else np.zeros(3, np.uint8)
        self.mins = self.pverts.min(axis=0)
        self.maxs = self.pverts.max(axis=0)

    def render(self, origin, angles, fov, w=960, h=540, gain=1.6, sky=True):
        f, r, u = _basis(angles)
        k = (w / 2) / math.tan(math.radians(fov) / 2)
        color = np.zeros((h, w, 3), np.uint8)
        color[:] = SKY
        depth = np.full((h, w), np.inf)
        _raster(self.poly_start, self.poly_n, self.pverts, self.f_sv, self.f_tv, self.f_toff, self.f_tw, self.f_th,
                self.f_lm, self.f_lmw, self.f_lmh, self.f_lms, self.f_lmt, self.f_flag, self.atlas, self.light,
                np.array(origin, np.float64), np.array(f), np.array(r), np.array(u), k, w, h, gain, 1 if sky else 0,
                color, depth)
        return color, depth


def _basis(a):
    p, y, rl = (math.radians(v) for v in a)
    fwd = (math.cos(p) * math.cos(y), math.cos(p) * math.sin(y), -math.sin(p))
    right0 = (math.sin(y), -math.cos(y), 0.0)
    up0 = np.cross(right0, fwd)
    cr, sr = math.cos(rl), math.sin(rl)
    right = tuple(cr * right0[i] + sr * up0[i] for i in range(3))
    up = tuple(-sr * right0[i] + cr * up0[i] for i in range(3))
    return fwd, right, up


@njit(cache=True)
def _raster(pstart, pn, pv, fsv, ftv, ftoff, ftw, fth, flm, flmw, flmh, flms, flmt, fflag, atlas, light,
            o, fw, rt, up, k, w, h, gain, drawsky, color, depth):
    near = 2.0
    cx, cy = w / 2.0, h / 2.0
    cam = np.empty((24, 3))
    wld = np.empty((24, 3))
    outc = np.empty((24, 3))
    outw = np.empty((24, 3))
    for fi in range(pstart.shape[0]):
        n = pn[fi]
        if n > 20:
            n = 20
        anyfront = False
        for i in range(n):
            vx = pv[pstart[fi] + i, 0] - o[0]
            vy = pv[pstart[fi] + i, 1] - o[1]
            vz = pv[pstart[fi] + i, 2] - o[2]
            cam[i, 0] = vx * rt[0] + vy * rt[1] + vz * rt[2]
            cam[i, 1] = vx * up[0] + vy * up[1] + vz * up[2]
            cam[i, 2] = vx * fw[0] + vy * fw[1] + vz * fw[2]
            wld[i, 0] = pv[pstart[fi] + i, 0]
            wld[i, 1] = pv[pstart[fi] + i, 1]
            wld[i, 2] = pv[pstart[fi] + i, 2]
            if cam[i, 2] > near:
                anyfront = True
        if not anyfront:
            continue
        # clip against the near plane
        m = 0
        for i in range(n):
            j = (i + 1) % n
            ai = cam[i, 2] >= near
            bj = cam[j, 2] >= near
            if ai:
                for c in range(3):
                    outc[m, c] = cam[i, c]
                    outw[m, c] = wld[i, c]
                m += 1
            if ai != bj:
                tt = (near - cam[i, 2]) / (cam[j, 2] - cam[i, 2])
                for c in range(3):
                    outc[m, c] = cam[i, c] + (cam[j, c] - cam[i, c]) * tt
                    outw[m, c] = wld[i, c] + (wld[j, c] - wld[i, c]) * tt
                m += 1
        if m < 3:
            continue
        # screen coords
        sx = np.empty(m)
        sy = np.empty(m)
        iz = np.empty(m)
        for i in range(m):
            iz[i] = 1.0 / outc[i, 2]
            sx[i] = cx + k * outc[i, 0] * iz[i]
            sy[i] = cy - k * outc[i, 1] * iz[i]
        flag = fflag[fi]
        if flag == 1 and drawsky == 0:
            continue
        for tri in range(1, m - 1):
            a, b, c2 = 0, tri, tri + 1
            x0, y0, x1, y1, x2, y2 = sx[a], sy[a], sx[b], sy[b], sx[c2], sy[c2]
            area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
            if abs(area) < 1e-9:
                continue
            minx = max(int(math.floor(min(x0, x1, x2))), 0)
            maxx = min(int(math.ceil(max(x0, x1, x2))), w - 1)
            miny = max(int(math.floor(min(y0, y1, y2))), 0)
            maxy = min(int(math.ceil(max(y0, y1, y2))), h - 1)
            if minx > maxx or miny > maxy:
                continue
            for py in range(miny, maxy + 1):
                fy = py + 0.5
                for px in range(minx, maxx + 1):
                    fx = px + 0.5
                    w0 = ((x1 - fx) * (y2 - fy) - (x2 - fx) * (y1 - fy)) / area
                    w1 = ((x2 - fx) * (y0 - fy) - (x0 - fx) * (y2 - fy)) / area
                    w2 = 1.0 - w0 - w1
                    if w0 < 0 or w1 < 0 or w2 < 0:
                        continue
                    izp = w0 * iz[a] + w1 * iz[b] + w2 * iz[c2]
                    z = 1.0 / izp
                    if z >= depth[py, px]:
                        continue
                    if flag == 1:
                        depth[py, px] = z
                        color[py, px, 0] = 150
                        color[py, px, 1] = 156
                        color[py, px, 2] = 160
                        continue
                    wx = (w0 * outw[a, 0] * iz[a] + w1 * outw[b, 0] * iz[b] + w2 * outw[c2, 0] * iz[c2]) * z
                    wy = (w0 * outw[a, 1] * iz[a] + w1 * outw[b, 1] * iz[b] + w2 * outw[c2, 1] * iz[c2]) * z
                    wz = (w0 * outw[a, 2] * iz[a] + w1 * outw[b, 2] * iz[b] + w2 * outw[c2, 2] * iz[c2]) * z
                    s = wx * fsv[fi, 0] + wy * fsv[fi, 1] + wz * fsv[fi, 2] + fsv[fi, 3]
                    t = wx * ftv[fi, 0] + wy * ftv[fi, 1] + wz * ftv[fi, 2] + ftv[fi, 3]
                    tw, th = ftw[fi], fth[fi]
                    tx = int(math.floor(s)) % tw
                    ty = int(math.floor(t)) % th
                    ti = ftoff[fi] + ty * tw + tx
                    if atlas[ti, 3] == 0:
                        continue
                    depth[py, px] = z
                    lr, lg, lb = 255.0, 255.0, 255.0
                    if flag == 0 and flm[fi] >= 0:
                        ls = s / 16.0 - flms[fi]
                        lt = t / 16.0 - flmt[fi]
                        lw, lh = flmw[fi], flmh[fi]
                        ls = min(max(ls, 0.0), lw - 1.0)
                        lt = min(max(lt, 0.0), lh - 1.0)
                        s0 = int(ls)
                        t0 = int(lt)
                        s1 = min(s0 + 1, lw - 1)
                        t1 = min(t0 + 1, lh - 1)
                        fs = ls - s0
                        ft = lt - t0
                        base = flm[fi]
                        i00 = base + 3 * (t0 * lw + s0)
                        i10 = base + 3 * (t0 * lw + s1)
                        i01 = base + 3 * (t1 * lw + s0)
                        i11 = base + 3 * (t1 * lw + s1)
                        if i11 + 2 < light.shape[0]:
                            lr = ((light[i00] * (1 - fs) + light[i10] * fs) * (1 - ft)
                                  + (light[i01] * (1 - fs) + light[i11] * fs) * ft)
                            lg = ((light[i00 + 1] * (1 - fs) + light[i10 + 1] * fs) * (1 - ft)
                                  + (light[i01 + 1] * (1 - fs) + light[i11 + 1] * fs) * ft)
                            lb = ((light[i00 + 2] * (1 - fs) + light[i10 + 2] * fs) * (1 - ft)
                                  + (light[i01 + 2] * (1 - fs) + light[i11 + 2] * fs) * ft)
                    color[py, px, 0] = min(255.0, atlas[ti, 0] * lr / 255.0 * gain)
                    color[py, px, 1] = min(255.0, atlas[ti, 1] * lg / 255.0 * gain)
                    color[py, px, 2] = min(255.0, atlas[ti, 2] * lb / 255.0 * gain)


def bsp_dir_list(extra=None):
    return [d for d in (extra, os.environ.get("FILM_BSP_DIR"), ROOT / "content" / "dod" / "maps",
                        ROOT / "content" / "valve" / "maps") if d]


def wad_dir_list(extra=None):
    return [d for d in (extra, os.environ.get("FILM_WAD_DIR"), ROOT / "content" / "dod",
                        ROOT / "content" / "valve") if d]


def find_file(name, dirs):
    for d in dirs:
        p = Path(d) / name
        if p.is_file():
            return p
    raise FileNotFoundError(f"{name} not found in {', '.join(str(d) for d in dirs)} (set FILM_BSP_DIR)")


_LEVELS = {}


def level(mapname):
    if mapname not in _LEVELS:
        _LEVELS[mapname] = Level(mapname)
    return _LEVELS[mapname]


def top(lv, size=1400, grid=512):
    """Overhead orthographic view with a labelled grid: for finding coordinates."""
    from PIL import Image, ImageDraw
    span = max(lv.maxs[0] - lv.mins[0], lv.maxs[1] - lv.mins[1])
    cxw, cyw = (lv.maxs[0] + lv.mins[0]) / 2, (lv.maxs[1] + lv.mins[1]) / 2
    # orthographic via a far camera with a narrow lens
    dist = 60000.0
    fov = math.degrees(2 * math.atan((span / 2 + 64) / dist))
    img, _ = lv.render((cxw, cyw, lv.maxs[2] + dist), (90, 90, 0), fov, size, size, sky=False)
    im = Image.fromarray(img)
    d = ImageDraw.Draw(im)
    scale = size / (span + 128)

    def px(x, y):
        return (size / 2 + (x - cxw) * scale, size / 2 - (y - cyw) * scale)
    g0x = math.floor(lv.mins[0] / grid) * grid
    g0y = math.floor(lv.mins[1] / grid) * grid
    x = g0x
    while x <= lv.maxs[0] + grid:
        a = px(x, 0)[0]
        d.line([(a, 0), (a, size)], fill=(255, 255, 0), width=1)
        d.text((a + 2, 2), str(int(x)), fill=(255, 255, 0))
        x += grid
    y = g0y
    while y <= lv.maxs[1] + grid:
        b = px(0, y)[1]
        d.line([(0, b), (size, b)], fill=(0, 255, 255), width=1)
        d.text((2, b + 2), str(int(y)), fill=(0, 255, 255))
        y += grid
    for e in lv.entities:
        cls = e.get("classname", "")
        if cls in ("dod_control_point", "info_player_allies", "info_player_axis") and e.get("origin"):
            ox, oy, _ = (float(v) for v in e["origin"].split())
            p = px(ox, oy)
            col = {"dod_control_point": (255, 60, 60), "info_player_allies": (80, 255, 80)}.get(cls, (80, 160, 255))
            d.ellipse([p[0] - 4, p[1] - 4, p[0] + 4, p[1] + 4], fill=col)
            if cls == "dod_control_point":
                d.text((p[0] + 6, p[1] - 6), e.get("point_name", e.get("targetname", "")), fill=col)
    return im


def shot_frames(ep_path, out_dir, only=None, w=640, h=360):
    """A, mid and B frames of every shot, with its actors' positions marked, plus one contact sheet."""
    from PIL import Image, ImageDraw
    from . import film, actors
    ep = film.load_episode(ep_path)
    tl = film.build_timeline(ep)
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    tiles = []
    for seg in tl["segments"]:
        if seg["kind"] != "shot" or (only and seg["id"] not in only):
            continue
        lv = level(seg["map"])
        row = []
        for label, t in (("A", 0.0), ("M", seg["dur"] / 2), ("B", seg["dur"])):
            o, a, fov = campath.sample(seg["path"], t)
            img, depth = lv.render(o, a, fov, w, h)
            im = Image.fromarray(img)
            d = ImageDraw.Draw(im)
            fw, rt, up = _basis(a)
            k = (w / 2) / math.tan(math.radians(fov) / 2)
            for c in seg.get("cast") or []:
                pos, _, pose, _ = actors.sample(c, t)
                hgt = {"prone": 14, "crawl": 14, "fallen": 10, "crouch": 40, "crouch_walk": 40, "crouch_aim": 40}.get(pose, 64)
                feet = (pos[0], pos[1], pos[2] - 36)
                pts = []
                for zz in (0, hgt):
                    v = np.array([feet[0] - o[0], feet[1] - o[1], feet[2] + zz - o[2]])
                    zc = v @ np.array(fw)
                    if zc < 4:
                        break
                    pts.append((w / 2 + k * (v @ np.array(rt)) / zc, h / 2 - k * (v @ np.array(up)) / zc, zc))
                if len(pts) == 2:
                    (x0, y0, z0), (x1, y1, _) = pts
                    ix, iy = int(x0), int(min(max(y0, 0), h - 1))
                    hidden = 0 <= ix < w and depth[iy, ix] < z0 - 24
                    bw = max(2, (y0 - y1) * 0.22)
                    col = (60, 200, 255) if c["team"] == "axis" else (255, 140, 0)
                    d.rectangle([x0 - bw, y1, x0 + bw, y0], outline=col if not hidden else (90, 90, 90), width=2)
            d.rectangle([0, 0, 260, 16], fill=(0, 0, 0))
            d.text((4, 2), f"{seg['id']} {label} t={t:.1f}s fov={fov:.0f}", fill=(255, 255, 255))
            name = out / f"{seg['id']}_{label}.png"
            im.save(name)
            row.append(im)
        tiles.append(row)
    if tiles:
        sheet = Image.new("RGB", (w * 3, h * len(tiles)), (0, 0, 0))
        for i, row in enumerate(tiles):
            for j, im in enumerate(row):
                sheet.paste(im, (j * w, i * h))
        sheet.save(out / "contact.jpg", quality=85)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    v = sub.add_parser("view")
    v.add_argument("map")
    v.add_argument("x", type=float)
    v.add_argument("y", type=float)
    v.add_argument("z", type=float)
    v.add_argument("--angles", type=float, nargs=3, default=(0, 0, 0))
    v.add_argument("--look", type=float, nargs=3, help="look at this point instead of --angles")
    v.add_argument("--fov", type=float, default=70)
    v.add_argument("--size", type=int, nargs=2, default=(960, 540))
    v.add_argument("-o", "--output", default="view.png")
    s = sub.add_parser("shots")
    s.add_argument("episode")
    s.add_argument("--out")
    s.add_argument("--only", nargs="*")
    t = sub.add_parser("top")
    t.add_argument("map")
    t.add_argument("--grid", type=int, default=512)
    t.add_argument("--size", type=int, default=1400)
    t.add_argument("-o", "--output", default="top.png")
    args = ap.parse_args(argv)
    from PIL import Image
    if args.cmd == "view":
        lv = level(args.map)
        ang = campath.look_angles((args.x, args.y, args.z), args.look) if args.look else args.angles
        img, _ = lv.render((args.x, args.y, args.z), ang, args.fov, *args.size)
        Image.fromarray(img).save(args.output)
        print(args.output)
    elif args.cmd == "top":
        lv = level(args.map)
        top(lv, args.size, args.grid).save(args.output)
        if lv.missing:
            print(f"textures not found: {len(lv.missing)}", file=sys.stderr)
        print(args.output)
    else:
        from . import film
        ep = film.load_episode(args.episode)
        out = args.out or str(ROOT / "out" / "film" / ep["id"] / "scout")
        print(shot_frames(args.episode, out, args.only))


if __name__ == "__main__":
    main()
