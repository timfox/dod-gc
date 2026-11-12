#!/usr/bin/env python3
"""Regression: never stage the whole mapmodels farm for one Cube map."""

from __future__ import annotations

import importlib.util
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / "scripts" / "stage_bsp_mapmodels.py"


def load_helper():
	spec = importlib.util.spec_from_file_location("stage_bsp_mapmodels", HELPER)
	mod = importlib.util.module_from_spec(spec)
	assert spec.loader is not None
	spec.loader.exec_module(mod)
	return mod


def write_fake_bsp(path: Path, models: list[str]) -> None:
	ents = ['{\n"classname" "worldspawn"\n}']
	for i, m in enumerate(models):
		ents.append(f'{{\n"classname" "cycler"\n"model" "{m}"\n"origin" "{i} 0 0"\n}}')
	ents.append("\0")
	blob = "".join(ents).encode("latin1")
	# lump 0 = entities at offset 8+header padding; keep simple: header then ents
	header = bytearray(124)  # enough for lump0
	struct.pack_into("<ii", header, 4, 124, len(blob))  # entities off/len
	path.write_bytes(bytes(header) + blob)


class StageBspMapmodelsTest(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls.mod = load_helper()

	def test_avalanche_refs_are_tiny(self):
		bsp = ROOT / "content" / "dod" / "maps" / "dod_avalanche.bsp"
		if not bsp.is_file():
			self.skipTest("Steam dod_avalanche.bsp not linked")
		refs = self.mod.bsp_model_refs(bsp)
		self.assertLessEqual(len(refs), 32)
		self.assertTrue(all("mapmodels/" in r or r.startswith("models/") for r in refs))
		# Must not look like a whole-tree dump.
		self.assertLess(len(refs), 40)

	def test_refuses_absurd_ref_count(self):
		with tempfile.TemporaryDirectory() as tmp:
			td = Path(tmp)
			bsp = td / "toy.bsp"
			src = td / "src"
			lean = td / "lean"
			(src / "models" / "mapmodels").mkdir(parents=True)
			models = [f"models/mapmodels/prop{i}.mdl" for i in range(40)]
			for rel in models:
				p = src / rel
				p.write_bytes(b"MDL0" + b"\0" * 60)
			write_fake_bsp(bsp, models)
			with self.assertRaises(SystemExit) as ctx:
				self.mod.stage(
					bsp, src, lean, max_models=32, max_bytes=4 * 1024 * 1024
				)
			self.assertIn("cap 32", str(ctx.exception))

	def test_stages_only_referenced(self):
		with tempfile.TemporaryDirectory() as tmp:
			td = Path(tmp)
			bsp = td / "toy.bsp"
			src = td / "src"
			lean = td / "lean"
			mm = src / "models" / "mapmodels"
			mm.mkdir(parents=True)
			(mm / "keep.mdl").write_bytes(b"KEEP" + b"\0" * 28)
			(mm / "stray.mdl").write_bytes(b"STRAY" + b"\0" * 27)
			write_fake_bsp(bsp, ["models/mapmodels/keep.mdl"])
			result = self.mod.stage(
				bsp, src, lean, max_models=32, max_bytes=4 * 1024 * 1024
			)
			self.assertEqual(result["copied"], ["models/mapmodels/keep.mdl"])
			self.assertTrue((lean / "models" / "mapmodels" / "keep.mdl").is_file())
			self.assertFalse((lean / "models" / "mapmodels" / "stray.mdl").exists())

	def test_ignores_model_strings_outside_entity_lump(self):
		"""Regex must not scan texdata/padding — only lump 0."""
		with tempfile.TemporaryDirectory() as tmp:
			td = Path(tmp)
			bsp = td / "poison.bsp"
			ents = (
				'{\n"classname" "worldspawn"\n}\n'
				'{\n"classname" "cycler"\n'
				'"model" "models/mapmodels/keep.mdl"\n'
				'"origin" "0 0 0"\n}\n\0'
			).encode("latin1")
			# Entity lump at offset 128; plant a decoy "model" string earlier
			# in the file (would be matched by a whole-BSP findall).
			header = bytearray(128)
			struct.pack_into("<ii", header, 4, 128, len(ents))
			decoy = b'"model" "models/mapmodels/poison_from_texdata.mdl"'
			header[40 : 40 + len(decoy)] = decoy
			bsp.write_bytes(bytes(header) + ents)
			refs = self.mod.bsp_model_refs(bsp)
			self.assertEqual(refs, ["models/mapmodels/keep.mdl"])
			self.assertNotIn("models/mapmodels/poison_from_texdata.mdl", refs)


if __name__ == "__main__":
	unittest.main()
