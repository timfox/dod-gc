"""Storyboard sheets for an episode: numbered panels drawn from the camera paths the engine will fly.

Each panel is a pencil-style sketch of what the lens sees, projected from the map's waypoint graph (ground mesh,
horizon, stand-in figures), with the frame's aspect, a move arrow, an overhead inset of the camera path, and the
shot's slate: number, shot size, angle, lens, move, duration, action, narration and sound. Moving shots get an
A (start) and B (end) panel. Because the sketch uses the same camera path as the capture, the film frames
the boards.

    tools/film/film.py boards film/episodes/ep01-omaha.json
"""
import math

from . import actors, campath, shots

PAPER = (244, 240, 230)
INK = (40, 38, 34)
SOFT = (120, 114, 104)
ACCENT = (176, 52, 40)
PW, COLS, ROWS = 760, 3, 2


def _vecs(a):
    p, y, r = (math.radians(v) for v in a)
    fwd = (math.cos(p) * math.cos(y), math.cos(p) * math.sin(y), -math.sin(p))
    right0 = (math.sin(y), -math.cos(y), 0.0)
    up0 = (right0[1] * fwd[2] - right0[2] * fwd[1], right0[2] * fwd[0] - right0[0] * fwd[2],
           right0[0] * fwd[1] - right0[1] * fwd[0])  # right x forward
    # roll the right/up pair about forward (positive roll tips the horizon clockwise)
    cr, sr = math.cos(r), math.sin(r)
    right = tuple(cr * right0[i] + sr * up0[i] for i in range(3))
    up = tuple(-sr * right0[i] + cr * up0[i] for i in range(3))
    return fwd, right, up


class Lens:
    def __init__(self, o, a, fov, w, h):
        self.o, self.w, self.h = o, w, h
        self.f, self.r, self.u = _vecs(a)
        self.k = (w / 2) / math.tan(math.radians(fov) / 2)

    def project(self, p):
        d = (p[0] - self.o[0], p[1] - self.o[1], p[2] - self.o[2])
        z = sum(d[i] * self.f[i] for i in range(3))
        if z < 8:
            return None
        x = sum(d[i] * self.r[i] for i in range(3))
        y = sum(d[i] * self.u[i] for i in range(3))
        return (self.w / 2 + self.k * x / z, self.h / 2 - self.k * y / z, z)


def _shade(z, near=200, far=4500):
    t = max(0.0, min(1.0, (z - near) / (far - near)))
    v = int(INK[0] + (PAPER[0] - 30 - INK[0]) * t)
    return (v, v - 2, v - 6)


def sketch(graph, o, a, fov, w, h, figures=(), sea=None):
    """Camera view of the waypoint ground as a pencil drawing."""
    from PIL import Image, ImageDraw
    im = Image.new("RGB", (w, h), PAPER)
    d = ImageDraw.Draw(im)
    lens = Lens(o, a, fov, w, h)
    # horizon: points far away at eye height in every direction ahead
    far = [lens.project((o[0] + 1e6 * math.cos(math.radians(t)), o[1] + 1e6 * math.sin(math.radians(t)), o[2]))
           for t in range(0, 360, 2)]
    far = sorted([p for p in far if p and -w < p[0] < 2 * w], key=lambda p: p[0])
    if len(far) > 1:
        # sea or land below the horizon: light hatching so the frame reads as ground
        sky_line = [(p[0], p[1]) for p in far]
        # tone the ground below the horizon so sky and land separate at a glance
        poly = [(-w, sky_line[0][1])] + sky_line + [(2 * w, sky_line[-1][1]), (2 * w, 4 * h), (-w, 4 * h)]
        d.polygon(poly, fill=(234, 229, 216))
        if sea is not None:
            hz = max(p[1] for p in sky_line)
            for yy in range(0, h, 7):
                if yy > hz + 0.22 * h:  # sea only in the far band below the horizon; nearer is beach
                    break
                pts = [p for p in sky_line if p[1] < yy]
                if pts:
                    d.line([(max(0, min(p[0] for p in pts)), yy), (min(w, max(p[0] for p in pts)), yy)],
                           fill=(214, 210, 200), width=1)
        d.line(sky_line, fill=SOFT, width=2)
    # ground mesh, far to near so near strokes sit on top
    floor = [(x, y, z - 36) for x, y, z in graph.nodes]
    proj = [lens.project(p) for p in floor]
    segs = []
    for i, adj in enumerate(graph.adj):
        pi = proj[i]
        if pi is None:
            continue
        for to, _k, _c in adj:
            pj = proj[to]
            if to > i and pj is not None:
                segs.append(((pi[2] + pj[2]) / 2, pi, pj))
    segs.sort(key=lambda s: -s[0])
    for z, pi, pj in segs:
        if max(abs(pi[0]), abs(pj[0])) > 4 * w or max(abs(pi[1]), abs(pj[1])) > 4 * h:
            continue
        if math.dist(pi[:2], pj[:2]) > 0.45 * w:  # an edge running past the lens, not ground in view
            continue
        d.line([pi[:2], pj[:2]], fill=_shade(z), width=2 if z < 900 else 1)
    # height cues: a short vertical tick at walkable points reads as walls / crests on the bluffs
    for (x, y, zf), p in zip(floor, proj):
        if p and zf > -60 and 0 <= p[0] < w and 0 <= p[1] < h and p[2] < 3000:
            top = lens.project((x, y, zf + 24))
            if top:
                d.line([p[:2], top[:2]], fill=_shade(p[2] + 1200), width=1)
    # stand-in figures (bots on the day): helmet, body, legs at true scale
    # (x, y, z) or (x, y, z, pose): crouched figures are shorter, prone ones lie along the ground
    for fig in sorted(figures, key=lambda q: -math.dist(q[:3], o)):
        fx, fy, fz = fig[:3]
        low = actors.LOW.get(fig[3]) if len(fig) > 3 else None
        feet = lens.project((fx, fy, fz - 36))
        head = lens.project((fx, fy, fz + (30 if not low else 4 if low == "crouch" else -24)))
        if not feet or not head:
            continue
        if low == "prone":
            # a body lying flat: a low lump about a player's length wide at that distance
            span = lens.project((fx, fy, fz + 36))
            if not span or feet[2] < 120:
                continue
            unit = max(2.0, (feet[1] - span[1]) / 72 * 64)
            if unit > 0.9 * w:
                continue
            col = _shade(feet[2] - 600)
            d.ellipse([feet[0] - unit * 0.5, feet[1] - unit * 0.14, feet[0] + unit * 0.5, feet[1]], fill=col)
            d.ellipse([feet[0] + unit * 0.42, feet[1] - unit * 0.17, feet[0] + unit * 0.6, feet[1] - unit * 0.01], fill=col)
            continue
        hgt = feet[1] - head[1]
        if hgt < 3 or hgt > 0.9 * h or feet[2] < 120:  # too small to read, or right on the lens
            continue
        cx = (feet[0] + head[0]) / 2
        wb = hgt * (0.22 if not low else 0.34)
        col = _shade(feet[2] - 600)
        d.ellipse([cx - wb * 0.55, head[1], cx + wb * 0.55, head[1] + hgt * 0.16], fill=col)          # helmet
        d.polygon([(cx - wb * 0.6, head[1] + hgt * 0.17), (cx + wb * 0.6, head[1] + hgt * 0.17),
                   (cx + wb * 0.5, head[1] + hgt * 0.58), (cx - wb * 0.5, head[1] + hgt * 0.58)], fill=col)
        d.line([(cx - wb * 0.25, head[1] + hgt * 0.58), (cx - wb * 0.35, feet[1])], fill=col, width=max(1, int(wb * 0.3)))
        d.line([(cx + wb * 0.25, head[1] + hgt * 0.58), (cx + wb * 0.45, feet[1])], fill=col, width=max(1, int(wb * 0.3)))
    return im


def _overhead(graph, path, t, size, cast=()):
    from PIL import Image, ImageDraw
    (x0, y0, _), (x1, y1, _) = graph.bounds()
    span = max(x1 - x0, y1 - y0) + 300
    s = (size - 12) / span
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    im = Image.new("RGB", (size, size), (250, 248, 242))
    d = ImageDraw.Draw(im)

    def px(p):
        return (size / 2 + (p[0] - cx) * s, size / 2 - (p[1] - cy) * s)
    for x, y, z in graph.nodes[::2]:
        q = px((x, y))
        d.point(q, fill=(170, 164, 152))
    pts = [px(campath.sample(path, path.duration * i / 60)[0]) for i in range(61)]
    d.line(pts, fill=ACCENT, width=2)
    o, a, fov = campath.sample(path, t)
    q = px(o)
    yaw = math.radians(a[1])
    for side in (-1, 1):
        e = yaw + side * math.radians(fov / 2)
        d.line([q, (q[0] + 26 * math.cos(e), q[1] - 26 * math.sin(e))], fill=INK, width=1)
    d.ellipse([q[0] - 3, q[1] - 3, q[0] + 3, q[1] + 3], fill=INK)
    for fx, fy, _fz, _pose in actors.figures(cast, t):
        f = px((fx, fy))
        d.ellipse([f[0] - 1.5, f[1] - 1.5, f[0] + 1.5, f[1] + 1.5], fill=(90, 120, 60))
    d.rectangle([0, 0, size - 1, size - 1], outline=SOFT)
    return im


def _arrow(d, a, b, col, width=4):
    d.line([a, b], fill=col, width=width)
    ang = math.atan2(b[1] - a[1], b[0] - a[0])
    for s in (-1, 1):
        e = ang + math.pi + s * 0.45
        d.line([b, (b[0] + 18 * math.cos(e), b[1] + 18 * math.sin(e))], fill=col, width=width)


def move_label(spec):
    t = spec["type"]
    if t == "still":
        fe = spec.get("fov_end", spec.get("lens_end"))
        if fe is None:
            return "Locked off"
        return "Zoom in (push)" if ("lens_end" in spec and spec["lens_end"] > spec.get("lens", 0)) or \
            ("fov_end" in spec and spec["fov_end"] < spec.get("fov", 99)) else "Zoom out (pull)"
    return {"dolly": "Dolly / track", "orbit": "Arc around subject", "crane": "Crane up" if spec.get("to_height", 1) >=
            spec.get("from_height", 0) else "Crane down", "pan": "Pan", "keys": "Keyed move"}[t]


def panels(timeline):
    """One or two panels per shot: (seg, label, t)."""
    out = []
    n = 0
    for seg in timeline["segments"]:
        if seg["kind"] != "shot":
            continue
        n += 1
        b = seg["shot"].get("board", {})
        no = b.get("no", str(n))
        cam = seg["shot"]["camera"]
        moving = cam["type"] in ("dolly", "crane", "orbit", "pan", "keys") or (cam["type"] == "still" and
                                                                                 ("fov_end" in cam or "lens_end" in cam))
        if moving and seg["dur"] >= 4:
            out.append((seg, f"{no}A", 0.0))
            out.append((seg, f"{no}B", seg["dur"]))
        else:
            out.append((seg, no, seg["dur"] * 0.5))
    return out


def render_sheets(ep, timeline, out_dir, anchors):
    from PIL import Image, ImageDraw
    from . import film
    aspect = film.ASPECTS[ep.get("aspect", "16:9")]
    fh = int(PW / aspect)
    text_h = 205
    margin, gap = 50, 34
    W = margin * 2 + COLS * PW + (COLS - 1) * gap
    H = 150 + ROWS * (fh + text_h + gap) + 40
    f_title = film._font(film.FONT_SERIF_B, 40)
    f_no = film._font(film.FONT_SANS_B, 30)
    f_slate = film._font(film.FONT_SANS_B, 17)
    f_body = film._font(film.FONT_SERIF, 18)
    f_vo = film._font(film.FONT_SERIF, 17)
    f_small = film._font(film.FONT_SANS, 15)
    items = panels(timeline)
    per = COLS * ROWS
    pages = []
    for pg in range(0, len(items), per):
        page = Image.new("RGB", (W, H), (255, 253, 248))
        d = ImageDraw.Draw(page)
        d.text((margin, 44), ep["title"], font=f_title, fill=INK)
        sub = f"Storyboard · sheet {pg // per + 1} of {math.ceil(len(items) / per)} · {ep.get('aspect', '16:9')} · {timeline['fps']} fps"
        d.text((W - margin - d.textlength(sub, font=f_small), 62), sub, font=f_small, fill=SOFT)
        d.line([(margin, 108), (W - margin, 108)], fill=SOFT, width=1)
        for i, (seg, label, t) in enumerate(items[pg:pg + per]):
            col, row = i % COLS, i // COLS
            x = margin + col * (PW + gap)
            y = 140 + row * (fh + text_h + gap)
            shot = seg["shot"]
            cam = shot["camera"]
            b = shot.get("board", {})
            graph = timeline["graphs"][seg["map"]]
            path = seg["path"]
            o, a, fov = campath.sample(path, t)
            # the cast where the shot has one; otherwise the board's own stand-ins
            figs = actors.figures(seg["cast"], t) if seg.get("cast") else \
                [shots._up(shots.resolve(p, graph, anchors.get(seg["map"], {})), 0) for p in b.get("figures", [])]
            img = sketch(graph, o, a, fov, PW, fh, figs, sea=b.get("sea"))
            pd = ImageDraw.Draw(img)
            # move arrow on the end panel of a camera move
            if label.endswith("B") and cam["type"] != "still":
                _arrow(pd, (PW / 2 - 60, fh - 40), (PW / 2 + 60, fh - 40), ACCENT)
            if cam["type"] == "still" and ("fov_end" in cam or "lens_end" in cam) and label.endswith("B"):
                inset = 0.18 if move_label(cam).startswith("Zoom in") else -0.12
                mx, my = PW * abs(inset), fh * abs(inset)
                pd.rectangle([mx, my, PW - mx, fh - my], outline=ACCENT, width=2)
            img.paste(_overhead(graph, path, t, 120, seg.get("cast") or ()), (PW - 128, 8))
            page.paste(img, (x, y))
            d.rectangle([x - 1, y - 1, x + PW, y + fh], outline=INK, width=2)
            # slate
            ty = y + fh + 12
            d.text((x, ty), label, font=f_no, fill=ACCENT)
            lens_mm = cam.get("lens") or round(shots.fov_lens(fov))
            if "lens" in cam and "lens_end" in cam:
                lens_txt = f"{cam['lens']}>{cam['lens_end']}mm"
            else:
                lens_txt = f"{lens_mm}mm"
            slate = " · ".join(v for v in (b.get("size"), b.get("angle"), lens_txt, move_label(cam),
                                           "Handheld" if cam.get("handheld") else None,
                                           f"Dutch {cam['dutch']}°" if cam.get("dutch") else None,
                                           f"{seg['dur']:.0f}s") if v)
            sl = film._wrap(d, slate.upper(), f_slate, PW - 80)
            for k, ln in enumerate(sl[:2]):
                d.text((x + 74, ty + 8 + 21 * k), ln, font=f_slate, fill=INK)
            yy = ty + 46 + 21 * (min(len(sl), 2) - 1)
            for ln in film._wrap(d, b.get("action", shot.get("note", "")), f_body, PW)[:3]:
                d.text((x, yy), ln, font=f_body, fill=INK)
                yy += 24
            vo = " ".join(n["text"] for n in seg["narration"])
            if vo and not label.endswith("B"):
                yy += 4
                for k, ln in enumerate(film._wrap(d, "VO: " + vo, f_vo, PW)[:4]):
                    d.text((x, yy), ln, font=f_vo, fill=SOFT)
                    yy += 22
            elif label.endswith("B"):
                yy += 4
                d.text((x, yy), "(end of move)", font=f_vo, fill=SOFT)
                yy += 22
            if b.get("sound") and not label.endswith("B"):
                d.text((x, min(yy + 2, ty + text_h - 26)), "SFX: " + b["sound"], font=f_small, fill=SOFT)
        p = out_dir / f"storyboard-{pg // per + 1:02d}.png"
        page.save(p)
        pages.append(p)
    return pages


def cast_label(cast):
    """"3 Allies, 1 Axis" for the shot list."""
    if not cast:
        return ""
    n = {t: sum(1 for a in cast if a["team"] == t) for t in actors.TEAMS}
    return ", ".join(f"{c} {t.capitalize()}" for t, c in n.items() if c)


def shot_list(ep, timeline):
    """The boards as a table: what a 1st AD would hand round."""
    rows = ["| # | Shot | Size | Angle | Lens | Move | Dur | Cast | Action |", "|---|---|---|---|---|---|---|---|---|"]
    n = 0
    for seg in timeline["segments"]:
        if seg["kind"] != "shot":
            continue
        n += 1
        b, cam = seg["shot"].get("board", {}), seg["shot"]["camera"]
        o, a, fov = campath.sample(seg["path"], 0)
        lens = f"{cam['lens']}>{cam['lens_end']}mm" if "lens" in cam and "lens_end" in cam else \
            f"{cam.get('lens') or round(shots.fov_lens(fov))}mm"
        move = move_label(cam) + (", handheld" if cam.get("handheld") else "") + \
            (f", dutch {cam['dutch']}°" if cam.get("dutch") else "")
        rows.append(f"| {b.get('no', n)} | `{seg['id']}` | {b.get('size', '')} | {b.get('angle', '')} | {lens} | {move} "
                    f"| {seg['dur']:.0f}s | {cast_label(seg.get('cast') or [])} | {b.get('action', seg['shot'].get('note', ''))} |")
    return f"# {ep['title']}: shot list\n\n" + "\n".join(rows) + "\n"
