#!/usr/bin/env python3
"""Studio prop DL bake: Avalanche pack is small, valid GCsd, keeps full tris."""

from __future__ import annotations

import importlib.util
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "bake_studio_dl.py"


def load_tool():
	spec = importlib.util.spec_from_file_location("bake_studio_dl", TOOL)
	mod = importlib.util.module_from_spec(spec)
	assert spec.loader is not None
	spec.loader.exec_module(mod)
	return mod


class BakeStudioDlTest(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls.mod = load_tool()

	def test_avalanche_pack_shape(self):
		bsp = ROOT / "content" / "dod" / "maps" / "dod_avalanche.bsp"
		src = ROOT / "content" / "dod"
		if not bsp.is_file() or not (src / "models" / "mapmodels" / "sandbags.mdl").is_file():
			self.skipTest("Steam dod content not linked")
		with tempfile.TemporaryDirectory() as td:
			out = Path(td) / "sd_dod_avalanche.gsd"
			rc = self.mod.bake_map(bsp, src, out)
			self.assertEqual(rc, 0)
			data = out.read_bytes()
			magic, ver, n, fsz, mx = struct.unpack_from(">4sHHII", data, 0)
			self.assertEqual(magic, b"GCsd")
			self.assertEqual(ver, 1)
			self.assertEqual(fsz, len(data))
			self.assertGreaterEqual(n, 4)  # at least one body per prop
			self.assertLessEqual(n, 64)
			self.assertLess(len(data), 512 * 1024)  # tip-safe disc size
			self.assertGreater(mx, 0)
			self.assertLessEqual(mx, 64 * 1024)
			# Dir records are 32 bytes; first entry must point inside the file.
			h, body, skin, ofs, nbytes = struct.unpack_from(">IHHII", data, 32)
			self.assertNotEqual(h, 0)
			self.assertEqual(ofs % 32, 0)
			self.assertGreater(nbytes, 32)
			self.assertLessEqual(ofs + nbytes, len(data))

	def test_sandbags_rest_pose_has_tris(self):
		mdl_path = ROOT / "content" / "dod" / "models" / "mapmodels" / "sandbags.mdl"
		if not mdl_path.is_file():
			self.skipTest("sandbags.mdl missing")
		mdl = self.mod.StudioMdl(mdl_path)
		entries = self.mod.bake_mdl_entries("models/mapmodels/sandbags.mdl", mdl)
		self.assertEqual(len(entries), 7)  # bodygroup variants
		self.assertGreater(sum(e["tris"] for e in entries), 100)


if __name__ == "__main__":
	unittest.main()
