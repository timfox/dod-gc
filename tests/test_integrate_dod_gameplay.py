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
	"""integrate_weapon must add dod_game.h even when gamerules.h is absent."""

	GLOCK = ('#include "extdll.h"\n#include "weapons.h"\n#include "player.h"\n'
		'void CGlock::Precache()\n{\n\tPRECACHE_MODEL( "models/p_9mmhandgun.mdl" );\n}\n'
		'BOOL CGlock::Deploy()\n{\n\treturn DefaultDeploy( "v", "p", GLOCK_DRAW, "onehanded" );\n}\n')

	def integrate(self, body):
		with tempfile.TemporaryDirectory() as directory:
			path = Path(directory) / "glock.cpp"
			path.write_text(body)
			INTEGRATOR.integrate_weapon(path, "CGlock", "DOD_MODEL_PISTOL", "GLOCK_DRAW", "onehanded",
				"9mmhandgun", '\tPRECACHE_MODEL( "models/v_colt.mdl" );\n')
			first = path.read_text()
			INTEGRATOR.integrate_weapon(path, "CGlock", "DOD_MODEL_PISTOL", "GLOCK_DRAW", "onehanded",
				"9mmhandgun", '\tPRECACHE_MODEL( "models/v_colt.mdl" );\n')
			self.assertEqual(path.read_text(), first)
			return first

	def test_include_added_without_gamerules(self):
		result = self.integrate(self.GLOCK)
		self.assertEqual(result.count('#include "dod_game.h"'), 1)
		self.assertLess(result.index('#include "dod_game.h"'), result.index("CGlock::Deploy"))

	def test_include_added_after_gamerules(self):
		result = self.integrate(self.GLOCK.replace('#include "weapons.h"\n', '#include "weapons.h"\n#include "gamerules.h"\n'))
		self.assertEqual(result.count('#include "dod_game.h"'), 1)
		self.assertIn('#include "gamerules.h"\n#include "dod_game.h"', result)

	def test_weapon_without_known_include_fails_loudly(self):
		with tempfile.TemporaryDirectory() as directory:
			path = Path(directory) / "glock.cpp"
			path.write_text("int unrelated;\n")
			with self.assertRaises(SystemExit):
				INTEGRATOR.integrate_weapon(path, "CGlock", "DOD_MODEL_PISTOL", "GLOCK_DRAW", "onehanded", "9mmhandgun", "")


class GrenadeIntegrationTest(unittest.TestCase):
	HAND = ('#include "extdll.h"\n#include "weapons.h"\n#include "nodes.h"\n#include "player.h"\n\n'
		'void CHandGrenade::Precache( void )\n{\n\tPRECACHE_MODEL( "models/w_grenade.mdl" );\n'
		'\tPRECACHE_MODEL( "models/v_grenade.mdl" );\n\tPRECACHE_MODEL( "models/p_grenade.mdl" );\n}\n'
		'BOOL CHandGrenade::Deploy()\n{\n\tm_flReleaseThrow = -1;\n'
		'\treturn DefaultDeploy( "models/v_grenade.mdl", "models/p_grenade.mdl", HANDGRENADE_DRAW, "crowbar" );\n}\n'
		'void CHandGrenade::PrimaryAttack()\n{\n'
		'\tif( !m_flStartThrow && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 0 )\n\t{\n'
		'\t\tm_flStartThrow = gpGlobals->time;\n\n\t\tSendWeaponAnim( HANDGRENADE_PINPULL );\n\t}\n}\n'
		'void CHandGrenade::WeaponIdle( void )\n{\n'
		'\t\tCGrenade::ShootTimed( m_pPlayer->pev, vecSrc, vecThrow, time );\n}\n')
	GGRENADE = ('#include "weapons.h"\n#include "decals.h"\n#include "game.h"\n\n'
		'void CGrenade::Explode( TraceResult *pTrace, int bitsDamageType )\n{\n\t// body\n}\n')

	def integrate(self):
		with tempfile.TemporaryDirectory() as directory:
			dlls = Path(directory)
			(dlls / "handgrenade.cpp").write_text(self.HAND)
			(dlls / "ggrenade.cpp").write_text(self.GGRENADE)
			INTEGRATOR.integrate_grenades(dlls)
			first = ((dlls / "handgrenade.cpp").read_text(), (dlls / "ggrenade.cpp").read_text())
			INTEGRATOR.integrate_grenades(dlls)
			again = ((dlls / "handgrenade.cpp").read_text(), (dlls / "ggrenade.cpp").read_text())
			self.assertEqual(first, again, "integration must be idempotent")
			return first

	def test_handgrenade_hooks(self):
		hand, _ = self.integrate()
		self.assertIn('#include "dod_grenade.h"', hand)
		self.assertIn("DodGrenade_Precache();", hand)
		self.assertIn("DOD_MODEL_GRENADE", hand)
		self.assertIn("&& DodGrenade_CanPrime() )", hand)
		self.assertIn("weapons/grenpinpull.wav", hand)
		self.assertIn("if( !DodGrenade_Throw( m_pPlayer, gpGlobals->time - m_flStartThrow ) )", hand)

	def test_stock_deploy_state_reset_kept(self):
		# The DoD model branch sits after m_flReleaseThrow = -1, never before it.
		hand, _ = self.integrate()
		self.assertLess(hand.index("m_flReleaseThrow = -1;"), hand.index("DOD_MODEL_GRENADE"))
		self.assertIn('DefaultDeploy( "models/v_grenade.mdl", "models/p_grenade.mdl", HANDGRENADE_DRAW, "crowbar" );', hand)

	def test_explode_hook_runs_first(self):
		_, ggrenade = self.integrate()
		self.assertIn('#include "dod_grenade.h"', ggrenade)
		self.assertLess(ggrenade.index("DodGrenade_Explode"), ggrenade.index("// body"))

	def test_missing_anchor_fails_loudly(self):
		with tempfile.TemporaryDirectory() as directory:
			dlls = Path(directory)
			(dlls / "handgrenade.cpp").write_text("int unrelated;\n")
			(dlls / "ggrenade.cpp").write_text(self.GGRENADE)
			with self.assertRaises(SystemExit):
				INTEGRATOR.integrate_grenades(dlls)


if __name__ == "__main__":
	unittest.main()
