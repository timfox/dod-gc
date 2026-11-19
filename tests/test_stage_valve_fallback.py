#!/usr/bin/env python3
"""The play disc carries the Half-Life sprites and sounds DoD falls back on."""

from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "scripts" / "stage_valve_fallback.py"


def load_tool():
	spec = importlib.util.spec_from_file_location("stage_valve_fallback", TOOL)
	mod = importlib.util.module_from_spec(spec)
	assert spec.loader is not None
	spec.loader.exec_module(mod)
	return mod


def put(path: Path, data: bytes) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_bytes(data)


class StageValveFallbackTest(unittest.TestCase):
	def setUp(self):
		self.tool = load_tool()
		self.tmp = tempfile.TemporaryDirectory()
		self.addCleanup(self.tmp.cleanup)
		root = Path(self.tmp.name)
		self.dod, self.valve = root / "dod", root / "valve"
		put(self.dod / "sprites" / "muzzleflash1.spr", b"dod")
		put(self.dod / "sound" / "Player" / "pl_step1.wav", b"dod-step")
		put(self.valve / "sprites" / "muzzleflash1.spr", b"valve")
		put(self.valve / "sprites" / "xspark1.spr", b"spark")
		put(self.valve / "sprites" / "blood.spr", b"blood")
		put(self.valve / "sprites" / "640hud1.spr", b"hl hud")
		put(self.valve / "sound" / "items" / "gunpickup2.wav", b"pickup")
		put(self.valve / "sound" / "player" / "pl_step1.wav", b"valve-step")
		put(self.valve / "sound" / "player" / "pl_wade1.wav", b"wade")
		put(self.valve / "sound" / "scientist" / "scream1.wav", b"no")
		put(self.valve / "sound" / "items" / "readme.txt", b"no")

	def test_missing_files_come_from_valve_and_dod_wins(self):
		stats = self.tool.stage(self.dod, self.valve)
		self.assertEqual((self.dod / "sprites" / "muzzleflash1.spr").read_bytes(), b"dod")
		# Precache spelling, not the retail file's.
		self.assertEqual((self.dod / "sprites" / "XSpark1.spr").read_bytes(), b"spark")
		self.assertEqual((self.dod / "sprites" / "blood.spr").read_bytes(), b"blood")
		self.assertFalse((self.dod / "sprites" / "640hud1.spr").exists())
		self.assertEqual((self.dod / "sound" / "items" / "gunpickup2.wav").read_bytes(), b"pickup")
		# DoD's own file wins, and its directory spelling is reused.
		self.assertEqual((self.dod / "sound" / "Player" / "pl_step1.wav").read_bytes(), b"dod-step")
		self.assertEqual((self.dod / "sound" / "Player" / "pl_wade1.wav").read_bytes(), b"wade")
		self.assertFalse((self.dod / "sound" / "player").exists())
		self.assertFalse((self.dod / "sound" / "scientist").exists())
		self.assertFalse((self.dod / "sound" / "items" / "readme.txt").exists())
		self.assertEqual(stats["sprites"], 2)
		self.assertEqual(stats["sounds"], 2)

	def test_second_run_adds_nothing(self):
		self.tool.stage(self.dod, self.valve)
		stats = self.tool.stage(self.dod, self.valve)
		self.assertEqual((stats["sprites"], stats["sounds"]), (0, 0))

	def test_play_disc_build_runs_it(self):
		text = (ROOT / "scripts" / "build-dodgc.sh").read_text()
		self.assertIn("stage_valve_fallback.py", text)


if __name__ == "__main__":
	unittest.main()
