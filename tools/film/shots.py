"""Camera moves for a shot, from a few numbers and the map's bot waypoint graph.

Every generator returns a campath.Path the engine can play. Points in a shot spec are any of:

    [x, y]          a spot on the map; z comes from the nearest waypoint (player origin height)
    [x, y, z]       an exact point
    "anchor-name"   a point named in the episode's "anchors" for this map
    {"node": 123}   a waypoint by index

Waypoint z is the standing player's origin (36 above the floor); eye height is 28 above that.
"""
import importlib.util
import math
from pathlib import Path as FsPath

from . import campath

ROOT = FsPath(__file__).resolve().parents[2]
EYE = 28.0


def _gen_waypoints():
    spec = importlib.util.spec_from_file_location("gen_waypoints", ROOT / "tools" / "gen_waypoints.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class MapGraph:
    """A map's waypoint graph (data/waypoints/<map>.wpt): floor positions and walkable links."""

    def __init__(self, nodes, adj, name="?"):
        self.nodes, self.adj, self.name = nodes, adj, name
        self._gw = None

    @classmethod
    def load(cls, mapname, root=ROOT):
        gw = _gen_waypoints()
        data = (FsPath(root) / "data" / "waypoints" / f"{mapname}.wpt").read_bytes()
        g = gw.read_wpt(data)
        graph = cls(g["nodes"], g["adj"], mapname)
        graph._gw = gw
        return graph

    def nearest(self, x, y, z=None):
        def d(n):
            dd = (n[0] - x) ** 2 + (n[1] - y) ** 2
            return dd + ((n[2] - z) ** 2 if z is not None else 0.0)
        return min(range(len(self.nodes)), key=lambda i: d(self.nodes[i]))

    def route(self, a, b):
        gw = self._gw or _gen_waypoints()
        path = gw.shortest_path(self.nodes, self.adj, a, b)
        if path is None:
            raise ValueError(f"{self.name}: no walkable route between waypoints {a} and {b}")
        return [self.nodes[i] for i in path]

    def bounds(self):
        xs = [n[0] for n in self.nodes]
        ys = [n[1] for n in self.nodes]
        zs = [n[2] for n in self.nodes]
        return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))


def resolve(point, graph, anchors=None):
    """A shot-spec point -> (x, y, z)."""
    if isinstance(point, str):
        if not anchors or point not in anchors:
            raise ValueError(f"unknown anchor {point!r}")
        return resolve(anchors[point], graph, anchors)
    if isinstance(point, dict) and "node" in point:
        return tuple(float(v) for v in graph.nodes[int(point["node"])])
    if len(point) == 3:
        return tuple(float(v) for v in point)
    if len(point) == 2:
        n = graph.nodes[graph.nearest(point[0], point[1])]
        return (float(point[0]), float(point[1]), float(n[2]))
    raise ValueError(f"bad point {point!r}")


def _up(p, h):
    return (p[0], p[1], p[2] + h)


def _resample(points, spacing):
    """Points along a polyline every `spacing` units, ends kept."""
    out = [points[0]]
    carry = 0.0
    for a, b in zip(points, points[1:]):
        seg = math.dist(a, b)
        if seg <= 0:
            continue
        d = spacing - carry
        while d <= seg:
            s = d / seg
            out.append(tuple(a[i] + (b[i] - a[i]) * s for i in range(3)))
            d += spacing
        carry = (carry + seg) % spacing
    if math.dist(out[-1], points[-1]) > spacing * 0.25:
        out.append(points[-1])
    else:
        out[-1] = points[-1]
    return out


def _smooth(points, passes=2):
    """Moving average over neighbours (ends fixed): takes the stair-steps out of a grid route."""
    pts = list(points)
    for _ in range(passes):
        pts = [pts[0]] + [tuple((pts[i - 1][c] + 2 * pts[i][c] + pts[i + 1][c]) / 4 for c in range(3))
                          for i in range(1, len(pts) - 1)] + [pts[-1]]
    return pts


def dolly(graph, start, end, height=EYE, speed=110.0, look="ahead", lookahead=320.0, pitch=4.0, fov=70.0,
          anchors=None, max_keys=40, duration=None):
    """Glide along the walkable route from start to end, `height` above the player origin.

    look: "ahead" (down the route), "back" (toward where it came from), or a point to keep framed.
    duration: stretch or squeeze the move to this many seconds (otherwise length / speed).
    """
    a = resolve(start, graph, anchors)
    b = resolve(end, graph, anchors)
    route = [a] + graph.route(graph.nearest(*a), graph.nearest(*b)) + [b]
    route = [_up(p, height) for p in route]
    length = sum(math.dist(p, q) for p, q in zip(route, route[1:]))
    spacing = max(96.0, length / (max_keys - 1))
    pts = _smooth(_resample(route, spacing))
    target = None if isinstance(look, str) else _up(resolve(look, graph, anchors), EYE)
    keys, t = [], 0.0
    cum = [0.0]
    for p, q in zip(pts, pts[1:]):
        cum.append(cum[-1] + math.dist(p, q))
    for i, p in enumerate(pts):
        if i:
            t += math.dist(pts[i - 1], p) / speed
        if target is not None:
            ang = campath.look_angles(p, target)
        else:
            # aim at the route point `lookahead` further along (or back)
            want = cum[i] + (lookahead if look != "back" else -lookahead)
            j = min(range(len(pts)), key=lambda k: abs(cum[k] - want))
            if j == i:
                j = min(i + 1, len(pts) - 1) if look != "back" else max(i - 1, 0)
            if j == i:
                ang = keys[-1].a if keys else (pitch, 0.0, 0.0)
            else:
                q = pts[j]
                ang = (pitch, math.degrees(math.atan2(q[1] - p[1], q[0] - p[0])), 0.0)
        keys.append(campath.Key(round(t, 3), p, ang, fov))
    # steady the heading: the route's grid turns would otherwise wobble the camera left and right
    yaws = [keys[0].a[1]]
    for k in keys[1:]:
        y = k.a[1]
        while y - yaws[-1] > 180:
            y -= 360
        while y - yaws[-1] < -180:
            y += 360
        yaws.append(y)
    for _ in range(3):
        yaws = [yaws[0]] + [(yaws[i - 1] + 2 * yaws[i] + yaws[i + 1]) / 4 for i in range(1, len(yaws) - 1)] + [yaws[-1]]
    if target is None:
        for k, y in zip(keys, yaws):
            k.a = (k.a[0], y, k.a[2])
    # a set duration, or a minimum so a very short route still plays as a move, not a flash
    want = float(duration) if duration else max(keys[-1].t, 2.0)
    if abs(keys[-1].t - want) > 1e-3:
        scale = want / max(keys[-1].t, 1e-3)
        for k in keys:
            k.t = round(k.t * scale, 3)
    return campath.Path(campath.validate(keys))


def orbit(graph, center, radius=400.0, height=160.0, start_deg=0.0, end_deg=90.0, duration=10.0, fov=60.0,
          anchors=None):
    """Circle a point, always facing it. Angles are map yaw degrees of the camera's position around it."""
    c = _up(resolve(center, graph, anchors), EYE)
    steps = max(2, int(abs(end_deg - start_deg) / 15.0) + 1)
    keys = []
    for i in range(steps):
        s = i / (steps - 1)
        th = math.radians(start_deg + (end_deg - start_deg) * s)
        p = (c[0] + radius * math.cos(th), c[1] + radius * math.sin(th), c[2] + height)
        keys.append(campath.Key(round(duration * s, 3), p, campath.look_angles(p, c), fov))
    return campath.Path(campath.validate(keys))


def crane(graph, at, target, from_height=40.0, to_height=600.0, duration=8.0, fov=65.0, anchors=None, drift=None):
    """Rise straight up (optionally drifting toward a point) while holding a target in frame."""
    base = resolve(at, graph, anchors)
    tgt = _up(resolve(target, graph, anchors), EYE)
    end = resolve(drift, graph, anchors) if drift is not None else base
    keys = []
    for i in range(4):
        s = i / 3
        p = (base[0] + (end[0] - base[0]) * s, base[1] + (end[1] - base[1]) * s,
             base[2] + from_height + (to_height - from_height) * s)
        keys.append(campath.Key(round(duration * s, 3), p, campath.look_angles(p, tgt), fov))
    return campath.Path(campath.validate(keys))


def still(graph, eye, target, duration=6.0, fov=70.0, fov_end=None, height=EYE, anchors=None):
    """A locked-off shot; with fov_end, a slow push in (or out) by zoom."""
    p = _up(resolve(eye, graph, anchors), height)
    tgt = _up(resolve(target, graph, anchors), EYE)
    ang = campath.look_angles(p, tgt)
    return campath.Path(campath.validate([
        campath.Key(0.0, p, ang, fov, campath.LINEAR),
        campath.Key(float(duration), p, ang, fov_end if fov_end is not None else fov),
    ]))


def pan(graph, eye, from_target, to_target, duration=8.0, fov=70.0, height=EYE, anchors=None):
    """Turn on the spot from one point to another."""
    p = _up(resolve(eye, graph, anchors), height)
    a0 = campath.look_angles(p, _up(resolve(from_target, graph, anchors), EYE))
    a1 = campath.look_angles(p, _up(resolve(to_target, graph, anchors), EYE))
    return campath.Path(campath.validate([
        campath.Key(0.0, p, a0, fov), campath.Key(float(duration), p, a1, fov)]))


def from_keys(rows):
    """Keys written by hand or with cine_mark: [[t, x, y, z, pitch, yaw, roll, fov, mode?], ...]."""
    keys = [campath.Key(r[0], tuple(r[1:4]), tuple(r[4:7]), r[7], r[8] if len(r) > 8 else campath.SMOOTH)
            for r in rows]
    return campath.Path(campath.validate(keys))


GENERATORS = {"dolly": dolly, "orbit": orbit, "crane": crane, "still": still, "pan": pan}


# --- film grammar on top of a move ---------------------------------------------------------------------
SENSOR_W = 36.0  # full-frame still / large-format cine width: a 35mm lens reads like it does on set


def lens_fov(mm):
    """Horizontal field of view of a focal length on a 36 mm wide sensor (24 mm ~ 74, 50 mm ~ 40, 85 mm ~ 24)."""
    return math.degrees(2 * math.atan(SENSOR_W / (2.0 * float(mm))))


def fov_lens(fov):
    return SENSOR_W / (2 * math.tan(math.radians(fov) / 2))


def dutch(path, roll):
    """Tilt the horizon: roll degrees, or [start, end] to roll during the shot."""
    r0, r1 = (roll, roll) if not isinstance(roll, (list, tuple)) else roll
    T = path.duration or 1.0
    keys = [campath.Key(k.t, k.o, (k.a[0], k.a[1], k.a[2] + r0 + (r1 - r0) * (k.t / T)), k.fov, k.mode)
            for k in path.keys]
    return campath.Path(campath.validate(keys))


def handheld(path, amount=0.5, seed=1, rate=4.0):
    """Operator shake: resample the move and add slow, smooth drift (no frame-to-frame jitter).

    amount 0.2 is a steadied shoulder rig, 0.5 a walking operator, 1.0 running. Deterministic per seed so a
    re-capture matches the board.
    """
    import random
    rng = random.Random(seed)
    T = path.duration
    n = min(campath.MAX_KEYS, max(2, int(T * rate) + 1))

    def drift(scale):
        # sum of a few slow sines with random phase: reads as breathing and footfall, not noise
        parts = [(rng.uniform(0.25, 0.6), rng.uniform(0, 6.28), 1.0), (rng.uniform(0.9, 1.6), rng.uniform(0, 6.28), 0.45),
                 (rng.uniform(2.0, 2.8), rng.uniform(0, 6.28), 0.2)]
        return lambda t: scale * sum(w * math.sin(2 * math.pi * f * t + ph) for f, ph, w in parts)

    dp, dy, dr = drift(1.1 * amount), drift(1.4 * amount), drift(0.9 * amount)
    dx, dz = drift(2.5 * amount), drift(2.0 * amount)
    keys = []
    for i in range(n):
        t = T * i / (n - 1)
        o, a, fov = campath.sample(path, t)
        keys.append(campath.Key(round(t, 3), (o[0] + dx(t), o[1] + dx(t + 7.0), o[2] + dz(t)),
                                (a[0] + dp(t), a[1] + dy(t), a[2] + dr(t)), fov))
    return campath.Path(campath.validate(keys))


MODIFIERS = ("dutch", "handheld", "seed")


def build(spec, graph, anchors=None):
    """A shot's "camera" object -> campath.Path.

    Besides the move's own numbers a camera takes film grammar: "lens"/"lens_end" (focal length in mm, in place
    of fov/fov_end), "dutch" (roll degrees, or [start, end]) and "handheld" (0..1 operator shake).
    """
    spec = dict(spec)
    kind = spec.pop("type")
    mods = {k: spec.pop(k) for k in MODIFIERS if k in spec}
    if "lens" in spec:
        spec["fov"] = round(lens_fov(spec.pop("lens")), 2)
    if "lens_end" in spec:
        spec["fov_end"] = round(lens_fov(spec.pop("lens_end")), 2)
    if kind == "keys":
        path = from_keys(spec["keys"])
    elif kind not in GENERATORS:
        raise ValueError(f"unknown camera type {kind!r}")
    else:
        path = GENERATORS[kind](graph, anchors=anchors, **spec)
    if "dutch" in mods:
        path = dutch(path, mods["dutch"])
    if mods.get("handheld"):
        path = handheld(path, float(mods["handheld"]), int(mods.get("seed", 1)))
    return path
