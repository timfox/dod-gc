"""tools/film: camera paths (mirror of the engine's cine_path.c), frame stamps, capture splitting, planning."""
import io
import json
import tempfile
import unittest
from pathlib import Path

from PIL import Image

import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.film import campath, film, shots, stamp  # noqa: E402

# The same path xash3d-gc tests/fixtures/cine_path_test.c samples; the numbers below match it.
DOLLY = """# a test dolly
cam 1
key 0   0   0   0   0  350 0 90
key 2 100   0   0  10   10 0 90 linear   # 20 degrees through north
key 4 100 100   0   0   20 0 60 hold
key 6 200 200  50   0   30 0 60
key 8 300 200  50   0   40 0 60
key 10 400 200 50   0   50 0 60
"""


class CamPathTests(unittest.TestCase):
    def test_sampling_matches_engine_cases(self):
        p = campath.parse(DOLLY)
        self.assertEqual(len(p.keys), 6)
        self.assertAlmostEqual(p.duration, 10.0)
        o, a, fov = campath.sample(p, 0.5)
        self.assertTrue(0 < o[0] < 25)                 # eases in
        o, a, fov = campath.sample(p, 1.0)
        self.assertAlmostEqual(a[1], 0.0, places=2)    # 350 -> 10 through 0
        self.assertAlmostEqual(o[0], 50.0, places=2)
        o, a, fov = campath.sample(p, 2.5)
        self.assertAlmostEqual(o[1], 25.0, places=2)   # linear
        self.assertAlmostEqual(fov, 82.5, places=2)
        o, _, _ = campath.sample(p, 5.99)
        self.assertEqual(o[:2], (100.0, 100.0))        # hold, then cut
        o, _, _ = campath.sample(p, 6.0)
        self.assertAlmostEqual(o[0], 200.0)
        b, _, _ = campath.sample(p, 7.95)
        c, _, _ = campath.sample(p, 8.05)
        self.assertGreater(c[0] - b[0], 4.0)           # passes through a middle key

    def test_round_trip_and_errors(self):
        p = campath.parse(DOLLY)
        again = campath.parse(campath.dumps(p, "comment"))
        self.assertEqual(len(again.keys), 6)
        self.assertEqual(again.keys[2].mode, campath.HOLD)
        for bad in ("key 0 0 0 0 0 0 0 90\n", "cam 1\nkey 1 0 0 0 0 0 0 90\nkey 1 0 0 0 0 0 0 90\n",
                    "cam 1\nkey 0 0 0 0 0 0 0 90 wobble\n", "cam 2\n", "cam 1\n", "cam 1\nkey 0 0 0 0 0 0 0 200\n"):
            with self.assertRaises(ValueError):
                campath.parse(bad)

    def test_look_angles(self):
        self.assertAlmostEqual(campath.look_angles((0, 0, 0), (100, 0, 0))[1], 0.0)
        self.assertAlmostEqual(campath.look_angles((0, 0, 0), (0, 100, 0))[1], 90.0)
        self.assertGreater(campath.look_angles((0, 0, 100), (100, 0, 0))[0], 0.0)  # looking down is positive


class StampTests(unittest.TestCase):
    def test_word_round_trip(self):
        for f in (0, 1, 2, 1000, 54321, (1 << 17) - 1):
            self.assertEqual(stamp.frame_of(stamp.word(f)), f)
            self.assertIsNone(stamp.frame_of(stamp.word(f) ^ 4))

    def test_read_from_pictures(self):
        for size in ((640, 480), (1440, 1080), (1920, 1080)):
            im = Image.new("RGB", size, (90, 120, 100))
            stamp.draw(im, 1234)
            self.assertEqual(stamp.read(im), 1234)
            buf = io.BytesIO()
            im.save(buf, "JPEG", quality=80)
            self.assertEqual(stamp.read(Image.open(io.BytesIO(buf.getvalue()))), 1234)
        self.assertIsNone(stamp.read(Image.new("RGB", (640, 480), (128, 128, 128))))
        self.assertIsNone(stamp.read(Image.new("RGB", (640, 480), (0, 0, 0))))
        self.assertIsNone(stamp.read(Image.new("RGB", (640, 480), (255, 255, 255))))


class CaptureTests(unittest.TestCase):
    def test_split_and_fill(self):
        # shot A frames 0..4 with a duplicate and a gap; shot B restarts at 0
        stamped = [(0, "a0"), (1, "a1"), (1, "a1b"), (3, "a3"), (4, "a4"), (0, "b0"), (1, "b1"), (2, "b2")]
        runs = film.split_runs(stamped)
        self.assertEqual(len(runs), 2)
        frames, missing = film.frame_list(runs[0], 5)
        self.assertEqual(frames, ["a0", "a1", "a1", "a3", "a4"])
        self.assertEqual(missing, 1)
        frames, missing = film.frame_list(runs[1], 3)
        self.assertEqual((frames, missing), (["b0", "b1", "b2"], 0))

    def test_read_session_skips_unstamped(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            Image.new("RGB", (320, 240), (40, 40, 40)).save(d / "framedump_0.png")    # preroll
            for i in range(3):
                stamp.draw(Image.new("RGB", (320, 240), (40, 60, 40)), i).save(d / f"framedump_{i + 1}.png")
            stamp.draw(Image.new("RGB", (320, 240), (40, 60, 40)), 0).save(d / "framedump_10.png")  # next shot
            got = film.read_session(d)
            self.assertEqual([f for f, _ in got], [0, 1, 2, 0])            # natural order: 10 after 3
            self.assertEqual(len(film.split_runs(got)), 2)


class PlanTests(unittest.TestCase):
    def test_sample_episode_plans(self):
        ep = film.load_episode(ROOT / "film" / "episodes" / "ep01-omaha.json")
        tl = film.build_timeline(ep)
        with tempfile.TemporaryDirectory() as tmp:
            plan = film.write_plan(ep, tl, Path(tmp))
            shots_ = [s for s in tl["segments"] if s["kind"] == "shot"]
            self.assertGreaterEqual(len(shots_), 4)
            for s in shots_:
                cam = campath.parse((Path(tmp) / "cine" / f"{s['id']}.cam").read_text())
                self.assertLessEqual(len(cam.keys), campath.MAX_KEYS)
                self.assertAlmostEqual(cam.duration, s["dur"], places=2)
                for line in s["narration"]:
                    self.assertLessEqual(line["at"] + line["dur"], s["dur"] + 0.05, (s["id"], line["text"]))
            cfg = (Path(tmp) / "film-dod_charlie.cfg").read_text()
            self.assertIn("cine_play s01_dawn", cfg)
            self.assertTrue(cfg.rstrip().endswith("film_0"))
            self.assertEqual(plan["sessions"]["dod_charlie"][0], "s01_dawn")
            self.assertIn("-->", (Path(tmp) / "subtitles.srt").read_text())

    def test_dolly_follows_walkable_route(self):
        g = shots.MapGraph.load("dod_charlie")
        p = shots.dolly(g, [-352, -3168], [224, 960], height=40, duration=12)
        self.assertAlmostEqual(p.duration, 12.0, places=2)
        self.assertLessEqual(len(p.keys), campath.MAX_KEYS)
        # every key sits near a waypoint (the camera stays over walkable ground)
        for k in p.keys:
            n = g.nodes[g.nearest(k.o[0], k.o[1])]
            self.assertLess(((n[0] - k.o[0]) ** 2 + (n[1] - k.o[1]) ** 2) ** 0.5, 400)


class GrammarTests(unittest.TestCase):
    def setUp(self):
        self.g = shots.MapGraph.load("dod_charlie")

    def test_lens(self):
        self.assertAlmostEqual(shots.lens_fov(50), 39.6, places=1)
        self.assertAlmostEqual(shots.lens_fov(24), 73.7, places=1)
        self.assertAlmostEqual(shots.fov_lens(shots.lens_fov(35)), 35.0, places=3)

    def test_modifiers(self):
        spec = {"type": "still", "eye": [0, -384], "target": [224, 960], "duration": 10, "lens": 35, "lens_end": 75,
                "dutch": [0, -8], "handheld": 0.5, "seed": 2}
        p = shots.build(spec, self.g)
        q = shots.build(spec, self.g)
        self.assertEqual([k.o for k in p.keys], [k.o for k in q.keys])       # same seed, same shake
        self.assertLessEqual(len(p.keys), campath.MAX_KEYS)
        self.assertAlmostEqual(p.duration, 10.0, places=2)
        _, a0, f0 = campath.sample(p, 0)
        _, a1, f1 = campath.sample(p, 10)
        self.assertAlmostEqual(f0, shots.lens_fov(35), delta=0.1)
        self.assertAlmostEqual(f1, shots.lens_fov(75), delta=0.1)
        self.assertLess(a1[2], -6)                                          # rolled into the dutch
        steady = shots.build({k: v for k, v in spec.items() if k not in ("handheld", "seed", "dutch")}, self.g)
        self.assertTrue(any(abs(campath.sample(p, t)[1][1] - campath.sample(steady, t)[1][1]) > 0.2
                            for t in (2, 4, 6, 8)))                         # handheld moves the frame

    def test_boards_render(self):
        ep = film.load_episode(ROOT / "film" / "episodes" / "ep01-omaha.json")
        tl = film.build_timeline(ep)
        from tools.film import board
        with tempfile.TemporaryDirectory() as tmp:
            pages = board.render_sheets(ep, tl, Path(tmp), ep["anchors"])
            self.assertGreaterEqual(len(pages), 2)
            self.assertEqual(Image.open(pages[0]).mode, "RGB")
        self.assertIn("| 1 |", board.shot_list(ep, tl))
        # the sketch puts the ground below the sky: a camera looking level sees mesh in the lower half
        im = board.sketch(self.g, (-352, -3168, -200), (0, 85, 0), 70, 400, 200)
        lower = sum(1 for x in range(0, 400, 4) for y in range(110, 200, 4) if im.getpixel((x, y))[0] < 200)
        upper = sum(1 for x in range(0, 400, 4) for y in range(0, 60, 4) if im.getpixel((x, y))[0] < 200)
        self.assertGreater(lower, upper)


if __name__ == "__main__":
    unittest.main()
