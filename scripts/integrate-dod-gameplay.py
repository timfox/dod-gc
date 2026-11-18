#!/usr/bin/env python3
"""Apply the tracked DoD runtime overlay to the linked HLSDK checkout."""
from __future__ import annotations

import argparse
import shutil
from pathlib import Path


def remove_once(path: Path, old: str) -> None:
	"""Delete `old` from the file if it is still there (replace_once cannot: an empty
	replacement is always "already in" the text)."""
	text = path.read_text()
	if old in text:
		path.write_text(text.replace(old, "", 1))


def replace_once(path: Path, old: str, new: str) -> None:
	text = path.read_text()
	if new in text:
		return
	if old not in text:
		raise SystemExit(f"integration anchor not found in {path}: {old[:90]!r}")
	path.write_text(text.replace(old, new, 1))


def integrate_static_edict_gate(sv_game: Path) -> None:
	"""Use BSS edicts during interactive menu map loads on GameCube."""
	text = sv_game.read_text()
	start = text.find('if( GI->max_edicts > 0 && GI->max_edicts <= GC_SP_EDICT_CAP')
	end = text.find('\n\t{', start)
	if start < 0 or end < 0:
		raise SystemExit(f"static edict gate not found in {sv_game}")

	gate = text[start:end]
	required = (
		'svs.maxclients <= 1',
		'Sys_CheckParm( "-gclistenserver" )',
		'Sys_CheckParm( "-gcworldrender" )',
		'GC_MapLoadMemoryOpt()',
	)
	if all(token in gate for token in required) and 'svs.maxclients <= 4' not in gate:
		return

	canonical = (
		'if( GI->max_edicts > 0 && GI->max_edicts <= GC_SP_EDICT_CAP\n'
		'\t\t&& ( svs.maxclients <= 1\n'
		'\t\t\t|| Sys_CheckParm( "-gclistenserver" )\n'
		'\t\t\t|| Sys_CheckParm( "-gcworldrender" )\n'
		'\t\t\t|| GC_MapLoadMemoryOpt() ) )'
	)
	sv_game.write_text(text[:start] + canonical + text[end:])


def integrate_heap_tip_gate(sv_game: Path) -> None:
	"""Lower the entity heap-tip threshold to 2 KiB on dod_ maps.

	Prefer the CapFaces worldspawn-safe gate (entity_index > 1); fall back to
	the older tip check if a tree still has the pre-kraftstoff form.
	"""
	dod_threshold = (
		'GC_MemHeapFreeBytes() <\n'
		'\t\t\t\t( !Q_strnicmp( sv.name, "dod_", 4 ) ? 2u * 1024u : 8u * 1024u ))\n'
	)
	for guard in ('if( entity_index > 1 && ', 'if( '):
		if f'\t\t\t{guard}{dod_threshold}' in sv_game.read_text():
			return
	for guard in ('if( entity_index > 1 && ', 'if( '):
		old = f'\t\t\t{guard}GC_MemHeapFreeBytes() < ( 8u * 1024u ))\n'
		if old in sv_game.read_text():
			replace_once(sv_game, old, f'\t\t\t{guard}{dod_threshold}')
			return
	raise SystemExit(f"heap tip gate not found in {sv_game}")


def integrate_weapon(path: Path, class_name: str, kind: str, deploy_anim: str,
					 event: str, legacy_model: str, models: str) -> None:
	text = path.read_text()
	if '#include "dod_game.h"' not in text:
		text = text.replace('#include "gamerules.h"\n', '#include "gamerules.h"\n#include "dod_game.h"\n', 1)
	if '#include "dod_game.h"' not in text:
		# glock.cpp has no gamerules.h include; weapons.h is in every weapon file.
		text = text.replace('#include "weapons.h"\n', '#include "weapons.h"\n#include "dod_game.h"\n', 1)
	if '#include "dod_game.h"' not in text:
		raise SystemExit(f"no include anchor for dod_game.h in {path}")
	if models not in text:
		anchor = f'\tPRECACHE_MODEL( "models/p_{legacy_model}.mdl" );\n'
		if anchor not in text:
			raise SystemExit(f"weapon precache anchor not found in {path}: {anchor.strip()}")
		text = text.replace(anchor, anchor + models, 1)
	old = f'BOOL {class_name}::Deploy()\n{{\n'
	new = (old + '\tconst char *viewModel, *playerModel;\n'
		   f'\tif( Dod_GameIsActive() && Dod_GetWeaponModels( m_pPlayer->entindex(), {kind}, &viewModel, &playerModel ) )\n'
		   f'\t\treturn DefaultDeploy( viewModel, playerModel, {deploy_anim}, "{event}" );\n')
	if new not in text:
		if old not in text:
			raise SystemExit(f"weapon deploy anchor not found in {path}: {old.strip()}")
		text = text.replace(old, new, 1)
	path.write_text(text)


def integrate_gc_hud(client: Path) -> None:
	"""Give the GoldSrc team/class ShowMenu a usable GameCube pad cursor."""
	keys = client.parent / "engine" / "keydefs.h"
	replace_once(keys, '#define K_JOY4\t\t206\n',
		'#define K_JOY4\t\t206\n'
		'\n/* Xash3D GameCube pad keys (must match engine/keydefs.h). */\n'
		'#define K_A_BUTTON\t207\n#define K_B_BUTTON\t208\n'
		'#define K_DPAD_UP\t222\n#define K_DPAD_DOWN\t223\n'
		'#define K_DPAD_LEFT\t224\n#define K_DPAD_RIGHT\t225\n')
	input_cpp = client / "input.cpp"
	replace_once(input_cpp, '#include "in_defs.h"\n',
		'#include "in_defs.h"\n#include "keydefs.h"\n')
	replace_once(input_cpp,
		'int DLLEXPORT HUD_Key_Event( int down, int keynum, const char *pszCurrentBinding )\n{\n',
		'int DLLEXPORT HUD_Key_Event( int down, int keynum, const char *pszCurrentBinding )\n{\n'
		'#if XASH_GAMECUBE\n'
		'\tif( gHUD.m_Menu.m_fMenuDisplayed )\n'
		'\t{\n'
		'\t\tCHudMenu *menu = &gHUD.m_Menu;\n'
		'\t\tint slot = menu->m_iGCSelectedSlot;\n'
		'\t\tint step = keynum == K_DPAD_UP ? -1 : 1;\n'
		'\t\tif( down && ( keynum == K_DPAD_UP || keynum == K_DPAD_DOWN ))\n'
		'\t\t{\n'
		'\t\t\tint tries;\n'
		'\t\t\tfor( tries = 0; tries < 10; ++tries )\n'
		'\t\t\t{\n'
		'\t\t\t\tslot = (( slot - 1 + step + 10 ) % 10 ) + 1;\n'
		'\t\t\t\tif( menu->m_bitsValidSlots & ( 1 << ( slot - 1 )))\n'
		'\t\t\t\t{ menu->m_iGCSelectedSlot = slot; break; }\n'
		'\t\t\t}\n'
		'\t\t\treturn 0;\n'
		'\t\t}\n'
		'\t\tif( down && ( keynum == K_A_BUTTON || keynum == K_ENTER || keynum == K_KP_ENTER ))\n'
		'\t\t{ menu->SelectMenuItem( slot ); return 0; }\n'
		'\t\tif( down && keynum >= \'1\' && keynum <= \'9\' )\n'
		'\t\t{ menu->SelectMenuItem( keynum - \'0\' ); return 0; }\n'
		'\t}\n'
		'#endif\n')

	hud_h = client / "hud.h"
	replace_once(hud_h, '\tint m_fWaitingForMore;\n};\n',
		'\tint m_fWaitingForMore;\n\tint m_iGCSelectedSlot;\n};\n')
	menu_cpp = client / "menu.cpp"
	replace_once(menu_cpp,
		'\tm_fMenuDisplayed = 0;\n\tm_bitsValidSlots = 0;\n\tm_iFlags &= ~HUD_ACTIVE;\n',
		'\tm_fMenuDisplayed = 0;\n\tm_bitsValidSlots = 0;\n\tm_iGCSelectedSlot = 1;\n\tm_iFlags &= ~HUD_ACTIVE;\n')
	replace_once(menu_cpp,
		'\t\t\tstrlcpy( g_szPrelocalisedMenuString, READ_STRING(), MAX_MENU_STRING );\n',
		'\t\t\tstrlcpy( g_szPrelocalisedMenuString, READ_STRING(), MAX_MENU_STRING );\n'
		'\t\t\tfor( m_iGCSelectedSlot = 1; m_iGCSelectedSlot <= 10; ++m_iGCSelectedSlot )\n'
		'\t\t\t\tif( m_bitsValidSlots & ( 1 << ( m_iGCSelectedSlot - 1 ))) break;\n'
		'\t\t\tif( m_iGCSelectedSlot > 10 ) m_iGCSelectedSlot = 1;\n')
	replace_once(menu_cpp,
		'\t\t\tstrlcpy( menubuf, ptr, Q_min(( sptr - ptr + 1 ), (int)sizeof( menubuf )));\n'
		'\t\t\tif( menu_ralign )\n',
		'\t\t\tstrlcpy( menubuf, ptr, Q_min(( sptr - ptr + 1 ), (int)sizeof( menubuf )));\n'
		'\t\t\tint draw_r = menu_r, draw_g = menu_g, draw_b = menu_b;\n'
		'\t\t\tif( menubuf[0] >= \'1\' && menubuf[0] <= \'9\' && menubuf[1] == \'.\'\n'
		'\t\t\t\t&& menubuf[0] - \'0\' == m_iGCSelectedSlot )\n'
		'\t\t\t{ draw_r = 96; draw_g = 255; draw_b = 128; }\n'
		'\t\t\tif( menu_ralign )\n')
	menu_text = menu_cpp.read_text()
	menu_text = menu_text.replace(
		'\t\t\t\tmenu_x = gHUD.DrawHudStringReverse( menu_x, y, 0, menubuf, menu_r, menu_g, menu_b );\n'
		'\t\t\telse menu_x = gHUD.DrawHudString( menu_x, y, 320, menubuf, menu_r, menu_g, menu_b );',
		'\t\t\t\tmenu_x = gHUD.DrawHudStringReverse( menu_x, y, 0, menubuf, draw_r, draw_g, draw_b );\n'
		'\t\t\telse menu_x = gHUD.DrawHudString( menu_x, y, 320, menubuf, draw_r, draw_g, draw_b );', 1)
	menu_cpp.write_text(menu_text)


def integrate_weapons(dlls: Path) -> None:
	"""Register the DoD guns' ItemInfo and ammo when the game precaches weapons."""
	weapons = dlls / "weapons.cpp"
	replace_once(weapons, '#include "gamerules.h"\n',
		'#include "gamerules.h"\n#include "dod_weapon.h"\n')
	replace_once(weapons,
		'\tUTIL_PrecacheOtherWeapon("weapon_handgrenade");\n',
		'\tUTIL_PrecacheOtherWeapon("weapon_handgrenade");\n'
		'\t/* DoD guns: ItemInfo and ammo only; models load when granted. */\n'
		'\tDodWeapons_PrecacheAll();\n')
	# Every weapon animation goes through here: translate the generic / stock numbers to the sequence the retail
	# view model actually has (see Dod_RemapAnim), so a draw does not play a reload.
	replace_once(weapons,
		'void CBasePlayerWeapon::SendWeaponAnim( int iAnim, int skiplocal, int body )\n{\n',
		'void CBasePlayerWeapon::SendWeaponAnim( int iAnim, int skiplocal, int body )\n{\n'
		'\t{\n'
		'\t\textern int DodGC_RemapWeaponAnim( const char *classname, int anim );\n'
		'\t\tiAnim = DodGC_RemapWeaponAnim( STRING( pev->classname ), iAnim );\n'
		'\t}\n')


def integrate_trigger_hurt(dlls: Path) -> None:
	"""trigger_hurt honours DoD's team flags (64 hurts Axis only, 128 Allies only): without it the spawn-protection
	zones on maps like dod_flash hurt everyone, killing players that spawn inside their own base."""
	triggers = dlls / "triggers.cpp"
	replace_once(triggers,
		'\tif( ( pev->spawnflags & SF_TRIGGER_HURT_NO_CLIENTS ) && pOther->IsPlayer() )\n\t\treturn;\n',
		'\tif( ( pev->spawnflags & SF_TRIGGER_HURT_NO_CLIENTS ) && pOther->IsPlayer() )\n\t\treturn;\n'
		'\t{\n'
		'\t\textern BOOL DodGC_TriggerHurtApplies( int spawnflags, CBaseEntity *pOther );\n'
		'\t\tif( !DodGC_TriggerHurtApplies( pev->spawnflags, pOther ) )\n'
		'\t\t\treturn;\t/* a DoD team zone that is not for this player\'s side */\n'
		'\t}\n')


def integrate_default_items(dlls: Path) -> None:
	"""The Half-Life deathmatch spawn gives every player a crowbar and a 9mm handgun (and precaches/binds their models);
	in a DoD match the class kit is the only thing handed out, so the stock grant is skipped while DoD rules are active."""
	rules = dlls / "multiplay_gamerules.cpp"
	replace_once(rules,
		'void CHalfLifeMultiplay::PlayerSpawn( CBasePlayer *pPlayer )\n{\n',
		'extern BOOL DodGC_SkipDefaultItems( void );\n\nvoid CHalfLifeMultiplay::PlayerSpawn( CBasePlayer *pPlayer )\n{\n')
	replace_once(rules,
		'\tif( addDefault )\n\t{\n',
		'\tif( addDefault && !DodGC_SkipDefaultItems() )\n\t{\n')


def integrate_voice_menu(dlls: Path) -> None:
	"""impulse 100 (the Half-Life flashlight, which DoD does not have; the pad's Y button) opens the voice menu in a DoD
	match: the game decides in DodGC_VoiceMenuImpulse and the flashlight stays for everything else."""
	replace_once(dlls / "player.cpp",
		'\tcase 100:\n        // temporary flashlight for level designers\n',
		'\tcase 100:\n\t\t{\n\t\t\textern BOOL DodGC_VoiceMenuImpulse( CBasePlayer *pPlayer );\n\t\t\tif( DodGC_VoiceMenuImpulse( this ) )\n\t\t\t\tbreak;\n\t\t}\n'
		'        // temporary flashlight for level designers\n')


def integrate_usercmd(dlls: Path) -> None:
	"""The pad's analog stick moves the player through usercmd.forwardmove without setting IN_FORWARD, so the game cannot see a
	double tap of forward in pev->button. CmdStart hands every usercmd to the game's DodGC_UserCmd."""
	replace_once(dlls / "client.cpp",
		'\tpl->random_seed = random_seed;\n}\n',
		'\tpl->random_seed = random_seed;\n\t{\n\t\textern void DodGC_UserCmd( CBasePlayer *pPlayer, const struct usercmd_s *cmd );\n\t\tDodGC_UserCmd( pl, cmd );\n\t}\n}\n')


def integrate_doddetect(dlls: Path) -> None:
	"""info_doddetect is linked to a plain point entity in the SDK; the game's CDoDDetect (dod_runtime.cpp) takes the class name to read the
	respawn factors, so the SDK's link line goes."""
	replace_once(dlls / "subs.cpp",
		'LINK_ENTITY_TO_CLASS( info_doddetect, CPointEntity )\n',
		'/* info_doddetect is linked by the DoD game code (dod_runtime.cpp) */\n')


def integrate_death_anims(dlls: Path) -> None:
	"""CBasePlayer::SetAnimation(PLAYER_DIE) picks the death sequence of the stub player.mdl; the DoD body models have their own (die_simple 19 ...
	die_prone1 29), picked by the game when the player is killed (DodGC_DeathSequence). -1 keeps the stock choice."""
	replace_once(dlls / "player.cpp",
		'\t\tanimDesired = LookupActivity( m_Activity );\n\n\t\t// Already using the desired animation?\n',
		'\t\tanimDesired = LookupActivity( m_Activity );\n'
		'\t\tif( playerAnim == PLAYER_DIE )\n'
		'\t\t{\n\t\t\textern int DodGC_DeathSequence( CBasePlayer *pPlayer );\n'
		'\t\t\tconst int dod_seq = DodGC_DeathSequence( this );\n\t\t\tif( dod_seq >= 0 )\n\t\t\t\tanimDesired = dod_seq;\n\t\t}\n\n'
		'\t\t// Already using the desired animation?\n')


def integrate_body_queue(dlls: Path) -> None:
	"""The lean route leaves g_pBodyQueueHead NULL, so CopyToBodyQue() returned at once and a dead player vanished
	with no corpse. DoD keeps the four body-queue edicts (the stock InitBodyQue) so corpses stay until four newer deaths."""
	replace_once(dlls / "world.cpp",
		'\tif( !GC_FullActors() )\n\t{\n\t\tg_pBodyQueueHead = NULL;\n\t\treturn;\n\t}\n',
		'\t/* DoD keeps the four corpse edicts (dod-gc integrate_body_queue). */\n')
	# Charlie leaves ~23 free edicts with three bots: the stock four corpse edicts cost the fourth bot. Two corpses are kept.
	replace_once(dlls / "world.cpp",
		'\t// Reserve 3 more slots for dead bodies\n\tfor( int i = 0; i < 3; i++ )\n',
		'\t// DoD keeps 2 corpses (edict budget): 1 more slot for dead bodies\n\tfor( int i = 0; i < 1; i++ )\n')
	# The lean server has no world collision for MOVETYPE_TOSS, so a corpse fell through the floor (and drew black inside it).
	# It stays where the player died, a unit above that origin.
	replace_once(dlls / "world.cpp",
		'\tpevHead->movetype\t= MOVETYPE_TOSS;\n\tpevHead->velocity\t= pev->velocity;\n',
		'\tpevHead->movetype\t= MOVETYPE_NONE;\n\tpevHead->velocity\t= g_vecZero;\n')
	# Stock CopyToBodyQue copies neither body nor skin; the DoD body index is class x helmet x head x gear, so body 0 drew black.
	replace_once(dlls / "world.cpp",
		'\tpevHead->colormap\t= pev->colormap;\n',
		'\tpevHead->colormap\t= pev->colormap;\n\tpevHead->body\t\t= pev->body;\n\tpevHead->skin\t\t= pev->skin;\n')
	replace_once(dlls / "world.cpp",
		'\tUTIL_SetOrigin( pevHead, pev->origin );\n',
		'\tUTIL_SetOrigin( pevHead, pev->origin + Vector( 0, 0, 1.0f ) );\n')


def integrate_grenades(dlls: Path) -> None:
	"""DoD hand grenades: models, fuse, damage and a budgeted explosion."""
	hand = dlls / "handgrenade.cpp"
	replace_once(hand, '#include "player.h"\n',
		'#include "player.h"\n#include "dod_game.h"\n#include "dod_grenade.h"\n')
	replace_once(hand,
		'\tPRECACHE_MODEL( "models/p_grenade.mdl" );\n}',
		'\tPRECACHE_MODEL( "models/p_grenade.mdl" );\n\tDodGrenade_Precache();\n}')
	replace_once(hand,
		'\treturn DefaultDeploy( "models/v_grenade.mdl", "models/p_grenade.mdl", HANDGRENADE_DRAW, "crowbar" );\n',
		'\t{\n'
		'\t\tconst char *viewModel, *playerModel;\n'
		'\t\tif( Dod_GameIsActive() && Dod_GetWeaponModels( m_pPlayer->entindex(), DOD_MODEL_GRENADE, &viewModel, &playerModel ) )\n'
		'\t\t\treturn DefaultDeploy( viewModel, playerModel, HANDGRENADE_DRAW, "crowbar" );\n'
		'\t}\n'
		'\treturn DefaultDeploy( "models/v_grenade.mdl", "models/p_grenade.mdl", HANDGRENADE_DRAW, "crowbar" );\n')
	replace_once(hand,
		'\tif( !m_flStartThrow && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 0 )\n',
		'\tif( !m_flStartThrow && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 0 && DodGrenade_CanPrime() )\n')
	replace_once(hand,
		'\t\tSendWeaponAnim( HANDGRENADE_PINPULL );\n',
		'\t\tSendWeaponAnim( HANDGRENADE_PINPULL );\n'
		'\t\tif( Dod_GameIsActive() )\n'
		'\t\t\tEMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "weapons/grenpinpull.wav", 1.0f, ATTN_NORM );\n')
	replace_once(hand,
		'\t\tCGrenade::ShootTimed( m_pPlayer->pev, vecSrc, vecThrow, time );\n',
		'\t\t/* DoD: team model, 5s fuse that cooking shortens, capped live count. */\n'
		'\t\tif( !DodGrenade_Throw( m_pPlayer, gpGlobals->time - m_flStartThrow ) )\n'
		'\t\t\tCGrenade::ShootTimed( m_pPlayer->pev, vecSrc, vecThrow, time );\n')
	ggrenade = dlls / "ggrenade.cpp"
	replace_once(ggrenade, '#include "game.h"\n', '#include "game.h"\n#include "dod_grenade.h"\n')
	replace_once(ggrenade,
		'void CGrenade::Explode( TraceResult *pTrace, int bitsDamageType )\n{\n',
		'void CGrenade::Explode( TraceResult *pTrace, int bitsDamageType )\n{\n'
		'\tif( DodGrenade_Explode( this, pTrace, bitsDamageType ) )\n'
		'\t\treturn;\n')


# Every game DLL source this repo owns. src/ holds nothing else named dod_*.
DOD_DLL_SOURCES = (
	"dod_game.c", "dod_game.h", "dod_runtime.cpp", "dod_runtime.h",
	"dod_grenade.cpp", "dod_grenade.h", "dod_weapons.c", "dod_weapons.h",
	"dod_weapon.cpp", "dod_weapon.h", "dod_viewanims.h", "dod_viewsounds.h", "dod_hud.h", "dod_hud_retail.h",
	"dod_bots.c", "dod_bots.h", "dod_bot.cpp", "dod_bot.h",
)
# Client HUD sources, copied into cl_dll/ (dod_hud.h is shared with the DLL).
DOD_CLIENT_SOURCES = (
	("dod_hud.h", "dod_hud.h"),
	("dod_hud_retail.h", "dod_hud_retail.h"),
	("client/hud_dod.h", "hud_dod.h"),
	("client/hud_dod.cpp", "hud_dod.cpp"),
	("client/dod_cam.h", "dod_cam.h"),
)


def sync_dod_sources(src: Path, dlls: Path) -> list[str]:
	"""Copy our sources into the HLSDK tree and delete the ones we no longer own.

	The tree is shared between branches and the overlay only ever added files, so
	a source from another branch (a bots file, say) lingered and broke the build
	of a branch that does not have it. Returns the names that were removed.
	"""
	removed = []
	for stale in sorted(dlls.glob("dod_*")):
		if stale.is_file() and stale.suffix in (".c", ".cpp", ".h") and stale.name not in DOD_DLL_SOURCES:
			stale.unlink()
			removed.append(stale.name)
	for name in DOD_DLL_SOURCES:
		shutil.copy2(src / name, dlls / name)
	return removed


def sync_dod_client_sources(src: Path, client: Path) -> list[str]:
	"""Same for cl_dll/: copy the HUD sources, delete dod_*/hud_dod* files we no longer own."""
	owned = {dest for _, dest in DOD_CLIENT_SOURCES}
	removed = []
	for pattern in ("dod_*", "hud_dod*"):
		for stale in sorted(client.glob(pattern)):
			if stale.is_file() and stale.suffix in (".c", ".cpp", ".h") and stale.name not in owned:
				stale.unlink()
				removed.append(stale.name)
	for source, dest in DOD_CLIENT_SOURCES:
		shutil.copy2(src / source, client / dest)
	return removed


def integrate_dod_hud(client: Path) -> None:
	"""Hook the DoD HUD element into the client HUD (normal and lean quality-0 paths)."""
	replace_once(client / "hud.h", "class CHud\n{",
		'#include "hud_dod.h"\n\nclass CHud\n{')
	replace_once(client / "hud.h", "\tCHudMenu\t\tm_Menu;\n",
		"\tCHudMenu\t\tm_Menu;\n\tCHudDod\t\tm_Dod;\n")
	hud = client / "hud.cpp"
	replace_once(hud,
		'\tgEngfuncs.Con_Printf( "Xash3D GameCube: CHud::Init begin quality=%d\\n", quality );\n',
		'\tgEngfuncs.Con_Printf( "Xash3D GameCube: CHud::Init begin quality=%d\\n", quality );\n'
		'\t/* cl_dodhud and the DodHud message: needed by the lean path too. */\n'
		'\tm_Dod.InitState();\n'
		'\t/* the lean (quality 0) path returns before hud_textmode is created; DrawHudString reads it */\n'
		'\tif( !hud_textmode )\n\t\thud_textmode = CVAR_CREATE( "hud_textmode", "0", FCVAR_ARCHIVE );\n'
		'\t/* spectator camera code reads spec_pip even when CHudSpectator::Init never ran (lean HUD) */\n'
		'\tif( !m_Spectator.m_pip )\n\t\tm_Spectator.m_pip = gEngfuncs.pfnRegisterVariable( "spec_pip", "0", 0 );\n')
	# HUD_AddEntity feeds the overview map in spectator modes; the lean HUD has none.
	replace_once(client / "entity.cpp",
		'\t\tgHUD.m_Spectator.AddOverviewEntity( type, ent, modelname );\n\n\t\tif( (	g_iUser1 == OBS_IN_EYE || gHUD.m_Spectator.m_pip->value == INSET_IN_EYE ) &&',
		'\t\tif( (	g_iUser1 == OBS_IN_EYE || ( gHUD.m_Spectator.m_pip && gHUD.m_Spectator.m_pip->value == INSET_IN_EYE ) ) &&')
	# The retail clip-ammo graphic needs the held weapon (name, clip, ammo type).
	replace_once(client / "hud.h",
		"\tint GetLeanClip( void ) const { return m_pWeapon ? m_pWeapon->iClip : -1; }\n",
		"\tint GetLeanClip( void ) const { return m_pWeapon ? m_pWeapon->iClip : -1; }\n"
		"\tconst WEAPON *GetCurrentWeapon( void ) const { return m_pWeapon; }\n")
	replace_once(hud, "\tm_Menu.Init();\n", "\tm_Menu.Init();\n\tm_Dod.Init();\n")
	replace_once(hud, "\tm_Menu.VidInit();\n", "\tm_Menu.VidInit();\n\tm_Dod.VidInit();\n")
	replace_once(client / "hud_redraw.cpp",
		"\t\t\tif( m_Menu.m_fMenuDisplayed )\n\t\t\t\tm_Menu.Draw( flTime );\n",
		"\t\t\tif( m_Menu.m_fMenuDisplayed )\n\t\t\t\tm_Menu.Draw( flTime );\n"
		"\t\t\t/* DoD match HUD (cl_dodhud): score, clock, flags, round. */\n"
		"\t\t\tm_Dod.Draw( flTime );\n")
	# The lean (quality 0) tier is the HUD DoD matches run with: draw the retail
	# DoD HUD there instead of the Half-Life sprites, menus still on top.
	replace_once(client / "hud_redraw.cpp",
		'\t\t\t\tgEngfuncs.Con_Printf( "Xash3D GameCube: lean HUD scrInfo %dx%d\\n",\n'
		'\t\t\t\t\tm_scrinfo.iWidth, m_scrinfo.iHeight );\n',
		'\t\t\t\tgEngfuncs.Con_Printf( "Xash3D GameCube: lean HUD scrInfo %dx%d\\n",\n'
		'\t\t\t\t\tm_scrinfo.iWidth, m_scrinfo.iHeight );\n'
		'\n'
		'\t\t\t/* Day of Defeat: the retail HUD replaces the HL lean sprites. */\n'
		'\t\t\tif( m_Dod.DrawRetail( flTime ) )\n'
		'\t\t\t{\n'
		'\t\t\t\tif( m_Menu.m_fMenuDisplayed )\n'
		'\t\t\t\t\tm_Menu.Draw( flTime );\n'
		'\t\t\t\treturn 1;\n'
		'\t\t\t}\n')


def integrate_spectator_camera(client: Path) -> None:
	"""Smooth the spectator chase cameras (dod_cam.h) in the HLSDK client view."""
	view = client / "view.cpp"
	replace_once(view, '#include "view.h"\n', '#include "view.h"\n#include "dod_cam.h"\n')
	replace_once(view,
		'\t\tif( gHUD.m_Spectator.m_pip->value )\n\t\t\tpparams->nextView = 1;\t// force a second renderer view\n',
		'\t\t/* DoD: the chase cameras follow an entity origin that only moves on a network\n'
		'\t\t * update; low-pass them so the view glides (snaps on target or mode changes). */\n'
		'\t\t{\n'
		'\t\t\tstatic DodCamState s_dodCam;\n'
		'\t\t\tDodCam_Smooth( &s_dodCam, g_iUser1, g_iUser2, v_origin, v_angles, pparams->frametime, v_origin, v_angles );\n'
		'\t\t\tDodCam_Level( g_iUser1, v_angles );\n'
		'\t\t}\n'
		'\n'
		'\t\tif( gHUD.m_Spectator.m_pip->value )\n\t\t\tpparams->nextView = 1;\t// force a second renderer view\n')


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("--hlsdk-dir", required=True, type=Path)
	parser.add_argument("--engine-dir", type=Path)
	args = parser.parse_args()
	root = Path(__file__).resolve().parent.parent
	dlls = args.hlsdk_dir / "dlls"
	client = args.hlsdk_dir / "cl_dll"
	if not (dlls / "CMakeLists.txt").is_file() or not (client / "hud.cpp").is_file():
		raise SystemExit(f"not an hlsdk-portable source tree: {args.hlsdk_dir}")

	removed = sync_dod_sources(root / "src", dlls)
	removed += sync_dod_client_sources(root / "src", client)
	if removed:
		print("removed stale DoD sources from the HLSDK tree: " + ", ".join(removed))

	if args.engine_dir:
		main_menu = args.engine_dir / "3rdparty/mainui/menus/Main.cpp"
		replace_once(
			main_menu,
			'\tbool single = gpGlobals->maxClients < 2;\n',
			'\tbool single = gpGlobals->maxClients < 2;\n'
			'#if XASH_GAMECUBE\n'
			'\t/* Pic buttons may be constructed before menu atlas sizing is ready.\n'
			'\t * Refresh their virtual dimensions before CMenuBaseItem::VidInit\n'
			'\t * computes screen bounds, otherwise the menu art draws at 0x0. */\n'
			'\tresumeGame.size = uiStatic.buttons_draw_size;\n'
			'\tdisconnect.size = uiStatic.buttons_draw_size;\n'
			'\tnewGame.size = uiStatic.buttons_draw_size;\n'
			'\thazardCourse.size = uiStatic.buttons_draw_size;\n'
			'\tconfiguration.size = uiStatic.buttons_draw_size;\n'
			'\tsaveRestore.size = uiStatic.buttons_draw_size;\n'
			'\tquit.size = uiStatic.buttons_draw_size;\n'
			'#endif\n',
		)
		replace_once(
			main_menu,
			'\t\tif( mpOnly )\n\t\t{\n',
			'\t\tif( mpOnly )\n\t\t{\n'
			'#if XASH_GAMECUBE\n'
			'\t\t\t/* DoD strip textures have a black fringe on GX; use readable\n'
			'\t\t\t * menu text for the primary action like Find servers/Options. */\n'
			'\t\t\tif( UI_IsDayOfDefeat() ) newGame.SetPicture( (const char *)NULL );\n'
			'#endif\n',
		)
		create_game = args.engine_dir / "3rdparty/mainui/menus/CreateGame.cpp"
		replace_once(
			create_game,
			'\tif( !EngFuncs::CreateMapsList( true ) || (afile = (char *)EngFuncs::COM_LoadFile( "maps.lst", NULL )) == NULL )\n',
			'\t/* The disc staging step writes the exact DoD map list. Re-scanning BSPs\n'
			'\t * on every Create Game open needlessly parses multi-megabyte lumps on GX. */\n'
			'\tconst bool dodMaps = !stricmp( gMenu.m_gameinfo.gamefolder, "dod" );\n'
			'\tif(( !dodMaps && !EngFuncs::CreateMapsList( true ) )\n'
			'\t\t|| (afile = (char *)EngFuncs::COM_LoadFile( "maps.lst", NULL )) == NULL )\n',
		)
		pad = args.engine_dir / "engine/platform/gamecube/in_gamecube.c"
		replace_once(
			pad,
			'\tif( cls.key_dest == key_console )\n',
			'\t/* Synthetic controller input is useful to automated Dolphin probes, but\n'
			'\t * must never navigate the retail menu on a normal boot. */\n'
			'\tif( !Sys_CheckParm( "-gcmenuauto" ))\n'
			'\t\treturn 0;\n\n'
			'\tif( cls.key_dest == key_console )\n',
		)
		sv_game = args.engine_dir / "engine/server/sv_game.c"
		integrate_heap_tip_gate(sv_game)
		replace_once(
			sv_game,
			'\tif( bytes > GC_ENTITY_OVERFLOW_BYTES ) bytes = GC_ENTITY_OVERFLOW_BYTES;\n',
			'\tif( bytes > GC_ENTITY_OVERFLOW_BYTES ) bytes = GC_ENTITY_OVERFLOW_BYTES;\n'
			'\t/* DoD maps have no HL monster private-data bursts. Keep a smaller overflow\n'
			'\t * reserve so their worldspawn and team spawn entities retain MEM1 tip. */\n'
			'\tif( !Q_strnicmp( sv.name, "dod_", 4 ) && bytes > 8u * 1024u )\n'
			'\t\tbytes = 8u * 1024u;\n',
		)
		integrate_static_edict_gate(sv_game)
		model = args.engine_dir / "engine/common/model.c"
		# Already deferred (empty promote) — nothing to patch.
		model_text = model.read_text()
		if 'static const char *promote[] = { NULL };' not in model_text \
			or 'Mod_GCCapFacesClaimTeamViewModel' not in model_text:
			replace_once(
				model,
				'\t/* DoD CapFaces listen: lean Allies pistol + weapon stub (184 KiB BSS). */\n'
				'\tstatic const char *promote[] = {\n'
				'\t\t"models/v_colt.mdl",\n'
				'\t\t"models/w_crowbar.mdl",\n'
				'\t\tNULL\n'
				'\t};\n',
				'\t/* DoD CapFaces listen: keep the 184 KiB studio BSS empty until team pick\n'
				'\t * claims one lean rifle (Mod_GCCapFacesClaimTeamViewModel). Preloading\n'
				'\t * colt/crowbar left no room for garand/mp40. */\n'
				'\tstatic const char *promote[] = { NULL };\n',
			)
		video = args.engine_dir / "engine/client/cl_video.c"
		replace_once(
			video,
			'\tGC_MemHeapMark( "menu_handoff" );\n'
			'\tCon_Reportf( "Xash3D GameCube: mem after cinematic purge (menu handoff)\\n" );\n'
			'\tMem_PrintStats();\n',
			'\tGC_MemHeapMark( "menu_handoff" );\n'
			'\t/* GC_MemHeapMark records usage without walking every pool. Mem_PrintStats\n'
			'\t * runs Mem_Check and can stop menu handoff on unrelated tiny-pool sentinels. */\n',
		)
		image = args.engine_dir / "ref/gx/r_image.c"
		image_text = image.read_text()
		# VM skins prefer 128² RGB565 in-tree (ava-play99-43+). Skip the
		# legacy 64→128 Garand inject when that path is present.
		if (
			"Image_GCMaxLoadDimension" not in image_text
			and "vm_skin" not in image_text
			and "128² fits WorldDL MEM1" not in image_text
			and "Q_min( resampleWidth, 96 )" not in image_text
			and "Q_min( resampleWidth, 128 )" not in image_text
		):
			if "selected Garand/MP40 does not collapse" in image_text:
				replace_once(
					image,
					'\t\tresampleWidth = Q_min( resampleWidth, 64 );\n'
					'\t\tresampleHeight = Q_min( resampleHeight, 64 );\n',
					'\t\tresampleWidth = Q_min( resampleWidth, 128 );\n'
					'\t\tresampleHeight = Q_min( resampleHeight, 128 );\n',
				)


	# The guns are real DoD weapons now (dod_weapon.cpp); only the melee weapon is
	# still the HL1 crowbar wearing the knife/spade models.
	for filename, class_name, kind, animation, event, legacy_model, models in (
		("crowbar.cpp", "CCrowbar", "DOD_MODEL_MELEE", "CROWBAR_DRAW", "crowbar", "crowbar",
		 '\tPRECACHE_MODEL( "models/v_amerk.mdl" ); PRECACHE_MODEL( "models/p_amerk.mdl" );\n'
		 '\tPRECACHE_MODEL( "models/v_spade.mdl" );\n'
		 '\tPRECACHE_MODEL( "models/p_spade.mdl" );\n'),
	):
		integrate_weapon(dlls / filename, class_name, kind, animation, event, legacy_model, models)
	integrate_grenades(dlls)
	integrate_trigger_hurt(dlls)
	integrate_default_items(dlls)
	integrate_voice_menu(dlls)
	integrate_usercmd(dlls)
	integrate_doddetect(dlls)
	integrate_death_anims(dlls)
	integrate_body_queue(dlls)
	integrate_weapons(dlls)
	integrate_dod_hud(client)
	integrate_spectator_camera(client)

	replace_once(
		dlls / "CMakeLists.txt",
		"\tmultiplay_gamerules.cpp\n",
		"\tmultiplay_gamerules.cpp\n\tdod_game.c\n\tdod_weapons.c\n\tdod_runtime.cpp\n\tdod_grenade.cpp\n\tdod_weapon.cpp\n\tdod_bots.c\n\tdod_bot.cpp\n",
	)
	replace_once(
		dlls / "wscript",
		"\tsource = bld.path.ant_glob('**/*.cpp', excl=excluded_files)\n",
		"\tsource = bld.path.ant_glob('**/*.cpp', excl=excluded_files)\n"
		"\tsource += bld.path.ant_glob('dod_game.c')\n"
		"\tsource += bld.path.ant_glob('dod_weapons.c')\n"
		"\tsource += bld.path.ant_glob('dod_bots.c')\n",
	)
	replace_once(
		dlls / "gamerules.cpp",
		'#include\t"teamplay_gamerules.h"\n',
		'#include\t"teamplay_gamerules.h"\n#include\t"dod_runtime.h"\n',
	)
	replace_once(
		dlls / "gamerules.cpp",
		'\tSERVER_COMMAND( "exec game.cfg\\n" );\n\tSERVER_EXECUTE();\n',
		'\tSERVER_COMMAND( "exec game.cfg\\n" );\n\tSERVER_EXECUTE();\n'
		'\tchar gamefolder[256] = "";\n'
		'\tg_engfuncs.pfnGetGameDir( gamefolder );\n'
		"\tchar *gameleaf = strrchr( gamefolder, '/' );\n"
		"\tif( !gameleaf ) gameleaf = strrchr( gamefolder, '\\\\' );\n"
		'\tif( gameleaf ) ++gameleaf;\n'
		'\telse gameleaf = gamefolder;\n',
	)
	replace_once(
		dlls / "gamerules.cpp",
		'\t\t\tg_teamplay = 1;\n\t\t\treturn new CHalfLifeTeamplay;\n',
		'\t\t\tg_teamplay = 1;\n'
		'\t\t\tif( !stricmp( gameleaf, "dod" ) )\n'
		'\t\t\t\treturn new CHalfLifeDoD;\n'
		'\t\t\treturn new CHalfLifeTeamplay;\n',
	)
	# The worldrender listen route skips game.cfg, so mp_teamplay stays 0 and the
	# game would build vanilla deathmatch rules (no CHalfLifeDoD, no bots).
	replace_once(
		dlls / "gamerules.cpp",
		'\tif( !gpGlobals->deathmatch )\n\t{\n\t\t// generic half-life\n',
		'\tif( !stricmp( gameleaf, "dod" ) && gpGlobals->deathmatch )\n\t\tCVAR_SET_FLOAT( "mp_teamplay", 1 );\n'
		'\tif( !gpGlobals->deathmatch )\n\t{\n\t\t// generic half-life\n',
	)
	# The DoD runtime defines these classes; drop the hlsdk stubs so they do not collide.
	for stub in ("dod_control_point", "dod_control_point_master", "dod_score_ent"):
		remove_once(dlls / "subs.cpp", f"LINK_ENTITY_TO_CLASS( {stub}, CPointEntity )\n")
	replace_once(
		client / "hud.cpp",
		'int __MsgFunc_VGUIMenu( const char *pszName, int iSize, void *pbuf )\n',
		'int __MsgFunc_ShowMenu( const char *pszName, int iSize, void *pbuf );\n\n'
		'int __MsgFunc_VGUIMenu( const char *pszName, int iSize, void *pbuf )\n',
	)
	replace_once(
		client / "hud.cpp",
	'\t\tHOOK_MESSAGE( GameMode );\n\t\tHOOK_MESSAGE( SetFOV );\n',
	'\t\tHOOK_MESSAGE( GameMode );\n\t\tHOOK_MESSAGE( ShowMenu );\n\t\tHOOK_MESSAGE( SetFOV );\n',
	)
	hud_msg = client / "hud_msg.cpp"
	hud_msg_text = hud_msg.read_text()
	if "m_Menu.InitHUDData();" not in hud_msg_text:
		anchor = "\t// reset sensitivity\n"
		if anchor not in hud_msg_text:
			raise SystemExit(f"integration anchor not found in {hud_msg}: {anchor!r}")
		hud_msg.write_text(hud_msg_text.replace(
			anchor, "\t/* GC lean HUD still resets the DoD menu state. */\n"
			"\tm_Menu.InitHUDData();\n\n" + anchor, 1))
	integrate_gc_hud(client)
	hud_redraw = client / "hud_redraw.cpp"
	hud_redraw_text = hud_redraw.read_text()
	draw_block = ('\t\t\tif( m_Menu.m_fMenuDisplayed )\n'
		'\t\t\t\tm_Menu.Draw( flTime );\n')
	if hud_redraw_text.count('m_Menu.Draw( flTime );') > 1:
		prior = ('\t\t\t/* The lean path does not allocate the HUD list, so draw the stock\n'
			'\t\t\t * ShowMenu renderer directly for DoD team/class selection. */\n' + draw_block)
		hud_redraw_text = hud_redraw_text.replace(prior, '', 1)
	if 'm_Menu.Draw( flTime );' not in hud_redraw_text:
		anchor = '\t\t\t/* Once-flag lives on CHud — file/function statics re-spam on GC. */\n'
		if anchor not in hud_redraw_text:
			raise SystemExit(f"integration anchor not found in {hud_redraw}: {anchor!r}")
		hud_redraw_text = hud_redraw_text.replace(anchor,
			'\t\t\t/* Draw DoD ShowMenu once from the lean path. */\n' + draw_block + anchor, 1)
	hud_redraw.write_text(hud_redraw_text)
	print(f"integrated DoD gameplay into {args.hlsdk_dir}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
