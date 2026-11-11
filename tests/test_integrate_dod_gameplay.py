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


if __name__ == "__main__":
	unittest.main()
