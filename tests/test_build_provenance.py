"""Build provenance: sources are explicit or pinned, never a symlink; the stamp says what a disc is."""
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "scripts"


def git(cwd, *args):
    return subprocess.run(["git", "-C", str(cwd), *args], check=True, capture_output=True, text=True,
                          env={**os.environ, "GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@t",
                               "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@t"}).stdout.strip()


def fake_repo(parent, name, files):
    repo = Path(parent) / name
    for rel in files:
        path = repo / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("x\n")
    repo.mkdir(parents=True, exist_ok=True)
    git(repo, "init", "-q")
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", "init")
    return repo


def run(script_args, env_extra, clear=("DODGC_ENGINE", "DODGC_HLSDK", "DODGC_TOOLS", "HLSDK_PORTABLE_DIR",
                                       "HLSDK_GAMECUBE_BRANCH", "DODGC_DOLPHIN_SRC", "DODGC_HLSDK_UNPINNED")):
    env = {k: v for k, v in os.environ.items() if k not in clear}
    env.update(env_extra)
    return subprocess.run(["bash", *script_args], capture_output=True, text=True, env=env, cwd=ROOT)


class BuildProvenanceTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        t = Path(self.tmp.name)
        self.scratch = {"DODGC_OUT": str(t / "out"), "DODGC_PLAYTESTS": str(t / "pt"),
                        "XASH3D_GC_ISO_CACHE": str(t / "cache")}
        self.engine = fake_repo(t, "engine", ["scripts/build-gamecube.sh"])
        self.hlsdk = fake_repo(t, "hlsdk", ["dlls/player.cpp"])
        self.content = t / "content-dod"
        self.content.mkdir()
        (self.content / "liblist.gam").write_text("x\n")

    def env_sh(self, extra, snippet='echo "$DODGC_ENGINE|$DODGC_HLSDK|$DODGC_TOOLS|$HLSDK_GAMECUBE_BRANCH|$HLSDK_PORTABLE_DIR"'):
        return run(["-c", f'source "{SCRIPTS}/env.sh"; {snippet}'], {**self.scratch, **extra})

    def test_lock_pins_a_full_sdk_commit_and_a_branch(self):
        text = (SCRIPTS / "sources.lock").read_text()
        self.assertRegex(text, r'DODGC_HLSDK_COMMIT="[0-9a-f]{40}"')
        self.assertRegex(text, r'DODGC_HLSDK_BRANCH="[^"]+"')
        self.assertNotIn("mobile_hacks\"", text.split("DODGC_HLSDK_BRANCH=")[1].splitlines()[0])

    def test_no_sources_fails_loudly(self):
        result = self.env_sh({})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no engine / SDK sources selected", result.stderr)
        self.assertIn("fetch-sources.sh", result.stderr)

    def test_a_tracked_symlink_is_not_a_source(self):
        # The old default followed ROOT/engine and ROOT/hlsdk-portable; they are not tracked any more.
        tracked = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-s", "engine", "hlsdk-portable"],
                                 capture_output=True, text=True).stdout
        self.assertEqual(tracked.strip(), "")

    def test_explicit_sources_tools_default_and_sdk_branch(self):
        result = self.env_sh({"DODGC_ENGINE": str(self.engine), "DODGC_HLSDK": str(self.hlsdk)})
        self.assertEqual(result.returncode, 0, result.stderr)
        engine, hlsdk, tools, branch, portable = result.stdout.strip().split("|")
        self.assertEqual(engine, str(self.engine))
        self.assertEqual(hlsdk, str(self.hlsdk))
        self.assertEqual(portable, str(self.hlsdk))
        self.assertEqual(tools, str(ROOT / "tools"))
        self.assertEqual(branch, "claude/dod-hlsdk-integration")

    def test_build_refuses_an_unpinned_sdk(self):
        env = {**self.scratch, "DODGC_ENGINE": str(self.engine), "DODGC_HLSDK": str(self.hlsdk),
               "DODGC_CONTENT_DOD": str(self.content)}
        result = run([str(SCRIPTS / "build-dodgc.sh"), "--dol-only"], env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("pinned", result.stderr)
        self.assertIn("DODGC_HLSDK_UNPINNED=1", result.stderr)

    def test_failed_sdk_build_stops_the_script(self):
        text = (SCRIPTS / "build-dodgc.sh").read_text()
        block = text.split("hlsdk-gamecube-build.sh ||", 1)[1].split("SDK_SOURCE=", 1)[0]
        self.assertIn("exit 1", block)
        self.assertIn("DODGC_ALLOW_STALE_HLSDK", block)

    def test_stamp_lists_repos_pin_and_flags_a_dolphin_mismatch(self):
        dolphin = fake_repo(self.tmp.name, "dolphin", ["x.txt"])
        result = run([str(SCRIPTS / "build-info.sh"), "--short"],
                     {**self.scratch, "DODGC_ENGINE": str(self.engine), "DODGC_HLSDK": str(self.hlsdk),
                      "DODGC_DOLPHIN_SRC": str(dolphin)})
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[0], "build-info 1")
        for prefix in ("built ", "dod-gc ", "xash3d-gc ", "hlsdk ", "mainui ", "tools ", "dolphin "):
            self.assertTrue(any(line.startswith(prefix) for line in lines), prefix)
        self.assertIn(git(self.engine, "rev-parse", "HEAD"), result.stdout)
        self.assertRegex(result.stdout, r"hlsdk [0-9a-f]{40} .*pin=MISMATCH")
        self.assertRegex(result.stdout, r"dolphin MISMATCH: emulator built from [0-9a-f]{40}")
        self.assertLess(len(lines), 16, "the on-disc stamp stays short")

    def test_lean_disc_writes_the_stamp_into_the_staged_tree(self):
        text = (SCRIPTS / "build-lean-iso.sh").read_text()
        self.assertRegex(text, r'build-info\.sh" --short > "\$LEAN/build-info\.txt"')


if __name__ == "__main__":
    unittest.main()
