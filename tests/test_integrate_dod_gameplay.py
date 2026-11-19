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


class StubRemovalTest(unittest.TestCase):
	"""The DoD runtime defines dod_control_point, dod_control_point_master and dod_score_ent; the
	hlsdk stubs for them must go (replace_once cannot remove: an empty replacement is "already in")."""

	def test_remove_once_deletes_a_stub_and_is_idempotent(self):
		with tempfile.TemporaryDirectory() as directory:
			path = Path(directory) / "subs.cpp"
			stub = "LINK_ENTITY_TO_CLASS( dod_score_ent, CPointEntity )\n"
			path.write_text("a\n" + stub + "b\n")
			INTEGRATOR.remove_once(path, stub)
			self.assertEqual(path.read_text(), "a\nb\n")
			INTEGRATOR.remove_once(path, stub)
			self.assertEqual(path.read_text(), "a\nb\n")

	def test_integrator_removes_the_three_stubs(self):
		text = SCRIPT.read_text()
		for name in ("dod_control_point", "dod_control_point_master", "dod_score_ent"):
			self.assertIn(f'"{name}"', text)


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


class TriggerHurtIntegrationTest(unittest.TestCase):
	TRIGGERS = ('#include "gamerules.h"\n\nvoid CBaseTrigger::HurtTouch( CBaseEntity *pOther )\n{\n\tfloat fldmg;\n\n'
		'\tif( !pOther->pev->takedamage )\n\t\treturn;\n\n'
		'\tif( ( pev->spawnflags & SF_TRIGGER_HURT_NO_CLIENTS ) && pOther->IsPlayer() )\n\t\treturn;\n\n\tfldmg = pev->dmg;\n}\n')

	def test_team_filter_is_patched_in_once_after_the_stock_checks(self):
		with tempfile.TemporaryDirectory() as directory:
			dlls = Path(directory)
			(dlls / "triggers.cpp").write_text(self.TRIGGERS)
			INTEGRATOR.integrate_trigger_hurt(dlls)
			first = (dlls / "triggers.cpp").read_text()
			INTEGRATOR.integrate_trigger_hurt(dlls)
			self.assertEqual((dlls / "triggers.cpp").read_text(), first, "must be idempotent")
			self.assertEqual(first.count("DodGC_TriggerHurtApplies( pev->spawnflags, pOther )"), 1)
			self.assertLess(first.index("SF_TRIGGER_HURT_NO_CLIENTS"), first.index("DodGC_TriggerHurtApplies( pev"))
			self.assertLess(first.index("DodGC_TriggerHurtApplies( pev"), first.index("fldmg = pev->dmg"))

	def test_missing_anchor_fails_loudly(self):
		with tempfile.TemporaryDirectory() as directory:
			dlls = Path(directory)
			(dlls / "triggers.cpp").write_text('#include "gamerules.h"\nint unrelated;\n')
			with self.assertRaises(SystemExit):
				INTEGRATOR.integrate_trigger_hurt(dlls)


class VoiceMenuIntegrationTest(unittest.TestCase):
	PLAYER = ('void CBasePlayer::ImpulseCommands()\n{\n\tswitch( iImpulse )\n\t{\n'
		'\tcase 100:\n        // temporary flashlight for level designers\n\t\tif( FlashlightIsOn() )\n\t\t\tFlashlightTurnOff();\n\t\tbreak;\n\t}\n}\n')

	def test_impulse_100_asks_the_voice_menu_first_and_is_patched_once(self):
		with tempfile.TemporaryDirectory() as directory:
			dlls = Path(directory)
			(dlls / "player.cpp").write_text(self.PLAYER)
			INTEGRATOR.integrate_voice_menu(dlls)
			first = (dlls / "player.cpp").read_text()
			INTEGRATOR.integrate_voice_menu(dlls)
			self.assertEqual((dlls / "player.cpp").read_text(), first, "must be idempotent")
			self.assertEqual(first.count("DodGC_VoiceMenuImpulse( this )"), 1)
			self.assertLess(first.index("DodGC_VoiceMenuImpulse( this )"), first.index("FlashlightIsOn"))

	def test_missing_anchor_fails_loudly(self):
		with tempfile.TemporaryDirectory() as directory:
			dlls = Path(directory)
			(dlls / "player.cpp").write_text("int unrelated;\n")
			with self.assertRaises(SystemExit):
				INTEGRATOR.integrate_voice_menu(dlls)


class UserCmdIntegrationTest(unittest.TestCase):
	CLIENT = ('void CmdStart( const edict_t *player, const struct usercmd_s *cmd, unsigned int random_seed )\n{\n'
		'\tCBasePlayer *pl = NULL;\n\tpl->random_seed = random_seed;\n}\n')

	def test_cmdstart_forwards_the_usercmd_once(self):
		with tempfile.TemporaryDirectory() as directory:
			dlls = Path(directory)
			(dlls / "client.cpp").write_text(self.CLIENT)
			INTEGRATOR.integrate_usercmd(dlls)
			first = (dlls / "client.cpp").read_text()
			INTEGRATOR.integrate_usercmd(dlls)
			self.assertEqual((dlls / "client.cpp").read_text(), first, "must be idempotent")
			self.assertEqual(first.count("DodGC_UserCmd( pl, cmd )"), 1)
			self.assertLess(first.index("random_seed = random_seed"), first.index("DodGC_UserCmd( pl, cmd )"))

	def test_missing_anchor_fails_loudly(self):
		with tempfile.TemporaryDirectory() as directory:
			dlls = Path(directory)
			(dlls / "client.cpp").write_text("int unrelated;\n")
			with self.assertRaises(SystemExit):
				INTEGRATOR.integrate_usercmd(dlls)


class DeathAnimsIntegrationTest(unittest.TestCase):
	PLAYER = ('void CBasePlayer::SetAnimation( PLAYER_ANIM playerAnim )\n{\n\tswitch( m_IdealActivity )\n\t{\n\tdefault:\n'
		'\t\tm_Activity = m_IdealActivity;\n\n\t\tanimDesired = LookupActivity( m_Activity );\n\n\t\t// Already using the desired animation?\n'
		'\t\tif( pev->sequence == animDesired )\n\t\t\treturn;\n\t}\n}\n')

	def test_death_sequence_is_offered_to_the_game_once(self):
		with tempfile.TemporaryDirectory() as directory:
			dlls = Path(directory)
			(dlls / "player.cpp").write_text(self.PLAYER)
			INTEGRATOR.integrate_death_anims(dlls)
			first = (dlls / "player.cpp").read_text()
			INTEGRATOR.integrate_death_anims(dlls)
			self.assertEqual((dlls / "player.cpp").read_text(), first, "must be idempotent")
			self.assertEqual(first.count("DodGC_DeathSequence( this )"), 1)
			self.assertLess(first.index("LookupActivity( m_Activity )"), first.index("DodGC_DeathSequence( this )"))
			self.assertLess(first.index("DodGC_DeathSequence( this )"), first.index("Already using the desired animation"))

	def test_missing_anchor_fails_loudly(self):
		with tempfile.TemporaryDirectory() as directory:
			dlls = Path(directory)
			(dlls / "player.cpp").write_text("int unrelated;\n")
			with self.assertRaises(SystemExit):
				INTEGRATOR.integrate_death_anims(dlls)


class BodyQueueIntegrationTest(unittest.TestCase):
	WORLD = 'static void InitBodyQue( void )\n{\n#if XASH_GAMECUBE\n\t/* Lean. */\n\tif( !GC_FullActors() )\n\t{\n\t\tg_pBodyQueueHead = NULL;\n\t\treturn;\n\t}\n#endif\n\t// Reserve 3 more slots for dead bodies\n\tfor( int i = 0; i < 3; i++ )\n}\n'
	WORLD += 'void CopyToBodyQue( entvars_t *pev )\n{\n\tpevHead->colormap\t= pev->colormap;\n\tpevHead->movetype\t= MOVETYPE_TOSS;\n\tpevHead->velocity\t= pev->velocity;\n\tUTIL_SetOrigin( pevHead, pev->origin );\n}\n'

	def test_corpse_edicts_are_kept_once(self):
		with tempfile.TemporaryDirectory() as directory:
			dlls = Path(directory)
			(dlls / "world.cpp").write_text(self.WORLD)
			INTEGRATOR.integrate_body_queue(dlls)
			first = (dlls / "world.cpp").read_text()
			INTEGRATOR.integrate_body_queue(dlls)
			self.assertEqual((dlls / "world.cpp").read_text(), first, "must be idempotent")
			self.assertNotIn("g_pBodyQueueHead = NULL", first)
			self.assertNotIn("MOVETYPE_TOSS", first)
			self.assertIn("i < 1", first)
			self.assertEqual(first.count("pevHead->body\t\t= pev->body;"), 1)
			self.assertEqual(first.count("pevHead->skin\t\t= pev->skin;"), 1)
			self.assertIn("pev->origin + Vector( 0, 0, 1.0f )", first)

	def test_missing_anchor_fails_loudly(self):
		with tempfile.TemporaryDirectory() as directory:
			dlls = Path(directory)
			(dlls / "world.cpp").write_text("int unrelated;\n")
			with self.assertRaises(SystemExit):
				INTEGRATOR.integrate_body_queue(dlls)


class DoddetectIntegrationTest(unittest.TestCase):
	SUBS = 'LINK_ENTITY_TO_CLASS( info_player_observer, CPointEntity )\nLINK_ENTITY_TO_CLASS( info_doddetect, CPointEntity )\n'

	def test_the_sdk_link_is_dropped_once(self):
		with tempfile.TemporaryDirectory() as directory:
			dlls = Path(directory)
			(dlls / "subs.cpp").write_text(self.SUBS)
			INTEGRATOR.integrate_doddetect(dlls)
			first = (dlls / "subs.cpp").read_text()
			INTEGRATOR.integrate_doddetect(dlls)
			self.assertEqual((dlls / "subs.cpp").read_text(), first, "must be idempotent")
			self.assertNotIn("LINK_ENTITY_TO_CLASS( info_doddetect", first)
			self.assertIn("info_player_observer", first)

	def test_missing_anchor_fails_loudly(self):
		with tempfile.TemporaryDirectory() as directory:
			dlls = Path(directory)
			(dlls / "subs.cpp").write_text("int unrelated;\n")
			with self.assertRaises(SystemExit):
				INTEGRATOR.integrate_doddetect(dlls)


class DefaultItemsIntegrationTest(unittest.TestCase):
	RULES = ('void CHalfLifeMultiplay::PlayerSpawn( CBasePlayer *pPlayer )\n{\n\tBOOL addDefault = TRUE;\n\n'
		'\tif( addDefault )\n\t{\n\t\tpPlayer->GiveNamedItem( "weapon_crowbar" );\n'
		'\t\tpPlayer->GiveNamedItem( "weapon_9mmhandgun" );\n\t}\n}\n')

	def test_stock_grant_is_skipped_under_dod_rules_and_patched_once(self):
		with tempfile.TemporaryDirectory() as directory:
			dlls = Path(directory)
			(dlls / "multiplay_gamerules.cpp").write_text(self.RULES)
			INTEGRATOR.integrate_default_items(dlls)
			first = (dlls / "multiplay_gamerules.cpp").read_text()
			INTEGRATOR.integrate_default_items(dlls)
			self.assertEqual((dlls / "multiplay_gamerules.cpp").read_text(), first, "must be idempotent")
			self.assertEqual(first.count("DodGC_SkipDefaultItems"), 2)	# the declaration and the one condition
			self.assertIn("if( addDefault && !DodGC_SkipDefaultItems() )", first)
			self.assertLess(first.index("extern BOOL DodGC_SkipDefaultItems"), first.index("CHalfLifeMultiplay::PlayerSpawn"))

	def test_missing_anchor_fails_loudly(self):
		with tempfile.TemporaryDirectory() as directory:
			dlls = Path(directory)
			(dlls / "multiplay_gamerules.cpp").write_text("int unrelated;\n")
			with self.assertRaises(SystemExit):
				INTEGRATOR.integrate_default_items(dlls)


class WeaponHookIntegrationTest(unittest.TestCase):
	WEAPONS = ('#include "gamerules.h"\n\nvoid W_Precache( void )\n{\n'
		'\tUTIL_PrecacheOtherWeapon( "weapon_crowbar" );\n'
		'\t// hand grenade\n\tUTIL_PrecacheOtherWeapon("weapon_handgrenade");\n'
		'#if !OEM_BUILD\n\tUTIL_PrecacheOtherWeapon( "weapon_snark" );\n#endif\n}\n'
		'\nvoid CBasePlayerWeapon::SendWeaponAnim( int iAnim, int skiplocal, int body )\n{\n\tif( UseDecrement() )\n\t\tskiplocal = 0;\n}\n')

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

	def test_send_weapon_anim_remaps_first(self):
		text = self.integrate(self.WEAPONS)
		self.assertEqual(text.count("DodGC_RemapWeaponAnim( STRING( pev->classname ), iAnim )"), 1)
		self.assertLess(text.index("DodGC_RemapWeaponAnim( STRING"), text.index("if( UseDecrement() )"))

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
		for name in ("dod_weapons.c", "dod_weapons.h", "dod_weapon.cpp", "dod_weapon.h",
				"dod_bots.c", "dod_bots.h", "dod_bot.cpp", "dod_bot.h"):
			self.assertIn(f'"{name}"', source)
		cmake = source.split('dlls / "CMakeLists.txt",')[1].split("wscript")[0]
		self.assertIn("dod_weapon.cpp", cmake)
		self.assertIn("ant_glob('dod_weapons.c')", source)
		self.assertIn("ant_glob('dod_bots.c')", source)
		self.assertIn("dod_bots.c", cmake)
		self.assertIn("dod_bot.cpp", cmake)


class HudIntegrationTest(unittest.TestCase):
	HUD_H = ("class CHudAmmo\n{\npublic:\n"
		"\tint GetLeanClip( void ) const { return m_pWeapon ? m_pWeapon->iClip : -1; }\n};\n"
		"class CHudMenu\n{\n};\n\nclass CHud\n{\npublic:\n\tCHudMenu\t\tm_Menu;\n};\n")
	HUD_CPP = ('void CHud::Init( void )\n{\n#ifdef __GAMECUBE__\n'
		'\tgEngfuncs.Con_Printf( "Xash3D GameCube: CHud::Init begin quality=%d\\n", quality );\n#endif\n'
		'\tm_Menu.Init();\n}\nvoid CHud::VidInit( void )\n{\n\tm_Menu.VidInit();\n}\n')
	REDRAW = ('\t\t\tif( m_iGcLeanHudLog == 0 )\n'
		'\t\t\t\tgEngfuncs.Con_Printf( "Xash3D GameCube: lean HUD scrInfo %dx%d\\n",\n'
		'\t\t\t\t\tm_scrinfo.iWidth, m_scrinfo.iHeight );\n'
		"\t\t\tif( m_Menu.m_fMenuDisplayed )\n\t\t\t\tm_Menu.Draw( flTime );\n")
	ENTITY_CPP = ('\t\tgHUD.m_Spectator.AddOverviewEntity( type, ent, modelname );\n\n'
		'\t\tif( (\tg_iUser1 == OBS_IN_EYE || gHUD.m_Spectator.m_pip->value == INSET_IN_EYE ) &&')

	def integrate(self):
		with tempfile.TemporaryDirectory() as directory:
			client = Path(directory)
			(client / "hud.h").write_text(self.HUD_H)
			(client / "hud.cpp").write_text(self.HUD_CPP)
			(client / "hud_redraw.cpp").write_text(self.REDRAW)
			(client / "entity.cpp").write_text(self.ENTITY_CPP)
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
		self.assertIn("const WEAPON *GetCurrentWeapon( void ) const { return m_pWeapon; }", files["hud.h"])
		self.assertIn("m_Dod.InitState();", files["hud.cpp"])
		self.assertIn("m_Dod.Init();", files["hud.cpp"])
		self.assertIn("m_Dod.VidInit();", files["hud.cpp"])
		self.assertIn("m_Dod.Draw( flTime );", files["hud_redraw.cpp"])
		# the lean tier draws the retail HUD and skips the HL sprites
		self.assertIn("if( m_Dod.DrawRetail( flTime ) )", files["hud_redraw.cpp"])
		self.assertLess(files["hud_redraw.cpp"].index("lean HUD scrInfo"),
			files["hud_redraw.cpp"].index("m_Dod.DrawRetail( flTime )"))

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


class SpectatorCameraIntegrationTest(unittest.TestCase):
	VIEW = ('#include "view.h"\nvoid f()\n{\n\t\tif( gHUD.m_Spectator.m_pip->value )\n'
		'\t\t\tpparams->nextView = 1;\t// force a second renderer view\n}\n')

	def run_integration(self, text):
		with tempfile.TemporaryDirectory() as directory:
			client = Path(directory)
			(client / "view.cpp").write_text(text)
			INTEGRATOR.integrate_spectator_camera(client)
			first = (client / "view.cpp").read_text()
			INTEGRATOR.integrate_spectator_camera(client)
			self.assertEqual(first, (client / "view.cpp").read_text(), "must be idempotent")
			return first

	def test_smoothing_hooked_before_the_inset_view(self):
		text = self.run_integration(self.VIEW)
		self.assertIn('#include "dod_cam.h"', text)
		self.assertLess(text.index("DodCam_Smooth("), text.index("pparams->nextView = 1;"))
		self.assertLess(text.index("DodCam_Smooth("), text.index("DodCam_Level("))

	def test_missing_anchor_fails_loudly(self):
		with self.assertRaises(SystemExit):
			self.run_integration("int unrelated;\n")


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
			(dlls / "dod_old_feature.cpp").write_text("from another branch")
			(dlls / "dod_old_feature.h").write_text("from another branch")
			(dlls / "weapons.cpp").write_text("upstream")
			(dlls / "dod_notes.txt").write_text("not a source")
			removed = INTEGRATOR.sync_dod_sources(src, dlls)
			self.assertEqual(removed, ["dod_old_feature.cpp", "dod_old_feature.h"])
			self.assertFalse((dlls / "dod_old_feature.cpp").exists())
			(dlls / "dod_old_bot.cpp").write_text("from another branch")
			(dlls / "dod_old_bots.h").write_text("from another branch")
			(dlls / "weapons.cpp").write_text("upstream")
			(dlls / "dod_notes.txt").write_text("not a source")
			removed = INTEGRATOR.sync_dod_sources(src, dlls)
			self.assertEqual(removed, ["dod_old_bot.cpp", "dod_old_bots.h"])
			self.assertFalse((dlls / "dod_old_bot.cpp").exists())
			self.assertTrue((dlls / "weapons.cpp").exists(), "upstream files are never touched")
			self.assertTrue((dlls / "dod_notes.txt").exists(), "only C/C++ sources are removed")
			for name in INTEGRATOR.DOD_DLL_SOURCES:
				self.assertEqual((dlls / name).read_text(), "new " + name)
			self.assertEqual(INTEGRATOR.sync_dod_sources(src, dlls), [], "a second run removes nothing")


if __name__ == "__main__":
	unittest.main()


class RetailSoundsTest(unittest.TestCase):
	def test_stock_hl_sounds_become_retail_and_a_second_run_changes_nothing(self):
		import tempfile
		with tempfile.TemporaryDirectory() as tmp:
			sdk = Path(tmp)
			(sdk / "dlls").mkdir()
			(sdk / "pm_shared").mkdir()
			pain = ('\tfloat flRndSound;//sound randomizer\n\n\tflRndSound = RANDOM_FLOAT( 0.0f, 1.0f ); \n\n'
				'\tif( flRndSound <= 0.33f )\n\t\tEMIT_SOUND( ENT( pev ), CHAN_VOICE, "player/pl_pain5.wav", 1, ATTN_NORM );\n'
				'\telse if( flRndSound <= 0.66f )\t\n\t\tEMIT_SOUND( ENT( pev ), CHAN_VOICE, "player/pl_pain6.wav", 1, ATTN_NORM );\n'
				'\telse\n\t\tEMIT_SOUND( ENT( pev ), CHAN_VOICE, "player/pl_pain7.wav", 1, ATTN_NORM );\n')
			death = ('\t// temporarily using pain sounds for death sounds\n\tswitch( RANDOM_LONG( 1, 5 ) )\n\t{\n'
				'\tcase 1: \n\t\tEMIT_SOUND( ENT( pev ), CHAN_VOICE, "player/pl_pain5.wav", 1, ATTN_NORM );\n\t\tbreak;\n'
				'\tcase 2: \n\t\tEMIT_SOUND( ENT( pev ), CHAN_VOICE, "player/pl_pain6.wav", 1, ATTN_NORM );\n\t\tbreak;\n'
				'\tcase 3: \n\t\tEMIT_SOUND( ENT( pev ), CHAN_VOICE, "player/pl_pain7.wav", 1, ATTN_NORM );\n\t\tbreak;\n\t}\n')
			(sdk / "dlls/player.cpp").write_text("void Pain()\n{\n" + pain + "}\nvoid Death()\n{\n" + death + "}\n")
			(sdk / "dlls/items.cpp").write_text('EMIT_SOUND( e, CHAN_ITEM, "items/gunpickup2.wav", 1, ATTN_NORM );\n')
			(sdk / "dlls/weapons.cpp").write_text('"items/weapondrop1.wav" "items/9mmclip1.wav" "items/gunpickup2.wav"\n')
			(sdk / "dlls/client.cpp").write_text('PRECACHE_SOUND( "player/pl_pain2.wav" ); PRECACHE_SOUND( "player/pl_fallpain3.wav" );\n')
			(sdk / "pm_shared/pm_shared.c").write_text('"player/pl_fallpain3.wav"\n#define STEP_LADDER\t\t8\t\t// climbing ladder\n'
				'\t\tcase CHAR_TEX_SLOSH:\n\t\t\treturn STEP_SLOSH;\n\tcase STEP_SLOSH:\n\t\tswitch( irand )\n')
			INTEGRATOR.integrate_retail_sounds(sdk)
			first = {p.name: p.read_text() for p in list((sdk / "dlls").iterdir()) + list((sdk / "pm_shared").iterdir())}
			INTEGRATOR.integrate_retail_sounds(sdk)
			again = {p.name: p.read_text() for p in list((sdk / "dlls").iterdir()) + list((sdk / "pm_shared").iterdir())}
			self.assertEqual(first, again)
			self.assertIn("player/damage%d.wav", first["player.cpp"])
			self.assertIn("player/dmbullo%d.wav", first["player.cpp"])
			self.assertNotIn("pl_pain", first["player.cpp"])
			self.assertEqual(first["weapons.cpp"], '"items/weapondrop.wav" "items/ammopickup.wav" "items/weaponpickup.wav"\n')
			self.assertIn("items/weaponpickup.wav", first["items.cpp"])
			self.assertTrue(first["pm_shared.c"].startswith('"player/pl_fallpain.wav"\n'))

	def test_footsteps_gain_the_retail_wood_gravel_and_snow_floors(self):
		import tempfile
		with tempfile.TemporaryDirectory() as tmp:
			pm = Path(tmp) / "pm_shared.c"
			pm.write_text("#define STEP_LADDER\t\t8\t\t// climbing ladder\n"
				"\t\tcase CHAR_TEX_SLOSH:\n\t\t\treturn STEP_SLOSH;\n"
				"\tcase STEP_SLOSH:\n\t\tswitch( irand )\n")
			INTEGRATOR.integrate_retail_footsteps(pm)
			once = pm.read_text()
			INTEGRATOR.integrate_retail_footsteps(pm)
			self.assertEqual(pm.read_text(), once)
			for want in ("#define STEP_WOOD", "pl_gravel1.wav", "pl_snow4.wav", "pl_wood2.wav", "return STEP_GRAVEL;"):
				self.assertIn(want, once)
