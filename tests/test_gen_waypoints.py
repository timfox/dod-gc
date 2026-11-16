"""tools/gen_waypoints.py: hull collision, walking rules, thinning, ladders, and the .wpt format."""
import importlib.util
import math
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("gen_waypoints", ROOT / "tools" / "gen_waypoints.py")
G = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(G)

# The same literal tests/dod_bots_test.c parses and searches (TOY_WPT_HEX).
TOY_WPT_HEX = "44575054000200000001e2400005000900200120000000000000010064000000000300c800000000020064000000c80200c8000000c801000100000002c00300014004c00100040003"
TOY_NODES = [(0, 0, 0), (100, 0, 0), (200, 0, 0), (100, 0, 200), (200, 0, 200)]
TOY_ADJ = [
    [(1, G.KIND_WALK, 100)],
    [(0, G.KIND_WALK, 100), (2, G.KIND_WALK, 100), (3, G.KIND_LADDER, 400)],
    [(1, G.KIND_WALK, 100), (4, G.KIND_JUMP, 704)],
    [(1, G.KIND_LADDER, 304), (4, G.KIND_WALK, 100)],
    [(3, G.KIND_WALK, 100)],
]


def mini_world():
    """Clip hull of a tiny world: floor at z=0 for x<100 (hull origin z=36), a pit with its floor 100 lower for
    x>=100 (z=-64), and a wall: solid x>=300.  Planes are already hull-expanded, like a BSP's clip nodes."""
    planes = [
        (0.0, 0.0, 1.0, 36.0),     # 0: floor of the main area
        (0.0, 0.0, 1.0, -64.0),    # 1: floor of the pit
        (1.0, 0.0, 0.0, 100.0),    # 2: pit edge
        (1.0, 0.0, 0.0, 300.0),    # 3: wall
    ]
    clipnodes = [
        (0, G.CONTENTS_EMPTY, G.CONTENTS_SOLID),   # 0: above/below the main floor
        (1, G.CONTENTS_EMPTY, G.CONTENTS_SOLID),   # 1: above/below the pit floor
        (2, 1, 0),                                 # 2: x >= 100 -> pit floor, else main floor
        (3, G.CONTENTS_SOLID, 2),                  # 3: x >= 300 -> wall, else node 2   (root)
    ]
    return G.Hull(planes, clipnodes, 3)


class WptFormatTest(unittest.TestCase):
    def test_writer_matches_the_literal_the_c_tests_parse(self):
        self.assertEqual(G.write_wpt(TOY_NODES, TOY_ADJ, 123456, 32, 288).hex(), TOY_WPT_HEX)

    def test_roundtrip(self):
        data = G.read_wpt(bytes.fromhex(TOY_WPT_HEX))
        self.assertEqual(data["nodes"], TOY_NODES)
        self.assertEqual(data["bsp_size"], 123456)
        self.assertEqual([[(t, k) for t, k, _ in a] for a in data["adj"]], [[(t, k) for t, k, _ in a] for a in TOY_ADJ])
        self.assertEqual(data["adj"][1][2][:2], (3, G.KIND_LADDER))  # costs are derived from the distance, not stored

    def test_shortest_path_takes_the_ladder_up_and_walks_the_rest(self):
        data = G.read_wpt(bytes.fromhex(TOY_WPT_HEX))
        self.assertEqual(G.shortest_path(data["nodes"], data["adj"], 0, 3), [0, 1, 3])
        self.assertEqual(G.shortest_path(data["nodes"], data["adj"], 0, 4), [0, 1, 2, 4])
        self.assertIsNone(G.shortest_path(data["nodes"], [[], [], [], [], []], 0, 4))

    def test_bad_input_is_rejected(self):
        with self.assertRaises(ValueError):
            G.read_wpt(b"XXXX" + bytes.fromhex(TOY_WPT_HEX)[4:])


class HullTest(unittest.TestCase):
    def setUp(self):
        self.hull = mini_world()

    def test_contents(self):
        self.assertEqual(self.hull.contents((0, 0, 50)), G.CONTENTS_EMPTY)
        self.assertEqual(self.hull.contents((0, 0, 10)), G.CONTENTS_SOLID)
        self.assertEqual(self.hull.contents((310, 0, 50)), G.CONTENTS_SOLID)
        self.assertEqual(self.hull.contents((150, 0, 0)), G.CONTENTS_EMPTY)   # over the pit

    def test_trace_down_lands_on_the_floor(self):
        tr = self.hull.trace((0, 0, 100), (0, 0, 0))
        self.assertFalse(tr.startsolid)
        self.assertAlmostEqual(tr.endpos[2], 36.0, places=1)
        self.assertAlmostEqual(tr.fraction, 0.64, places=2)
        self.assertEqual(tr.normal_z, 1.0)

    def test_trace_into_the_wall_stops_at_it(self):
        tr = self.hull.trace((0, 0, 40), (400, 0, 40))
        self.assertAlmostEqual(tr.endpos[0], 300.0, delta=0.5)
        self.assertLess(tr.fraction, 1.0)

    def test_start_in_solid(self):
        self.assertTrue(self.hull.trace((0, 0, 10), (0, 0, 40)).startsolid)

    def test_find_ground(self):
        self.assertAlmostEqual(G.find_ground(self.hull, 10, 0, 50, -100), 36.0, places=1)
        self.assertAlmostEqual(G.find_ground(self.hull, 150, 0, 50, -100), -64.0, places=1)
        self.assertIsNone(G.find_ground(self.hull, 150, 0, 50, -20))   # nothing within reach


class WalkTest(unittest.TestCase):
    def setUp(self):
        self.hull = mini_world()

    def test_flat_walk(self):
        (x, y, z), kind = G.try_move(self.hull, (0, 0, 36.0), 32, 0)
        self.assertEqual((x, y, kind), (32, 0, G.KIND_WALK))
        self.assertAlmostEqual(z, 36.0, places=1)

    def test_walking_into_the_wall_is_refused(self):
        self.assertIsNone(G.try_move(self.hull, (280, 0, 36.0), 32, 0))

    def test_walking_off_the_edge_is_a_drop(self):
        (x, _y, z), kind = G.try_move(self.hull, (96, 0, 36.0), 32, 0)
        self.assertEqual(kind, G.KIND_DROP)
        self.assertAlmostEqual(z, -64.0, places=1)

    def test_flood_stays_on_the_floor_and_drops_one_way(self):
        cells, edges = G.flood(self.hull, [(0, 0, 40)], 32, max_cells=500)
        zs = {round(c[2]) for c in cells.values()}
        self.assertEqual(zs, {36, -64})
        # no cell inside the wall, and nothing climbs back out of the pit (a 100 unit rise is not a jump)
        self.assertTrue(all(c[0] < 300 for c in cells.values()))
        pit = [k for k, c in cells.items() if c[2] < 0]
        self.assertTrue(pit and all(kind != G.KIND_JUMP for k in pit for _k2, kind, _c in edges.get(k, [])))
        self.assertTrue(all(cells[k2][2] < 0 for k in pit for k2, _kind, _c in edges.get(k, [])))


class ThinningTest(unittest.TestCase):
    def line(self, n):
        cells = {(i, 0, 0): (i * 32.0, 0.0, 36.0) for i in range(n)}
        edges = {}
        for i in range(n):
            edges[(i, 0, 0)] = [((j, 0, 0), G.KIND_WALK, 32.0) for j in (i - 1, i + 1) if 0 <= j < n]
        return cells, edges

    def test_cover_and_connect(self):
        cells, edges = self.line(10)
        wps = G.select_waypoints(cells, edges, list(cells), 64.0)
        self.assertEqual(wps, [(0, 0, 0), (3, 0, 0), (6, 0, 0), (9, 0, 0)])
        adj = G.build_graph(cells, edges, wps)
        self.assertEqual([sorted(t for t, _k, _c in a) for a in adj], [[1], [0, 2], [1, 3], [2]])
        self.assertIsNotNone(G.shortest_path([cells[k] for k in wps], adj, 0, 3))

    def test_ladder_ends_are_forced_waypoints_and_coverage_does_not_cross(self):
        cells = {(0, 0, 0): (0.0, 0.0, 36.0), (1, 0, 0): (32.0, 0.0, 36.0), (0, 0, 20): (0.0, 0.0, 276.0)}
        edges = {
            (0, 0, 0): [((1, 0, 0), G.KIND_WALK, 32.0), ((0, 0, 20), G.KIND_LADDER, 480.0)],
            (1, 0, 0): [((0, 0, 0), G.KIND_WALK, 32.0)],
            (0, 0, 20): [((0, 0, 0), G.KIND_LADDER, 360.0)],
        }
        wps = G.select_waypoints(cells, edges, list(cells), 1000.0, forced=[(0, 0, 0), (0, 0, 20)])
        self.assertEqual(wps[:2], [(0, 0, 0), (0, 0, 20)])
        adj = G.build_graph(cells, edges, wps)
        self.assertIn((1, G.KIND_LADDER), [(t, k) for t, k, _c in adj[0]])
        self.assertIn((0, G.KIND_LADDER), [(t, k) for t, k, _c in adj[1]])


class SpawnCellTest(unittest.TestCase):
    def test_each_spawn_gets_a_cell_and_close_spawns_share_one(self):
        cells = {(i, 0, 1): (i * 32.0, 0.0, 36.0) for i in range(20)}
        seeds = [(0.0, 0.0, 36.0), (40.0, 0.0, 36.0), (320.0, 0.0, 36.0), (900.0, 0.0, 36.0)]
        keys = G.spawn_cells(cells, seeds, min_gap=96.0)
        self.assertEqual([cells[k][0] for k in keys], [0.0, 320.0])   # the 2nd is within 96 of the 1st; the 4th is off the map

    def test_a_spawn_on_another_floor_is_kept_separate(self):
        cells = {(0, 0, 1): (0.0, 0.0, 36.0), (0, 0, 9): (0.0, 0.0, 300.0)}
        keys = G.spawn_cells(cells, [(0.0, 0.0, 36.0), (0.0, 0.0, 300.0)], min_gap=96.0)
        self.assertEqual(sorted(cells[k][2] for k in keys), [36.0, 300.0])


class LadderLinkTest(unittest.TestCase):
    def test_a_ladder_joins_the_floor_at_its_foot_to_the_floor_it_tops_out_on(self):
        # floor z=0 (hull origin 36) for x<200; a platform at z=200 (origin 236) behind a wall plane at x=300
        planes = [(0, 0, 1, 36.0), (0, 0, 1, 236.0), (1, 0, 0, 300.0)]
        clipnodes = [
            (0, G.CONTENTS_EMPTY, G.CONTENTS_SOLID),
            (1, G.CONTENTS_EMPTY, G.CONTENTS_SOLID),
            (2, 1, 0),   # x >= 300: platform floor, else main floor
        ]
        hull = G.Hull(planes, clipnodes, 2)
        bsp = type("B", (), {})()
        bsp.models = [None, {"mins": (296, -16, 0), "maxs": (298, 16, 208), "origin": (0, 0, 0), "headnode": (0, 0, 0, 0)}]
        bsp.entities = [{"classname": "func_ladder", "model": "*1"}]
        cells = {(9, 0, 3): (288.0, 0.0, 36.0), (10, 0, 19): (320.0, 0.0, 236.0)}
        links = G.ladder_links(bsp, hull, cells, 32)
        self.assertEqual([(a, b) for a, b, _h in links], [((9, 0, 3), (10, 0, 19))])
        self.assertAlmostEqual(links[0][2], 200.0)


if __name__ == "__main__":
    unittest.main()
