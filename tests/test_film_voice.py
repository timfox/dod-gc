"""tools/film/voice.py: line naming, walla, prompts, and rendered lines landing on the film's clock and in the mix."""
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.film import film, voice  # noqa: E402

VOICES = {
    "narrator": {"design": "Older man, deep calm voice", "delivery": "Measured and grave", "mode": "clone"},
    "sgt": {"design": "Young man, gravelly voice"},
    "pvt": {"design": "Young man, light voice"},
    "de": {"design": "German man, low voice", "language": "German"},
}


def episode():
    return {
        "id": "vtest", "title": "Voice test", "fps": 30, "voices": VOICES,
        "sequence": [
            {"card": {"id": "a", "dur": 12, "narration": [{"text": "First line of narration here."},
                                                          {"text": "A second, longer line of narration follows it."}],
                      "adr": [{"voice": "sgt", "at": 1.0, "text": "Move up!", "delivery": "Shouting", "fx": "field",
                               "gain": -6, "pan": -0.5},
                              {"walla": True, "at": 2, "dur": 6, "voices": ["sgt", "pvt"],
                               "lines": ["Go!", "Medic!", "Get down!"], "fx": "distant", "gain": -18}]}},
            {"card": {"id": "b", "dur": 6, "narration": [{"text": "Recorded by hand.", "audio": "vo/b.wav"}],
                      "adr": [{"voice": "de", "at": 0.5, "text": "Da unten!", "subtitle": "Down there!",
                               "fx": "distant"},
                              {"voice": "pvt", "at": 3, "text": "We're on top. Over.", "fx": "radio"}]}},
        ],
    }


class LineTests(unittest.TestCase):
    def test_ids_modes_and_languages(self):
        lines = voice.episode_lines(episode())
        ids = [ln["id"] for ln in lines]
        # narration named like narration-script.md; hand-recorded lines are skipped
        self.assertEqual(ids, ["a_01", "a_02", "a_adr01", "a_walla02_01", "a_walla02_02", "a_walla02_03",
                               "b_adr01", "b_adr02"])
        by = {ln["id"]: ln for ln in lines}
        self.assertEqual(by["a_01"]["mode"], "clone")
        self.assertEqual(by["a_adr01"]["mode"], "design")
        self.assertEqual(by["b_adr01"]["language"], "German")
        self.assertEqual([by[f"a_walla02_0{i}"]["voice"] for i in (1, 2, 3)], ["sgt", "pvt", "sgt"])

    def test_walla_is_spread_and_repeatable(self):
        src = episode()["sequence"][0]["card"]
        w1 = [a for a in voice.expand_adr(src) if a.get("walla")]
        w2 = [a for a in voice.expand_adr(src) if a.get("walla")]
        self.assertEqual(w1, w2)
        ats = [a["at"] for a in w1]
        self.assertEqual(ats, sorted(ats))
        self.assertTrue(all(2 <= t < 8 for t in ats))
        self.assertTrue(all(-0.7 <= a["pan"] <= 0.7 for a in w1))

    def test_prompt_is_voice_then_delivery(self):
        v = VOICES["narrator"]
        self.assertEqual(voice.instruct(v, {}), "Older man, deep calm voice. Measured and grave.")
        self.assertEqual(voice.instruct(VOICES["sgt"], {"delivery": "Shouting"}), "Young man, gravelly voice. Shouting.")

    def test_bad_voice_and_fx(self):
        ep = episode()
        ep["sequence"][0]["card"]["adr"][0]["voice"] = "nobody"
        with self.assertRaises(ValueError):
            voice.episode_lines(ep)
        ep = episode()
        ep["sequence"][0]["card"]["adr"][0]["fx"] = "phaser"
        with self.assertRaises(ValueError):
            voice.episode_lines(ep)

    def test_cache_key_follows_what_is_said(self):
        ep = episode()
        ln = voice.episode_lines(ep)[2]
        k = voice.line_key(ep, ln, "qwen3")
        self.assertEqual(k, voice.line_key(ep, dict(ln), "qwen3"))
        self.assertNotEqual(k, voice.line_key(ep, {**ln, "delivery": "Whispering"}, "qwen3"))
        self.assertNotEqual(k, voice.line_key(ep, {**ln, "take": 2}, "qwen3"))


@unittest.skipUnless(shutil.which("ffmpeg"), "needs ffmpeg")
class RenderTests(unittest.TestCase):
    def test_placeholder_render_lands_on_the_clock_and_in_the_mix(self):
        ep = episode()
        with tempfile.TemporaryDirectory() as d:
            out = Path(d)
            vdir = out / "voice"
            voice.render(ep, vdir, voice.Placeholder())
            self.assertTrue((vdir / "cast" / "narrator.wav").exists())   # narration reads from the designed voice
            m = json.loads((vdir / "manifest.json").read_text())
            self.assertEqual(len(m), 8)
            for f in ("a_01", "b_adr02"):
                with wave.open(str(vdir / f"{f}.wav")) as w:
                    self.assertEqual((w.getframerate(), w.getnchannels()), (48000, 1))
            # cached the second time
            before = (vdir / "raw" / "a_01.wav").stat().st_mtime_ns
            voice.render(ep, vdir, voice.Placeholder())
            self.assertEqual(before, (vdir / "raw" / "a_01.wav").stat().st_mtime_ns)

            # a hand-recorded line and a music bed, so every bus is in the mix
            (out / "vo").mkdir()
            subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i", "sine=f=220:d=1.5", str(out / "vo" / "b.wav")],
                           check=True)
            subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i", "sine=f=110:d=4", str(out / "bed.wav")],
                           check=True)
            ep["music"] = {"file": "bed.wav", "gain": 0.3}
            tl = film.build_timeline(ep, voice_dir=vdir)
            a = tl["segments"][0]
            n1, n2 = a["narration"]
            self.assertAlmostEqual(n1["dur"], voice.wav_duration(vdir / "a_01.wav"), places=2)
            self.assertAlmostEqual(n2["at"], round(n1["at"] + n1["dur"] + film.LINE_GAP, 2), places=2)
            self.assertTrue(all(x["audio"] for x in a["adr"]))
            film.write_plan(ep, tl, out)
            srt = (out / "subtitles.srt").read_text()
            self.assertIn("Down there!", srt)
            self.assertNotIn("Move up!", srt)           # untranslated callouts stay unsubtitled
            self.assertIn("a_walla02_01.wav", (out / "narration-script.md").read_text())
            work = out / "work"
            work.mkdir()
            mixed = film.build_audio(ep, tl, out, work)
            with wave.open(str(mixed)) as w:
                self.assertEqual(w.getnchannels(), 2)
                self.assertAlmostEqual(w.getnframes() / w.getframerate(), 18.0, places=1)


if __name__ == "__main__":
    unittest.main()
