#!/usr/bin/env python3
"""Bake a bot waypoint graph from a GoldSrc BSP (host tool, offline).

    tools/gen_waypoints.py content/dod/maps/dod_charlie.bsp -o data/waypoints/dod_charlie.wpt

What it does
  1. Reads the BSP's player hull (hull 1, the 32x32x72 standing box: the clip nodes are already
     expanded, so a point trace through them is a box trace) and ports the engine's recursive hull
     check (SV_RecursiveHullCheck) to collide with it.
  2. (ladders: every func_ladder brush gets a LADDER edge pair between the floor at its foot and the floor
     it tops out on; ladders are not part of the hull data, and some spawns sit in pockets only they leave.)
  2b. Floods the walkable floor outward from the team spawns on a regular grid. A move from one cell
     to the next is a WALK (rise or fall up to a step, 18), a JUMP (up to 45) or a DROP (down to 260),
     each checked with hull traces the way the player would move. Slopes steeper than 0.7 are not floor.
  3. Thins the dense cells into a sparse graph: waypoints are chosen so every cell is within RADIUS of one
     (graph distance); an edge joins two waypoints whose regions touch.
  4. Writes a small big-endian binary (see WPT_* below) that the game DLL loads with the map.

The graph is derived data (coordinates and edges); the BSP is only read.
"""
import argparse
import heapq
import math
import struct
import sys
from collections import deque

# --- contents / movement constants (GoldSrc) -----------------------------------------------------------
CONTENTS_EMPTY = -1
CONTENTS_SOLID = -2
CONTENTS_WATER = -3
CONTENTS_SLIME = -4
CONTENTS_LAVA = -5
CONTENTS_SKY = -6
DIST_EPSILON = 0.03125

STEP = 18.0       # the player walks up/down this much without jumping
JUMP = 45.0       # and jumps up this much
MAX_DROP = 260.0  # walking off a ledge this far is fine (fall damage ~15)
MIN_NORMAL_Z = 0.7  # steeper than this is not floor

KIND_WALK, KIND_JUMP, KIND_DROP, KIND_LADDER = 0, 1, 2, 3

# --- .wpt layout (big-endian) ---------------------------------------------------------------------------
#   char magic[4] = "DWPT"; u16 version = 2; u16 reserved
#   u32 bsp_size            size of the BSP file it was baked from (a stale-data check)
#   u16 node_count; u16 edge_count; u16 cell; u16 radius
#   node[node_count]  { i16 x, y, z; u8 edge_count }                   7 bytes; edges of node i follow the
#   edge[edge_count]  { u16 kind << 14 | to }                           2 bytes (cost = distance + kind penalty, see edge_cost); edges of earlier nodes
WPT_MAGIC = b"DWPT"
WPT_VERSION = 2


# --- BSP -------------------------------------------------------------------------------------------------
LUMP_ENTITIES, LUMP_PLANES, LUMP_CLIPNODES, LUMP_MODELS = 0, 1, 9, 14


class Bsp:
    def __init__(self, data):
        version = struct.unpack_from("<i", data, 0)[0]
        if version != 30:
            raise ValueError(f"not a GoldSrc BSP (version {version})")
        self.size = len(data)
        lumps = [struct.unpack_from("<ii", data, 4 + 8 * i) for i in range(15)]

        def lump(i):
            off, length = lumps[i]
            return data[off:off + length]

        pl = lump(LUMP_PLANES)
        self.planes = [struct.unpack_from("<4f", pl, 20 * i) for i in range(len(pl) // 20)]  # nx ny nz dist
        cn = lump(LUMP_CLIPNODES)
        self.clipnodes = [struct.unpack_from("<ihh", cn, 8 * i) for i in range(len(cn) // 8)]
        md = lump(LUMP_MODELS)
        self.models = []
        for i in range(len(md) // 64):
            v = struct.unpack_from("<9f4i3i", md, 64 * i)
            self.models.append({"mins": v[0:3], "maxs": v[3:6], "origin": v[6:9], "headnode": v[9:13]})
        self.entities = parse_entities(lump(LUMP_ENTITIES).split(b"\0")[0].decode("latin-1"))
        self.hull1_root = self.models[0]["headnode"][1]


def parse_entities(text):
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


# --- hull collision (port of SV_HullPointContents / SV_RecursiveHullCheck) --------------------------------
class TraceResult:
    __slots__ = ("allsolid", "startsolid", "fraction", "endpos", "normal_z")

    def __init__(self):
        self.allsolid = True
        self.startsolid = False
        self.fraction = 1.0
        self.endpos = None
        self.normal_z = 0.0


class Hull:
    def __init__(self, planes, clipnodes, root):
        self.planes = planes
        self.clipnodes = clipnodes
        self.root = root

    def contents(self, p, num=None):
        num = self.root if num is None else num
        px, py, pz = p
        planes, clipnodes = self.planes, self.clipnodes
        while num >= 0:
            plane, c0, c1 = clipnodes[num]
            nx, ny, nz, dist = planes[plane]
            num = c0 if nx * px + ny * py + nz * pz - dist >= 0.0 else c1
        return num

    def trace(self, p1, p2):
        tr = TraceResult()
        tr.endpos = tuple(p2)
        self._recurse(tr, self.root, 0.0, 1.0, p1, p2)
        if tr.allsolid:
            tr.startsolid = True
        return tr

    def _recurse(self, tr, num, p1f, p2f, p1, p2):
        if num < 0:
            if num != CONTENTS_SOLID:
                tr.allsolid = False
            else:
                tr.startsolid = True
            return True
        plane, c0, c1 = self.clipnodes[num]
        nx, ny, nz, dist = self.planes[plane]
        t1 = nx * p1[0] + ny * p1[1] + nz * p1[2] - dist
        t2 = nx * p2[0] + ny * p2[1] + nz * p2[2] - dist
        if t1 >= 0.0 and t2 >= 0.0:
            return self._recurse(tr, c0, p1f, p2f, p1, p2)
        if t1 < 0.0 and t2 < 0.0:
            return self._recurse(tr, c1, p1f, p2f, p1, p2)
        # put the crosspoint DIST_EPSILON on the near side
        if t1 < 0.0:
            frac = (t1 + DIST_EPSILON) / (t1 - t2)
        else:
            frac = (t1 - DIST_EPSILON) / (t1 - t2)
        frac = min(1.0, max(0.0, frac))
        midf = p1f + (p2f - p1f) * frac
        mid = (p1[0] + frac * (p2[0] - p1[0]), p1[1] + frac * (p2[1] - p1[1]), p1[2] + frac * (p2[2] - p1[2]))
        side = 1 if t1 < 0.0 else 0
        children = (c0, c1)
        if not self._recurse(tr, children[side], p1f, midf, p1, mid):
            return False
        if self.contents(mid, children[side ^ 1]) != CONTENTS_SOLID:
            return self._recurse(tr, children[side ^ 1], midf, p2f, mid, p2)
        if tr.allsolid:
            return False  # never got out of the solid area
        # the other side of the node is solid: this is the impact point
        nz_hit = nz if side == 0 else -nz
        while self.contents(mid) == CONTENTS_SOLID:
            frac -= 0.1
            if frac < 0.0:
                tr.fraction = midf
                tr.endpos = mid
                tr.normal_z = nz_hit
                return False
            midf = p1f + (p2f - p1f) * frac
            mid = (p1[0] + frac * (p2[0] - p1[0]), p1[1] + frac * (p2[1] - p1[1]), p1[2] + frac * (p2[2] - p1[2]))
        tr.fraction = midf
        tr.endpos = mid
        tr.normal_z = nz_hit
        return False


# Brush entities that stop a walking player at runtime (the engine collides with them like with the world; the baked graph used to
# route straight through them). func_wall_toggle with spawnflag 1 starts invisible and non-solid; func_illusionary never blocks.
SOLID_BRUSH_CLASSES = ("func_wall", "func_wall_toggle", "func_breakable")
# func_wall_toggle is switched by map logic (charlie: shingle1, shingle2 start solid and cut spawns off from the control points), so it only
# blocks when --toggle-walls asks for it.
DEFAULT_SOLID = ("func_wall", "func_breakable")


def solid_brush_hulls(bsp, classes=DEFAULT_SOLID):
    """[(Hull, (ox, oy, oz))] for every solid brush entity's hull-1 clip tree."""
    out = []
    for ent in bsp.entities:
        cls = ent.get("classname")
        model = ent.get("model", "")
        if cls not in classes or not model.startswith("*"):
            continue
        if cls == "func_wall_toggle" and int(ent.get("spawnflags", "0") or 0) & 1:
            continue
        idx = int(model[1:])
        if idx <= 0 or idx >= len(bsp.models):
            continue
        root = bsp.models[idx]["headnode"][1]
        if root < 0:
            continue
        out.append((Hull(bsp.planes, bsp.clipnodes, root), origin_of(ent) or (0.0, 0.0, 0.0)))
    return out


class CompositeHull:
    """The world hull plus solid brush entities: a point is solid if any of them is, a trace stops at the nearest hit."""

    def __init__(self, world, extras):
        self.world = world
        self.extras = list(extras)
        self.root = world.root

    def contents(self, p):
        if self.world.contents(p) == CONTENTS_SOLID:
            return CONTENTS_SOLID
        for hull, o in self.extras:
            if hull.contents((p[0] - o[0], p[1] - o[1], p[2] - o[2])) == CONTENTS_SOLID:
                return CONTENTS_SOLID
        return self.world.contents(p)

    def trace(self, p1, p2):
        best = self.world.trace(p1, p2)
        for hull, o in self.extras:
            tr = hull.trace((p1[0] - o[0], p1[1] - o[1], p1[2] - o[2]), (p2[0] - o[0], p2[1] - o[1], p2[2] - o[2]))
            if tr.startsolid:
                best.startsolid = True
            if tr.fraction < best.fraction:
                best.fraction = tr.fraction
                best.endpos = (tr.endpos[0] + o[0], tr.endpos[1] + o[1], tr.endpos[2] + o[2])
                best.normal_z = tr.normal_z
                best.allsolid = tr.allsolid
        return best


# --- walking ---------------------------------------------------------------------------------------------
def find_ground(hull, x, y, z_from, z_to):
    """Z of the floor under (x, y) between z_from and z_to (hull origin height), or None."""
    tr = hull.trace((x, y, z_from), (x, y, z_to))
    if tr.startsolid or tr.fraction >= 1.0 or tr.normal_z < MIN_NORMAL_Z:
        return None
    contents = hull.contents(tr.endpos)
    if contents in (CONTENTS_SLIME, CONTENTS_LAVA, CONTENTS_SKY):
        return None
    return tr.endpos[2]


def try_move(hull, a, dx, dy):
    """Walk from ground position a one grid step. Returns ((x, y, z), kind) or None."""
    ax, ay, az = a
    bx, by = ax + dx, ay + dy
    for lift in (1.0, STEP):  # flat first (low ceilings), then up a step
        start, end = (ax, ay, az + lift), (bx, by, az + lift)
        tr = hull.trace(start, end)
        if tr.startsolid or tr.fraction < 1.0:
            continue
        gz = find_ground(hull, bx, by, az + lift, az + lift - (lift + MAX_DROP))
        if gz is None:
            return None  # a pit deeper than a drop, or too steep
        if gz - az < -STEP - 0.5:
            return (bx, by, gz), KIND_DROP
        return (bx, by, gz), KIND_WALK
    # blocked at step height: a jump?
    start, end = (ax, ay, az + JUMP), (bx, by, az + JUMP)
    tr = hull.trace(start, end)
    if tr.startsolid or tr.fraction < 1.0:
        return None
    gz = find_ground(hull, bx, by, az + JUMP, az + STEP)
    if gz is None or gz <= az + STEP + 0.5:
        return None
    return (bx, by, gz), KIND_JUMP


NEIGHBORS = [(1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)]


def cell_key(x, y, z, cell):
    return (int(round(x / cell)), int(round(y / cell)), int(math.floor(z / 12.0)))


def flood(hull, seeds, cell, max_cells=400000):
    """Dense walkable grid from the seeds: cells[key] = (x, y, z); edges[key] = [(key2, kind, cost)]."""
    cells, edges = {}, {}
    queue = deque()
    for sx, sy, sz in seeds:
        gx, gy = round(sx / cell) * cell, round(sy / cell) * cell
        gz = find_ground(hull, gx, gy, sz + 8.0, sz - 200.0)
        if gz is None:
            continue
        key = cell_key(gx, gy, gz, cell)
        if key not in cells:
            cells[key] = (gx, gy, gz)
            queue.append(key)
    while queue and len(cells) < max_cells:
        key = queue.popleft()
        a = cells[key]
        out = []
        for ox, oy in NEIGHBORS:
            moved = try_move(hull, a, ox * cell, oy * cell)
            if moved is None:
                continue
            (bx, by, bz), kind = moved
            k2 = cell_key(bx, by, bz, cell)
            if k2 not in cells:
                cells[k2] = (bx, by, bz)
                queue.append(k2)
            cost = math.hypot(bx - a[0], by - a[1]) + abs(bz - a[2]) * 0.5
            if kind == KIND_JUMP:
                cost += 40.0
            out.append((k2, kind, cost))
        edges[key] = out
    return cells, edges


# --- ladders ---------------------------------------------------------------------------------------------
def nearest_cell(index, cells, x, y, z, max_xy=40.0, max_dz=24.0):
    best, bd = None, 1e18
    ix, iy = int(round(x / 32.0)), int(round(y / 32.0))
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for key in index.get((ix + dx, iy + dy), ()):
                cx, cy, cz = cells[key]
                if abs(cz - z) > max_dz:
                    continue
                d = math.hypot(cx - x, cy - y)
                if d <= max_xy and d < bd:
                    best, bd = key, d
    return best


def spawn_cells(cells, seeds, min_gap=96.0, hull=None):
    """The dense cell under each player spawn (found by the hull's ground height when a hull is given), thinned so
    that spawns within min_gap of one another share a cell. These become waypoints, so a bot that (re)spawns always
    has a node it can walk straight to: the coverage radius alone leaves some spawns 100+ units from the nearest
    node, behind a wall or a step."""
    index = {}
    for key in cells:
        index.setdefault((key[0], key[1]), []).append(key)
    chosen = []
    for x, y, z in seeds:
        gz = z
        if hull is not None:
            g = find_ground(hull, x, y, z + 4.0, z - 200.0)
            if g is not None:
                gz = g
        key = nearest_cell(index, cells, x, y, gz, max_xy=48.0, max_dz=24.0) or \
            nearest_cell(index, cells, x, y, gz, max_xy=96.0, max_dz=64.0)
        if key is None or key in chosen:
            continue
        cx, cy, cz = cells[key]
        if any(math.hypot(cells[k][0] - cx, cells[k][1] - cy) < min_gap and abs(cells[k][2] - cz) < 40.0 for k in chosen):
            continue
        chosen.append(key)
    return chosen


def ladder_links(bsp, hull, cells, cell):
    """[(bottom_key, top_key, height)] for every func_ladder: the floor cell at its foot and the one it tops out on."""
    index = {}
    for key in cells:
        index.setdefault((key[0], key[1]), []).append(key)
    links = []
    for ent in bsp.entities:
        if ent.get("classname") != "func_ladder" or not ent.get("model", "").startswith("*"):
            continue
        m = bsp.models[int(ent["model"][1:])]
        mins, maxs = m["mins"], m["maxs"]
        ext = [maxs[i] - mins[i] for i in range(3)]
        t = 0 if ext[0] < ext[1] else 1   # the thin axis: the ladder's face looks along it
        w = 1 - t
        c = [(mins[i] + maxs[i]) * 0.5 for i in range(3)]

        def at(side, offset, z_from, z_to):
            p = [c[0], c[1]]
            p[t] = c[t] + side * (ext[t] * 0.5 + offset)
            gz = find_ground(hull, p[0], p[1], z_from, z_to)
            if gz is None:
                return None
            return nearest_cell(index, cells, p[0], p[1], gz)

        for side in (1, -1):
            foot = at(side, 20.0, mins[2] + 120.0, mins[2] - 100.0)
            if foot is None:
                continue
            # The landing is behind the ladder, on the far side of its plane. A floor at the same height on the
            # foot's own side is some other structure above it, not where the ladder leads.
            for top_side in (-side,):
                top = at(top_side, 36.0, maxs[2] + 72.0, maxs[2] - 72.0)
                if top is None or cells[top][2] - cells[foot][2] < 60.0:
                    continue
                link = (foot, top, cells[top][2] - cells[foot][2])
                if all(l[:2] != link[:2] for l in links):
                    links.append(link)
    return links


# --- thinning --------------------------------------------------------------------------------------------
def select_waypoints(cells, edges, order, radius, forced=()):
    """Greedy cover: every cell ends up within `radius` (path cost) of a waypoint. `forced` cells (ladder feet
    and tops) are waypoints first. Coverage never crosses a ladder."""
    covered = set()
    waypoints = []
    for key in list(forced) + list(order):
        if key in covered and key not in forced:
            continue
        if key in waypoints:
            continue
        waypoints.append(key)
        dist = {key: 0.0}
        heap = [(0.0, key)]
        while heap:
            d, k = heapq.heappop(heap)
            if d > dist.get(k, 1e18):
                continue
            covered.add(k)
            for k2, kind, cost in edges.get(k, ()):
                if kind == KIND_LADDER:
                    continue
                nd = d + cost
                if nd <= radius and nd < dist.get(k2, 1e18):
                    dist[k2] = nd
                    heapq.heappush(heap, (nd, k2))
    return waypoints


def build_graph(cells, edges, waypoints):
    """Voronoi regions of the waypoints over the dense graph; an edge where two regions touch."""
    index = {k: i for i, k in enumerate(waypoints)}
    owner, dist = {}, {}
    heap = []
    for k in waypoints:
        owner[k], dist[k] = k, 0.0
        heap.append((0.0, k))
    heapq.heapify(heap)
    while heap:
        d, k = heapq.heappop(heap)
        if d > dist.get(k, 1e18):
            continue
        for k2, kind, cost in edges.get(k, ()):
            if kind == KIND_LADDER:
                continue
            nd = d + cost
            if nd < dist.get(k2, 1e18):
                dist[k2], owner[k2] = nd, owner[k]
                heapq.heappush(heap, (nd, k2))
    best = {}  # (from index, to index) -> (cost, kind)
    for k, outs in edges.items():
        if k not in owner:
            continue
        for k2, kind, cost in outs:
            if k2 not in owner or owner[k] == owner[k2]:
                continue
            a, b = index[owner[k]], index[owner[k2]]
            total = dist[k] + cost + dist[k2]
            if (a, b) not in best or total < best[(a, b)][0]:
                best[(a, b)] = (total, kind)
    adj = [[] for _ in waypoints]
    for (a, b), (total, kind) in best.items():
        adj[a].append((b, kind, total))
    for lst in adj:
        lst.sort()
    return adj


def straight_walkable(hull, a, b, step=16.0):
    """Can a player walk from a to b by always heading for b (walking, hopping up, dropping)? This is how the
    bots follow an edge, so an edge that fails it must not be in the graph."""
    pos = a
    for _ in range(int(math.hypot(b[0] - a[0], b[1] - a[1]) / step) + 12):
        dx, dy = b[0] - pos[0], b[1] - pos[1]
        d = math.hypot(dx, dy)
        if d <= 24.0:
            return abs(pos[2] - b[2]) <= 40.0
        k = min(step, d) / d
        r = try_move(hull, pos, dx * k, dy * k)
        if r is None:
            return False
        pos = r[0]
    return False


def cell_path(edges, src, dst):
    """Cheapest dense route src -> dst (no ladders): [cell keys], or None."""
    dist, prev, heap = {src: 0.0}, {}, [(0.0, src)]
    while heap:
        d, k = heapq.heappop(heap)
        if k == dst:
            out = [k]
            while out[-1] != src:
                out.append(prev[out[-1]])
            return out[::-1]
        if d > dist.get(k, 1e18):
            continue
        for k2, kind, cost in edges.get(k, ()):
            if kind == KIND_LADDER:
                continue
            nd = d + cost
            if nd < dist.get(k2, 1e18):
                dist[k2], prev[k2] = nd, k
                heapq.heappush(heap, (nd, k2))
    return None


def build_walkable_graph(hull, cells, edges, order, radius, forced, rounds=10):
    """select_waypoints + build_graph, then drop edges a bot cannot walk straight and add a waypoint along the
    dense route of each dropped edge, until every kept edge is straight-walkable. Returns (waypoints, adj, bad)."""
    forced = list(forced)
    for _ in range(rounds):
        wps = select_waypoints(cells, edges, order, radius, forced)
        adj = build_graph(cells, edges, wps)
        bad = []
        for i, outs in enumerate(adj):
            keep = []
            for to, kind, cost in outs:
                if kind == KIND_LADDER or straight_walkable(hull, cells[wps[i]], cells[wps[to]]):
                    keep.append((to, kind, cost))
                else:
                    bad.append((wps[i], wps[to]))
            adj[i] = keep
        if not bad:
            return wps, adj, 0
        added = 0
        for a, b in bad:
            path = cell_path(edges, a, b)
            if not path or len(path) < 3:
                continue
            mid = path[len(path) // 2]
            if mid not in forced and mid not in wps:
                forced.append(mid)
                added += 1
        if not added:
            return wps, adj, len(bad)
    return wps, adj, len(bad)


# --- .wpt ------------------------------------------------------------------------------------------------
def edge_cost(kind, dist):
    """The cost the game DLL's A* gives an edge of this kind and length (the file stores no costs)."""
    if kind == KIND_JUMP:
        return dist + 1.0 + 64.0
    if kind == KIND_DROP:
        return dist + 1.0 + 16.0
    if kind == KIND_LADDER:
        return (dist + 1.0) * 1.5 + 64.0
    return dist + 1.0


def write_wpt(nodes, adj, bsp_size, cell, radius):
    """nodes: [(x, y, z)]; adj: [[(to, kind, cost)]] -> bytes. Edge costs are not stored."""
    edge_count = sum(len(a) for a in adj)
    if len(nodes) > 16383 or edge_count > 65535:
        raise ValueError("graph too large for the .wpt format")
    out = bytearray(WPT_MAGIC)
    out += struct.pack(">HHI", WPT_VERSION, 0, bsp_size)
    out += struct.pack(">HHHH", len(nodes), edge_count, cell, radius)
    for (x, y, z), a in zip(nodes, adj):
        if len(a) > 255:
            raise ValueError("a waypoint has more than 255 edges")
        out += struct.pack(">hhhB", int(round(x)), int(round(y)), int(round(z)), len(a))
    for a in adj:
        for to, kind, _cost in a:
            out += struct.pack(">H", (kind << 14) | to)
    return bytes(out)


def read_wpt(data):
    if data[:4] != WPT_MAGIC:
        raise ValueError("bad magic")
    version, _res, bsp_size = struct.unpack_from(">HHI", data, 4)
    if version != WPT_VERSION:
        raise ValueError(f"unsupported version {version}")
    node_count, edge_count, cell, radius = struct.unpack_from(">HHHH", data, 12)
    off = 20
    nodes, counts = [], []
    for _ in range(node_count):
        x, y, z, n = struct.unpack_from(">hhhB", data, off)
        nodes.append((x, y, z))
        counts.append(n)
        off += 7
    adj = []
    for i, n in enumerate(counts):
        lst = []
        for _ in range(n):
            (v,) = struct.unpack_from(">H", data, off)
            to, kind = v & 0x3FFF, v >> 14
            lst.append((to, kind, edge_cost(kind, math.dist(nodes[i], nodes[to]))))
            off += 2
        adj.append(lst)
    return {"bsp_size": bsp_size, "cell": cell, "radius": radius, "nodes": nodes, "adj": adj}


def shortest_path(nodes, adj, start, goal):
    """Dijkstra over the waypoint graph (the same search the game DLL does); returns [index] or None."""
    dist = {start: 0.0}
    prev = {}
    heap = [(0.0, start)]
    while heap:
        d, i = heapq.heappop(heap)
        if i == goal:
            path = [i]
            while path[-1] != start:
                path.append(prev[path[-1]])
            return path[::-1]
        if d > dist.get(i, 1e18):
            continue
        for to, _kind, cost in adj[i]:
            nd = d + cost
            if nd < dist.get(to, 1e18):
                dist[to], prev[to] = nd, i
                heapq.heappush(heap, (nd, to))
    return None


# --- driver ----------------------------------------------------------------------------------------------
def origin_of(ent):
    try:
        return tuple(float(v) for v in ent["origin"].split())
    except (KeyError, ValueError):
        return None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bsp")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--cell", type=int, default=32, help="grid spacing of the dense floor sample (units)")
    ap.add_argument("--toggle-walls", action="store_true", help="treat func_wall_toggle brushes as solid too")
    ap.add_argument("--radius", type=int, default=384, help="every walkable cell is within this path cost of a waypoint")
    args = ap.parse_args(argv)

    with open(args.bsp, "rb") as fh:
        data = fh.read()
    bsp = Bsp(data)
    hull = Hull(bsp.planes, bsp.clipnodes, bsp.hull1_root)
    extras = solid_brush_hulls(bsp, SOLID_BRUSH_CLASSES if args.toggle_walls else DEFAULT_SOLID)
    if extras:
        print(f"solid brush entities: {len(extras)} block the walk", file=sys.stderr)
        hull = CompositeHull(hull, extras)

    seeds = []
    for ent in bsp.entities:
        if ent.get("classname") in ("info_player_allies", "info_player_axis", "info_player_start", "info_player_deathmatch"):
            o = origin_of(ent)
            if o:
                seeds.append(o)
    # capture areas and control points are goals: seed them too, so an area on an island is reported
    goals = []
    for ent in bsp.entities:
        if ent.get("classname") == "dod_capture_area" and ent.get("model", "").startswith("*"):
            m = bsp.models[int(ent["model"][1:])]
            c = tuple((m["mins"][i] + m["maxs"][i]) * 0.5 for i in range(3))
            goals.append((ent.get("target", "?"), c))
    if not seeds:
        print("error: no player spawns in the entity lump", file=sys.stderr)
        return 1

    cells, edges = flood(hull, seeds, args.cell)
    print(f"dense floor: {len(cells)} cells from {len(seeds)} spawns", file=sys.stderr)
    links = ladder_links(bsp, hull, cells, args.cell)
    forced = spawn_cells(cells, seeds, hull=hull)
    print(f"spawns: {len(seeds)} spawn points -> {len(forced)} forced waypoints", file=sys.stderr)
    for foot, top, height in links:
        edges.setdefault(foot, []).append((top, KIND_LADDER, height * 2.0))
        edges.setdefault(top, []).append((foot, KIND_LADDER, height * 1.5))
        forced += [foot, top]
    print(f"ladders: {len(links)} links from {sum(1 for e in bsp.entities if e.get('classname') == 'func_ladder')} func_ladder", file=sys.stderr)
    order = list(cells.keys())  # discovery order (BFS from the spawns)
    wps, adj, bad = build_walkable_graph(hull, cells, edges, order, float(args.radius), forced)
    print(f"straight-walk check: {len(wps)} waypoints, {bad} edges still not straight-walkable", file=sys.stderr)
    nodes = [cells[k] for k in wps]
    blob = write_wpt(nodes, adj, bsp.size, args.cell, args.radius)
    with open(args.output, "wb") as fh:
        fh.write(blob)
    print(f"wrote {args.output}: {len(nodes)} waypoints, {sum(len(a) for a in adj)} edges, {len(blob)} bytes", file=sys.stderr)

    # report: can each goal be reached from a spawn waypoint?
    def nearest(p, limit=1e18):
        best, bd = None, limit
        for i, (x, y, z) in enumerate(nodes):
            d = math.dist((x, y, z), p)
            if d < bd:
                best, bd = i, d
        return best, bd

    spawn_wps = {nearest(s)[0] for s in seeds}
    for name, c in goals:
        i, d = nearest(c)
        reach = [shortest_path(nodes, adj, s, i) for s in sorted(spawn_wps)]
        ok = sum(1 for r in reach if r)
        print(f"goal {name}: nearest waypoint {i} at {d:.0f} units, reachable from {ok}/{len(spawn_wps)} spawn waypoints"
              + (f", shortest {min(len(r) for r in reach if r)} hops" if ok else ""), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
