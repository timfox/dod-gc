import importlib.util
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "integrate-dod-gameplay.py"
SPEC = importlib.util.spec_from_file_location("integrate_dod_gameplay", SCRIPT)
INTEGRATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INTEGRATOR)


PREFIX = "if( GI->max_edicts > 0 && GI->max_edicts <= GC_SP_EDICT_CAP\n"
SUFFIX = "\n\t{\n\t\tmemset( gc_singleplayer_edicts"
GATE_VARIANTS = (
	"\t\t&& ( svs.maxclients <= 1\n"
	"\t\t\t|| ( svs.maxclients <= 4 && Sys_CheckParm( \"-gclistenserver\" )) ) )",
	"\t\t&& ( svs.maxclients <= 1\n"
	"\t\t\t|| ( svs.maxclients <= 4 && ( Sys_CheckParm( \"-gclistenserver\" )\n"
	"\t\t\t\t|| Sys_CheckParm( \"-gcworldrender\" ) )) ) )",
	"\t\t&& ( svs.maxclients <= 1\n"
	"\t\t\t|| ( svs.maxclients <= 4\n"
	"\t\t\t\t&& ( Sys_CheckParm( \"-gclistenserver\" ) || GC_MapLoadMemoryOpt() )) ) )",
)


class StaticEdictGateTest(unittest.TestCase):
	def test_legacy_and_intermediate_gates_get_menu_map_load_path(self):
		for gate in GATE_VARIANTS:
			with self.subTest(gate=gate), tempfile.TemporaryDirectory() as directory:
				path = Path(directory) / "sv_game.c"
				path.write_text(PREFIX + gate + SUFFIX)

				INTEGRATOR.integrate_static_edict_gate(path)
				result = path.read_text()

				self.assertIn('Sys_CheckParm( "-gcworldrender" )', result)
				self.assertIn("GC_MapLoadMemoryOpt()", result)
				self.assertNotIn("svs.maxclients <= 4", result)
				self.assertTrue(result.endswith(SUFFIX))

	def test_menu_mapload_gate_covers_default_sixteen_clients(self):
		with tempfile.TemporaryDirectory() as directory:
			path = Path(directory) / "sv_game.c"
			path.write_text(PREFIX + GATE_VARIANTS[0] + SUFFIX)

			INTEGRATOR.integrate_static_edict_gate(path)
			gate = path.read_text().split(SUFFIX, 1)[0]

			self.assertIn("svs.maxclients <= 1", gate)
			self.assertIn("|| GC_MapLoadMemoryOpt()", gate)
			self.assertNotIn("svs.maxclients <= 4", gate)

	def test_gate_integration_is_idempotent(self):
		with tempfile.TemporaryDirectory() as directory:
			path = Path(directory) / "sv_game.c"
			path.write_text(PREFIX + GATE_VARIANTS[0] + SUFFIX)

			INTEGRATOR.integrate_static_edict_gate(path)
			first = path.read_text()
			INTEGRATOR.integrate_static_edict_gate(path)

			self.assertEqual(path.read_text(), first)


class HeapTipGateTest(unittest.TestCase):
	DOD = ('GC_MemHeapFreeBytes() <\n'
		'\t\t\t\t( !Q_strnicmp( sv.name, "dod_", 4 ) ? 2u * 1024u : 8u * 1024u ))\n')

	def run_gate(self, body):
		with tempfile.TemporaryDirectory() as directory:
			path = Path(directory) / "sv_game.c"
			path.write_text(body)
			INTEGRATOR.integrate_heap_tip_gate(path)
			first = path.read_text()
			INTEGRATOR.integrate_heap_tip_gate(path)
			self.assertEqual(path.read_text(), first)
			return first

	def test_worldspawn_safe_gate(self):
		result = self.run_gate("\t\t\tif( entity_index > 1 && GC_MemHeapFreeBytes() < ( 8u * 1024u ))\n")
		self.assertEqual(result, "\t\t\tif( entity_index > 1 && " + self.DOD)

	def test_pre_kraftstoff_gate_falls_back(self):
		result = self.run_gate("\t\t\tif( GC_MemHeapFreeBytes() < ( 8u * 1024u ))\n")
		self.assertEqual(result, "\t\t\tif( " + self.DOD)

	def test_missing_gate_fails_loudly(self):
		with tempfile.TemporaryDirectory() as directory:
			path = Path(directory) / "sv_game.c"
			path.write_text("int unrelated;\n")
			with self.assertRaises(SystemExit):
				INTEGRATOR.integrate_heap_tip_gate(path)


class WeaponIncludeTest(unittest.TestCase):
	SOURCE = (
		'#include "extdll.h"\n#include "weapons.h"\n#include "player.h"\n'
		'void CGlock::Precache( void )\n{\n\tPRECACHE_MODEL( "models/p_9mmhandgun.mdl" );\n}\n'
		'BOOL CGlock::Deploy()\n{\n\treturn FALSE;\n}\n'
	)

	def integrate(self, source):
		with tempfile.TemporaryDirectory() as directory:
			path = Path(directory) / "glock.cpp"
			path.write_text(source)
			INTEGRATOR.integrate_weapon(path, "CGlock", "DOD_MODEL_PISTOL", "GLOCK_DRAW",
				"onehanded", "9mmhandgun", '\tPRECACHE_MODEL( "models/v_colt.mdl" );\n')
			return path.read_text()

	def test_include_added_without_gamerules_header(self):
		text = self.integrate(self.SOURCE)
		self.assertIn('#include "dod_game.h"', text)
		self.assertIn("DOD_MODEL_PISTOL", text)
		self.assertEqual(text.count('#include "dod_game.h"'), 1)

	def test_include_added_after_gamerules_header(self):
		text = self.integrate(self.SOURCE.replace('"weapons.h"\n', '"weapons.h"\n#include "gamerules.h"\n'))
		self.assertEqual(text.count('#include "dod_game.h"'), 1)

	def test_integration_is_idempotent(self):
		once = self.integrate(self.SOURCE)
		with tempfile.TemporaryDirectory() as directory:
			path = Path(directory) / "glock.cpp"
			path.write_text(once)
			INTEGRATOR.integrate_weapon(path, "CGlock", "DOD_MODEL_PISTOL", "GLOCK_DRAW",
				"onehanded", "9mmhandgun", '\tPRECACHE_MODEL( "models/v_colt.mdl" );\n')
			self.assertEqual(path.read_text(), once)


if __name__ == "__main__":
	unittest.main()
