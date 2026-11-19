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
from tools.film import actors, campath, film, shots, stamp  # noqa: E402

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


SAMPLER_C = r"""
#include "dod_film.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
	static char text[65536];
	static DodFilmCast cast;
	char err[96];
	FILE *f = fopen(argv[1], "rb");
	size_t n = fread(text, 1, sizeof(text) - 1, f);
	int i, k;
	fclose(f);
	text[n] = 0;
	if (DodFilm_ParseCast(&cast, text, err, sizeof(err)) < 0) { printf("ERR %s\n", err); return 1; }
	for (i = 0; i < cast.actor_count; ++i)
		for (k = 2; k < argc; ++k) {
			DodFilmSample s;
			DodFilm_Sample(&cast.actors[i], (float)atof(argv[k]), &s);
			printf("%s %s %.2f %.2f %.2f %.2f %s %d\n", cast.actors[i].name, argv[k], s.origin[0], s.origin[1],
				s.origin[2], s.yaw, DodFilm_PoseName(s.pose), s.moving);
		}
	return 0;
}
"""


class ActorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.g = shots.MapGraph.load("dod_charlie")
        cls.anchors = json.loads((ROOT / "film" / "episodes" / "ep01-omaha.json").read_text())["anchors"]["dod_charlie"]

    def test_paced_path_hold_and_look(self):
        cast = actors.build([{"id": "a", "team": "allies", "path": [
            {"at": [0, -2000, -150], "pose": "run"}, {"at": [0, -1600, -150], "pose": "crouch", "hold": 2},
            {"t": 6, "pose": "crouch", "look": [400, -1600]}]}], self.g, self.anchors)
        keys = cast[0]["keys"]
        self.assertAlmostEqual(keys[1][0], 400 / actors.PACE["run"])     # got there at running pace
        p, yaw, pose, moving = actors.sample(cast[0], 1.0)
        self.assertAlmostEqual(p[1], -2000 + actors.PACE["run"], places=3)
        self.assertTrue(moving)
        self.assertAlmostEqual(yaw, 90.0, places=3)                        # facing the way it runs
        self.assertEqual(actors.sample(cast[0], 3.0)[2], "crouch")
        self.assertAlmostEqual(actors.sample(cast[0], 7.0)[1], 0.0, places=3)   # turned to look at the point

    def test_squad_route_and_limits(self):
        cast = actors.build([{"id": "s", "team": "allies", "squad": {"count": 3},
                              "path": [{"at": "waterline", "pose": "run"}, {"at": "shingle", "route": True}]}],
                            self.g, self.anchors)
        self.assertEqual([a["id"] for a in cast], ["s", "s_1", "s_2"])
        for a in cast:
            self.assertLessEqual(len(a["keys"]), actors.MAX_KEYS)
            self.assertEqual(sorted(k[0] for k in a["keys"]), [k[0] for k in a["keys"]])
        # squad members keep apart along the route
        mid = [actors.sample(a, 6.0)[0] for a in cast]
        self.assertGreater(min(((mid[i][0] - mid[j][0]) ** 2 + (mid[i][1] - mid[j][1]) ** 2) ** 0.5
                               for i in range(3) for j in range(i + 1, 3)), 20)
        bad = [
            [{"id": "A", "path": [{"at": [0, 0]}]}],
            [{"id": "a", "team": "nobody", "path": [{"at": [0, 0]}]}],
            [{"id": "a", "path": [{"at": [0, 0], "pose": "dance"}]}],
            [{"id": "a", "path": [{"at": [0, 0], "pose": "stand"}, {"at": [500, 0]}]}],   # standing does not travel
            [{"id": f"a{i}", "path": [{"at": [0, 0]}]} for i in range(actors.MAX_ACTORS + 1)],
        ]
        for spec in bad:
            with self.assertRaises(ValueError):
                actors.build(spec, self.g, self.anchors)

    def test_episode_casts_and_session(self):
        ep = film.load_episode(ROOT / "film" / "episodes" / "ep01-omaha.json")
        tl = film.build_timeline(ep)
        with tempfile.TemporaryDirectory() as tmp:
            film.write_plan(ep, tl, Path(tmp))
            cfg = (Path(tmp) / "film-dod_charlie.cfg").read_text()
            self.assertIn("film_cast s03_waterline 2; cine_onend film_3; cine_play s03_waterline 2", cfg)
            self.assertIn('alias film_0 "film_cast_clear;', cfg)             # no actors: the last shot's leave
            self.assertIn("film_cast_clear; echo FILM SESSION DONE", cfg)
            self.assertEqual((Path(tmp) / "cine" / "cast-dod_charlie.txt").read_text(), "allies\n")
            self.assertTrue((Path(tmp) / "cine" / "s04_crossing.act").exists())
            self.assertFalse((Path(tmp) / "cine" / "s01_dawn.act").exists())

    def test_game_dll_samples_the_same(self):
        """The cast file the plan writes, parsed and sampled by src/dod_film.c, puts everyone where previs does."""
        import subprocess
        ep = film.load_episode(ROOT / "film" / "episodes" / "ep01-omaha.json")
        tl = film.build_timeline(ep)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "sampler.c"
            src.write_text(SAMPLER_C)
            exe = Path(tmp) / "sampler"
            subprocess.run(["cc", "-std=c99", "-Wall", "-Werror", "-I", str(ROOT / "src"), str(src),
                            str(ROOT / "src" / "dod_film.c"), "-o", str(exe), "-lm"], check=True)
            checked = 0
            for seg in tl["segments"]:
                if seg["kind"] != "shot" or not seg["cast"]:
                    continue
                act = Path(tmp) / f"{seg['id']}.act"
                act.write_text(actors.dumps(seg["cast"]))
                times = [f"{seg['dur'] * k / 7:.3f}" for k in range(8)]
                out = subprocess.run([str(exe), str(act)] + times, check=True, capture_output=True, text=True).stdout
                for line in out.splitlines():
                    name, t, x, y, z, yaw, pose, moving = line.split()
                    a = next(a for a in seg["cast"] if a["id"] == name)
                    # the file rounds to 0.1 unit and 1 ms: compare against the rounded keys
                    p, py, ppose, pm = actors.sample(a, float(t))
                    self.assertLess(max(abs(float(x) - p[0]), abs(float(y) - p[1]), abs(float(z) - p[2])), 1.0, line)
                    self.assertLess(abs((float(yaw) - py + 180) % 360 - 180), 1.0, line)
                    self.assertEqual(pose, ppose, line)
                    checked += 1
            self.assertGreater(checked, 50)


if __name__ == "__main__":
    unittest.main()
