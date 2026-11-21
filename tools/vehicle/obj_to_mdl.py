"""A vehicle mesh (OBJ from fbx_parts.py, one group per loose part) and its skin -> a GoldSrc MDL v10.

    python3 tools/vehicle/obj_to_mdl.py panzeriv.obj diffuse.png out/panzer4.mdl \
        --up y --front -x --length 276 [--name panzer4] [--report parts.txt]

What the game needs from a cinematic vehicle (dod_film_actor.cpp DodFilm_PlaceVehicle):
- GoldSrc axes: Z up, the gun (front) along +X, the origin on the ground under the middle of the hull;
- real size: --length is the whole vehicle with its gun, in units (1 unit = 1 inch; a Panzer IV H with the
  long 7.5 cm gun is 7.02 m = 276 units);
- two bones, "hull" and "turret". The turret bone sits at the turret ring (the turret body's footprint centre,
  at its floor) and bone controller 0 turns it about Z, -180..180 degrees, so the game can traverse it;
- one sequence ("idle", one frame) and one 8-bit skin (the PNG quantised to 256 colours, 256x256 or smaller).

The turret is found from the parts: the turret body is the largest part (by footprint) sitting above the
hull deck; the turret is that part, every part standing on it inside its footprint, and the gun (the part that
reaches furthest forward from the turret's height band). Everything else is hull. --report lists each part
with its bone so a wrong split is easy to see; --turret-parts names them by hand instead.

No studiomdl: nothing on the build machines compiles an MDL, and a static two-bone model is small enough to
write directly (layout as hlsdk engine/studio.h, version 10).
"""
import argparse
import math
import struct
import sys

STUDIO_ZR = 0x0020


def read_obj(path):
    parts, cur = [], None
    vs, vts, textures = [], [], []
    for line in open(path):
        if line.startswith("# texture "):
            textures.append(line[len("# texture "):].strip())
            continue
        t = line.split()
        if not t:
            continue
        if t[0] == "g":
            cur = {"name": t[1] if len(t) > 1 else f"part_{len(parts):03d}", "tris": []}
            parts.append(cur)
        elif t[0] == "v":
            vs.append(tuple(float(x) for x in t[1:4]))
        elif t[0] == "vt":
            vts.append((float(t[1]), float(t[2])))
        elif t[0] == "f":
            if cur is None:
                cur = {"name": "part_000", "tris": []}
                parts.append(cur)
            corners = []
            for c in t[1:]:
                a = c.split("/")
                vi = int(a[0]) - 1
                ti = int(a[1]) - 1 if len(a) > 1 and a[1] else -1
                corners.append((vi, ti))
            for k in range(1, len(corners) - 1):   # fan any polygon into triangles
                cur["tris"].append((corners[0], corners[k], corners[k + 1]))
    return parts, vs, vts, textures


AXES = {"x": (1, 0, 0), "-x": (-1, 0, 0), "y": (0, 1, 0), "-y": (0, -1, 0), "z": (0, 0, 1), "-z": (0, 0, -1)}


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def to_goldsrc(vs, up, front):
    """Z up, front along +X, Y = Z x X (left)."""
    u, f = AXES[up], AXES[front]
    if abs(_dot(u, f)) > 0.5:
        sys.exit("error: --up and --front must be different axes")
    side = _cross(u, f)
    return [(_dot(p, f), _dot(p, side), _dot(p, u)) for p in vs]


def _bbox(pts):
    xs, ys, zs = zip(*pts)
    return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))


def classify(parts, vs, names=None):
    """{part index: 0 hull or 1 turret}, and the turret body's index."""
    boxes = []
    for p in parts:
        pts = [vs[c[0]] for tri in p["tris"] for c in tri]
        boxes.append(_bbox(pts))
    if names:
        tur = {i for i, p in enumerate(parts) if p["name"] in names}
        body = max(tur, key=lambda i: (boxes[i][1][0] - boxes[i][0][0]) * (boxes[i][1][1] - boxes[i][0][1]))
        return {i: (1 if i in tur else 0) for i in range(len(parts))}, body
    lo, hi = _bbox([b[0] for b in boxes] + [b[1] for b in boxes])
    height = hi[2] - lo[2]
    # the hull deck: the top of the biggest part by footprint (the hull itself)
    area = [(b[1][0] - b[0][0]) * (b[1][1] - b[0][1]) for b in boxes]
    hull = max(range(len(parts)), key=lambda i: area[i] * (1.0 if boxes[i][0][2] < lo[2] + 0.4 * height else 0.0))
    deck = boxes[hull][1][2]
    above = [i for i in range(len(parts)) if boxes[i][0][2] >= deck - 0.08 * height and i != hull]
    if not above:
        sys.exit("error: no part sits on the hull deck; name the turret with --turret-parts")
    body = max(above, key=lambda i: area[i])
    (bx0, by0, bz0), (bx1, by1, bz1) = boxes[body]
    mx, my = 0.1 * (bx1 - bx0), 0.1 * (by1 - by0)
    tur = {body}
    for i in above:
        (x0, y0, z0), (x1, y1, z1) = boxes[i]
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        if bx0 - mx <= cx <= bx1 + mx and by0 - my <= cy <= by1 + my and z0 >= bz0 - 0.05 * height:
            tur.add(i)
    # the gun: the part reaching furthest forward whose middle height is in the turret's band
    band = [i for i in range(len(parts)) if bz0 - 0.05 * height <= (boxes[i][0][2] + boxes[i][1][2]) / 2 <= bz1]
    gun = max(band, key=lambda i: boxes[i][1][0])
    if boxes[gun][1][0] > bx1:
        tur.add(gun)
    return {i: (1 if i in tur else 0) for i in range(len(parts))}, body


def quantise(png, size):
    from PIL import Image
    im = Image.open(png).convert("RGB")
    if im.size != (size, size):
        im = im.resize((size, size), Image.LANCZOS)
    q = im.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = (q.getpalette() or [])[:768]
    pal += [0] * (768 - len(pal))
    return bytes(q.tobytes()), bytes(pal)


def build(parts, vs, vts, skin_png, name, length, skin_size=256, turret_names=None, report=None):
    side_of, body = classify(parts, vs, turret_names)
    # scale and place: the whole length along X; the hull's footprint centred on the origin, ground at z 0
    lo, hi = _bbox([vs[c[0]] for p in parts for tri in p["tris"] for c in tri])
    k = length / (hi[0] - lo[0])
    hull_pts = [vs[c[0]] for i, p in enumerate(parts) if side_of[i] == 0 for tri in p["tris"] for c in tri]
    hlo, hhi = _bbox(hull_pts)
    ox, oy, oz = (hlo[0] + hhi[0]) / 2, (hlo[1] + hhi[1]) / 2, lo[2]

    def place(p):
        return ((p[0] - ox) * k, (p[1] - oy) * k, (p[2] - oz) * k)

    blo, bhi = _bbox([place(vs[c[0]]) for tri in parts[body]["tris"] for c in tri])
    ring = ((blo[0] + bhi[0]) / 2, (blo[1] + bhi[1]) / 2, blo[2])
    if report:
        with open(report, "w") as fh:
            for i, p in enumerate(parts):
                plo, phi = _bbox([place(vs[c[0]]) for tri in p["tris"] for c in tri])
                fh.write(f"{p['name']}\t{'turret' if side_of[i] else 'hull'}\t{len(p['tris'])} tris\t"
                         f"x {plo[0]:.0f}..{phi[0]:.0f} y {plo[1]:.0f}..{phi[1]:.0f} z {plo[2]:.0f}..{phi[2]:.0f}"
                         + ("\tturret body" if i == body else "") + "\n")
            fh.write(f"turret ring at {ring[0]:.1f} {ring[1]:.1f} {ring[2]:.1f}; scale {k:.3f}\n")

    # vertices per bone, in that bone's space (the hull at the origin, the turret at the ring)
    verts, vbone, norms, nbone, tris = [], [], [], [], []
    vkey = {}
    for i, p in enumerate(parts):
        bone = side_of[i]
        base = ring if bone else (0.0, 0.0, 0.0)
        for tri in p["tris"]:
            pts = [place(vs[c[0]]) for c in tri]
            e1 = [pts[1][j] - pts[0][j] for j in range(3)]
            e2 = [pts[2][j] - pts[0][j] for j in range(3)]
            n = _cross(e1, e2)
            ln = math.sqrt(_dot(n, n)) or 1.0
            n = tuple(x / ln for x in n)
            ni = len(norms)
            norms.append(n)
            nbone.append(bone)
            corner = []
            for c, q in zip(tri, pts):
                local = (q[0] - base[0], q[1] - base[1], q[2] - base[2])
                key = (bone, round(local[0], 3), round(local[1], 3), round(local[2], 3))
                vi = vkey.get(key)
                if vi is None:
                    vi = vkey[key] = len(verts)
                    verts.append(local)
                    vbone.append(bone)
                u, v = vts[c[1]] if c[1] >= 0 else (0.0, 0.0)
                s = min(skin_size - 1, max(0, int(round((u % 1.0 if u != 1.0 else 1.0) * (skin_size - 1)))))
                t = min(skin_size - 1, max(0, int(round((1.0 - (v % 1.0 if v != 1.0 else 1.0)) * (skin_size - 1)))))
                corner.append((vi, ni, s, t))
            tris.append(corner)
    pix, pal = quantise(skin_png, skin_size)
    world = [place(vs[c[0]]) for p in parts for tri in p["tris"] for c in tri]
    return write_mdl(name, verts, vbone, norms, nbone, tris, ring, pix, pal, skin_size, _bbox(world)), len(tris)


def write_mdl(name, verts, vbone, norms, nbone, tris, ring, pix, pal, skin_size, box):
    out = bytearray(244)   # the header, filled in last

    def align(n=4):
        while len(out) % n:
            out.append(0)

    def name_bytes(s, n):
        b = s.encode()[:n - 1]
        return b + b"\0" * (n - len(b))

    # bones: hull (root) and turret (child, at the ring); controller 0 turns the turret about Z
    boneindex = len(out)
    for bi, (bname, parent, pos) in enumerate((("hull", -1, (0.0, 0.0, 0.0)), ("turret", 0, ring))):
        ctl = [-1] * 6
        if bi == 1:
            ctl[5] = 0
        out += name_bytes(bname, 32) + struct.pack("<ii", parent, 0) + struct.pack("<6i", *ctl)
        out += struct.pack("<6f", pos[0], pos[1], pos[2], 0.0, 0.0, 0.0)
        out += struct.pack("<6f", 1.0, 1.0, 1.0, 1.0, 1.0, 1.0)
    ctlindex = len(out)
    out += struct.pack("<iiffii", 1, STUDIO_ZR, -180.0, 180.0, 0, 0)
    # one hitbox round the whole vehicle (bbox for culling and traces)
    hbindex = len(out)
    out += struct.pack("<ii6f", 0, 0, *box[0], *box[1])
    # sequence group 0 and the one sequence, its animation all bind pose (offsets 0)
    sgindex = len(out)
    out += name_bytes("default", 32) + name_bytes("", 64) + struct.pack("<ii", 0, 0)
    animindex = len(out)
    out += b"\0" * (12 * 2)
    seqindex = len(out)
    seq = name_bytes("idle", 32) + struct.pack("<fiiiiiiiiii", 10.0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0)
    seq += struct.pack("<3f", 0, 0, 0) + struct.pack("<ii", 0, 0) + struct.pack("<6f", *box[0], *box[1])
    seq += struct.pack("<ii", 1, animindex) + struct.pack("<2i", 0, 0) + struct.pack("<2f", 0, 0) + struct.pack("<2f", 1, 1)
    seq += struct.pack("<iiiiii", 0, 0, 0, 0, 0, 0)
    assert len(seq) == 176
    out += seq
    # bodypart, model, mesh
    bpindex = len(out)
    out += name_bytes("body", 64) + struct.pack("<iii", 1, 1, 0)
    modelindex = len(out)
    out += b"\0" * 112
    meshindex = len(out)
    out += b"\0" * 20
    # triangle commands: one 3-vertex strip per triangle
    align()
    triindex = len(out)
    for tri in tris:
        out += struct.pack("<h", 3)
        for vi, ni, s, t in tri:
            out += struct.pack("<4h", vi, ni, s, t)
    out += struct.pack("<h", 0)
    align()
    vinfo = len(out)
    out += bytes(vbone)
    align()
    ninfo = len(out)
    out += bytes(nbone)
    align()
    vindex = len(out)
    for v in verts:
        out += struct.pack("<3f", *v)
    nindex = len(out)
    for n in norms:
        out += struct.pack("<3f", *n)
    struct.pack_into("<iiiii", out, meshindex, len(tris), triindex, 0, len(norms), 0)
    radius = max(math.sqrt(_dot(v, v)) for v in verts)
    struct.pack_into("<64sif10i", out, modelindex, name_bytes(name, 64), 0, radius, 1, meshindex, len(verts), vinfo,
                     vindex, len(norms), ninfo, nindex, 0, 0)
    struct.pack_into("<i", out, bpindex + 72, modelindex)
    # skin: the texture header, the skin table, then the pixels and palette
    texindex = len(out)
    texdata_at = None
    out += name_bytes(name + ".bmp", 64) + struct.pack("<iiii", 0, skin_size, skin_size, 0)
    skinindex = len(out)
    out += struct.pack("<h", 0)
    align()
    texdata_at = len(out)
    struct.pack_into("<i", out, texindex + 76, texdata_at)
    out += pix + pal
    align()
    # header
    hdr = struct.pack("<4si64si", b"IDST", 10, name_bytes(name + ".mdl", 64), len(out))
    hdr += struct.pack("<3f", 0, 0, 0) + struct.pack("<3f", *box[0]) + struct.pack("<3f", *box[1])
    hdr += struct.pack("<3f", *box[0]) + struct.pack("<3f", *box[1]) + struct.pack("<i", 0)
    hdr += struct.pack("<26i", 2, boneindex, 1, ctlindex, 1, hbindex, 1, seqindex, 1, sgindex,
                       1, texindex, texdata_at, 1, 1, skinindex, 1, bpindex, 0, 0, 0, 0, 0, 0, 0, 0)
    assert len(hdr) == 244
    out[0:244] = hdr
    return bytes(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("obj")
    ap.add_argument("skin")
    ap.add_argument("out")
    ap.add_argument("--name", default=None)
    ap.add_argument("--up", default="z", choices=sorted(AXES))
    ap.add_argument("--front", default="x", choices=sorted(AXES))
    ap.add_argument("--length", type=float, required=True, help="whole length with the gun, in units")
    ap.add_argument("--skin-size", type=int, default=256)
    ap.add_argument("--turret-parts", default=None, help="comma-separated group names that turn with the turret")
    ap.add_argument("--report", default=None)
    a = ap.parse_args(argv)
    parts, vs, vts, _ = read_obj(a.obj)
    vs = to_goldsrc(vs, a.up, a.front)
    name = a.name or a.out.rsplit("/", 1)[-1].rsplit(".", 1)[0]
    names = set(a.turret_parts.split(",")) if a.turret_parts else None
    data, ntris = build(parts, vs, vts, a.skin, name, a.length, a.skin_size, names, a.report)
    with open(a.out, "wb") as fh:
        fh.write(data)
    print(f"obj_to_mdl: {len(parts)} parts, {ntris} triangles -> {a.out} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
