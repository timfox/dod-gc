"""scripts/check-menu-provenance.sh: a build is refused when mainui is not at the engine's pinned commit or the menu
string of the retired Steam Networking checkbox is in the sources or the DOL."""
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_fetch_sources import git, make_repo, commit, ROOT

SCRIPT = ROOT / "scripts" / "check-menu-provenance.sh"
BAD = b"xx Enable Steam Networking xx"


def run(*args, env=None):
    return subprocess.run(["bash", str(SCRIPT), *args], capture_output=True, text=True, env=env)


class DolCheck(unittest.TestCase):
    def test_dol(self):
        with tempfile.TemporaryDirectory() as t:
            good, bad = Path(t, "good.dol"), Path(t, "bad.dol")
            good.write_bytes(b"\0sv_lan\0Steam\0")
            bad.write_bytes(b"\0" + BAD + b"\0")
            self.assertEqual(run("--dol", str(good)).returncode, 0)
            r = run("--dol", str(bad))
            self.assertEqual(r.returncode, 1)
            self.assertIn("stale mainui", r.stderr)


class EngineCheck(unittest.TestCase):
    def make_engine(self, t, text):
        ui = make_repo(t, "ui", "main")
        commit(ui, "menus/CreateGame.cpp", text)
        pinned = git(ui, "rev-parse", "HEAD")
        eng = make_repo(t, "eng", "main")
        commit(eng, "README", "x")
        git(eng, "-c", "protocol.file.allow=always", "submodule", "add", "-q", str(ui), "3rdparty/mainui")
        git(eng, "commit", "-q", "-m", "pin")
        return eng, ui, pinned

    def test_clean_and_drift(self):
        with tempfile.TemporaryDirectory() as t:
            eng, ui, pinned = self.make_engine(t, "// clean\n")
            self.assertEqual(run("--engine", str(eng)).returncode, 0)
            sub = eng / "3rdparty/mainui"
            commit(sub, "menus/CreateGame.cpp", "n.szName = \"Enable Steam Networking\";\n")
            r = run("--engine", str(eng))
            self.assertEqual(r.returncode, 1)
            self.assertIn("pins", r.stderr)
            self.assertIn("contain", r.stderr)

    def test_source_string_at_pin(self):
        with tempfile.TemporaryDirectory() as t:
            eng, ui, pinned = self.make_engine(t, 'x = "Enable Steam Networking";\n')
            r = run("--engine", str(eng))
            self.assertEqual(r.returncode, 1)
            self.assertIn("contain", r.stderr)


if __name__ == "__main__":
    unittest.main()
