"""scripts/check-public-tree.sh keeps private (non-game) work out of this public repository."""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(ROOT, "scripts", "check-public-tree.sh")


class PublicTreeGuardTest(unittest.TestCase):
    def setUp(self):
        self.repo = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.repo)
        os.makedirs(os.path.join(self.repo, "scripts"))
        shutil.copy(SCRIPT, os.path.join(self.repo, "scripts"))
        self.git("init", "-q", "-b", "main")
        self.git("config", "user.email", "t@example.invalid")
        self.git("config", "user.name", "t")
        # Game-side files that only look film-related stay allowed.
        self.write("src/dod_film.c", "/* film_cast actor props */\n")
        self.write("tests/dod_film_test.c", "int main(void){return 0;}\n")
        self.write("scripts/flythrough-avalanche.sh", "# spectator camera fly control\n")
        self.commit("base")

    def git(self, *args):
        return subprocess.run(["git", "-C", self.repo, *args], check=True,
                              capture_output=True, text=True).stdout.strip()

    def write(self, rel, text):
        path = os.path.join(self.repo, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write(text)

    def commit(self, msg):
        self.git("add", "-A", "-f")
        self.git("commit", "-q", "-m", msg)
        return self.git("rev-parse", "HEAD")

    def run_guard(self, *args, verbose=False):
        env = dict(os.environ, DODGC_GUARD_VERBOSE="1" if verbose else "0")
        return subprocess.run(["bash", os.path.join(self.repo, "scripts", "check-public-tree.sh"), *args],
                              capture_output=True, text=True, env=env)

    def test_game_tree_passes(self):
        r = self.run_guard()
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_private_paths_fail(self):
        for rel in ("film/episodes/x.json", "tools/film/x.py", "docs/FILM_PIPELINE.md",
                    "scripts/film-capture.sh", "tests/test_film_voice.py", "out/film/a.png"):
            with self.subTest(rel=rel):
                self.write(rel, "x\n")
                self.commit("add " + rel)
                r = self.run_guard()
                self.assertEqual(r.returncode, 1, rel)
                self.git("rm", "-q", "-r", "--cached", rel)
                os.remove(os.path.join(self.repo, rel))
                self.commit("drop " + rel)

    def test_public_log_hides_private_names(self):
        self.write("film/episodes/secret-name.json", "{}\n")
        self.commit("leak")
        r = self.run_guard()
        self.assertEqual(r.returncode, 1)
        self.assertIn("film/ (1 file)", r.stderr)
        self.assertNotIn("secret-name", r.stderr)
        self.assertIn("secret-name", self.run_guard(verbose=True).stderr)

    def test_reference_to_private_tooling_fails(self):
        self.write("src/x.c", "/* see tools/film/plan.py */\n")
        self.commit("ref")
        r = self.run_guard()
        self.assertEqual(r.returncode, 1)
        self.assertIn("src/x.c:1", r.stderr)

    def test_added_then_deleted_in_range_fails(self):
        base = self.git("rev-parse", "HEAD")
        self.write("tools/film/x.py", "x\n")
        self.commit("add")
        self.git("rm", "-q", "-r", "tools/film")
        self.commit("remove")
        self.assertEqual(self.run_guard().returncode, 0)
        r = self.run_guard("--range", base + "..HEAD")
        self.assertEqual(r.returncode, 1)
        self.assertIn("tools/film/ (1 file)", r.stderr)


if __name__ == "__main__":
    unittest.main()
