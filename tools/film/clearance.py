"""Clearance: no camera path may go through the map, or through the soldiers in its shot.

The camera has no collision in the engine (cine_play puts the view where the keys say), so a move that
grazes a wall shows the inside of the world, and one that passes a soldier goes through his head. This
checks every shot against the map's own BSP before anything is captured:

- **inside the world**: the eye in solid (the point hull, hull 0, the one the renderer draws) anywhere along
  the path, sampled every few units, or the straight line between two samples crossing solid;
- **too close to a surface**: drawn geometry within a box 16 units each way across and 18 up and down of the
  eye, where the lens's near plane (4 units) and the 70 to 90 degree field of view start cutting into it.
  The box is tested in hull 0, on a grid finer than any wall is thick, so invisible player-clip brushes
  (the walls over the sea and along the shingle in dod_charlie) do not count: the camera may pass them;
- **through a soldier**: the eye inside an actor's body (a box 20 units round his origin, his pose's height).

Solid brush entities (func_wall, func_breakable, func_door ...) count as walls at their spawn positions.

    film.py check <ep>.json        # every shot; exits 1 on any problem
    film.py plan <ep>.json         # runs the same check when it finds the map's .bsp, and stops on a problem

BSPs are looked for in $FILM_BSP_DIR, content/dod/maps and content/valve/maps (the retail files are not in
the repo, so the check runs where the game content is: the capture machine).
"""
import math
import os
import struct
from pathlib import Path

from . import actors, campath

ROOT = Path(__file__).resolve().parents[2]
CONTENTS_SOLID = -2
LUMP_ENTITIES, LUMP_PLANES, LUMP_NODES, LUMP_LEAFS, LUMP_MODELS = 0, 1, 5, 10, 14
STEP = 4.0              # units between samples along a path
NEAR = (16.0, 18.0)     # the box round the eye: half width, half height (a duck hull's size)
ACTOR_R = 20.0
ACTOR_H = {"stand": 72.0, "crouch": 54.0, "prone": 20.0}
SOLID_BRUSHES = ("func_wall", "func_breakable", "func_door", "func_door_rotating", "func_wall_toggle",
                 "func_tank", "func_pushable", "func_train", "func_rotating")


class Bsp:
    """The parts of a GoldSrc BSP that the check needs: hull 0 (what is drawn) from nodes and leaves."""

    def __init__(self, data):
        if struct.unpack_from("<i", data, 0)[0] != 30:
            raise ValueError("not a GoldSrc BSP (version 30)")
        lumps = [struct.unpack_from("<ii", data, 4 + 8 * i) for i in range(15)]

        def lump(i):
            off, n = lumps[i]
            return data[off:off + n]
        pl = lump(LUMP_PLANES)
        self.planes = [struct.unpack_from("<4f", pl, 20 * i) for i in range(len(pl) // 20)]
        nd = lump(LUMP_NODES)
        self.nodes = [struct.unpack_from("<ihh", nd, 24 * i) for i in range(len(nd) // 24)]
        lf = lump(LUMP_LEAFS)
        self.leaf_contents = [struct.unpack_from("<i", lf, 28 * i)[0] for i in range(len(lf) // 28)]
        md = lump(LUMP_MODELS)
        self.models = []
        for i in range(len(md) // 64):
            v = struct.unpack_from("<9f4i3i", md, 64 * i)
            self.models.append({"mins": v[0:3], "maxs": v[3:6], "headnode": v[9:13]})
        self.entities = _entities(lump(LUMP_ENTITIES).split(b"\0")[0].decode("latin-1"))

    def contents0(self, root, p):
        """Point contents in hull 0 (the drawn world)."""
        num = root
        while num >= 0:
            plane, c0, c1 = self.nodes[num]
            nx, ny, nz, dist = self.planes[plane]
            num = c0 if nx * p[0] + ny * p[1] + nz * p[2] - dist >= 0.0 else c1
        return self.leaf_contents[-num - 1]


def _entities(text):
    ents, cur = [], None
    for line in text.splitlines():
        line = line.strip()
        if line == "{":
            cur = {}
        elif line == "}":
            if cur is not None:
                ents.append(cur)
            cur = None
        elif cur is not None and line.startswith('"'):
            parts = line.split('"')
            if len(parts) >= 4:
                cur[parts[1]] = parts[3]
    return ents


class World:
    """The world model plus the solid brush entities, at their spawn origins."""

    def __init__(self, bsp):
        self.bsp = bsp
        self.parts = [(bsp.models[0]["headnode"], (0.0, 0.0, 0.0), "world", bsp.models[0]["mins"], bsp.models[0]["maxs"])]
        for e in bsp.entities:
            cls, model = e.get("classname", ""), e.get("model", "")
            if cls not in SOLID_BRUSHES or not model.startswith("*"):
                continue
            if cls == "func_wall_toggle" and int(e.get("spawnflags", "0") or 0) & 1:
                continue    # starts off
            idx = int(model[1:])
            if 0 < idx < len(bsp.models):
                o = tuple(float(v) for v in e.get("origin", "0 0 0").split()) if e.get("origin") else (0.0, 0.0, 0.0)
                m = bsp.models[idx]
                self.parts.append((m["headnode"], o, e.get("targetname") or cls, m["mins"], m["maxs"]))

    def _solid(self, p, parts):
        for heads, o, name, *_ in parts:
            if heads[0] >= 0 and self.bsp.contents0(heads[0], (p[0] - o[0], p[1] - o[1], p[2] - o[2])) == CONTENTS_SOLID:
                return name
        return None

    def hit(self, p):
        """None when the eye at p is clear, else (what, which): 'solid' or 'near', and the model it is in."""
        name = self._solid(p, self.parts)
        if name:
            return "solid", name
        # the box round the eye, on an 8-unit grid; brush entities only where their bounds reach the box
        hx, hz = NEAR
        near_parts = [self.parts[0]] + [
            part for part in self.parts[1:]
            if all(part[3][c] + part[1][c] - (hx if c < 2 else hz) <= p[c] <= part[4][c] + part[1][c] + (hx if c < 2 else hz)
                   for c in range(3))]
        steps = (-1.0, -0.5, 0.0, 0.5, 1.0)
        for dz in steps:
            for dy in steps:
                for dx in steps:
                    if dx == dy == dz == 0.0:
                        continue
                    name = self._solid((p[0] + dx * hx, p[1] + dy * hx, p[2] + dz * hz), near_parts)
                    if name:
                        return "near", name
        return None


def find_bsp(mapname, root=ROOT):
    dirs = [os.environ.get("FILM_BSP_DIR"), root / "content" / "dod" / "maps", root / "content" / "valve" / "maps"]
    for d in dirs:
        if d:
            p = Path(d) / f"{mapname}.bsp"
            if p.is_file():
                return p
    return None


def _actor_hit(cast, t, p):
    for a in cast:
        q, _, pose, _ = actors.sample(a, t)
        h = ACTOR_H.get(actors.LOW.get(pose, "stand"), 72.0)
        floor = q[2] - 36.0
        if abs(p[0] - q[0]) < ACTOR_R and abs(p[1] - q[1]) < ACTOR_R and floor - 4 <= p[2] <= floor + h + 4:
            return a["id"]
    return None


def check_shot(world, seg, step=STEP):
    """Problems for one shot: [(t, (x, y, z), kind, what)], merged into runs (first time of each run kept)."""
    path, cast = seg["path"], seg.get("cast") or []
    dur = path.duration
    # sample densely enough that no two samples are more than `step` apart in space
    n, prev = 0, None
    for i in range(201):
        o = campath.sample(path, dur * i / 200)[0]
        if prev is not None:
            n += math.dist(o, prev)
        prev = o
    count = max(2, int(n / step) + 1, int(dur * 30) + 1)
    problems, last = [], None
    for i in range(count + 1):
        t = dur * i / count
        o = tuple(campath.sample(path, t)[0])
        h = world.hit(o) if world else None
        kind = (h[0], h[1]) if h else None
        if not kind:
            who = _actor_hit(cast, t, o)
            kind = ("actor", who) if who else None
        if kind and kind != last:
            problems.append((round(t, 2), tuple(round(v) for v in o), kind[0], kind[1]))
        last = kind
    return problems


def check(timeline, root=ROOT, bsps=None):
    """{shot id: problems} for every shot whose map's BSP can be found, and the maps that could not be checked
    against geometry (their shots still get the actor check)."""
    worlds, missing, out = {}, [], {}
    for seg in timeline["segments"]:
        if seg["kind"] != "shot":
            continue
        m = seg["map"]
        if m not in worlds:
            path = (bsps or {}).get(m) or find_bsp(m, root)
            worlds[m] = World(Bsp(Path(path).read_bytes())) if path else None
            if not path:
                missing.append(m)
        probs = check_shot(worlds[m], seg)
        if probs:
            out[seg["id"]] = probs
    return out, missing


def describe(problems):
    words = {"solid": "inside the world", "near": "too close to a surface", "actor": "through a soldier"}
    lines = []
    for sid, probs in problems.items():
        for t, p, kind, what in probs:
            lines.append(f"{sid} at {t:.2f}s: {words[kind]} ({what}) at {p[0]} {p[1]} {p[2]}")
    return lines
