"""Grenade audio staging stays inside the lean disc's WAV size ceiling."""
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


class StageGrenadeSfxTests(unittest.TestCase):
    def test_clips_staged_lowercase_and_under_ceiling(self):
        stager = load_stager()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dod_sound, valve_sound, lean = root / "dod/sound", root / "valve/sound", root / "lean"
            for rel in stager.GRENADE_SFX:
                if rel.startswith("weapons/debris"):
                    write_wav(valve_sound / rel, 3.0)  # debris only exists in valve
                else:
                    write_wav(dod_sound / rel, 3.0)
            report = stager.stage_grenade_sfx(dod_sound, lean / "sound", valve_sound, lean / "media")
            self.assertEqual(report["missing"], [])
            self.assertEqual(report["files"], len(stager.GRENADE_SFX))
            for rel in stager.GRENADE_SFX:
                staged = lean / "sound" / rel
                self.assertTrue(staged.is_file(), rel)
                self.assertTrue((lean / "media" / f"sfx_{Path(rel).name}").is_file(), rel)
                if shutil.which("ffmpeg"):
                    self.assertLessEqual(staged.stat().st_size, CEILING, rel)

    def test_missing_clip_is_reported_not_fatal(self):
        stager = load_stager()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_wav(root / "dod/sound/weapons/explode3.wav", 1.0)
            report = stager.stage_grenade_sfx(root / "dod/sound", root / "lean/sound", None, root / "lean/media")
            self.assertEqual(report["files"], 1)
            self.assertIn("weapons/grenthrow.wav", report["missing"])


if __name__ == "__main__":
    unittest.main()
