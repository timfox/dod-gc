"""Voice line staging reads the g_voices table of src/dod_game.c and stays under the lean disc's WAV ceiling."""
from pathlib import Path
import importlib.util
import shutil
import sys
import tempfile
import unittest
import wave

ROOT = Path(__file__).resolve().parents[1]
CEILING = 16 * 1024


def load_stager():
    spec = importlib.util.spec_from_file_location("stage_dod_audio", ROOT / "scripts/stage_dod_audio.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def write_wav(path: Path, seconds: float, rate: int = 22050) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(b"\x01\x02" * int(rate * seconds))


class StageVoiceSfxTests(unittest.TestCase):
    def test_table_lists_three_sides_per_command(self):
        stager = load_stager()
        sounds = stager.voice_table_sounds(ROOT / "src/dod_game.c")
        self.assertGreaterEqual(len(sounds), 14 * 3)
        self.assertEqual(len(sounds) % 3, 0)
        self.assertIn("player/usattack.wav", sounds)
        self.assertIn("player/britattack.wav", sounds)
        self.assertIn("player/germanleft.wav", sounds)
        self.assertEqual(len(set(sounds)), len(sounds))

    def test_clips_staged_and_under_ceiling(self):
        stager = load_stager()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dod_sound, lean = root / "dod/sound", root / "lean"
            sounds = stager.voice_table_sounds(ROOT / "src/dod_game.c")
            for rel in sounds:
                write_wav(dod_sound / rel, 2.5)
            report = stager.stage_voice_sfx(dod_sound, lean / "sound", ROOT / "src/dod_game.c")
            self.assertEqual(report["missing"], [])
            self.assertEqual(report["files"], len(sounds))
            for rel in sounds:
                staged = lean / "sound" / rel
                self.assertTrue(staged.is_file(), rel)
                if shutil.which("ffmpeg"):
                    self.assertLessEqual(staged.stat().st_size, CEILING, rel)

    def test_missing_clip_is_reported(self):
        stager = load_stager()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "dod/sound").mkdir(parents=True)
            report = stager.stage_voice_sfx(root / "dod/sound", root / "lean/sound", ROOT / "src/dod_game.c")
            self.assertEqual(report["files"], 0)
            self.assertIn("player/usattack.wav", report["missing"])


if __name__ == "__main__":
    unittest.main()
