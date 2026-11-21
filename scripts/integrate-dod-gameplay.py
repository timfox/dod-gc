#!/usr/bin/env python3
"""Apply the tracked DoD runtime overlay to the linked HLSDK checkout."""
from __future__ import annotations

import argparse
import os
import re
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
	# The team/class menu used to sit on the world: draw it on a retail-palette panel (ViewportBG 55 43 29, ClassMenuLight
	# 175 127 1 border) laid out by dod_hud_geom.h: inside the 5% safe area, under the objective row, above the status
	# panel, as wide as its widest line. Text is clipped to the screen (it was clipped at x 320).
	menu_text = menu_text.replace('static int menu_r, menu_g, menu_b, menu_x, menu_ralign;\n',
		'static int menu_r, menu_g, menu_b, menu_x, menu_ralign;\nstatic int g_iGCMenuLeft = 20, g_iGCMenuBottom = 1 << 20;\n', 1)
	menu_text = menu_text.replace(
		'\tmenu_r\t\t= 255;\n\tmenu_g\t\t= 255;\n\tmenu_b\t\t= 255;\n',
		'\t{\n'
		'\t\tconst float gsx = ScreenWidth / 640.0f, gsy = ScreenHeight / 480.0f;\n'
		'\t\tconst int glh = (int)( nFontHeight / gsy + 0.5f );\n'
		'\t\tint lines = nlc, tw = 0, n = 0, k;\n'
		'\t\tchar lb[80];\n'
		'\t\tconst char *q = g_szMenuString;\n'
		'\t\tDodRect pr;\n'
		'\t\tint px, py, pw, ph;\n'
		'\t\tif( lines > Dod_HudModalRows( glh )) lines = Dod_HudModalRows( glh );\n'
		'\t\tfor( ;; q++ )\n'
		'\t\t{\n'
		'\t\t\tif( *q == \'\\\\\' && q[1] ) { q++; continue; }\n'
		'\t\t\tif( *q == \'\\n\' || *q == 0 )\n'
		'\t\t\t{\n'
		'\t\t\t\tlb[n] = 0;\n'
		'\t\t\t\tk = gHUD.DrawHudStringLen( lb );\n'
		'\t\t\t\tif( k > tw ) tw = k;\n'
		'\t\t\t\tn = 0;\n'
		'\t\t\t\tif( *q == 0 ) break;\n'
		'\t\t\t}\n'
		'\t\t\telse if( n < (int)sizeof( lb ) - 1 ) lb[n++] = *q;\n'
		'\t\t}\n'
		'\t\tpr = Dod_HudMenuPanel( lines, (int)( tw / gsx + 0.5f ), glh );\n'
		'\t\tpx = (int)( pr.x * gsx + 0.5f ); py = (int)( pr.y * gsy + 0.5f );\n'
		'\t\tpw = (int)( pr.w * gsx + 0.5f ); ph = (int)( pr.h * gsy + 0.5f );\n'
		'\t\tgEngfuncs.pfnFillRGBABlend( px, py, pw, ph, 55, 43, 29, 235 );\n'
		'\t\tgEngfuncs.pfnFillRGBABlend( px, py, pw, 1, 175, 127, 1, 255 );\n'
		'\t\tgEngfuncs.pfnFillRGBABlend( px, py + ph - 1, pw, 1, 175, 127, 1, 255 );\n'
		'\t\tgEngfuncs.pfnFillRGBABlend( px, py, 1, ph, 175, 127, 1, 255 );\n'
		'\t\tgEngfuncs.pfnFillRGBABlend( px + pw - 1, py, 1, ph, 175, 127, 1, 255 );\n'
		'\t\ty = py + (int)( DOD_HUD_PANEL_PAD * gsy + 0.5f );\n'
		'\t\tg_iGCMenuLeft = px + (int)( DOD_HUD_PANEL_PAD * gsx + 0.5f );\n'
		'\t\tg_iGCMenuBottom = py + ph - (int)( DOD_HUD_PANEL_PAD * gsy + 0.5f );\n'
		'\t}\n'
		'\tmenu_r\t\t= 255;\n\tmenu_g\t\t= 255;\n\tmenu_b\t\t= 255;\n', 1)
	menu_text = menu_text.replace('\tmenu_x\t\t= 20;\n', '\tmenu_x\t\t= g_iGCMenuLeft;\n', 1)
	menu_text = menu_text.replace('\t\t\tmenu_x \t\t= 20;\n', '\t\t\tmenu_x \t\t= g_iGCMenuLeft;\n', 1)
	menu_text = menu_text.replace('menu_x = gHUD.DrawHudString( menu_x, y, 320, menubuf,', 'menu_x = gHUD.DrawHudString( menu_x, y, ScreenWidth, menubuf,')
	menu_text = menu_text.replace(
		'\t\t\tif( menu_ralign )\n\t\t\t\t// IMPORTANT',
		'\t\t\tif( y + nFontHeight > g_iGCMenuBottom )\n\t\t\t\t;\t// a line that no longer fits the panel is not drawn\n\t\t\telse if( menu_ralign )\n\t\t\t\t// IMPORTANT', 1)
	menu_cpp.write_text(menu_text)

	# Chat lines and the chat prompt inside the safe area (dod_hud_geom.h): lines bottom-anchored above the status panel,
	# hidden while a modal panel or the prompt is up, the prompt above the on-screen keyboard.
	say = client / "saytext.cpp"
	replace_once(say, "#define MAX_LINES\t5\n", "#define MAX_LINES\tDOD_HUD_CHAT_LINES\n")
	replace_once(say, "#define MAX_LINE_WIDTH  ( ScreenWidth - 40 )\n#define LINE_START  10\n",
		"#define MAX_LINE_WIDTH  ( ScreenWidth - ScreenWidth / 20 )\n#define LINE_START  ( ScreenWidth / 20 )\n")
	replace_once(say,
		"\tif( ScreenHeight >= 480 )\n\t\tY_START = ScreenHeight - 60;\n\telse\n\t\tY_START = ScreenHeight - 45;\n"
		"\tY_START -= ( line_height * ( MAX_LINES + 1 ) );\n",
		"\tY_START = (int)( Dod_HudModalBottom() * ( ScreenHeight / 480.0f )) - line_height * MAX_LINES;\n")
	replace_once(say, "\tif ( !m_HUD_saytext->value )\n\t\treturn 1;\n",
		"\tif ( !m_HUD_saytext->value || gHUD.m_Dod.Modal( ))\n\t\treturn 1;\n")
	spec = client / "vgui_SpectatorPanel.cpp"
	replace_once(spec, "extern \"C\" void DLLEXPORT HUD_ChatInputPosition( int *x, int *y )\n{\n",
		"extern \"C\" void DLLEXPORT HUD_ChatInputPosition( int *x, int *y )\n{\n"
		"#if XASH_GAMECUBE\n\tDodHud_ChatInputPosition( x, y );\n\treturn;\n#endif\n")


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


def integrate_film_actors(dlls: Path) -> None:
	"""Film actors (dod_film_actor.cpp) are always sent to the client. The server picks what to send by the PVS of the
	player's own origin, but during cine_play the view is the film camera, often far across the map, so a soldier in the
	shot was never in that PVS and never reached the screen. There are at most 12 of them, only while a film plays."""
	replace_once(dlls / "client.cpp",
		'\t\t\t// env_sky is visible always\n\t\t\tif( !FClassnameIs( ent, "env_sky" ) )\n',
		'\t\t\t// env_sky is visible always, and so are film actors (seen from the film camera, not the player)\n'
		'\t\t\tif( !FClassnameIs( ent, "env_sky" ) && !FClassnameIs( ent, "film_actor" ) )\n')


def integrate_mohaa_trigger_event(dlls: Path) -> None:
	"""MOHAA mission scripts wait on triggers ("$t waittill trigger"): tell the script VM when a trigger fires."""
	triggers = dlls / "triggers.cpp"
	replace_once(triggers, '#include "gamerules.h"\n', '#include "gamerules.h"\n#include "dod_mohaa_ents.h"\n')
	replace_once(triggers,
		'\tif( !FStringNull( pev->noise ) )\n\t\tEMIT_SOUND( ENT( pev ), CHAN_VOICE, STRING( pev->noise ), 1, ATTN_NORM );\n\n'
		'\t// don\'t trigger again until reset\n',
		'\tDodGC_MohaaEntityEvent( edict(), "trigger" );\n'
		'\tif( !FStringNull( pev->noise ) )\n\t\tEMIT_SOUND( ENT( pev ), CHAN_VOICE, STRING( pev->noise ), 1, ATTN_NORM );\n\n'
		'\t// don\'t trigger again until reset\n')


def integrate_doddetect(dlls: Path) -> None:
	"""info_doddetect is linked to a plain point entity in the SDK; the game's CDoDDetect (dod_runtime.cpp) takes the class name to read the
	respawn factors, so the SDK's link line goes."""
	replace_once(dlls / "subs.cpp",
		'LINK_ENTITY_TO_CLASS( info_doddetect, CPointEntity )\n',
		'/* info_doddetect is linked by the DoD game code (dod_runtime.cpp) */\n')


def integrate_pm_layout_hook(dlls: Path) -> None:
	"""GameDLLInit publishes the playermove_t layout (DodGC_RegisterPmLayout in dod_runtime.cpp) so the engine can stop with a clear
	message when the game DLL and the engine were built with different XASH_GC_PM_MAX_* values."""
	game = dlls / "game.cpp"
	text = game.read_text()
	if "DodGC_RegisterPmLayout" in text:
		return
	replace_once(game, 'void GameDLLInit( void )\n{\n',
		'void DodGC_RegisterPmLayout( void );\nvoid GameDLLInit( void )\n{\n\tDodGC_RegisterPmLayout();\n')


def integrate_precache_bodies_hook(dlls: Path) -> None:
	"""ServerActivate precaches the map's team bodies (DodGC_PrecacheTeamBodies in dod_runtime.cpp), after the entities spawn."""
	client = dlls / "client.cpp"
	if "DodGC_PrecacheTeamBodies" in client.read_text():
		return
	replace_once(client, "void ServerActivate( edict_t *pEdictList, int edictCount, int clientMax )\n{\n",
		"void DodGC_PrecacheTeamBodies( void );\nvoid ServerActivate( edict_t *pEdictList, int edictCount, int clientMax )\n{\n")
	replace_once(client, "\tg_serveractive = 1;\n",
		"\tg_serveractive = 1;\n#ifdef __GAMECUBE__\n\tDodGC_PrecacheTeamBodies();\n#endif\n")


def integrate_weaponbox_null_check(dlls: Path) -> None:
	"""CBaseEntity::Create returns NULL when the engine has no edict or no private block left (a crowded GameCube match), and
	the stock weapon box code writes through the result: a dead player or a dropped gun then wrote through a NULL pointer. Skip the
	drop instead."""
	player = dlls / "player.cpp"
	text = player.read_text()
	if "weaponbox: no edict" in text:
		return
	replace_once(player,
		"	CWeaponBox *pWeaponBox = (CWeaponBox *)CBaseEntity::Create( \"weaponbox\", pev->origin, pev->angles, edict() );\n\n	pWeaponBox->pev->angles.x = 0;",
		"	CWeaponBox *pWeaponBox = (CWeaponBox *)CBaseEntity::Create( \"weaponbox\", pev->origin, pev->angles, edict() );\n"
		"	if( !pWeaponBox )\n		return;	// weaponbox: no edict or private block left, drop nothing\n\n	pWeaponBox->pev->angles.x = 0;")
	replace_once(player,
		"			CWeaponBox *pWeaponBox = (CWeaponBox *)CBaseEntity::Create( \"weaponbox\", pev->origin + gpGlobals->v_forward * 10, pev->angles, edict() );\n			pWeaponBox->pev->angles.x = 0;",
		"			CWeaponBox *pWeaponBox = (CWeaponBox *)CBaseEntity::Create( \"weaponbox\", pev->origin + gpGlobals->v_forward * 10, pev->angles, edict() );\n"
		"			if( !pWeaponBox )\n				return;	// weaponbox: no edict or private block left\n			pWeaponBox->pev->angles.x = 0;")


def integrate_weapon_model_null_check(hlsdk: Path) -> None:
	"""StudioDrawPlayer merges the weapon model's bones into the player's. When the weapon model is not resident
	(IEngineStudio.Mod_Extradata gives NULL: a GameCube match with a dozen bots in view and the weapon models unloaded) the next
	line read m_pStudioHeader->numseq through NULL: a data fault (DAR 0xa4) that ended in libogc's reset screen and froze the match.
	Skip the weapon then, and say so once per model so the cause (a model that is not loaded) can be fixed in the engine."""
	renderer = hlsdk / "cl_dll" / "StudioModelRenderer.cpp"
	if "DodGC_WeaponModelResident" in renderer.read_text():
		return
	replace_once(renderer,
		"int CStudioModelRenderer::StudioDrawPlayer( int flags, entity_state_t *pplayer )\n",
		"static bool DodGC_WeaponModelResident( int index )\n{\n"
		"	static int logged[8];\n	static int nlogged;\n"
		"	model_t *m = IEngineStudio.GetModelByIndex( index );\n"
		"	if( m && IEngineStudio.Mod_Extradata( m ))\n		return true;\n"
		"	for( int i = 0; i < nlogged; i++ )\n		if( logged[i] == index )\n			return false;\n"
		"	if( nlogged < 8 )\n		logged[nlogged++] = index;\n"
		"	gEngfuncs.Con_Printf( \"DoD weapon model %d (%s) not resident, weapon not drawn\\n\", index, m ? m->name : \"(none)\" );\n"
		"	return false;\n}\n\n"
		"int CStudioModelRenderer::StudioDrawPlayer( int flags, entity_state_t *pplayer )\n")
	replace_once(renderer,
		"		if( pplayer->weaponmodel )\n		{\n			cl_entity_t saveent = *m_pCurrentEntity;\n\n			model_t *pweaponmodel = IEngineStudio.GetModelByIndex( pplayer->weaponmodel );\n",
		"		if( pplayer->weaponmodel && DodGC_WeaponModelResident( pplayer->weaponmodel ))\n		{\n			cl_entity_t saveent = *m_pCurrentEntity;\n\n			model_t *pweaponmodel = IEngineStudio.GetModelByIndex( pplayer->weaponmodel );\n")


def integrate_gib_null_check(dlls: Path) -> None:
	"""Every `GetClassPtr( (T *)NULL )` makes its entity with CREATE_ENTITY, which gives no edict at the 460 tip of the GameCube, and
	the code then read through the NULL edict: a data fault that froze the match (a bot gibbed in CBasePlayer::Killed, a rocket, an
	explosion's sparks ...). GetClassPtr now returns NULL, and each caller bails out after its call: `return;` in a void function,
	`return NULL;` in a pointer function, `return 0;` otherwise, `break;` inside a loop. Gibs are cosmetic: they also stop 24 edicts
	short of the tip so they never eat the reserve the engine keeps for rockets and grenades."""
	import re
	cbase = dlls / "cbase.h"
	if "no edict from CREATE_ENTITY" not in cbase.read_text():
		replace_once(cbase, "	if( pev == NULL )\n		pev = VARS( CREATE_ENTITY() );\n",
			"	if( pev == NULL )\n	{\n		edict_t *pentNew = CREATE_ENTITY();\n		if( FNullEnt( pentNew ))\n			return NULL;	// no edict from CREATE_ENTITY (the tip)\n		pev = VARS( pentNew );\n	}\n")
	plats = dlls / "plats.cpp"
	if "pTrig" not in plats.read_text():
		replace_once(plats, "	GetClassPtr( (CPlatTrigger *)NULL )->SpawnInsideTrigger( GetClassPtr( (CFuncPlat *)pevPlatform ) );\n",
			"	CPlatTrigger *pTrig = GetClassPtr( (CPlatTrigger *)NULL );\n	if( pTrig )\n		pTrig->SpawnInsideTrigger( GetClassPtr( (CFuncPlat *)pevPlatform ) );\n")
	decl = re.compile(r"^(\s*)(?:[\w:]+\s*\*\s*)?(\w+)\s*=\s*GetClassPtr\(\s*\(\s*(\w+)\s*\*\s*\)\s*NULL\s*\)\s*;\s*$")
	for path in sorted(dlls.glob("*.cpp")):
		text = path.read_text()
		if "GetClassPtr(" not in text or "// no edict" in text:
			continue
		lines = text.split("\n")
		out = []
		changed = False
		for i, line in enumerate(lines):
			m = decl.match(line)
			if not m:
				out.append(line)
				continue
			indent, name, cls = m.group(1), m.group(2), m.group(3)
			if cls == "CGib":
				line = line.replace("GetClassPtr( (CGib *)NULL )", "( gpGlobals->maxEntities - g_engfuncs.pfnNumberOfEntities() < 24 ) ? NULL : GetClassPtr( (CGib *)NULL )")
			# the function header, and whether a loop encloses the call
			ind = len(indent.replace("\t", "    "))
			in_loop = False
			header = None
			cur = ind
			for j in range(i - 1, -1, -1):
				prev = lines[j]
				if not prev.strip():
					continue
				pind = len(prev[:len(prev) - len(prev.lstrip())].replace("\t", "    "))
				if pind == 0 and not prev.lstrip().startswith(("{", "}", "#", "/", "*")):
					header = prev
					break
				if pind < cur and prev.strip() != "{":
					if re.match(r"\s*(for|while)\b", prev):
						in_loop = True
					cur = pind
			bail = "return 0;"
			if in_loop:
				bail = "break;"
			elif header is not None:
				hm = re.match(r"\s*(.*?)\b[\w:~]+\s*\(", header)
				rtype = hm.group(1) if hm else ""
				if "*" in rtype:
					bail = "return NULL;"
				elif "void" in rtype.split() and "*" not in rtype:
					bail = "return;"
			out.append(line)
			out.append("%sif( !%s )\n%s\t%s	// no edict" % (indent, name, indent, bail))
			changed = True
		if changed:
			path.write_text("\n".join(out))


def integrate_studio_anim_pack(hlsdk: Path, engine: Path | None) -> None:
	"""The client DLL's StudioModelRenderer::StudioGetAnim loads a sequence group with IEngineStudio.LoadCacheFile. For a
	split DoD model the group is an animation pack (.gca, 3.5 MB), which cannot be read into MEM1 (Host_Error: LoadCacheFile:
	can't load models/player/anim_*.gca on the first bot). The engine (engine_studio_api_t.GCGetAnim) fetches a sequence
	from the pack in ARAM; the DLL uses it for any group with a pack. Needs an engine with the member."""
	if engine is not None and "GCGetAnim" not in (engine / "common" / "r_studioint.h").read_text():
		raise SystemExit("the engine checkout has no engine_studio_api_t.GCGetAnim; merge the engine change first "
			"(the client DLL's studio renderer would try to read a 3.5 MB animation pack into MEM1)")
	api = hlsdk / "common" / "r_studioint.h"
	replace_once(api,
		"	void		( *StudioRenderShadow )( int iSprite, float *p1, float *p2, float *p3, float *p4 );\n} engine_studio_api_t;",
		"	void		( *StudioRenderShadow )( int iSprite, float *p1, float *p2, float *p3, float *p4 );\n\n"
		"	// GameCube extension (xash3d-gc common/r_studioint.h): a sequence of a split model from its animation pack\n"
		"	void		*( *GCGetAnim )( void *phdr, void *submodel, void *pseqdesc );\n} engine_studio_api_t;")
	replace_once(hlsdk / "cl_dll" / "StudioModelRenderer.cpp",
		"	if( pseqdesc->seqgroup == 0 )\n	{\n		return (mstudioanim_t *)( (byte *)m_pStudioHeader + pseqdesc->animindex );\n	}\n\n	paSequences",
		"	if( pseqdesc->seqgroup == 0 )\n	{\n		return (mstudioanim_t *)( (byte *)m_pStudioHeader + pseqdesc->animindex );\n	}\n\n"
		"	/* GameCube: a sequence group that is an animation pack (.gca) is streamed from ARAM by the engine. */\n"
		"	if( IEngineStudio.GCGetAnim && strstr( pseqgroup->name, \".gca\" ))\n"
		"	{\n		mstudioanim_t *packed = (mstudioanim_t *)IEngineStudio.GCGetAnim( m_pStudioHeader, m_pSubModel, pseqdesc );\n"
		"		if( packed )\n			return packed;\n	}\n\n	paSequences")


def rename_sounds(path: Path, renames: dict[str, str]) -> None:
	"""Point every use of a stock HL sound name in `path` at the retail DoD one (idempotent; a missing name is not an error)."""
	text = path.read_text()
	new = text
	for old, retail in renames.items():
		new = new.replace(f'"{old}"', f'"{retail}"')
	if new != text:
		path.write_text(new)


def integrate_retail_sounds(hlsdk: Path) -> None:
	"""The stock HL player, item and fall sounds become the retail DoD ones (the files exist in retail dod/sound; the
	retail game DLL precaches exactly these names): pain is player/damage1-11, a death player/dmbullo1-3 (the three
	slots the stock code fills with pl_pain5-7), pickups and drops items/weaponpickup, ammopickup and weapondrop, a
	hard landing player/pl_fallpain."""
	dlls = hlsdk / "dlls"
	player = dlls / "player.cpp"
	pain = (
		'\tfloat flRndSound;//sound randomizer\n\n\tflRndSound = RANDOM_FLOAT( 0.0f, 1.0f ); \n\n'
		'\tif( flRndSound <= 0.33f )\n\t\tEMIT_SOUND( ENT( pev ), CHAN_VOICE, "player/pl_pain5.wav", 1, ATTN_NORM );\n'
		'\telse if( flRndSound <= 0.66f )\t\n\t\tEMIT_SOUND( ENT( pev ), CHAN_VOICE, "player/pl_pain6.wav", 1, ATTN_NORM );\n'
		'\telse\n\t\tEMIT_SOUND( ENT( pev ), CHAN_VOICE, "player/pl_pain7.wav", 1, ATTN_NORM );\n')
	replace_once(player, pain,
		'\tchar sound[32];\n\t/* retail: one of the eleven player/damage grunts */\n'
		'\tsnprintf( sound, sizeof( sound ), "player/damage%d.wav", RANDOM_LONG( 1, 11 ) );\n'
		'\tPRECACHE_SOUND( sound );\n\tEMIT_SOUND( ENT( pev ), CHAN_VOICE, sound, 1, ATTN_NORM );\n')
	death = (
		'\t// temporarily using pain sounds for death sounds\n\tswitch( RANDOM_LONG( 1, 5 ) )\n\t{\n'
		'\tcase 1: \n\t\tEMIT_SOUND( ENT( pev ), CHAN_VOICE, "player/pl_pain5.wav", 1, ATTN_NORM );\n\t\tbreak;\n'
		'\tcase 2: \n\t\tEMIT_SOUND( ENT( pev ), CHAN_VOICE, "player/pl_pain6.wav", 1, ATTN_NORM );\n\t\tbreak;\n'
		'\tcase 3: \n\t\tEMIT_SOUND( ENT( pev ), CHAN_VOICE, "player/pl_pain7.wav", 1, ATTN_NORM );\n\t\tbreak;\n\t}\n')
	replace_once(player, death,
		'\t{\n\t\tchar sound[32];\n\t\t/* retail: player/dmbullo1-3, every death speaks */\n'
		'\t\tsnprintf( sound, sizeof( sound ), "player/dmbullo%d.wav", RANDOM_LONG( 1, 3 ) );\n'
		'\t\tPRECACHE_SOUND( sound );\n\t\tEMIT_SOUND( ENT( pev ), CHAN_VOICE, sound, 1, ATTN_NORM );\n\t}\n')
	pickups = {"items/gunpickup2.wav": "items/weaponpickup.wav", "items/weapondrop1.wav": "items/weapondrop.wav"}
	for name in ("items.cpp", "weapons.cpp"):
		rename_sounds(dlls / name, pickups)
	rename_sounds(dlls / "weapons.cpp", {"items/9mmclip1.wav": "items/ammopickup.wav"})
	rename_sounds(dlls / "client.cpp", {
		"player/pl_pain2.wav": "player/damage1.wav", "player/pl_pain4.wav": "player/damage2.wav",
		"player/pl_pain5.wav": "player/damage3.wav", "player/pl_pain6.wav": "player/damage4.wav",
		"player/pl_pain7.wav": "player/damage5.wav", "player/pl_fallpain2.wav": "player/pl_fallpain.wav",
		"player/pl_fallpain3.wav": "player/pl_fallpain.wav", "items/gunpickup2.wav": "items/weaponpickup.wav"})
	rename_sounds(hlsdk / "pm_shared" / "pm_shared.c", {"player/pl_fallpain2.wav": "player/pl_fallpain.wav",
		"player/pl_fallpain3.wav": "player/pl_fallpain.wav"})
	integrate_retail_footsteps(hlsdk / "pm_shared" / "pm_shared.c")


def integrate_pm_caps(hlsdk: Path, engine: Path) -> None:
	"""playermove_t array caps. The engine (xash3d-gc wscript, XASH_GC_PM_MAX_PHYSENTS / XASH_GC_PM_MAX_MOVEENTS) and the
	game DLL are separate builds with their own copy of pm_defs.h, and the struct layout must match, so the SDK copy gets
	the same numbers. Unset leaves the stock 600 / 64."""
	phys, move = os.environ.get("XASH_GC_PM_MAX_PHYSENTS", ""), os.environ.get("XASH_GC_PM_MAX_MOVEENTS", "")
	defs = hlsdk / "pm_shared" / "pm_defs.h"
	if engine and (phys.isdigit() or move.isdigit()) and "GC_PM_MAX_PHYSENTS" not in (engine / "pm_shared" / "pm_defs.h").read_text():
		raise SystemExit("the engine checkout does not support XASH_GC_PM_MAX_PHYSENTS; merge the engine change first "
			"or set XASH_GC_PM_MAX_PHYSENTS=stock (the DLL and engine must agree on playermove_t)")
	if phys.isdigit():
		text = re.sub(r"#define MAX_PHYSENTS\t\d+", "#define MAX_PHYSENTS\t" + phys, defs.read_text(), count=1)
		defs.write_text(text)
	if move.isdigit():
		text = re.sub(r"#define MAX_MOVEENTS\t\d+", "#define MAX_MOVEENTS\t" + move, defs.read_text(), count=1)
		defs.write_text(text)


def restore_pm_caps(hlsdk: Path) -> None:
	"""Put the SDK's pm_defs.h back to the stock 600 / 64. The patch in integrate_pm_caps is only needed while the game archives
	are compiled; the archives keep the layout they were built with, so the source tree is restored right after and never left
	patched. A patched tree pairs only with an engine built with the same XASH_GC_PM_MAX_* values (a mismatch crashes in
	PM_InitTextureTypes: playermove_t differs between the game DLL and the engine)."""
	defs = hlsdk / "pm_shared" / "pm_defs.h"
	text = defs.read_text()
	text = re.sub(r"#define MAX_PHYSENTS\t\d+", "#define MAX_PHYSENTS\t600", text, count=1)
	text = re.sub(r"#define MAX_MOVEENTS\t\d+", "#define MAX_MOVEENTS\t64", text, count=1)
	defs.write_text(text)


def integrate_retail_footsteps(pm: Path) -> None:
	# Footsteps: retail has wood, gravel and snow floors (pl_wood, pl_gravel, pl_snow) and grass, sand and leaves walk as
	# dirt; the stock code has no such steps (a wooden floor sounds like concrete).
	replace_once(pm, "#define STEP_LADDER\t\t8\t\t// climbing ladder\n",
		"#define STEP_LADDER\t\t8\t\t// climbing ladder\n"
		"#define STEP_WOOD\t\t9\t\t// retail DoD: wooden floor ('W')\n"
		"#define STEP_GRAVEL\t\t10\t\t// retail DoD: gravel ('L')\n"
		"#define STEP_SNOW\t\t11\t\t// retail DoD: snow ('N')\n")
	replace_once(pm, "\t\tcase CHAR_TEX_SLOSH:\n\t\t\treturn STEP_SLOSH;\n",
		"\t\tcase CHAR_TEX_SLOSH:\n\t\t\treturn STEP_SLOSH;\n"
		"\t\tcase 'H':\t// heavy metal\n\t\t\treturn STEP_METAL;\n"
		"\t\tcase CHAR_TEX_WOOD:\n\t\t\treturn STEP_WOOD;\n"
		"\t\tcase 'L':\n\t\t\treturn STEP_GRAVEL;\n"
		"\t\tcase CHAR_TEX_SNOW:\n\t\t\treturn STEP_SNOW;\n"
		"\t\tcase 'P':\t// grass\n\t\tcase 'A':\t// sand\n\t\tcase 'E':\t// leaves\n\t\t\treturn STEP_DIRT;\n")
	replace_once(pm, "\tcase STEP_SLOSH:\n\t\tswitch( irand )\n",
		"\tcase STEP_WOOD:\n\t\tpmove->PM_PlaySound( CHAN_BODY, irand == 0 ? \"player/pl_wood1.wav\" : irand == 1 ? \"player/pl_wood3.wav\" :\n"
		"\t\t\tirand == 2 ? \"player/pl_wood2.wav\" : \"player/pl_wood4.wav\", fvol, ATTN_NORM, 0, PITCH_NORM );\n\t\tbreak;\n"
		"\tcase STEP_GRAVEL:\n\t\tpmove->PM_PlaySound( CHAN_BODY, irand == 0 ? \"player/pl_gravel1.wav\" : irand == 1 ? \"player/pl_gravel3.wav\" :\n"
		"\t\t\tirand == 2 ? \"player/pl_gravel2.wav\" : \"player/pl_gravel4.wav\", fvol, ATTN_NORM, 0, PITCH_NORM );\n\t\tbreak;\n"
		"\tcase STEP_SNOW:\n\t\tpmove->PM_PlaySound( CHAN_BODY, irand == 0 ? \"player/pl_snow1.wav\" : irand == 1 ? \"player/pl_snow3.wav\" :\n"
		"\t\t\tirand == 2 ? \"player/pl_snow2.wav\" : \"player/pl_snow4.wav\", fvol, ATTN_NORM, 0, PITCH_NORM );\n\t\tbreak;\n"
		"\tcase STEP_SLOSH:\n\t\tswitch( irand )\n")


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
	"dod_weapon.cpp", "dod_weapon.h", "dod_viewanims.h", "dod_viewsounds.h", "dod_hud.h", "dod_hud_retail.h", "dod_hud_geom.h",
	"dod_bots.c", "dod_bots.h", "dod_bot.cpp", "dod_bot.h",
	"dod_film.c", "dod_film.h", "dod_film_actor.cpp",
	"dod_achievements.c", "dod_achievements.h",
	"dod_grunts.c", "dod_grunts.h", "dod_grunt.cpp",
	"dod_mohaa.c", "dod_mohaa.h", "dod_mohaa_ents.cpp", "dod_mohaa_ents.h",
)
# The MOHAA script VM (src/mohaa/, vendored from timfox/mohaa-gc), copied flat into dlls/.
MOHAA_VM_SOURCES = ("morpheus.h", "ms_internal.h", "ms_value.c", "ms_compile.c", "ms_vm.c", "ms_builtins.c")
# Client HUD sources, copied into cl_dll/ (dod_hud.h is shared with the DLL).
DOD_CLIENT_SOURCES = (
	("dod_hud.h", "dod_hud.h"),
	("dod_hud_retail.h", "dod_hud_retail.h"),
	("dod_hud_geom.h", "dod_hud_geom.h"),
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
	for name in MOHAA_VM_SOURCES:
		shutil.copy2(src / "mohaa" / name, dlls / name)
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
	replace_once(view, '#include "view.h"\n', '#include "view.h"\n#include "dod_cam.h"\n'
		'\n'
		'/* Clip a spectator camera move against the world: trace start to org (world and brush entities, not\n'
		' * players) and, when blocked, back org off the wall by a few units. */\n'
		'extern "C" int PM_GetPhysEntInfo( int ent );\n'
		'static void DodCam_ClipToWorld( const float *start, float *org )\n'
		'{\n'
		'\tvec3_t from;\n'
		'\tint loops = 4, ignore = -1;\n'
		'\n'
		'\tVectorCopy( start, from );\n'
		'\twhile( loops-- > 0 )\n'
		'\t{\n'
		'\t\tpmtrace_t *tr = gEngfuncs.PM_TraceLine( from, org, PM_TRACELINE_PHYSENTSONLY, 2, ignore );\n'
		'\t\tcl_entity_t *hit;\n'
		'\n'
		'\t\tstatic int s_calls, s_blocked;\n'
		'\t\tif( ++s_calls % 600 == 1 )\n'
		'\t\t\tgEngfuncs.Con_Printf( "DODCAM clip calls=%d blocked=%d frac=%.2f contents=%d\\n", s_calls, s_blocked, tr->fraction, gEngfuncs.PM_PointContents( org, NULL ) );\n'
		'\t\tif( tr->fraction >= 1.0f )\n'
		'\t\t\treturn;\n'
		'\t\ts_blocked++;\n'
		'\t\thit = tr->ent > 0 ? gEngfuncs.GetEntityByIndex( PM_GetPhysEntInfo( tr->ent ) ) : NULL;\n'
		'\t\tif( !hit || ( hit->curstate.solid == SOLID_BSP && !hit->player ) || tr->ent <= 0 )\n'
		'\t\t{\n'
		'\t\t\tDodCam_PullIn( start, org, tr->fraction, 6.0f, org );\n'
		'\t\t\treturn;\n'
		'\t\t}\n'
		'\t\t/* a player or other non-brush solid: look past it */\n'
		'\t\tignore = tr->ent;\n'
		'\t}\n'
		'}\n')
	replace_once(view,
		'\t\tif( gHUD.m_Spectator.m_pip->value )\n\t\t\tpparams->nextView = 1;\t// force a second renderer view\n',
		'\t\t/* DoD: the chase cameras follow an entity origin that only moves on a network\n'
		'\t\t * update; low-pass them so the view glides (snaps on target or mode changes). */\n'
		'\t\t{\n'
		'\t\t\tstatic DodCamState s_dodCam;\n'
		'\t\t\tstatic vec3_t s_dodPrev;\n'
		'\t\t\tstatic int s_dodPrevMode;\n'
		'\t\t\tDodCam_Smooth( &s_dodCam, g_iUser1, g_iUser2, v_origin, v_angles, pparams->frametime, v_origin, v_angles );\n'
		'\t\t\tDodCam_Level( g_iUser1, v_angles );\n'
		'\t\t\t/* Never let the camera pass through the world: chase cameras are clipped on the ray\n'
		'\t\t\t * from the followed player\'s eye, free-fly on the move from last frame\'s position. */\n'
		'\t\t\tif( g_iUser1 == OBS_CHASE_LOCKED || g_iUser1 == OBS_CHASE_FREE )\n'
		'\t\t\t{\n'
		'\t\t\t\tcl_entity_t *tgt = gEngfuncs.GetEntityByIndex( g_iUser2 );\n'
		'\t\t\t\tif( tgt )\n'
		'\t\t\t\t{\n'
		'\t\t\t\t\tvec3_t eye;\n'
		'\t\t\t\t\tVectorCopy( tgt->origin, eye );\n'
		'\t\t\t\t\teye[2] += 24.0f;\n'
		'\t\t\t\t\t/* dod_spec_cine 1: cinematic mode, moving shots around the followed player\n'
		'\t\t\t\t\t * (orbit, track, crane, flyby, low angle) cutting every few seconds. */\n'
		'\t\t\t\t\tif( gEngfuncs.pfnGetCvarFloat( "dod_spec_cine" ) != 0.0f )\n'
		'\t\t\t\t\t{\n'
		'\t\t\t\t\t\tstatic DodCamCine s_cine;\n'
		'\t\t\t\t\t\tstatic DodCamState s_cineSmooth;\n'
		'\t\t\t\t\t\tvec3_t feet, aim;\n'
		'\t\t\t\t\t\tfloat t;\n'
		'\t\t\t\t\t\tif( DodCam_CineStep( &s_cine, g_iUser2, gEngfuncs.GetClientTime(), &t ) )\n'
		'\t\t\t\t\t\t{\n'
		'\t\t\t\t\t\t\ts_cine.yaw = tgt->angles[1];\n'
		'\t\t\t\t\t\t\tDodCam_Reset( &s_cineSmooth );\n'
		'\t\t\t\t\t\t}\n'
		'\t\t\t\t\t\tVectorCopy( tgt->origin, feet );\n'
		'\t\t\t\t\t\tfeet[2] -= 36.0f;\n'
		'\t\t\t\t\t\tDodCam_ShotPose( s_cine.shot, t, feet, s_cine.yaw, g_iUser2 + s_cine.shot, v_origin, v_angles );\n'
		'\t\t\t\t\t\t/* the target moves on network updates: glide the shot like the chase camera */\n'
		'\t\t\t\t\t\tDodCam_Smooth( &s_cineSmooth, OBS_CHASE_FREE, g_iUser2, v_origin, v_angles, pparams->frametime, v_origin, v_angles );\n'
		'\t\t\t\t\t\tDodCam_ClipToWorld( eye, v_origin );\n'
		'\t\t\t\t\t\tVectorCopy( feet, aim );\n'
		'\t\t\t\t\t\taim[2] += 56.0f;\n'
		'\t\t\t\t\t\tDodCam_LookAt( v_origin, aim, v_angles );\n'
		'\t\t\t\t\t}\n'
		'\t\t\t\t\telse\n'
		'\t\t\t\t\t\tDodCam_ClipToWorld( eye, v_origin );\n'
		'\t\t\t\t}\n'
		'\t\t\t\ts_dodPrevMode = 0;\n'
		'\t\t\t}\n'
		'\t\t\telse if( g_iUser1 == OBS_ROAMING )\n'
		'\t\t\t{\n'
		'\t\t\t\tif( s_dodPrevMode == OBS_ROAMING )\n'
		'\t\t\t\t\tDodCam_ClipToWorld( s_dodPrev, v_origin );\n'
		'\t\t\t\tVectorCopy( v_origin, s_dodPrev );\n'
		'\t\t\t\ts_dodPrevMode = OBS_ROAMING;\n'
		'\t\t\t}\n'
		'\t\t\telse\n'
		'\t\t\t\ts_dodPrevMode = 0;\n'
		'\t\t}\n'
		'\n'
		'\t\tif( gHUD.m_Spectator.m_pip->value )\n\t\t\tpparams->nextView = 1;\t// force a second renderer view\n')


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("--hlsdk-dir", required=True, type=Path)
	parser.add_argument("--engine-dir", type=Path)
	parser.add_argument("--restore-pm-caps", action="store_true", help="restore the SDK pm_defs.h to the stock 600/64 and exit")
	args = parser.parse_args()
	if args.restore_pm_caps:
		restore_pm_caps(args.hlsdk_dir)
		return 0
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
	integrate_film_actors(dlls)
	integrate_doddetect(dlls)
	integrate_mohaa_trigger_event(dlls)
	integrate_pm_layout_hook(dlls)
	integrate_precache_bodies_hook(dlls)
	integrate_weaponbox_null_check(dlls)
	integrate_studio_anim_pack(args.hlsdk_dir, args.engine_dir)
	integrate_weapon_model_null_check(args.hlsdk_dir)
	integrate_gib_null_check(args.hlsdk_dir / "dlls")
	integrate_death_anims(dlls)
	integrate_retail_sounds(args.hlsdk_dir)
	integrate_pm_caps(args.hlsdk_dir, args.engine_dir)
	integrate_body_queue(dlls)
	integrate_weapons(dlls)
	integrate_dod_hud(client)
	integrate_spectator_camera(client)

	replace_once(
		dlls / "CMakeLists.txt",
		"\tmultiplay_gamerules.cpp\n",
		"\tmultiplay_gamerules.cpp\n\tdod_game.c\n\tdod_weapons.c\n\tdod_runtime.cpp\n\tdod_grenade.cpp\n\tdod_weapon.cpp\n\tdod_bots.c\n\tdod_bot.cpp\n\tdod_film.c\n\tdod_film_actor.cpp\n\tdod_achievements.c\n\tdod_grunts.c\n\tdod_grunt.cpp\n\tdod_mohaa.c\n\tdod_mohaa_ents.cpp\n\tms_value.c\n\tms_compile.c\n\tms_vm.c\n\tms_builtins.c\n",
	)
	replace_once(
		dlls / "wscript",
		"\tsource = bld.path.ant_glob('**/*.cpp', excl=excluded_files)\n",
		"\tsource = bld.path.ant_glob('**/*.cpp', excl=excluded_files)\n"
		"\tsource += bld.path.ant_glob('dod_game.c')\n"
		"\tsource += bld.path.ant_glob('dod_weapons.c')\n"
		"\tsource += bld.path.ant_glob('dod_bots.c')\n"
		"\tsource += bld.path.ant_glob('dod_film.c')\n"
		"\tsource += bld.path.ant_glob('dod_achievements.c')\n"
		"\tsource += bld.path.ant_glob('dod_grunts.c')\n"
		"\tsource += bld.path.ant_glob('dod_mohaa.c')\n"
		"\tsource += bld.path.ant_glob('ms_*.c')\n",
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
	# game would build vanilla deathmatch rules (no CHalfLifeDoD, no bots). mp_teamlist stays the
	# stock "hgrunt;scientist" too, so every player's side read as no side and everybody (bots and
	# the human) spawned at the map origin: set both.
	replace_once(
		dlls / "gamerules.cpp",
		'\tif( !gpGlobals->deathmatch )\n\t{\n\t\t// generic half-life\n',
		'\tif( !stricmp( gameleaf, "dod" ) && gpGlobals->deathmatch )\n\t{\n\t\tCVAR_SET_FLOAT( "mp_teamplay", 1 );\n'
		'\t\tCVAR_SET_STRING( "mp_teamlist", "Allies;Axis" );\n\t}\n'
		'\tif( !gpGlobals->deathmatch )\n\t{\n\t\t// generic half-life\n',
	)
	# Mounted guns (func_tank): the stock Half-Life tank aims at and fires on the first client in its view with no team check
	# (CFuncTank::FindTarget returns the player), so an active func_tank killed the Axis bots at their own spawn on avalanche
	# (an Axis MG at 508,2099,387, spawnflags 1). A DoD mounted gun fires for the player on it, not on its own.
	replace_once(
		dlls / "func_tank.cpp",
		"edict_t *CFuncTank::FindTarget( edict_t *pPlayer )\n{\n\treturn pPlayer;\n}",
		"edict_t *CFuncTank::FindTarget( edict_t *pPlayer )\n{\n\t/* Day of Defeat: no automatic target; a controlled tank follows its controller (TrackTarget) */\n\treturn NULL;\n}",
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
