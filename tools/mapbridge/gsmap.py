"""Pure-python GoldSrc .map writer (Valve 220 format). No Maya imports, so it
is unit-testable on ceres and reused by the Maya adapter (maya_export.py).

A "brush" is a convex polyhedron given as polygons (lists of 3D points in
GoldSrc units, Z up). Each polygon becomes one clip plane of the brush.
"""
from __future__ import annotations
import math

def _sub(a, b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def _cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def _dot(a, b): return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]
def _len(a): return math.sqrt(_dot(a, a))

def poly_plane(poly):
    """Newell normal + offset for a polygon (outward if CCW seen from outside)."""
    nx = ny = nz = 0.0
    for i, p in enumerate(poly):
        q = poly[(i+1) % len(poly)]
        nx += (p[1]-q[1])*(p[2]+q[2]); ny += (p[2]-q[2])*(p[0]+q[0]); nz += (p[0]-q[0])*(p[1]+q[1])
    n = (nx, ny, nz); l = _len(n)
    if l < 1e-9: return None
    n = (n[0]/l, n[1]/l, n[2]/l)
    c = [sum(p[i] for p in poly)/len(poly) for i in range(3)]
    return n, _dot(n, c)

def _basis(n):
    """Three integer-ish points on the plane, wound so the .map plane faces along n."""
    # pick the axis least aligned with n for a stable tangent
    ax = min(range(3), key=lambda i: abs(n[i]))
    up = [0.0, 0.0, 0.0]; up[ax] = 1.0
    t = _cross(n, tuple(up)); tl = _len(t); t = (t[0]/tl, t[1]/tl, t[2]/tl)
    b = _cross(n, t)
    return t, b

def _axes(n):
    """Quake-style box projection texture axes for dominant normal axis."""
    ax = max(range(3), key=lambda i: abs(n[i]))
    if ax == 2:  return (1, 0, 0), (0, -1, 0)
    if ax == 1:  return (1, 0, 0), (0, 0, -1)
    return (0, 1, 0), (0, 0, -1)

def _fmt(v):
    r = round(v)
    return str(int(r)) if abs(v-r) < 1e-6 else ("%.4f" % v).rstrip("0").rstrip(".")

def brush_text(polys, texture, snap=True):
    """Return the text of one brush. Planes are rebuilt from three points on the
    polygon plane, far enough apart that integer rounding barely tilts them."""
    out = ["{"]
    for poly in polys:
        pl = poly_plane(poly)
        if not pl: continue
        n, d = pl
        if snap:  # snap the plane to the integer grid the way editors do
            n = tuple(round(x, 6) for x in n)
        t, b = _basis(n)
        o = (n[0]*d, n[1]*d, n[2]*d)
        R = 64.0
        # GoldSrc plane points are clockwise seen from outside: p0, p0+b*R? choose
        # so cross(p1-p0, p2-p0) points INTO the brush, i.e. against n.
        p0 = (o[0], o[1], o[2])
        p1 = (o[0]+t[0]*R, o[1]+t[1]*R, o[2]+t[2]*R)
        p2 = (o[0]+b[0]*R, o[1]+b[1]*R, o[2]+b[2]*R)
        if _dot(_cross(_sub(p1, p0), _sub(p2, p0)), n) > 0:
            p1, p2 = p2, p1
        u, v = _axes(n)
        pts = " ".join("( %s %s %s )" % tuple(_fmt(c) for c in p) for p in (p0, p1, p2))
        out.append("%s %s [ %d %d %d 0 ] [ %d %d %d 0 ] 0 1 1" % (pts, texture, *u, *v))
    out.append("}")
    return "\n".join(out)

def entity_text(keys, brushes=()):
    lines = ["{"]
    for k, v in keys.items():
        lines.append('"%s" "%s"' % (k, str(v).replace('"', "'")))
    lines += list(brushes)
    lines.append("}")
    return "\n".join(lines)

def write_map(path, world_brushes, entities, wad, extra_world=None):
    """world_brushes: list of (polys, texture). entities: list of dicts
    {'keys': {...}, 'brushes': [(polys, texture), ...]} (brushes empty = point entity)."""
    world = {"classname": "worldspawn", "mapversion": "220", "wad": wad}
    world.update(extra_world or {})
    parts = [entity_text(world, [brush_text(p, t) for p, t in world_brushes])]
    for e in entities:
        parts.append(entity_text(e["keys"], [brush_text(p, t) for p, t in e.get("brushes", ())]))
    text = "\n".join(parts) + "\n"
    tmp = path + ".tmp"
    with open(tmp, "w", newline="\n") as f: f.write(text)
    import os; os.replace(tmp, path)  # atomic: the watcher never sees a half file
    return text

def box(mins, maxs):
    """Convex box as 6 outward-CCW polygons."""
    x0, y0, z0 = mins; x1, y1, z1 = maxs
    return [
        [(x0,y0,z1),(x1,y0,z1),(x1,y1,z1),(x0,y1,z1)],  # +z
        [(x0,y1,z0),(x1,y1,z0),(x1,y0,z0),(x0,y0,z0)],  # -z
        [(x1,y0,z0),(x1,y1,z0),(x1,y1,z1),(x1,y0,z1)],  # +x
        [(x0,y1,z0),(x0,y0,z0),(x0,y0,z1),(x0,y1,z1)],  # -x
        [(x1,y1,z0),(x0,y1,z0),(x0,y1,z1),(x1,y1,z1)],  # +y
        [(x0,y0,z0),(x1,y0,z0),(x1,y0,z1),(x0,y0,z1)],  # -y
    ]
