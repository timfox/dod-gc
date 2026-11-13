"""Host-side tests for the engine-independent Day of Defeat rules core."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DodGameRulesTests(unittest.TestCase):
    def test_native_rules_core(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "dod-game-rules"
            subprocess.run(
                [
                    "cc",
                    "-std=c99",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=address,undefined",
                    "-I",
                    str(ROOT / "src"),
                    str(ROOT / "src/dod_game.c"),
                    str(ROOT / "src/dod_weapons.c"),
                    str(ROOT / "tests/dod_game_rules_test.c"),
                    "-o",
                    str(binary),
                    "-lm",
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_native_hud_layout(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "dod-hud"
            subprocess.run(
                [
                    "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined",
                    "-I", str(ROOT / "src"),
                    str(ROOT / "tests/dod_hud_test.c"),
                    "-o", str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_native_bot_logic(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "dod-bots"
            subprocess.run(
                [
                    "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined",
                    "-I", str(ROOT / "src"),
                    str(ROOT / "src/dod_bots.c"),
                    str(ROOT / "tests/dod_bots_test.c"),
                    "-o", str(binary), "-lm",
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
