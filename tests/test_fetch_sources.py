"""scripts/fetch-sources.sh, tip-status.sh: a rebuild cannot silently use a stale engine, and only the clones under
.deps are ever reset."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "scripts"
GIT_ENV = {"GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@t", "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@t"}


def git(cwd, *args):
    return subprocess.run(["git", "-C", str(cwd), *args], check=True, capture_output=True, text=True,
                          env={**os.environ, **GIT_ENV}).stdout.strip()


def make_repo(parent, name, branch):
    repo = Path(parent) / name
    repo.mkdir(parents=True)
    git(repo, "init", "-q", "-b", branch)
    return repo


def commit(repo, rel, text):
    (repo / rel).parent.mkdir(parents=True, exist_ok=True)
    (repo / rel).write_text(text)
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", f"change {rel}")
    return git(repo, "rev-parse", "HEAD")


def tip_status(directory, ref, env=None):
    return subprocess.run(["bash", "-c", f'source "{SCRIPTS}/tip-status.sh"; dodgc_tip_status "{directory}" "{ref}"'],
                          capture_output=True, text=True, env={**os.environ, **(env or {})}).stdout.strip()


class TipStatusTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.origin = make_repo(self.tmp.name, "origin", "master")
        commit(self.origin, "a.txt", "1\n")
        self.clone = Path(self.tmp.name) / "clone"
        subprocess.run(["git", "clone", "-q", str(self.origin), str(self.clone)], check=True, env={**os.environ, **GIT_ENV})

    def test_at_the_tip(self):
        self.assertEqual(tip_status(self.clone, "master"), "tip=yes")

    def test_behind_is_caught_even_if_never_fetched(self):
        commit(self.origin, "a.txt", "2\n")
        self.assertRegex(tip_status(self.clone, "master"), r"^tip=NO \(origin/master is [0-9a-f]{10}; the checkout is behind")
        git(self.clone, "fetch", "-q", "origin")
        self.assertRegex(tip_status(self.clone, "master"), r"^tip=NO \(behind 1: origin/master is [0-9a-f]{10}\)$")

    def test_ahead_and_not_a_branch_and_unreachable(self):
        commit(self.clone, "b.txt", "local\n")
        self.assertTrue(tip_status(self.clone, "master").startswith("tip=ahead"))
        self.assertTrue(tip_status(self.clone, "v1.0").startswith("tip=n/a"))
        git(self.clone, "remote", "set-url", "origin", str(Path(self.tmp.name) / "gone"))
        self.assertTrue(tip_status(self.clone, "master").startswith("tip=unknown"))
        self.assertTrue(tip_status(Path(self.tmp.name) / "nothing", "master").startswith("tip=unknown"))

    def test_engine_is_stale_exit_codes(self):
        run = lambda: subprocess.run(["bash", "-c", f'source "{SCRIPTS}/tip-status.sh"; dodgc_engine_is_stale "{self.clone}" master']).returncode
        self.assertEqual(run(), 1)       # at the tip: not stale
        commit(self.origin, "a.txt", "2\n")
        self.assertEqual(run(), 0)       # behind: stale


class FetchSourcesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        t = self.tmp.name
        self.engine_origin = make_repo(t, "engine-origin", "master")
        commit(self.engine_origin, "scripts/build-gamecube.sh", "x\n")
        self.sdk_origin = make_repo(t, "sdk-origin", "sdkbranch")
        self.pin = commit(self.sdk_origin, "dlls/player.cpp", "pinned\n")
        commit(self.sdk_origin, "dlls/player.cpp", "moved on\n")
        self.deps = Path(t) / "deps"
        self.lock = Path(t) / "sources.lock"
        self.lock.write_text(f'DODGC_ENGINE_URL="{self.engine_origin}"\nDODGC_ENGINE_REF="master"\n'
                             f'DODGC_HLSDK_URL="{self.sdk_origin}"\nDODGC_HLSDK_BRANCH="sdkbranch"\nDODGC_HLSDK_COMMIT="{self.pin}"\n')

    def fetch(self, *args, env_extra=None):
        env = {k: v for k, v in os.environ.items() if k not in ("DODGC_ENGINE", "DODGC_HLSDK")}
        env.update({"DODGC_SOURCES_LOCK": str(self.lock), "DODGC_DEPS_DIR": str(self.deps), **GIT_ENV, **(env_extra or {})})
        return subprocess.run(["bash", str(SCRIPTS / "fetch-sources.sh"), *args], capture_output=True, text=True, env=env, cwd=ROOT)

    def test_update_moves_the_engine_to_the_branch_tip_and_resets_the_integrated_sdk(self):
        first = self.fetch()
        self.assertEqual(first.returncode, 0, first.stderr)
        sdk = self.deps / "hlsdk-portable"
        self.assertEqual(git(sdk, "rev-parse", "HEAD"), self.pin)
        # what the integrator does to the SDK clone on every build
        (sdk / "dlls" / "player.cpp").write_text("integrated\n")
        (sdk / "dlls" / "dod_bot.cpp").write_text("copied in\n")
        new_tip = commit(self.engine_origin, "scripts/build-gamecube.sh", "newer\n")
        without = self.fetch()                       # no --update: nothing moves
        self.assertEqual(without.returncode, 0, without.stderr)
        self.assertNotEqual(git(self.deps / "xash3d-gc", "rev-parse", "HEAD"), new_tip)
        updated = self.fetch("--update")
        self.assertEqual(updated.returncode, 0, updated.stderr)
        self.assertEqual(git(self.deps / "xash3d-gc", "rev-parse", "HEAD"), new_tip)
        self.assertEqual(git(sdk, "rev-parse", "HEAD"), self.pin)
        self.assertEqual((sdk / "dlls" / "player.cpp").read_text(), "pinned\n")
        self.assertFalse((sdk / "dlls" / "dod_bot.cpp").exists())

    def test_an_engine_clone_with_local_changes_is_still_refused(self):
        self.assertEqual(self.fetch().returncode, 0)
        (self.deps / "xash3d-gc" / "scripts" / "build-gamecube.sh").write_text("my edit\n")
        result = self.fetch("--update")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("local changes", result.stderr)

    def test_a_checkout_you_supplied_is_never_touched(self):
        mine = make_repo(self.tmp.name, "my-sdk", "main")
        commit(mine, "dlls/player.cpp", "mine\n")
        (mine / "dlls" / "player.cpp").write_text("uncommitted work\n")
        (mine / "dlls" / "untracked.cpp").write_text("keep me\n")
        result = self.fetch("--update", env_extra={"DODGC_HLSDK": str(mine)})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((mine / "dlls" / "player.cpp").read_text(), "uncommitted work\n")
        self.assertEqual((mine / "dlls" / "untracked.cpp").read_text(), "keep me\n")


class WiringTest(unittest.TestCase):
    def test_the_build_script_gates_on_the_engine_being_at_the_tip(self):
        text = (SCRIPTS / "build-dodgc.sh").read_text()
        self.assertIn("tip-status.sh", text)
        self.assertIn("dodgc_tip_status", text)
        self.assertIn("DODGC_ALLOW_STALE_ENGINE", text)
        self.assertIn(".deps/xash3d-gc", text)   # only the default clone is checked, not one you supplied

    def test_build_info_reports_the_tip_status_of_each_source(self):
        text = (SCRIPTS / "build-info.sh").read_text()
        self.assertIn("$engine_tip", text)
        self.assertIn("branch-$hlsdk_tip", text)


if __name__ == "__main__":
    unittest.main()
