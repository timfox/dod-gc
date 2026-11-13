#!/usr/bin/env python3
"""DoD loading plaque bake uses splash tiles and stays tip-safe."""

from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "scripts" / "bake_gc_loading_plaque.py"


def load_tool():
	spec = importlib.util.spec_from_file_location("bake_gc_loading_plaque", TOOL)
	mod = importlib.util.module_from_spec(spec)
	assert spec.loader is not None
	spec.loader.exec_module(mod)
	return mod


class BakeGcLoadingPlaqueTest(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls.mod = load_tool()

	def test_bakes_dod_tiles_from_content(self):
		src = ROOT / "content" / "dod"
		tile = src / "resource" / "background" / "800_1_a_loading.tga"
		if not tile.is_file():
			self.skipTest("DoD loading tiles not linked")
		with tempfile.TemporaryDirectory() as td:
			out = Path(td)
			rc = self.mod.bake(src, out)
			self.assertEqual(rc, 0)
			loading = out / "resource" / "gc_menu" / "loading.tga"
			intro = out / "resource" / "gc_menu" / "intro.tga"
			self.assertTrue(loading.is_file())
			self.assertTrue(intro.is_file())
			# Uncompressed 320×240 RGB TGA ≈ 18 + 320*240*3 bytes; keep under 512 KiB.
			self.assertLess(loading.stat().st_size, 512 * 1024)
			data = loading.read_bytes()
			self.assertGreaterEqual(len(data), 18)
			w = data[12] | (data[13] << 8)
			h = data[14] | (data[15] << 8)
			self.assertEqual((w, h), (320, 240))


if __name__ == "__main__":
	unittest.main()
