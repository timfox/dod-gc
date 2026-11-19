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



class StripSteam(unittest.TestCase):
    def test_utf16_and_utf8(self):
        sys_path = str(ROOT / "scripts")
        import sys
        sys.path.insert(0, sys_path)
        import strip_steam_strings as m
        with tempfile.TemporaryDirectory() as t:
            u16 = Path(t, "a.txt")
            u16.write_bytes(b"\xff\xfe" + '"Keep" "1"\r\n"Valve_Enable_SteamNetworking" "Enable Steam Networking"\r\n"Z" "2"\r\n'.encode("utf-16-le"))
            u8 = Path(t, "b.txt")
            u8.write_text('"Valve_Enable_SteamNetworking" "Enable Steam Networking"\n"x" "y"\n')
            other = Path(t, "c.cfg")
            other.write_text("Enable Steam Networking\n")
            self.assertEqual(m.main([t]), 1)  # c.cfg is not a localisation file, still reported
            other.unlink()
            u16.write_bytes(b"\xff\xfe" + '"Keep" "1"\r\n"Valve_Enable_SteamNetworking" "Enable Steam Networking"\r\n'.encode("utf-16-le"))
            u8.write_text('"Valve_Enable_SteamNetworking" "Enable Steam Networking"\n"x" "y"\n')
            self.assertEqual(m.main([t]), 0)
            self.assertNotIn("Steam", u16.read_bytes().decode("utf-16"))
            self.assertIn("Keep", u16.read_bytes().decode("utf-16"))
            self.assertEqual(u8.read_text(), '"x" "y"\n')


if __name__ == "__main__":
    unittest.main()
