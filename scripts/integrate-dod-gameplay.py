#!/usr/bin/env python3
"""Apply the tracked DoD runtime overlay to the linked HLSDK checkout."""
from __future__ import annotations

import argparse
import shutil
from pathlib import Path


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

	for name in ("dod_game.c", "dod_game.h", "dod_runtime.cpp", "dod_runtime.h"):
		shutil.copy2(root / "src" / name, dlls / name)

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
		studio = args.engine_dir / "ref/gx/r_studio.c"
		studio_text = studio.read_text()
		# forward=16/up=+8 — sticky mid-idle; avoid fwd=36 pillar / f=0 freeze.
		if "G162 viewmodel frame offset forward=16 up=+8" not in studio_text:
			for old_up, old_msg in (
				(
					'\t\t\tVectorMA( view->origin, 12.0f, RI.vforward, view->origin );\n'
					'\t\t\tVectorMA( view->origin, 10.0f, RI.vup, view->origin );\n',
					'Xash3D GameCube: G162 viewmodel frame offset forward=12 up=+10\\n',
				),
				(
					'\t\t\tVectorMA( view->origin, 36.0f, RI.vforward, view->origin );\n'
					'\t\t\tVectorMA( view->origin, 2.0f, RI.vup, view->origin );\n',
					'Xash3D GameCube: G162 viewmodel frame offset forward=36 up=+2\\n',
				),
				(
					'\t\t\tVectorMA( view->origin, 30.0f, RI.vforward, view->origin );\n'
					'\t\t\tVectorMA( view->origin, 2.0f, RI.vup, view->origin );\n',
					'Xash3D GameCube: G162 viewmodel frame offset forward=30 up=+2\\n',
				),
				(
					'\t\t\tVectorMA( view->origin, 32.0f, RI.vforward, view->origin );\n'
					'\t\t\tVectorMA( view->origin, 2.0f, RI.vup, view->origin );\n',
					'Xash3D GameCube: G162 viewmodel frame offset forward=32 up=+2\\n',
				),
				(
					'\t\t\tVectorMA( view->origin, 42.0f, RI.vforward, view->origin );\n'
					'\t\t\tVectorMA( view->origin, 1.0f, RI.vup, view->origin );\n',
					'Xash3D GameCube: G162 viewmodel frame offset forward=42 up=+1\\n',
				),
			):
				if old_up in studio_text:
					replace_once(
						studio,
						old_up,
						'\t\t\tVectorMA( view->origin, 16.0f, RI.vforward, view->origin );\n'
						'\t\t\tVectorMA( view->origin, 8.0f, RI.vup, view->origin );\n',
					)
					replace_once(
						studio,
						old_msg,
						'Xash3D GameCube: G162 viewmodel frame offset forward=16 up=+8\\n',
					)
					break
		studio_text = studio.read_text()
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


	for filename, class_name, kind, animation, event, legacy_model, models in (
		("mp5.cpp", "CMP5", "DOD_MODEL_AUTOMATIC", "MP5_DEPLOY", "mp5", "9mmAR",
		 '\tPRECACHE_MODEL( "models/v_garand.mdl" ); PRECACHE_MODEL( "models/p_garand.mdl" );\n'
		 '\tPRECACHE_MODEL( "models/v_98k.mdl" ); PRECACHE_MODEL( "models/p_k98.mdl" );\n'
		 '\tPRECACHE_MODEL( "models/v_tommy.mdl" ); PRECACHE_MODEL( "models/p_tommy.mdl" );\n'
		 '\tPRECACHE_MODEL( "models/v_mp40.mdl" ); PRECACHE_MODEL( "models/p_mp40.mdl" );\n'
		 '\tPRECACHE_MODEL( "models/v_30cal.mdl" ); PRECACHE_MODEL( "models/p_30cal.mdl" );\n'
		 '\tPRECACHE_MODEL( "models/v_mg42.mdl" ); PRECACHE_MODEL( "models/p_mg42pr.mdl" );\n'),
		("shotgun.cpp", "CShotgun", "DOD_MODEL_SUPPORT", "SHOTGUN_DRAW", "shotgun", "shotgun",
		 '\tPRECACHE_MODEL( "models/v_bren.mdl" ); PRECACHE_MODEL( "models/p_bren.mdl" );\n'
		 '\tPRECACHE_MODEL( "models/v_mp44.mdl" ); PRECACHE_MODEL( "models/p_stg44.mdl" );\n'),
		("crossbow.cpp", "CCrossbow", "DOD_MODEL_SNIPER", "m_iClip ? CROSSBOW_DRAW1 : CROSSBOW_DRAW2", "bow", "crossbow",
		 '\tPRECACHE_MODEL( "models/v_spring.mdl" ); PRECACHE_MODEL( "models/p_spring.mdl" );\n'
		 '\tPRECACHE_MODEL( "models/v_scoped98k.mdl" ); PRECACHE_MODEL( "models/p_k98.mdl" );\n'),
		("rpg.cpp", "CRpg", "DOD_MODEL_ROCKET", "m_iClip ? RPG_DRAW1 : RPG_DRAW_UL", "rpg", "rpg",
		 '\tPRECACHE_MODEL( "models/v_bazooka.mdl" ); PRECACHE_MODEL( "models/p_bazooka.mdl" );\n'
		 '\tPRECACHE_MODEL( "models/v_panzerschreck.mdl" ); PRECACHE_MODEL( "models/p_pschreck.mdl" );\n'),
		("crowbar.cpp", "CCrowbar", "DOD_MODEL_MELEE", "CROWBAR_DRAW", "crowbar", "crowbar",
		 '\tPRECACHE_MODEL( "models/v_spade.mdl" );\n'
		 '\tPRECACHE_MODEL( "models/p_spade.mdl" );\n'),
	):
		integrate_weapon(dlls / filename, class_name, kind, animation, event, legacy_model, models)

	replace_once(
		dlls / "CMakeLists.txt",
		"\tmultiplay_gamerules.cpp\n",
		"\tmultiplay_gamerules.cpp\n\tdod_game.c\n\tdod_runtime.cpp\n",
	)
	replace_once(
		dlls / "wscript",
		"\tsource = bld.path.ant_glob('**/*.cpp', excl=excluded_files)\n",
		"\tsource = bld.path.ant_glob('**/*.cpp', excl=excluded_files)\n"
		"\tsource += bld.path.ant_glob('dod_game.c')\n",
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
	replace_once(
		dlls / "subs.cpp",
	'LINK_ENTITY_TO_CLASS( dod_control_point, CPointEntity )\n',
	"",
	)
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
