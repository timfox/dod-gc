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


class WeaponHookIntegrationTest(unittest.TestCase):
	WEAPONS = ('#include "gamerules.h"\n\nvoid W_Precache( void )\n{\n'
		'\tUTIL_PrecacheOtherWeapon( "weapon_crowbar" );\n'
		'\t// hand grenade\n\tUTIL_PrecacheOtherWeapon("weapon_handgrenade");\n'
		'#if !OEM_BUILD\n\tUTIL_PrecacheOtherWeapon( "weapon_snark" );\n#endif\n}\n')

	def integrate(self, body):
		with tempfile.TemporaryDirectory() as directory:
			dlls = Path(directory)
			(dlls / "weapons.cpp").write_text(body)
			INTEGRATOR.integrate_weapons(dlls)
			first = (dlls / "weapons.cpp").read_text()
			INTEGRATOR.integrate_weapons(dlls)
			self.assertEqual((dlls / "weapons.cpp").read_text(), first, "must be idempotent")
			return first

	def test_precache_hook_registers_guns_after_the_grenade(self):
		text = self.integrate(self.WEAPONS)
		self.assertIn('#include "dod_weapon.h"', text)
		self.assertEqual(text.count("DodWeapons_PrecacheAll();"), 1)
		self.assertLess(text.index('"weapon_handgrenade"'), text.index("DodWeapons_PrecacheAll();"))

	def test_missing_anchor_fails_loudly(self):
		with tempfile.TemporaryDirectory() as directory:
			dlls = Path(directory)
			(dlls / "weapons.cpp").write_text('#include "gamerules.h"\nint unrelated;\n')
			with self.assertRaises(SystemExit):
				INTEGRATOR.integrate_weapons(dlls)

	def test_only_melee_still_uses_the_stand_in_hooks(self):
		source = SCRIPT.read_text()
		for stand_in in ("mp5.cpp", "shotgun.cpp", "crossbow.cpp", "rpg.cpp", "glock.cpp"):
			self.assertNotIn(f'("{stand_in}", "C', source, f"{stand_in} is a real DoD weapon now")
		self.assertIn('("crowbar.cpp", "CCrowbar"', source)

	def test_new_sources_are_copied_and_built(self):
		source = SCRIPT.read_text()
		for name in ("dod_weapons.c", "dod_weapons.h", "dod_weapon.cpp", "dod_weapon.h"):
			self.assertIn(f'"{name}"', source)
		self.assertIn("dod_weapon.cpp", source.split("CMakeLists.txt")[1])
		self.assertIn("ant_glob('dod_weapons.c')", source)


class HudIntegrationTest(unittest.TestCase):
	HUD_H = "class CHudMenu\n{\n};\n\nclass CHud\n{\npublic:\n\tCHudMenu\t\tm_Menu;\n};\n"
	HUD_CPP = ('void CHud::Init( void )\n{\n#ifdef __GAMECUBE__\n'
		'\tgEngfuncs.Con_Printf( "Xash3D GameCube: CHud::Init begin quality=%d\\n", quality );\n#endif\n'
		'\tm_Menu.Init();\n}\nvoid CHud::VidInit( void )\n{\n\tm_Menu.VidInit();\n}\n')
	REDRAW = ("\t\t\tif( m_Menu.m_fMenuDisplayed )\n\t\t\t\tm_Menu.Draw( flTime );\n")

	def integrate(self):
		with tempfile.TemporaryDirectory() as directory:
			client = Path(directory)
			(client / "hud.h").write_text(self.HUD_H)
			(client / "hud.cpp").write_text(self.HUD_CPP)
			(client / "hud_redraw.cpp").write_text(self.REDRAW)
			INTEGRATOR.integrate_dod_hud(client)
			first = {n: (client / n).read_text() for n in ("hud.h", "hud.cpp", "hud_redraw.cpp")}
			INTEGRATOR.integrate_dod_hud(client)
			again = {n: (client / n).read_text() for n in first}
			self.assertEqual(first, again, "must be idempotent")
			return first

	def test_hooks_in_all_three_files(self):
		files = self.integrate()
		self.assertIn('#include "hud_dod.h"\n\nclass CHud\n{', files["hud.h"])
		self.assertIn("CHudDod\t\tm_Dod;", files["hud.h"])
		self.assertIn("m_Dod.InitState();", files["hud.cpp"])
		self.assertIn("m_Dod.Init();", files["hud.cpp"])
		self.assertIn("m_Dod.VidInit();", files["hud.cpp"])
		self.assertIn("m_Dod.Draw( flTime );", files["hud_redraw.cpp"])

	def test_state_is_set_up_before_the_lean_branch(self):
		# the lean (quality 0) path never runs m_Menu.Init(), so InitState runs first
		files = self.integrate()
		self.assertLess(files["hud.cpp"].index("m_Dod.InitState();"), files["hud.cpp"].index("m_Menu.Init();"))

	def test_missing_anchor_fails_loudly(self):
		with tempfile.TemporaryDirectory() as directory:
			client = Path(directory)
			for name in ("hud.h", "hud.cpp", "hud_redraw.cpp"):
				(client / name).write_text("int unrelated;\n")
			with self.assertRaises(SystemExit):
				INTEGRATOR.integrate_dod_hud(client)


class ClientSourceSyncTest(unittest.TestCase):
	def test_client_sources_are_copied_and_stale_ones_removed(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			src, client = root / "src", root / "cl_dll"
			(src / "client").mkdir(parents=True)
			client.mkdir()
			for source, dest in INTEGRATOR.DOD_CLIENT_SOURCES:
				(src / source).write_text("new " + dest)
			(client / "hud_dod.h").write_text("old")
			(client / "hud_dod_old.cpp").write_text("from another branch")
			(client / "dod_extra.h").write_text("stale")
			(client / "hud.cpp").write_text("upstream")
			removed = INTEGRATOR.sync_dod_client_sources(src, client)
			self.assertEqual(removed, ["dod_extra.h", "hud_dod_old.cpp"])
			self.assertTrue((client / "hud.cpp").exists())
			for _, dest in INTEGRATOR.DOD_CLIENT_SOURCES:
				self.assertEqual((client / dest).read_text(), "new " + dest)

	def test_every_client_source_exists_in_the_repo(self):
		src = Path(__file__).resolve().parents[1] / "src"
		for source, _ in INTEGRATOR.DOD_CLIENT_SOURCES:
			self.assertTrue((src / source).is_file(), source)
		on_disk = {p.name for p in (src / "client").glob("*")}
		self.assertEqual(on_disk, {Path(s).name for s, _ in INTEGRATOR.DOD_CLIENT_SOURCES if s.startswith("client/")})


class SourceSyncTest(unittest.TestCase):
	def test_list_covers_every_dod_source_in_src(self):
		src = Path(__file__).resolve().parents[1] / "src"
		on_disk = {p.name for p in src.glob("dod_*") if p.suffix in (".c", ".cpp", ".h")}
		self.assertEqual(on_disk, set(INTEGRATOR.DOD_DLL_SOURCES),
			"a new src/dod_* file must be added to DOD_DLL_SOURCES (and built)")

	def test_stale_sources_are_removed_and_current_ones_copied(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			src, dlls = root / "src", root / "dlls"
			src.mkdir()
			dlls.mkdir()
			for name in INTEGRATOR.DOD_DLL_SOURCES:
				(src / name).write_text("new " + name)
				(dlls / name).write_text("old " + name)
			(dlls / "dod_bot.cpp").write_text("from another branch")
			(dlls / "dod_bots.h").write_text("from another branch")
			(dlls / "weapons.cpp").write_text("upstream")
			(dlls / "dod_notes.txt").write_text("not a source")
			removed = INTEGRATOR.sync_dod_sources(src, dlls)
			self.assertEqual(removed, ["dod_bot.cpp", "dod_bots.h"])
			self.assertFalse((dlls / "dod_bot.cpp").exists())
			self.assertTrue((dlls / "weapons.cpp").exists(), "upstream files are never touched")
			self.assertTrue((dlls / "dod_notes.txt").exists(), "only C/C++ sources are removed")
			for name in INTEGRATOR.DOD_DLL_SOURCES:
				self.assertEqual((dlls / name).read_text(), "new " + name)
			self.assertEqual(INTEGRATOR.sync_dod_sources(src, dlls), [], "a second run removes nothing")


if __name__ == "__main__":
	unittest.main()
