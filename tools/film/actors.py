"""Film actors: the soldiers in a shot, written as the cast files the game DLL plays.

A shot's ``actors`` list turns into ``cine/<shot>.act`` (format in src/dod_film.h). Each actor is a
prop in a team's player model, not a bot, so a shot can hold up to 12 of them on the GameCube:

    "actors": [
      {"id": "a1", "team": "allies", "class": "rifleman", "speed": 200,
       "path": [{"at": "wave_l", "pose": "run"},
                {"at": "obstacles", "pose": "crouch", "hold": 3},
                {"at": "beach_mid", "pose": "run", "route": true}]}
    ]

- ``at`` is any point a camera takes (anchor, ``[x, y]``, ``[x, y, z]``, ``{"node": N}``); left out, the
  actor stays where it is. ``offset: [dx, dy]`` on the actor shifts every point (a squad from one path).
- Time: give a stop ``t`` (seconds from the shot's first frame), or leave it out and the actor gets there
  at its pose's pace (``speed`` overrides). ``hold`` waits that long at the stop, in its pose.
- ``route: true`` walks the waypoint graph to the stop instead of the straight line.
- ``yaw`` is degrees, or ``look`` a point to face; otherwise the actor faces the way it moves.
- ``squad: {"count": 3, "spacing": 56}`` repeats the actor in a loose file with a few frames of stagger.

``sample`` mirrors DodFilm_Sample in src/dod_film.c, so boards and the animatic show where the game puts
everyone at any frame.
"""
import math
import re

from . import shots

MAX_ACTORS = 12
MAX_KEYS = 16
POSES = ("stand", "walk", "run", "crouch", "crouch_walk", "prone", "crawl", "aim", "crouch_aim", "fallen")
CLASSES = ("rifleman", "assault", "support", "sniper", "mg", "rocket")
TEAMS = ("allies", "axis")
# units per second for each moving pose, close to a DoD player's pace
PACE = {"walk": 90.0, "run": 200.0, "crouch_walk": 65.0, "crawl": 28.0}
LOW = {"crouch": "crouch", "crouch_walk": "crouch", "crouch_aim": "crouch",
       "prone": "prone", "crawl": "prone", "fallen": "prone"}


def _yaw_to(a, b):
    return math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))


def _thin(points, n):
    """At most n points from a polyline, ends kept, spread by distance."""
    if len(points) <= n:
        return points
    cum = [0.0]
    for a, b in zip(points, points[1:]):
        cum.append(cum[-1] + math.dist(a, b))
    out = [points[0]]
    for k in range(1, n - 1):
        want = cum[-1] * k / (n - 1)
        i = min(range(len(cum)), key=lambda j: abs(cum[j] - want))
        if points[i] is not out[-1]:
            out.append(points[i])
    out.append(points[-1])
    return out


def _nearest_level(graph, x, y, z, reach=160.0):
    """The nearest waypoint to (x, y) among those within reach of height z (any height if none is)."""
    near = [i for i, n in enumerate(graph.nodes) if abs(n[2] - z) <= reach]
    pool = near or range(len(graph.nodes))
    return min(pool, key=lambda i: (graph.nodes[i][0] - x) ** 2 + (graph.nodes[i][1] - y) ** 2)


def build_actor(spec, graph, anchors, where=""):
    aid = spec.get("id", "")
    if not re.fullmatch(r"[a-z0-9][a-z0-9_]{0,30}", aid):
        raise ValueError(f"{where}actor id {aid!r}: lowercase letters, digits and _")
    team = spec.get("team", "allies")
    if team not in TEAMS:
        raise ValueError(f"{where}{aid}: team must be allies or axis")
    cls = spec.get("class", "rifleman")
    if cls not in CLASSES:
        raise ValueError(f"{where}{aid}: class must be one of {', '.join(CLASSES)}")
    off = spec.get("offset", [0, 0])
    stops = spec.get("path") or []
    if not stops or "at" not in stops[0]:
        raise ValueError(f"{where}{aid}: path must start with a stop that has \"at\"")

    def point(p, near_z=None):
        """Resolve a stop. Where the point has no z of its own, the floor comes from the nearest waypoint,
        and once the actor is somewhere, the nearest one at about its height: the 2D nearest can be a roof
        or a bunker top above the actor's path."""
        x, y, z = shots.resolve(p, graph, anchors)
        has_z = isinstance(p, (list, tuple)) and len(p) == 3
        if off[0] or off[1]:
            x, y = x + off[0], y + off[1]
        if not has_z and (off[0] or off[1] or near_z is not None):
            z = graph.nodes[_nearest_level(graph, x, y, near_z if near_z is not None else z)][2]
        return (x, y, z)

    keys = []  # [t, (x, y, z), yaw or None, pose]
    t = float(stops[0].get("t", 0.0))
    pos = point(stops[0]["at"])
    pose = stops[0].get("pose", "stand")
    for i, st in enumerate(stops):
        pose_next = st.get("pose", pose)
        if pose_next not in POSES:
            raise ValueError(f"{where}{aid}: unknown pose {pose_next!r} (one of {', '.join(POSES)})")
        dest = point(st["at"], pos[2]) if "at" in st and i > 0 else pos
        if i > 0 and dest != pos:
            # the leg there moves in the previous stop's pose
            legs = [pos, dest]
            if st.get("route"):
                legs = graph.route(graph.nearest(*pos), graph.nearest(*dest))
                # a squad member keeps its place in the file along the route, not just at the stops
                legs = [pos] + [(n[0] + off[0], n[1] + off[1], float(n[2])) for n in legs[1:-1]] + [dest]
            length = sum(math.dist(a, b) for a, b in zip(legs, legs[1:]))
            if "t" in st:
                t_end = float(st["t"])
                if t_end < t:
                    raise ValueError(f"{where}{aid}: stop {i} at t={t_end} is before the actor is free (t={t:.2f})")
            else:
                pace = float(spec.get("speed") or PACE.get(pose, 0.0))
                if pace <= 0:
                    raise ValueError(f"{where}{aid}: stop {i}: a '{pose}' actor does not move; give the stop a t"
                                     " or the previous stop a moving pose (walk, run, crouch_walk, crawl)")
                t_end = t + length / pace
            legs = _thin(legs, 6)
            seg_len = [math.dist(a, b) for a, b in zip(legs, legs[1:])]
            run = 0.0
            for p, sl in zip(legs[1:-1], seg_len):
                run += sl
                keys.append([t + (t_end - t) * run / length, p, None, pose])
            t = t_end
            pos = dest
        elif "t" in st and i > 0:
            # standing still until t in the current pose
            keys.append([t, pos, None, pose])
            t = max(t, float(st["t"]))
        yaw = st.get("yaw")
        if "look" in st:
            yaw = _yaw_to(pos, shots.resolve(st["look"], graph, anchors))
        pose = pose_next
        keys.append([t, pos, yaw, pose])
        if st.get("hold"):
            t += float(st["hold"])
            keys.append([t, pos, yaw, pose])
    # drop exact duplicates the steps above can leave
    out = []
    for k in keys:
        if out and abs(out[-1][0] - k[0]) < 1e-6 and out[-1][1] == k[1] and out[-1][3] == k[3]:
            out[-1] = k
            continue
        out.append(k)
    if len(out) > MAX_KEYS:
        raise ValueError(f"{where}{aid}: {len(out)} keys, the game takes {MAX_KEYS}: fewer stops or no route")
    return {"id": aid, "team": team, "class": cls, "keys": out}


def build(specs, graph, anchors, where=""):
    cast = []
    for spec in specs or []:
        squad = spec.get("squad")
        if squad:
            n = int(squad.get("count", 3))
            gap = float(squad.get("spacing", 56))
            lag = float(squad.get("stagger", 0.35))
            first = build_actor({k: v for k, v in spec.items() if k != "squad"}, graph, anchors, where)
            # file behind the leader: offset across the first leg, start a little later
            k0, k1 = first["keys"][0], first["keys"][min(1, len(first["keys"]) - 1)]
            ang = math.radians(_yaw_to(k0[1], k1[1]) if k0[1] != k1[1] else 0.0)
            side = (-math.sin(ang), math.cos(ang))
            back = (-math.cos(ang), -math.sin(ang))
            cast.append(first)
            for j in range(1, n):
                s = -1 if j % 2 else 1
                dx = side[0] * gap * s * ((j + 1) // 2) + back[0] * gap * 0.6 * j
                dy = side[1] * gap * s * ((j + 1) // 2) + back[1] * gap * 0.6 * j
                o = spec.get("offset", [0, 0])
                sub = dict(spec, id=f"{spec['id']}_{j}", offset=[o[0] + dx, o[1] + dy])
                sub.pop("squad")
                a = build_actor(sub, graph, anchors, where)
                for k in a["keys"][1:]:
                    k[0] += lag * j
                cast.append(a)
        else:
            cast.append(build_actor(spec, graph, anchors, where))
    ids = [a["id"] for a in cast]
    if len(set(ids)) != len(ids):
        raise ValueError(f"{where}duplicate actor ids")
    if len(cast) > MAX_ACTORS:
        raise ValueError(f"{where}{len(cast)} actors, the game takes {MAX_ACTORS} per shot")
    return cast


def dumps(cast, header=""):
    lines = ["act 1"]
    if header:
        lines.append(f"// {header}")
    for a in cast:
        lines.append(f"actor {a['id']} {a['team']} {a['class']}")
        for t, p, yaw, pose in a["keys"]:
            ys = "-" if yaw is None else f"{yaw:.1f}"
            lines.append(f"key {t:.3f} {p[0]:.1f} {p[1]:.1f} {p[2]:.1f} {ys} {pose}")
    return "\n".join(lines) + "\n"


def _travel(a, b):
    if (b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2 < 1.0:
        return None
    return _yaw_to(a, b)


def _key_yaw(keys, i):
    if keys[i][2] is not None:
        return keys[i][2]
    for j in range(i, len(keys) - 1):
        y = _travel(keys[j][1], keys[j + 1][1])
        if y is not None:
            return y
    for j in range(i, 0, -1):
        y = _travel(keys[j - 1][1], keys[j][1])
        if y is not None:
            return y
    return 0.0


def sample(actor, t):
    """(x, y, z), yaw, pose, moving at time t: the same answer as DodFilm_Sample."""
    keys = actor["keys"]
    if t <= keys[0][0] or len(keys) == 1:
        return keys[0][1], _key_yaw(keys, 0), keys[0][3], False
    for i in range(len(keys) - 1):
        a, b = keys[i], keys[i + 1]
        if t < b[0]:
            span = b[0] - a[0]
            u = (t - a[0]) / span if span > 0 else 1.0
            p = tuple(a[1][c] + (b[1][c] - a[1][c]) * u for c in range(3))
            moving = math.dist(a[1], b[1]) >= 1.0 and span > 0
            if a[2] is not None:
                yaw = a[2]
            else:
                yaw = _travel(a[1], b[1])
                if yaw is None:
                    yaw = _key_yaw(keys, i)
            return p, yaw, a[3], moving
    k = keys[-1]
    return k[1], _key_yaw(keys, len(keys) - 1), k[3], False


def figures(cast, t):
    """Board stand-ins at time t: (x, y, z, pose)."""
    out = []
    for a in cast:
        p, _, pose, _ = sample(a, t)
        out.append((p[0], p[1], p[2], pose))
    return out


def teams(cast):
    return sorted({a["team"] for a in cast})
