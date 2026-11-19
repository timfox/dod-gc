/* HLSDK bridge for the Day of Defeat rules core. */
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "gamerules.h"
#include "teamplay_gamerules.h"
#include "dod_runtime.h"
#include "usercmd.h"
#include "client.h"
#include "dod_hud.h"
#include "dod_game.h"
#include "dod_weapons.h"
#include "dod_bot.h"
#include "dod_bots.h"
#include "animation.h"
#include "pm_defs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Round length when mp_timelimit is 0, and how many rounds win a match. */
static cvar_t dod_roundtime = { "dod_roundtime", "600", FCVAR_SERVER };
static cvar_t dod_rounds = { "dod_rounds", "2", FCVAR_SERVER };
/* dod_spectate 1: the local human joins as a spectator instead of picking a side,
 * to watch the bots fight. dod_spec_auto 1: a director moves the camera between
 * the bots by itself (any manual target pick pauses it for a while). */
static cvar_t dod_spectate = { "dod_spectate", "0", FCVAR_SERVER };
/* dod_gc_join "<seconds> <team> <class>" (debug, off when empty; needs dod_spectate 1): the local human, spectating the bots, takes that side
 * (1 Allies, 2 Axis) and class (1 rifleman .. 6 rocket) once the map has run that many seconds: a human joining a round in progress. */
static cvar_t dod_gc_join = { "dod_gc_join", "", FCVAR_SERVER };
static BOOL g_join_done[DOD_MAX_PLAYERS + 1];
static cvar_t dod_spec_auto = { "dod_spec_auto", "1", FCVAR_SERVER };
/* dod_gc_autojoin "<team> <class>" (debug, off when empty): the local human takes that side and class a couple of
 * seconds after connecting, with the steps the menus take, so an automated run (no pad) gets a playable human and a
 * view model. team 1 Allies / 2 Axis, class 1 rifleman .. 6 (DodClass). */
static cvar_t dod_gc_autojoin = { "dod_gc_autojoin", "", FCVAR_SERVER };
/* dod_gc_autotele "<x> <y> <z>" (test only, off when empty): a few seconds after the human has joined, park them there in
 * noclip, so a packet-size or view test can look from a chosen open spot instead of the team spawn. */
static cvar_t dod_gc_autotele = { "dod_gc_autotele", "", FCVAR_SERVER };
static float g_autotele_seen[DOD_MAX_PLAYERS + 1];
static BOOL g_autotele_done[DOD_MAX_PLAYERS + 1];
static float g_autojoin_seen[DOD_MAX_PLAYERS + 1];
static BOOL g_autojoin_spec_done[DOD_MAX_PLAYERS + 1];
/* Team body models (models/player/<name>/<name>.mdl, split onto the disc as
 * gc_studio/<name>.mdl). A map cfg can switch to us-para/axis-para/brit-inf. */
/* Paratrooper teams: -1 follows the map (Dod_ParatroopersForMap), otherwise a bit mask: 1 Allies, 2 Axis. */
static cvar_t dod_paras = { "dod_paras", "-1", FCVAR_SERVER };
/* Class limits (retail mp_limit<side><gun>, -1 unlimited, 0 off) and the class masks (mp_alliesclasses / mp_axisclasses, -1 all, else
 * bit 1 << class). The names are retail's; the class a gun belongs to is this port's six classes. A side with no gun for a class
 * (the British have no machine gunner) has no cvar for it. */
static const struct DodLimitCvar { const char *name; int team; int player_class; } g_limit_cvars[] = {
	{ "mp_limitalliesgarand", DOD_TEAM_ALLIES, DOD_CLASS_RIFLEMAN }, { "mp_limitalliesthompson", DOD_TEAM_ALLIES, DOD_CLASS_ASSAULT },
	{ "mp_limitalliesbar", DOD_TEAM_ALLIES, DOD_CLASS_SUPPORT }, { "mp_limitalliesspring", DOD_TEAM_ALLIES, DOD_CLASS_SNIPER },
	{ "mp_limitallies30cal", DOD_TEAM_ALLIES, DOD_CLASS_MG }, { "mp_limitalliesbazooka", DOD_TEAM_ALLIES, DOD_CLASS_ROCKET },
	{ "mp_limitbritlight", DOD_TEAM_ALLIES, DOD_CLASS_RIFLEMAN }, { "mp_limitbritassault", DOD_TEAM_ALLIES, DOD_CLASS_ASSAULT },
	{ "mp_limitbritmg", DOD_TEAM_ALLIES, DOD_CLASS_SUPPORT }, { "mp_limitbritsniper", DOD_TEAM_ALLIES, DOD_CLASS_SNIPER },
	{ "mp_limitbritpiat", DOD_TEAM_ALLIES, DOD_CLASS_ROCKET },
	{ "mp_limitaxiskar", DOD_TEAM_AXIS, DOD_CLASS_RIFLEMAN }, { "mp_limitaxismp40", DOD_TEAM_AXIS, DOD_CLASS_ASSAULT },
	{ "mp_limitaxismp44", DOD_TEAM_AXIS, DOD_CLASS_SUPPORT }, { "mp_limitaxisscopedkar", DOD_TEAM_AXIS, DOD_CLASS_SNIPER },
	{ "mp_limitaxismg42", DOD_TEAM_AXIS, DOD_CLASS_MG }, { "mp_limitaxispschreck", DOD_TEAM_AXIS, DOD_CLASS_ROCKET },
};
#define DOD_LIMIT_CVARS ( (int)( sizeof( g_limit_cvars ) / sizeof( g_limit_cvars[0] ) ) )
static cvar_t g_limit_cvar_storage[DOD_LIMIT_CVARS];
static cvar_t mp_alliesclasses = { "mp_alliesclasses", "-1", FCVAR_SERVER };
static cvar_t mp_axisclasses = { "mp_axisclasses", "-1", FCVAR_SERVER };
/* mp_spawnbazookas (every retail map cfg sets it): whether the map spawns bazooka pickups at the start; read by nothing yet, registered so the cfg line is not an unknown command */
/* The base of the respawn waves in seconds, times the side's map factor (retail's base is not recorded; this is the port's own). */
static cvar_t dod_respawn_delay = { "dod_respawn_delay", "10", FCVAR_SERVER };
static cvar_t mp_spawnbazookas = { "mp_spawnbazookas", "1", FCVAR_SERVER };

static cvar_t dod_allies_body = { "dod_allies_body", "us-inf", FCVAR_SERVER };
static cvar_t dod_axis_body = { "dod_axis_body", "axis-inf", FCVAR_SERVER };
static BOOL g_dod_cvars_registered;
#define DOD_SPEC_SLOTS 8	/* spectator director state for client slots 1..8 */
static float g_spec_hold_until[DOD_SPEC_SLOTS + 1];
static float g_spec_target_since[DOD_SPEC_SLOTS + 1];
static int g_spec_last_target[DOD_SPEC_SLOTS + 1];
/* Eye of the player the spectator was following, so free-fly starts there and not inside the map. */
static Vector g_spec_eye[DOD_SPEC_SLOTS + 1];
static BOOL g_spec_eye_valid[DOD_SPEC_SLOTS + 1];
/* Last free-fly position known to be in the open: the camera never moves into the world. */
static Vector g_spec_good[DOD_SPEC_SLOTS + 1];
static BOOL g_spec_good_valid[DOD_SPEC_SLOTS + 1];
static float g_spec_next_think;
/* dod_spectate 1: InitHUD asks for the join, SpectatorThink makes it. The first
 * InitHUD runs inside CBasePlayer::Spawn, whose tail (game_playerspawn, the kit,
 * observer flags reset) undid a join made there, and bots are not yet marked as
 * bots at that point. */
static BOOL g_spec_join_pending[DOD_MAX_PLAYERS + 1];
static int gmsgDodHud;
static int gmsgDodBoard;
static int gmsgDodVoice;
static int gmsgDodKill;
static float g_dod_next_hud;
static int g_dod_seen_restart;
static int g_dod_seen_match;

#define DOD_POINT_SCORE_PER_MINUTE 0.0f	/* retail has no continuous score: capture points and timed points (Dod_SetObjectiveTeamPoints) */

extern int gmsgShowMenu;
static DodTeam DodGC_TeamForPlayer( CBasePlayer *pPlayer ); // used by the team call-outs above its definition
extern int gmsgScoreInfo;

static int g_dod_menu_state[DOD_MAX_PLAYERS + 1];
/* Join flow (retail): a human starts on the side menu as an observer, picks a side, picks a class and
 * is spawned on that side with no death. g_dod_joined: the human has been through that once.
 * g_dod_pending_team: the side picked at the menu while still an observer (observers read as spectators). */
static int g_dod_joined[DOD_MAX_PLAYERS + 1];
static int g_dod_pending_team[DOD_MAX_PLAYERS + 1];
static int g_dod_winner_announced;

/* The winning side's fanfare, to every human at the start of the intermission (2D, not attenuated). It streams from ARAM at full length. */
static void DodGC_PlayRoundEndMusic( DodTeam winner )
{
	const char *path = Dod_RoundEndSound( winner, Dod_GetAlliesCountry() == DOD_COUNTRY_BRITISH );
	if( !path )
		return;
	/* the map's own win relay plays this file through an ambient_generic when the engine keeps that entity: then this is only the fallback */
	for( CBaseEntity *amb = NULL; ( amb = UTIL_FindEntityByClassname( amb, "ambient_generic" ) ) != NULL; )
		if( amb->pev->message && FStrEq( STRING( amb->pev->message ), path ) )
		{
			ALERT( at_console, "DoD round-end fanfare: the map's ambient_generic plays %s\n", path );
			return;
		}
	PRECACHE_SOUND( (char *)path );
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *player = (CBasePlayer *)UTIL_PlayerByIndex( i );
		if( player && player->IsPlayer() && !( player->pev->flags & FL_FAKECLIENT ) )
			EMIT_SOUND_DYN( ENT( player->pev ), CHAN_STATIC, path, 1.0f, ATTN_NONE, 0, 100 );
	}
	ALERT( at_console, "DoD round-end fanfare: %s\n", path );
}
static DodTeam DodGC_TeamForPlayer( CBasePlayer *pPlayer );
/* Retail announcements spoken by the side's own announcer and heard by that side only: "uspointcaptured" and "usstartround"
 * (us / brit for the Allies by country, ger for the Axis). 2D, not attenuated. */
static void DodGC_PlayTeamAnnouncement( DodTeam team, const char *dir, const char *what )
{
	char path[64];
	const char *side = team == DOD_TEAM_AXIS ? "ger" : ( Dod_GetAlliesCountry() == DOD_COUNTRY_BRITISH ? "brit" : "us" );
	if( team != DOD_TEAM_ALLIES && team != DOD_TEAM_AXIS )
		return;
	snprintf( path, sizeof( path ), "%s/%s%s.wav", dir, side, what );
	PRECACHE_SOUND( path );
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *player = (CBasePlayer *)UTIL_PlayerByIndex( i );
		if( player && player->IsPlayer() && !( player->pev->flags & FL_FAKECLIENT ) && DodGC_TeamForPlayer( player ) == team )
			EMIT_SOUND_DYN( ENT( player->pev ), CHAN_STATIC, path, 1.0f, ATTN_NONE, 0, 100 );
	}
}
static float g_dod_next_status;
static int g_dod_death_seq[DOD_MAX_PLAYERS + 1];	/* the retail death sequence picked when the player was killed */
static char g_spawn_map[64];	/* the map whose spawn points are cached (DodGC_LoadSpawns) */
/* Voice lines: nobody speaks more than once every 2 s, automatic lines (bots spotting, grenades) once every 8 s. */
#define DOD_VOICE_COOLDOWN 2.0f
#define DOD_VOICE_AUTO_COOLDOWN 8.0f
static float g_dod_voice_next[DOD_MAX_PLAYERS + 1];
/* The pad voice menu (Y): the selected line + 1 while it is open, 0 when closed; it closes itself after 8 s. */
#define DOD_VOICE_MENU_SECONDS 8.0f
static int g_dod_voice_menu[DOD_MAX_PLAYERS + 1];
static float g_dod_voice_menu_until[DOD_MAX_PLAYERS + 1];
static float g_dod_voice_auto_next[DOD_MAX_PLAYERS + 1];
/* Scoreboard on the lean HUD (D-pad right, "dod_score"): shown for DOD_BOARD_SECONDS, refreshed once a second,
 * and pressed again it goes away. */
#define DOD_BOARD_SECONDS 8.0f
static float g_dod_board_until[DOD_MAX_PLAYERS + 1];
static float g_dod_next_board;

static int DodGC_PlayerIndex( CBasePlayer *pPlayer )
{
	if( !pPlayer )
		return 0;
	return pPlayer->entindex();
}

static void DodGC_SendMenu( CBasePlayer *pPlayer, int slots, const char *text )
{
	if( !pPlayer )
		return;
	MESSAGE_BEGIN( MSG_ONE, gmsgShowMenu, NULL, pPlayer->edict() );
		WRITE_SHORT( slots );
		WRITE_CHAR( -1 );
		WRITE_BYTE( 0 );
		WRITE_STRING( text ? text : "" );
	MESSAGE_END();
}

static void DodGC_ShowTeamMenu( CBasePlayer *pPlayer )
{
	const int index = DodGC_PlayerIndex( pPlayer );
	if( index > 0 && index <= DOD_MAX_PLAYERS )
		g_dod_menu_state[index] = 1;
	DodGC_SendMenu( pPlayer, 0x0023,
		"\\yDAY OF DEFEAT - CHOOSE A SIDE\n\\w\n"
		"1. Allies\n2. Axis\n6. Spectate\n\n"
		"Choose a side to continue.\n" );
}

static DodTeam DodGC_TeamForPlayer( CBasePlayer *pPlayer );

/* The side the class menu is for: the pending pick while the player is still an observer. */
static DodTeam DodGC_MenuTeam( CBasePlayer *pPlayer )
{
	const int index = DodGC_PlayerIndex( pPlayer );
	if( index >= 1 && index <= DOD_MAX_PLAYERS && g_dod_pending_team[index] )
		return (DodTeam)g_dod_pending_team[index];
	return DodGC_TeamForPlayer( pPlayer );
}

/* Bring the core's class limits up to date with the cvars (they can change at any time), then ask whether a class is open. The British
 * kit reads the mp_limitbrit* cvars, the American kit the mp_limitallies* ones. */
static void DodGC_SyncClassLimits( void )
{
	const BOOL british = Dod_GetAlliesCountry() == DOD_COUNTRY_BRITISH;
	int k;
	for( int c = DOD_CLASS_RIFLEMAN; c < DOD_CLASS_COUNT; c++ ) {
		Dod_SetClassLimit( DOD_TEAM_ALLIES, (DodClass)c, -1 );
		Dod_SetClassLimit( DOD_TEAM_AXIS, (DodClass)c, -1 );
	}
	for( k = 0; k < DOD_LIMIT_CVARS; k++ ) {
		const BOOL brit_cvar = !strncmp( g_limit_cvars[k].name, "mp_limitbrit", 12 );
		if( g_limit_cvars[k].team == DOD_TEAM_ALLIES && brit_cvar != british )
			continue;
		Dod_SetClassLimit( (DodTeam)g_limit_cvars[k].team, (DodClass)g_limit_cvars[k].player_class,
			(int)CVAR_GET_FLOAT( g_limit_cvars[k].name ) );
	}
	Dod_SetClassMask( DOD_TEAM_ALLIES, Dod_RetailClassMask( DOD_TEAM_ALLIES, (int)CVAR_GET_FLOAT( "mp_alliesclasses" ) ) );
	Dod_SetClassMask( DOD_TEAM_AXIS, Dod_RetailClassMask( DOD_TEAM_AXIS, (int)CVAR_GET_FLOAT( "mp_axisclasses" ) ) );
}

static BOOL DodGC_ClassOpen( DodTeam team, DodClass player_class, int player_index )
{
	DodGC_SyncClassLimits();
	return Dod_ClassAvailable( team, player_class, player_index ) ? TRUE : FALSE;
}

static void DodGC_ShowClassMenu( CBasePlayer *pPlayer )
{
	const int index = DodGC_PlayerIndex( pPlayer );
	const DodTeam team = DodGC_MenuTeam( pPlayer );
	char text[256];
	int len;
	if( index > 0 && index <= DOD_MAX_PLAYERS )
		g_dod_menu_state[index] = 2;
	len = snprintf( text, sizeof( text ), "\\y%s - CHOOSE A CLASS\n\\w",
		team == DOD_TEAM_AXIS ? "AXIS" : "ALLIES" );
	for( int c = DOD_CLASS_RIFLEMAN; c < DOD_CLASS_COUNT && len < (int)sizeof( text ); c++ )
		len += snprintf( text + len, sizeof( text ) - len, "%d. %s (%s)%s\n", c,
			Dod_TeamClassName( team, (DodClass)c ), Dod_TeamClassWeapon( team, (DodClass)c ),
			DodGC_ClassOpen( team, (DodClass)c, index ) ? "" : " - full" );
	DodGC_SendMenu( pPlayer, 0x003f, text );
}

static void DodGC_ClearMenu( CBasePlayer *pPlayer )
{
	const int index = DodGC_PlayerIndex( pPlayer );
	if( index > 0 && index <= DOD_MAX_PLAYERS )
		g_dod_menu_state[index] = 0;
	DodGC_SendMenu( pPlayer, 0, "" );
}

static DodTeam DodGC_TeamForPlayer( CBasePlayer *pPlayer )
{
	if( !pPlayer )
		return DOD_TEAM_UNASSIGNED;
	/* StartObserver blanks the HLSDK team name, so a spectator is read from the flag. */
	if( pPlayer->IsObserver() )
		return DOD_TEAM_SPECTATOR;
	return (DodTeam)Dod_TeamFromName( pPlayer->m_szTeamName );
}

/* Called by SendWeaponAnim (patched in by the integrator): the number a weapon asks for becomes the sequence its
 * retail view model has. Outside a DoD game the stock numbers stand. */
int DodGC_RemapWeaponAnim( const char *classname, int anim )
{
	/* each distinct (weapon, asked, sent) is logged once: enough to see every weapon's fire, reload and draw */
	static char seen_class[64][24];
	static short seen_anim[64], seen_sent[64];
	static int seen_count;
	int mapped, i;
	if( !Dod_GameIsActive() )
		return anim;
	mapped = Dod_RemapAnim( classname, anim );
	if( mapped != anim && classname && seen_count < 64 )
	{
		for( i = 0; i < seen_count; i++ )
			if( !strncmp( seen_class[i], classname, sizeof( seen_class[0] ) - 1 ) && seen_anim[i] == anim && seen_sent[i] == mapped )
				break;
		if( i == seen_count )
		{
			strncpy( seen_class[i], classname, sizeof( seen_class[0] ) - 1 );
			seen_class[i][sizeof( seen_class[0] ) - 1] = '\0';
			seen_anim[i] = (short)anim;
			seen_sent[i] = (short)mapped;
			seen_count++;
			ALERT( at_console, "DoD anim: %s asked for %d, sends %d\n", classname, anim, mapped );
		}
	}
	return mapped;
}

/* trigger_hurt's DoD team flags (see Dod_HurtAppliesToTeam); called from CBaseTrigger::HurtTouch by the integrator's patch. */
BOOL DodGC_TriggerHurtApplies( int spawnflags, CBaseEntity *pOther )
{
	if( !pOther || !pOther->IsPlayer() || !( spawnflags & ( DOD_HURT_AXIS_ONLY | DOD_HURT_ALLIES_ONLY ) ) )
		return TRUE;
	return Dod_HurtAppliesToTeam( spawnflags, DodGC_TeamForPlayer( (CBasePlayer *)pOther ) ) ? TRUE : FALSE;
}

static void DodGC_EnsurePlayer( CBasePlayer *pPlayer )
{
	const int index = DodGC_PlayerIndex( pPlayer );
	const DodPlayerState *state;
	if( index < 1 || index > DOD_MAX_PLAYERS )
		return;
	state = Dod_GetPlayer( index );
	if( !state || !state->connected )
		Dod_PlayerConnect( index );
	Dod_AssignPlayerTeam( index, DodGC_TeamForPlayer( pPlayer ) );
}

/* Become an observer: no class, no gun, no spawn, no collision. Only the local human does this
 * (a bot never watches), and no death or frag is recorded. announce: tell them they are spectating
 * (not when they are only waiting at the side menu). */
static void DodGC_EnterObserver( CBasePlayer *pPlayer, BOOL announce )
{
	/* StartObserver -> RemoveAllItems -> UpdateClientData calls InitHUD again
	 * (m_fGameHUDInitialized is only set after InitHUD returns), and IsObserver()
	 * is still false at that point, so InitHUD came straight back here. With
	 * dod_spectate 1 that recursed ~770 times and ran the 128 KiB main stack
	 * down into the engine's file system statics. Ignore the nested call. */
	static bool joining;
	if( !pPlayer || DodBots_IsBot( pPlayer ) || joining )
		return;
	joining = true;
	DodGC_ClearMenu( pPlayer );
	if( !pPlayer->IsObserver() ) {
		pPlayer->StartObserver( pPlayer->pev->origin, pPlayer->pev->v_angle );
		pPlayer->pev->iuser1 = 0;	/* force Observer_SetMode to pick the mode and a target */
		pPlayer->Observer_SetMode( DOD_SPEC_CHASE_FREE );
	}
	joining = false;
	DodGC_EnsurePlayer( pPlayer );
	ALERT( at_console, "DoD spectator joined: mode=%d target=%d team=%d\n", pPlayer->pev->iuser1, pPlayer->pev->iuser2, (int)DodGC_TeamForPlayer( pPlayer ) );
	ClientPrint( pPlayer->pev, HUD_PRINTTALK, "You are spectating. Fire/Aim: next/previous player, Jump: camera mode.\n" );
	if( announce )
		ClientPrint( pPlayer->pev, HUD_PRINTTALK, "You are spectating. Fire/Aim: next/previous player, Jump: camera mode.\n" );
}

static void DodGC_JoinSpectator( CBasePlayer *pPlayer )
{
	DodGC_EnterObserver( pPlayer, TRUE );
}

static void DodGC_ShowTeamMenu( CBasePlayer *pPlayer );

/* A human who has not joined yet waits at the side menu as an observer instead of being dropped
 * onto a team with a default kit. */
static void DodGC_HoldForJoin( CBasePlayer *pPlayer )
{
	const int index = DodGC_PlayerIndex( pPlayer );
	DodGC_EnterObserver( pPlayer, FALSE );
	if( index >= 1 && index <= DOD_MAX_PLAYERS )
		g_dod_pending_team[index] = 0;
	DodGC_ShowTeamMenu( pPlayer );
	ALERT( at_console, "DoD join: %s waits at the side menu\n", STRING( pPlayer->pev->netname ) );
}

/* The body the engine draws for a player comes from the "model" userinfo key
 * (models/player/<model>/<model>.mdl); the server entity stays models/player.mdl
 * for hulls. Bots are fake clients with their own infobuffer, so this covers them. */
static const char *DodGC_BodyForTeam( DodTeam team )
{
	if( team == DOD_TEAM_AXIS ) {
		const char *body = CVAR_GET_STRING( "dod_axis_body" );
		if( Dod_TeamIsParatroopers( DOD_TEAM_AXIS ) && !strcmp( body, "axis-inf" ) )
			return "axis-para";
		return body;
	}
	if( team == DOD_TEAM_ALLIES ) {
		const char *body = CVAR_GET_STRING( "dod_allies_body" );
		/* On the British maps the Allies are British unless the map cfg named another model. */
		if( Dod_GetAlliesCountry() == DOD_COUNTRY_BRITISH && !strcmp( body, "us-inf" ) )
			return "brit-inf";
		if( Dod_TeamIsParatroopers( DOD_TEAM_ALLIES ) && !strcmp( body, "us-inf" ) )
			return "us-para";
		return body;
	}
	return NULL;
}

const char *DodGC_BodyModelForTeam( DodTeam team )
{
	return DodGC_BodyForTeam( team );
}

static void DodGC_ApplyBody( CBasePlayer *pPlayer )
{
	const char *body;
	char *info;
	if( !pPlayer || pPlayer->IsObserver() )
		return;
	body = DodGC_BodyForTeam( DodGC_TeamForPlayer( pPlayer ) );
	if( !body || !body[0] )
		return;
	info = g_engfuncs.pfnGetInfoKeyBuffer( pPlayer->edict() );
	if( !info )
		return;
	if( !strcmp( g_engfuncs.pfnInfoKeyValue( info, "model" ), body ) )
		return;
	g_engfuncs.pfnSetClientKeyValue( pPlayer->entindex(), info, "model", (char *)body );
	ALERT( at_console, "DoD body %s -> %s\n", STRING( pPlayer->pev->netname ), body );
}

#if defined( __GAMECUBE__ )
/* Called from ServerActivate (scripts/integrate-dod-gameplay.py hooks it), after the map's entities have spawned and
 * before the server goes active: precaching the bodies in the gamerules constructor loaded them ahead of the entities
 * and left caen 34 KB short, so every func_wall of the map was skipped (kept=0 skipped=289). Stock DoD precaches the
 * player models at map start; the engine promotes only precached models (Mod_GCPromoteMapModels). */
void DodGC_PrecacheTeamBodies( void )
{
	const DodTeam teams[2] = { DOD_TEAM_ALLIES, DOD_TEAM_AXIS };
	for( int t = 0; t < 2; t++ ) {
		const char *body = DodGC_BodyForTeam( teams[t] );
		char path[96];
		if( !body || !body[0] )
			continue;
		snprintf( path, sizeof( path ), "models/player/%s/%s.mdl", body, body );
		PRECACHE_MODEL( path );
	}
}
#endif

static int g_dod_weaponlist_resent[DOD_MAX_PLAYERS + 1];

static void DodGC_ApplyClass( CBasePlayer *pPlayer, DodClass player_class )
{
	if( pPlayer && pPlayer->IsObserver() )
		return;	/* a spectator has no class */
	int index = DodGC_PlayerIndex( pPlayer );
	if( !pPlayer || index < 1 || index > DOD_MAX_PLAYERS )
		return;
	DodGC_EnsurePlayer( pPlayer );
	if( !Dod_SelectPlayerClass( index, player_class ) )
		return;

	/* The WeaponList burst (UpdateClientData's m_fKnownItem block) first runs
	 * from ClientPutInServer, before the engine marks the client spawned, and
	 * the engine drops user messages to unspawned clients: the client never
	 * learned the weapon names, so the retail ammo HUD and weapon slots had
	 * nothing. Send it once more when the player first gets a kit. */
	if( !g_dod_weaponlist_resent[index] && !DodBots_IsBot( pPlayer ) ) {
		g_dod_weaponlist_resent[index] = 1;
		pPlayer->m_fKnownItem = FALSE;
	}

	/* Melee (the HL1 crowbar wearing the knife/spade models), the class gun
	 * from the DoD weapon table, a Colt/Luger for the heavy classes, and
	 * grenades. Guns are real DoD weapons (dod_weapon.cpp); their models and
	 * sounds are loaded here, when granted, never at map load. */
	{
		const DodTeam team = DodGC_TeamForPlayer( pPlayer );
		const DodWeaponDef *weapon = Dod_ClassWeaponDef( team, player_class );
		/* A bot has no view: its guns must not precache viewmodels. */
		DodBots_SetGranting( DodBots_IsBot( pPlayer ) );
		const DodWeaponDef *sidearm = Dod_ClassSidearmDef( team, player_class );
		DodLoadout load;
		DodGrenadeDef grenade;

		pPlayer->RemoveAllItems( FALSE );
		/* RemoveAllItems -> UpdateClientData can run the first InitHUD, which
		 * with dod_spectate 1 turns the human into a spectator right here: give
		 * an observer no kit and no body. */
		if( pPlayer->IsObserver() ) {
			DodBots_SetGranting( FALSE );
			return;
		}
		/* A bot gets only its primary weapon (Dod_KitPlan lean): every weapon is an edict and a bot never
		 * switches, throws or drops. The primary comes last either way, so it is the best weapon and is
		 * the active item at spawn. */
		DodKitPlan kit;
		Dod_KitPlan( team, player_class, DodBots_IsBot( pPlayer ) ? 1 : 0, &kit );
		if( kit.crowbar )
			pPlayer->GiveNamedItem( "weapon_crowbar" );
		if( sidearm && kit.sidearm ) {
			pPlayer->GiveNamedItem( sidearm->classname );
			pPlayer->GiveAmmo( sidearm->reserve, sidearm->ammo, sidearm->ammo_max );
		}
		if( weapon ) {
			/* Last, so it is the best weapon and gets drawn. */
			pPlayer->GiveNamedItem( weapon->classname );
			pPlayer->GiveAmmo( weapon->reserve, weapon->ammo, weapon->ammo_max );
		}
		if( kit.grenade && Dod_GetLoadout( team, player_class, &load ) && load.grenades > 0 ) {
			/* The weapon spawns with one grenade; top up to the loadout. */
			pPlayer->GiveNamedItem( "weapon_handgrenade" );
			if( load.grenades > 1 && Dod_GetGrenade( team, &grenade ) )
				pPlayer->GiveAmmo( load.grenades - 1, "Hand Grenade", grenade.max_carry );
		}
		DodBots_SetGranting( FALSE );
	}
	DodGC_ApplyBody( pPlayer );
	Dod_StaminaReset( index );
	{
		/* the sub-models of the retail player model that make up this class (see Dod_PlayerBodyValue) */
		const char *model = DodGC_BodyForTeam( DodGC_TeamForPlayer( pPlayer ) );
		pPlayer->pev->body = Dod_PlayerBodyValue( model, player_class, index );
		ALERT( at_console, "DoD player body %s: %s %s body %d\n", STRING( pPlayer->pev->netname ), model ? model : "-", Dod_ClassName( player_class ), pPlayer->pev->body );
	}
	DodGC_ClearMenu( pPlayer );
}

/* Publish this DLL's playermove_t layout (size and array caps, from its own copy of pm_defs.h) so the engine can compare
 * it with its own before PM_Init runs. A mismatch (the DLL built with another XASH_GC_PM_MAX_PHYSENTS / MAX_MOVEENTS than
 * the engine) used to crash in PM_InitTextureTypes; the engine now stops with a message that names both layouts. Called
 * from GameDLLInit (integrate-dod-gameplay.py hooks it in). */
void DodGC_RegisterPmLayout( void )
{
	static char layout[32];
	static cvar_t gc_pm_layout_dll = { "gc_pm_layout_dll", layout, 0 };

	snprintf( layout, sizeof( layout ), "%d,%d,%d", (int)sizeof( playermove_t ), MAX_PHYSENTS, MAX_MOVEENTS );
	CVAR_REGISTER( &gc_pm_layout_dll );
}

CHalfLifeDoD::CHalfLifeDoD()
{
	DodBots_Init();
	float round_seconds;
	if( !g_dod_cvars_registered ) {
		Dod_CvarRegisterKeep( &dod_roundtime );
		Dod_CvarRegisterKeep( &dod_rounds );
		Dod_CvarRegisterKeep( &dod_spectate );
		Dod_CvarRegisterKeep( &dod_spec_auto );
		Dod_CvarRegisterKeep( &dod_gc_autojoin );
		Dod_CvarRegisterKeep( &dod_gc_autotele );
		Dod_CvarRegisterKeep( &dod_gc_join );
		Dod_CvarRegisterKeep( &dod_paras );
		for( int k = 0; k < DOD_LIMIT_CVARS; k++ ) {
			g_limit_cvar_storage[k].name = g_limit_cvars[k].name;
			g_limit_cvar_storage[k].string = (char *)"-1";
			g_limit_cvar_storage[k].flags = FCVAR_SERVER;
			Dod_CvarRegisterKeep( &g_limit_cvar_storage[k] );
		}
		Dod_CvarRegisterKeep( &mp_alliesclasses );
		Dod_CvarRegisterKeep( &mp_axisclasses );
		Dod_CvarRegisterKeep( &mp_spawnbazookas );
		Dod_CvarRegisterKeep( &dod_respawn_delay );
		Dod_CvarRegisterKeep( &dod_allies_body );
		Dod_CvarRegisterKeep( &dod_axis_body );
		g_dod_cvars_registered = TRUE;
	}
	Dod_GameInit();
	g_spawn_map[0] = '\0';	/* the spawn points of the last map are gone */
	/* before the weapons precache: the British maps give the Allies the British kit (see dod_weapons.h) */
	Dod_SetAlliesCountry( Dod_AlliesCountryForMap( STRING( gpGlobals->mapname ) ) );
	ALERT( at_console, "DoD: the Allies are %s on %s\n", Dod_GetAlliesCountry() == DOD_COUNTRY_BRITISH ? "British" : "American",
		STRING( gpGlobals->mapname ) );
	{
		int paras = (int)CVAR_GET_FLOAT( "dod_paras" );
		if( paras < 0 )
			paras = Dod_ParatroopersForMap( STRING( gpGlobals->mapname ) );
		Dod_SetParatroopers( paras & 1, paras & 2 );
		ALERT( at_console, "DoD: paratroopers: Allies %s, Axis %s\n", paras & 1 ? "yes" : "no", paras & 2 ? "yes" : "no" );
	}
	DodFilm_Init();	/* after the country and paratroopers: they pick the actors' bodies */
	Dod_ClearObjectives();
	round_seconds = CVAR_GET_FLOAT( "mp_timelimit" ) * 60.0f;
	if( round_seconds <= 0.0f )
		round_seconds = CVAR_GET_FLOAT( "dod_roundtime" );
	Dod_SetRoundsToWin( (int)CVAR_GET_FLOAT( "dod_rounds" ) );
	Dod_StartRound( round_seconds );
	g_dod_seen_restart = 0;
	g_dod_seen_match = 0;
	g_dod_next_hud = 0.0f;
	/* Registered here, before any client connects, so the client can hook it. */
	gmsgDodHud = REG_USER_MSG( "DodHud", -1 );
	gmsgDodBoard = REG_USER_MSG( "DodBoard", -1 );
	gmsgDodVoice = REG_USER_MSG( "DodVoice", -1 );
	gmsgDodKill = REG_USER_MSG( "DodKill", -1 );
	memset( g_dod_menu_state, 0, sizeof( g_dod_menu_state ) );
	memset( g_dod_joined, 0, sizeof( g_dod_joined ) );
	memset( g_dod_weaponlist_resent, 0, sizeof( g_dod_weaponlist_resent ) );
	memset( g_dod_pending_team, 0, sizeof( g_dod_pending_team ) );
	g_dod_winner_announced = 0;
	g_dod_next_status = 0.0f;
	memset( g_spec_hold_until, 0, sizeof( g_spec_hold_until ) );
	memset( g_spec_target_since, 0, sizeof( g_spec_target_since ) );
	memset( g_spec_last_target, 0, sizeof( g_spec_last_target ) );
	g_spec_next_think = 0.0f;
	memset( g_spec_join_pending, 0, sizeof( g_spec_join_pending ) );
	memset( g_autojoin_seen, 0, sizeof( g_autojoin_seen ) );
	memset( g_autotele_seen, 0, sizeof( g_autotele_seen ) );
	memset( g_autotele_done, 0, sizeof( g_autotele_done ) );
	memset( g_autojoin_spec_done, 0, sizeof( g_autojoin_spec_done ) );
	memset( g_join_done, 0, sizeof( g_join_done ) );
}

/* The first open class for a side, starting at the one asked for: a bot's pick when limits turn its choice away. */
DodClass DodGC_OpenClass( CBasePlayer *pPlayer, DodTeam team, DodClass wanted )
{
	const int index = DodGC_PlayerIndex( pPlayer );
	for( int n = 0; n < DOD_CLASS_COUNT - 1; n++ ) {
		const DodClass c = (DodClass)( DOD_CLASS_RIFLEMAN + ( wanted - DOD_CLASS_RIFLEMAN + n ) % ( DOD_CLASS_COUNT - 1 ) );
		if( DodGC_ClassOpen( team, c, index ) )
			return c;
	}
	return wanted;	/* every class is full or off: the limits are ignored for this spawn, as retail does */
}

void DodGC_BotJoin( CBasePlayer *pPlayer, DodTeam team, DodClass player_class )
{
	const int index = DodGC_PlayerIndex( pPlayer );
	if( !pPlayer || index < 1 || index > DOD_MAX_PLAYERS )
		return;
	DodGC_EnsurePlayer( pPlayer );
	if( g_pGameRules && stricmp( pPlayer->m_szTeamName, Dod_TeamName( team ) ) )
		( (CHalfLifeDoD *)g_pGameRules )->ChangePlayerTeam( pPlayer, Dod_TeamName( team ), TRUE, FALSE );
	Dod_AssignPlayerTeam( index, team );
	/* ClientPutInServer already spawned the bot with the default kit. A second Spawn() gives
	 * the default crowbar again; a duplicate cannot attach (AddDuplicate fails), so it was left
	 * loose on the floor at the bot's feet. Clear the inventory first. */
	pPlayer->RemoveAllItems( FALSE );
	/* ClientPutInServer spawned the bot before it had a team (default spot, no kit): spawn
	 * again now so it uses its own team's info_player_* spawn points. */
	pPlayer->Spawn();
	Dod_AssignPlayerTeam( index, team );	/* Spawn() resets the DoD team record */
	DodGC_ApplyClass( pPlayer, player_class );
}

/* Pick a side at the menu or with dod_team. A player who is playing and alive and changes sides
 * dies for it (retail: a suicide, no frag penalty); one who is still at the menu or already dead
 * just switches. The class menu follows. */
static void DodGC_PickTeam( CBasePlayer *pPlayer, DodTeam team )
{
	const int index = DodGC_PlayerIndex( pPlayer );
	const BOOL playing = !pPlayer->IsObserver() && index >= 1 && index <= DOD_MAX_PLAYERS && g_dod_joined[index];
	if( index < 1 || index > DOD_MAX_PLAYERS )
		return;
	if( stricmp( pPlayer->m_szTeamName, Dod_TeamName( team ) ) )
		( (CHalfLifeDoD *)g_pGameRules )->ChangePlayerTeam( pPlayer, Dod_TeamName( team ), playing && pPlayer->IsAlive(), FALSE );
	g_dod_pending_team[index] = pPlayer->IsObserver() ? (int)team : 0;
	if( !pPlayer->IsObserver() )
		Dod_AssignPlayerTeam( index, team );
	DodGC_ShowClassMenu( pPlayer );
}

/* A human takes a side and a class with the same steps the menus take (the test harness uses this; DodGC_BotJoin is for
 * fake clients and leaves a human in observer mode). */
void DodGC_HumanJoin( CBasePlayer *pPlayer, DodTeam team, DodClass player_class );

/* Pick a class. At the menu (an observer who has not spawned) it spawns the player on the picked
 * side with that class at once, with no death. While playing it applies at the next respawn
 * (retail: no suicide for a class change). */
static void DodGC_PickClass( CBasePlayer *pPlayer, DodClass player_class );

void DodGC_HumanJoin( CBasePlayer *pPlayer, DodTeam team, DodClass player_class )
{
	DodGC_PickTeam( pPlayer, team );
	DodGC_PickClass( pPlayer, player_class );
}

static void DodGC_PickClass( CBasePlayer *pPlayer, DodClass player_class )
{
	const int index = DodGC_PlayerIndex( pPlayer );
	const DodTeam team = DodGC_MenuTeam( pPlayer );
	if( index < 1 || index > DOD_MAX_PLAYERS || player_class <= DOD_CLASS_NONE || player_class >= DOD_CLASS_COUNT )
		return;
	if( team != DOD_TEAM_ALLIES && team != DOD_TEAM_AXIS ) {
		DodGC_ShowTeamMenu( pPlayer );	/* no side yet */
		return;
	}
	if( !DodGC_ClassOpen( team, player_class, index ) ) {
		ClientPrint( pPlayer->pev, HUD_PRINTCENTER, "That class is full\n" );
		ALERT( at_console, "DoD class: %s asked for %s %s, which is full or off\n", STRING( pPlayer->pev->netname ), Dod_TeamName( team ),
			Dod_TeamClassName( team, player_class ) );
		DodGC_ShowClassMenu( pPlayer );
		return;
	}
	if( pPlayer->IsObserver() || !g_dod_joined[index] ) {
		g_dod_joined[index] = 1;
		g_dod_pending_team[index] = 0;
		DodGC_ClearMenu( pPlayer );
		if( pPlayer->IsObserver() )
			pPlayer->StopObserver();	/* Spawn(): PlayerSpawn puts them on their side */
		Dod_AssignPlayerTeam( index, team );	/* Spawn() resets the DoD team record */
		DodGC_ApplyClass( pPlayer, player_class );
		ALERT( at_console, "DoD join: %s spawned as %s %s\n", STRING( pPlayer->pev->netname ), Dod_TeamName( team ),
			Dod_TeamClassName( team, player_class ) );
		return;
	}
	if( Dod_SelectPlayerClass( index, player_class ) ) {
		DodGC_ClearMenu( pPlayer );
		ClientPrint( pPlayer->pev, HUD_PRINTTALK, UTIL_VarArgs( "You will respawn as %s.\n", Dod_TeamClassName( team, player_class ) ) );
		ALERT( at_console, "DoD join: %s changes to %s at the next respawn\n", STRING( pPlayer->pev->netname ),
			Dod_TeamClassName( team, player_class ) );
	}
}

/* The scoreboard text for one player (or an empty block, which clears the channel). */
static void DodGC_ShowBoard( CBasePlayer *pPlayer, BOOL show )
{
	char board[240];	/* a user message string is limited; Dod_FormatScoreboard drops whole lines that do not fit */
	const char *names[DOD_MAX_PLAYERS + 1];
	int i;

	board[0] = '\0';
	if( show ) {
		for( i = 0; i <= DOD_MAX_PLAYERS; i++ )
			names[i] = NULL;
		for( i = 1; i <= gpGlobals->maxClients && i <= DOD_MAX_PLAYERS; i++ ) {
			CBasePlayer *other = (CBasePlayer *)UTIL_PlayerByIndex( i );
			if( other && other->IsPlayer() )
				names[i] = STRING( other->pev->netname );
		}
		Dod_FormatScoreboard( board, sizeof( board ), names, ENTINDEX( pPlayer->edict() ) );
	}
	if( gmsgDodBoard ) {
		MESSAGE_BEGIN( MSG_ONE, gmsgDodBoard, NULL, pPlayer->edict() );
			WRITE_BYTE( show && board[0] ? 1 : 0 );
			if( show && board[0] )
				WRITE_STRING( board );
		MESSAGE_END();
	}
	if( show && board[0] && CVAR_GET_FLOAT( "cl_dodhud" ) == 0.0f ) {
		/* No sprite HUD: the plain HUD text block. */
		hudtextparms_t params;
		memset( &params, 0, sizeof( params ) );
		params.channel = 4;
		params.x = 0.08f;
		params.y = 0.2f;
		params.r1 = 255; params.g1 = 255; params.b1 = 255; params.a1 = 255;
		params.holdTime = 1.5f;
		UTIL_HudMessage( pPlayer, params, board );
	}
}

static void DodGC_CloseVoiceMenu( CBasePlayer *pPlayer );

/* Players that asked for the scoreboard get it again every second while their time runs. */
static void DodGC_RefreshBoards( void )
{
	int i;
	if( gpGlobals->time < g_dod_next_board )
		return;
	g_dod_next_board = gpGlobals->time + 1.0f;
	for( i = 1; i <= gpGlobals->maxClients && i <= DOD_MAX_PLAYERS; i++ ) {
		CBasePlayer *player;
		if( g_dod_voice_menu[i] && g_dod_voice_menu_until[i] <= gpGlobals->time ) {
			player = (CBasePlayer *)UTIL_PlayerByIndex( i );
			if( player )
				DodGC_CloseVoiceMenu( player );
			else
				g_dod_voice_menu[i] = 0;
		}
		if( g_dod_board_until[i] <= 0.0f )
			continue;
		player = (CBasePlayer *)UTIL_PlayerByIndex( i );
		if( !player || !player->IsPlayer() || g_dod_board_until[i] <= gpGlobals->time ) {
			g_dod_board_until[i] = 0.0f;
			continue;
		}
		DodGC_ShowBoard( player, TRUE );
	}
}

/* Everyone of `team` who stands in the capture volume gets credit for the capture. A sphere point is the
 * 128-unit radius around it; an area is its brush bounds. */
static void DodGC_CreditCapture( DodTeam team, int point_index, const Vector &center, float radius, const Vector *mins, const Vector *maxs )
{
	int i;
	const DodObjectiveState *objective = Dod_GetObjective( point_index );
	const int points = objective ? objective->points_for_cap : 1;
	DodGC_PlayTeamAnnouncement( team, "ambience", "pointcaptured" );
	for( i = 1; i <= gpGlobals->maxClients; ++i ) {
		CBasePlayer *player = (CBasePlayer *)UTIL_PlayerByIndex( i );
		if( !player || !player->IsPlayer() || player->pev->deadflag != DEAD_NO ) continue;
		if( DodGC_TeamForPlayer( player ) != team ) continue;
		const Vector &o = player->pev->origin;
		if( mins && maxs ) {
			if( o.x < mins->x || o.y < mins->y || o.z < mins->z - 8 || o.x > maxs->x || o.y > maxs->y || o.z > maxs->z + 72 ) continue;
		} else if( ( o - center ).Length() > radius ) {
			continue;
		}
		Dod_RecordCapture( DodGC_PlayerIndex( player ), points );
	}
}

BOOL DodGC_Voice( CBasePlayer *pPlayer, int voice, BOOL automatic )
{
	char path[64];
	const int index = DodGC_PlayerIndex( pPlayer );
	const DodTeam team = pPlayer ? DodGC_TeamForPlayer( pPlayer ) : DOD_TEAM_UNASSIGNED;
	if( !pPlayer || index < 1 || index > DOD_MAX_PLAYERS || pPlayer->pev->deadflag != DEAD_NO || pPlayer->IsObserver() )
		return FALSE;
	if( gpGlobals->time < g_dod_voice_next[index] || ( automatic && gpGlobals->time < g_dod_voice_auto_next[index] ) )
		return FALSE;
	if( !Dod_VoiceSound( voice, team, Dod_GetAlliesCountry() == DOD_COUNTRY_BRITISH, path, sizeof( path ) ) )
		return FALSE;
	g_dod_voice_next[index] = gpGlobals->time + DOD_VOICE_COOLDOWN;
	if( automatic )
		g_dod_voice_auto_next[index] = gpGlobals->time + DOD_VOICE_AUTO_COOLDOWN;
	PRECACHE_SOUND( path );	/* loaded when first spoken, like the weapon sounds */
	EMIT_SOUND_DYN( ENT( pPlayer->pev ), CHAN_VOICE, path, 1.0f, ATTN_NORM, 0, 100 );
	ALERT( at_console, "DoD voice %s: %s\n", STRING( pPlayer->pev->netname ), Dod_VoiceLabel( voice ) );
	return TRUE;
}

void DodGC_VoiceAuto( CBasePlayer *pPlayer, const char *name )
{
	DodGC_Voice( pPlayer, Dod_VoiceFind( name ), TRUE );
}

static void DodGC_SendVoiceMenu( CBasePlayer *pPlayer, int selected )
{
	char text[200];
	int i, len = 0;
	if( !gmsgDodVoice || !pPlayer )
		return;
	text[0] = '\0';
	if( selected >= 0 ) {
		for( i = 0; i < DOD_VOICE_MENU_COUNT && len < (int)sizeof( text ) - 24; i++ )
			len += snprintf( text + len, sizeof( text ) - len, "%s%s", i ? "\n" : "", Dod_VoiceLabel( i ) );
	}
	MESSAGE_BEGIN( MSG_ONE, gmsgDodVoice, NULL, pPlayer->edict() );
		WRITE_BYTE( selected >= 0 ? 1 : 0 );
		if( selected >= 0 ) {
			WRITE_BYTE( selected );
			WRITE_STRING( text );
		}
	MESSAGE_END();
}

static void DodGC_CloseVoiceMenu( CBasePlayer *pPlayer )
{
	const int index = DodGC_PlayerIndex( pPlayer );
	if( index < 1 || index > DOD_MAX_PLAYERS || !g_dod_voice_menu[index] )
		return;
	g_dod_voice_menu[index] = 0;
	DodGC_SendVoiceMenu( pPlayer, -1 );
}

/* Y (impulse 100, the flashlight in HL, which DoD does not have): open the voice menu, then speak the chosen line.
 * D-pad up/down move the choice and D-pad left cancels. Patched into CBasePlayer::ImpulseCommands by the integrator. */
BOOL DodGC_VoiceMenuImpulse( CBasePlayer *pPlayer )
{
	const int index = DodGC_PlayerIndex( pPlayer );
	if( !Dod_GameIsActive() || index < 1 || index > DOD_MAX_PLAYERS || pPlayer->IsObserver() || pPlayer->pev->deadflag != DEAD_NO )
		return FALSE;
	if( !g_dod_voice_menu[index] ) {
		g_dod_voice_menu[index] = 1;
		g_dod_voice_menu_until[index] = gpGlobals->time + DOD_VOICE_MENU_SECONDS;
		DodGC_SendVoiceMenu( pPlayer, 0 );
	} else {
		const int chosen = g_dod_voice_menu[index] - 1;
		DodGC_CloseVoiceMenu( pPlayer );
		DodGC_Voice( pPlayer, chosen, FALSE );
	}
	return TRUE;
}

static void DodGC_CycleWeapon( CBasePlayer *pPlayer, int step );

BOOL CHalfLifeDoD::ClientCommand( CBasePlayer *pPlayer, const char *pcmd )
{
	const int index = DodGC_PlayerIndex( pPlayer );
	if( index >= 1 && index <= DOD_MAX_PLAYERS && !g_join_done[index] && CVAR_GET_STRING( "dod_gc_join" )[0] && !DodBots_IsBot( pPlayer )
		&& ( FStrEq( pcmd, "dod_team" ) || FStrEq( pcmd, "dod_class" ) || FStrEq( pcmd, "menuselect" ) ) ) {
		float delay = 0.0f;
		sscanf( CVAR_GET_STRING( "dod_gc_join" ), "%f", &delay );
		if( gpGlobals->time < delay ) {
			ALERT( at_console, "DoD join: held '%s' from the human until game time %.0f\n", pcmd, delay );
			return TRUE;	/* a test that joins the human at a set time: nothing joins it earlier */
		}
	}
	if( index >= 1 && index <= DOD_MAX_PLAYERS && g_dod_voice_menu[index]
		&& ( FStrEq( pcmd, "dod_nextweapon" ) || FStrEq( pcmd, "dod_prevweapon" ) || FStrEq( pcmd, "dod_lastweapon" ) ) ) {
		/* the voice menu owns the D-pad while it is open */
		if( FStrEq( pcmd, "dod_lastweapon" ) ) {
			DodGC_CloseVoiceMenu( pPlayer );
		} else {
			const int count = DOD_VOICE_MENU_COUNT;
			const int sel = ( g_dod_voice_menu[index] - 1 + ( FStrEq( pcmd, "dod_nextweapon" ) ? 1 : count - 1 ) ) % count;
			g_dod_voice_menu[index] = sel + 1;
			g_dod_voice_menu_until[index] = gpGlobals->time + DOD_VOICE_MENU_SECONDS;
			DodGC_SendVoiceMenu( pPlayer, sel );
		}
		return TRUE;
	}
	if( FStrEq( pcmd, "dod_nextweapon" ) || FStrEq( pcmd, "dod_prevweapon" ) ) {
		DodGC_CycleWeapon( pPlayer, FStrEq( pcmd, "dod_nextweapon" ) ? 1 : -1 );
		return TRUE;
	}
	if( FStrEq( pcmd, "dod_lastweapon" ) ) {
		if( !pPlayer->IsObserver() && pPlayer->IsAlive() )
			pPlayer->SelectLastItem();
		return TRUE;
	}
	if( FStrEq( pcmd, "dod_team" ) ) {
		const DodTeam team = (DodTeam)Dod_TeamFromName( CMD_ARGV( 1 ) );
		if( team == DOD_TEAM_SPECTATOR ) {
			DodGC_JoinSpectator( pPlayer );
			return TRUE;
		}
		if( team == DOD_TEAM_ALLIES || team == DOD_TEAM_AXIS )
			DodGC_PickTeam( pPlayer, team );
		return TRUE;
	}
	if( FStrEq( pcmd, "spectate" ) ) {
		DodGC_JoinSpectator( pPlayer );
		return TRUE;
	}
	if( !strncmp( pcmd, "voice_", 6 ) ) {
		const int voice = Dod_VoiceFind( pcmd + 6 );
		if( voice >= 0 && voice < DOD_VOICE_MENU_COUNT )
			DodGC_Voice( pPlayer, voice, FALSE );
		return TRUE;
	}
	if( FStrEq( pcmd, "dod_score" ) ) {
		if( index >= 1 && index <= DOD_MAX_PLAYERS ) {
			const BOOL showing = g_dod_board_until[index] > gpGlobals->time;
			g_dod_board_until[index] = showing ? 0.0f : gpGlobals->time + DOD_BOARD_SECONDS;
			DodGC_ShowBoard( pPlayer, !showing );
		}
		return TRUE;
	}
	if( FStrEq( pcmd, "dod_class" ) ) {
		const char *name = CMD_ARGV( 1 );
		DodClass player_class = DOD_CLASS_NONE;
		if( !stricmp( name, "rifleman" ) ) player_class = DOD_CLASS_RIFLEMAN;
		else if( !stricmp( name, "assault" ) ) player_class = DOD_CLASS_ASSAULT;
		else if( !stricmp( name, "support" ) ) player_class = DOD_CLASS_SUPPORT;
		else if( !stricmp( name, "sniper" ) ) player_class = DOD_CLASS_SNIPER;
		else if( !stricmp( name, "mg" ) || !stricmp( name, "machinegunner" ) ) player_class = DOD_CLASS_MG;
		else if( !stricmp( name, "rocket" ) ) player_class = DOD_CLASS_ROCKET;
		if( player_class != DOD_CLASS_NONE )
			DodGC_PickClass( pPlayer, player_class );
		return TRUE;
	}
	if( FStrEq( pcmd, "menuselect" ) ) {
		if( index < 1 || index > DOD_MAX_PLAYERS || CMD_ARGC() < 2 )
			return TRUE;
		const int slot = atoi( CMD_ARGV( 1 ) );
		if( g_dod_menu_state[index] == 1 ) {
			if( slot == 6 ) {
				DodGC_JoinSpectator( pPlayer );
				return TRUE;
			}
			if( slot == 1 || slot == 2 )
				DodGC_PickTeam( pPlayer, slot == 1 ? DOD_TEAM_ALLIES : DOD_TEAM_AXIS );
			return TRUE;
		}
		if( g_dod_menu_state[index] == 2 && slot >= 1 && slot <= 6 ) {
			DodGC_PickClass( pPlayer, (DodClass)( DOD_CLASS_RIFLEMAN + slot - 1 ) );
			return TRUE;
		}
	}
	return CHalfLifeTeamplay::ClientCommand( pPlayer, pcmd );
}

/* Weapon cycling for the pad. invnext / invprev / lastinv are client HUD commands that need the
 * HUD's weapon list and selection overlay, which the lean GameCube client does not run, so the
 * D-pad did nothing with them. The pad is bound to these server commands instead; they walk the
 * carried weapons in slot order (melee, sidearm, primary, rocket, grenade) and select the next one. */
static void DodGC_CycleWeapon( CBasePlayer *pPlayer, int step )
{
	CBasePlayerItem *list[16];
	int count = 0, current = -1, i;
	if( !pPlayer || pPlayer->IsObserver() || !pPlayer->IsAlive() )
		return;
	for( int slot = 0; slot < MAX_ITEM_TYPES; slot++ )
		for( CBasePlayerItem *item = pPlayer->m_rgpPlayerItems[slot]; item && count < 16; item = item->m_pNext )
			list[count++] = item;
	if( count < 2 )
		return;
	for( i = 0; i < count; i++ )
		if( list[i] == pPlayer->m_pActiveItem )
			current = i;
	i = ( ( current < 0 ? 0 : current ) + step + count ) % count;
	pPlayer->SelectItem( STRING( list[i]->pev->classname ) );
}

void CHalfLifeDoD::InitHUD( CBasePlayer *pPlayer )
{
	CHalfLifeTeamplay::InitHUD( pPlayer );
	DodGC_EnsurePlayer( pPlayer );
	if( pPlayer->IsObserver() )
		return;	/* still spectating: nothing to choose */
	if( !DodBots_IsBot( pPlayer ) && CVAR_GET_FLOAT( "dod_spectate" ) != 0.0f ) {
		const int index = DodGC_PlayerIndex( pPlayer );
		if( index >= 1 && index <= DOD_MAX_PLAYERS )
			g_spec_join_pending[index] = TRUE;
		return;
	}
	DodGC_ShowTeamMenu( pPlayer );
}

/* Called from the SDK's CHalfLifeMultiplay::PlayerSpawn (patched in by scripts/integrate-dod-gameplay.py): in a DoD
 * match the class kit is the only thing a player gets, not the Half-Life crowbar and 9mm. */
BOOL DodGC_SkipDefaultItems( void )
{
	return Dod_GameIsActive() ? TRUE : FALSE;
}

void CHalfLifeDoD::PlayerSpawn( CBasePlayer *pPlayer )
{
	/* A round restart respawns living players, who still hold their items. The base spawn gives a
	 * crowbar again, and a duplicate crowbar (no ammo to extract) stays lying in the world: one
	 * leaked edict per player per round until the edict pool ran out. Start from nothing. */
	pPlayer->RemoveAllItems( FALSE );
	CHalfLifeTeamplay::PlayerSpawn( pPlayer );
	DodGC_EnsurePlayer( pPlayer );
	Dod_NoteRespawn( DodGC_PlayerIndex( pPlayer ) );
	if( DodGC_PlayerIndex( pPlayer ) >= 1 && DodGC_PlayerIndex( pPlayer ) <= DOD_MAX_PLAYERS )
		g_dod_death_seq[DodGC_PlayerIndex( pPlayer )] = 0;
	if( pPlayer->IsObserver() )
		return;
	/* The GameCube SDK's CBasePlayer::Spawn takes the base spawn spot (not this rules class's) on the -gcnewgame route, so a
	 * respawned Axis bot appeared on an Allied spawn, inside the Allies' spawn-protection zone, and died there. Put the
	 * player on a spawn of their own side here, whichever route spawned them. */
	{
		const DodTeam side = DodGC_TeamForPlayer( pPlayer );
		if( side == DOD_TEAM_ALLIES || side == DOD_TEAM_AXIS ) {
			edict_t *spot = GetPlayerSpawnSpot( pPlayer );
			if( spot )
				UTIL_SetOrigin( pPlayer->pev, pPlayer->pev->origin );
		}
	}
	const int index = DodGC_PlayerIndex( pPlayer );
	if( !DodBots_IsBot( pPlayer ) && index >= 1 && index <= DOD_MAX_PLAYERS && !g_dod_joined[index]
		&& CVAR_GET_FLOAT( "dod_spectate" ) == 0.0f ) {
		DodGC_HoldForJoin( pPlayer );
		return;
	}
	const DodPlayerState *state = Dod_GetPlayer( index );
	if( state && state->player_class != DOD_CLASS_NONE )
		DodGC_ApplyClass( pPlayer, state->player_class );
	else
		DodGC_ApplyClass( pPlayer, DOD_CLASS_RIFLEMAN );
}

/* The kill feed: the lean client drops the stock DeathNotice, so one DodKill message goes to everyone with the sides, the names
 * and what killed. */
/* The retail death sequence of the hit, picked when the player is killed and handed to CBasePlayer::SetAnimation(PLAYER_DIE) by the integrator's
 * patch (DodGC_DeathSequence). */
static void DodGC_PickDeathSequence( CBasePlayer *pVictim, entvars_t *pKiller, entvars_t *pInflictor )
{
	const int index = DodGC_PlayerIndex( pVictim );
	int from_front = -1, melee = 0, explosion = 0;
	if( index < 1 || index > DOD_MAX_PLAYERS )
		return;
	if( pKiller && pKiller != pVictim->pev && !FNullEnt( ENT( pKiller ) ) && FBitSet( pKiller->flags, FL_CLIENT | FL_FAKECLIENT ) ) {
		const float yaw = pVictim->pev->angles.y * (float)M_PI / 180.0f;
		const Vector to = pKiller->origin - pVictim->pev->origin;
		from_front = ( to.x * cos( yaw ) + to.y * sin( yaw ) ) > 0.0f ? 1 : 0;
	} else if( pInflictor && pInflictor != pVictim->pev ) {
		const float yaw = pVictim->pev->angles.y * (float)M_PI / 180.0f;
		const Vector to = pInflictor->origin - pVictim->pev->origin;
		from_front = ( to.x * cos( yaw ) + to.y * sin( yaw ) ) > 0.0f ? 1 : 0;
	}
	explosion = ( pVictim->m_bitsDamageType & ( DMG_BLAST | DMG_BURN ) ) != 0 ? 1 : 0;
	if( pInflictor && FClassnameIs( pInflictor, "weapon_crowbar" ) )
		melee = 1;
	if( pKiller && pKiller != pVictim->pev && pKiller->weaponmodel ) {
		const char *wm = STRING( pKiller->weaponmodel );
		if( strstr( wm, "knife" ) || strstr( wm, "amerk" ) || strstr( wm, "spade" ) || strstr( wm, "fairbairn" ) || strstr( wm, "paraknife" ) )
			melee = 1;
	}
	g_dod_death_seq[index] = Dod_DeathSequence( pVictim->m_LastHitGroup, explosion, melee, ( pVictim->pev->flags & FL_DUCKING ) != 0,
		0 /* prone: the game has no prone yet */, from_front );
}

/* Called from CBasePlayer::SetAnimation(PLAYER_DIE) (patched in by the integrator): the death sequence of the body model, -1 outside a DoD game. */
int DodGC_DeathSequence( CBasePlayer *pPlayer )
{
	const int index = DodGC_PlayerIndex( pPlayer );
	if( !Dod_GameIsActive() || index < 1 || index > DOD_MAX_PLAYERS || g_dod_death_seq[index] <= 0 )
		return -1;
	return g_dod_death_seq[index];
}

static void DodGC_SendKillFeed( CBasePlayer *pVictim, entvars_t *pKiller, entvars_t *pInflictor )
{
	CBaseEntity *killer = pKiller ? CBaseEntity::Instance( pKiller ) : NULL;
	const BOOL by_player = killer && killer->IsPlayer();
	const char *weapon = "World";
	const char *kname = "";
	DodTeam kteam = DOD_TEAM_UNASSIGNED;
	if( !gmsgDodKill || !pVictim )
		return;
	if( by_player ) {
		CBasePlayer *kp = (CBasePlayer *)killer;
		kname = STRING( kp->pev->netname );
		kteam = DodGC_TeamForPlayer( kp );
		if( pInflictor && !( pInflictor->flags & FL_CLIENT ) && pInflictor != pKiller )
			weapon = Dod_KillWeaponLabel( STRING( pInflictor->classname ), kteam, Dod_GetAlliesCountry() == DOD_COUNTRY_BRITISH );
		else if( kp->m_pActiveItem )
			weapon = Dod_KillWeaponLabel( STRING( kp->m_pActiveItem->pev->classname ), kteam, Dod_GetAlliesCountry() == DOD_COUNTRY_BRITISH );
		else
			weapon = "Weapon";
	} else if( pInflictor ) {
		weapon = Dod_KillWeaponLabel( STRING( pInflictor->classname ), DOD_TEAM_UNASSIGNED, 0 );
	}
	MESSAGE_BEGIN( MSG_ALL, gmsgDodKill );
		WRITE_BYTE( by_player ? (int)kteam : 0 );
		WRITE_STRING( kname );
		WRITE_BYTE( (int)DodGC_TeamForPlayer( pVictim ) );
		WRITE_STRING( STRING( pVictim->pev->netname ) );
		WRITE_STRING( weapon );
	MESSAGE_END();
}

void CHalfLifeDoD::PlayerKilled( CBasePlayer *pVictim, entvars_t *pKiller, entvars_t *pInflictor )
{
	const int attacker = pKiller ? ENTINDEX( ENT( pKiller ) ) : 0;
	Dod_RecordKill( attacker, DodGC_PlayerIndex( pVictim ) );
	Dod_NoteDeath( DodGC_PlayerIndex( pVictim ) );
	DodGC_PickDeathSequence( pVictim, pKiller, pInflictor );
	DodGC_SendKillFeed( pVictim, pKiller, pInflictor );
	CHalfLifeTeamplay::PlayerKilled( pVictim, pKiller, pInflictor );
}

void CHalfLifeDoD::ClientDisconnected( edict_t *pClient )
{
	if( pClient ) {
		const int index = ENTINDEX( pClient );
		if( index >= 1 && index <= DOD_MAX_PLAYERS )
			g_dod_joined[index] = g_dod_pending_team[index] = g_dod_weaponlist_resent[index] = 0;
		Dod_PlayerDisconnect( index );
	}
	CHalfLifeMultiplay::ClientDisconnected( pClient );
}

/* The spawn points of the map and the side of each: the engine keeps info_player_allies / info_player_axis when this DLL exports
 * them; a DLL that only knows DM starts gets info_player_deathmatch with the side in v.team (1 Allies, 2 Axis), so that is the
 * fallback. */
#define DOD_MAX_SPAWNS 128
static edict_t *g_spawn_edict[DOD_MAX_SPAWNS];
static int g_spawn_team[DOD_MAX_SPAWNS];
static int g_spawn_count;

static void DodGC_LoadSpawns( void )
{
	const char *map = STRING( gpGlobals->mapname );
	static const char *const classes[3] = { "info_player_allies", "info_player_axis", "info_player_deathmatch" };
	edict_t *spot;
	int i, by_class = 0, allies = 0, axis = 0;
	if( g_spawn_map[0] && !strcmp( g_spawn_map, map ) )
		return;
	strncpy( g_spawn_map, map, sizeof( g_spawn_map ) - 1 );
	g_spawn_count = 0;
	for( i = 0; i < 2; i++ ) {
		spot = NULL;
		while( g_spawn_count < DOD_MAX_SPAWNS && !FNullEnt( spot = FIND_ENTITY_BY_STRING( spot, "classname", classes[i] ) ) ) {
			g_spawn_edict[g_spawn_count] = spot;
			g_spawn_team[g_spawn_count++] = i == 0 ? DOD_TEAM_ALLIES : DOD_TEAM_AXIS;
			by_class = 1;
		}
	}
	if( !by_class ) {
		spot = NULL;
		while( g_spawn_count < DOD_MAX_SPAWNS && !FNullEnt( spot = FIND_ENTITY_BY_STRING( spot, "classname", classes[2] ) ) ) {
			if( spot->v.team != DOD_TEAM_ALLIES && spot->v.team != DOD_TEAM_AXIS )
				continue;	/* a plain DM start (no side) is nobody's spawn here */
			g_spawn_edict[g_spawn_count] = spot;
			g_spawn_team[g_spawn_count++] = spot->v.team;
		}
	}
	for( i = 0; i < g_spawn_count; i++ ) {
		if( g_spawn_team[i] == DOD_TEAM_ALLIES ) allies++;
		else axis++;
	}
	ALERT( at_console, "DoD spawns: %d Allies, %d Axis (%s)\n", allies, axis, by_class ? "by classname" : "by v.team" );
}

edict_t *CHalfLifeDoD::GetPlayerSpawnSpot( CBasePlayer *pPlayer )
{
	const DodTeam team = DodGC_TeamForPlayer( pPlayer );
	int count = 0, i;
	edict_t *chosen = NULL;
	DodGC_LoadSpawns();
	for( i = 0; i < g_spawn_count; i++ )
		if( g_spawn_team[i] == (int)team )
			count++;
	if( count > 0 ) {
		int pick = RANDOM_LONG( 0, count - 1 );
		for( i = 0; i < g_spawn_count; i++ ) {
			if( g_spawn_team[i] != (int)team )
				continue;
			if( pick-- == 0 ) { chosen = g_spawn_edict[i]; break; }
		}
	}
	if( !chosen )
		return CHalfLifeTeamplay::GetPlayerSpawnSpot( pPlayer );
	pPlayer->pev->origin = chosen->v.origin + Vector( 0, 0, 1 );
	pPlayer->pev->v_angle = g_vecZero;
	pPlayer->pev->velocity = g_vecZero;
	pPlayer->pev->angles = chosen->v.angles;
	pPlayer->pev->punchangle = g_vecZero;
	pPlayer->pev->fixangle = TRUE;
	return chosen;
}

/* The numbers the client sprite HUD draws, one message per human client. */
static void DodGC_SendHud( void )
{
	const DodGameRulesState *state = Dod_GetState();
	int owners[DOD_HUD_MAX_POINTS], points = 0, id, i;
	unsigned short packed;

	if( !gmsgDodHud || !state )
		return;
	for( id = 0; id < DOD_HUD_MAX_POINTS; id++ ) {
		const DodObjectiveState *objective = Dod_GetObjective( id );
		if( objective && objective->active )
			owners[points++] = (int)objective->owner;
	}
	packed = Dod_HudPackOwners( owners, points );
	for( i = 1; i <= gpGlobals->maxClients; i++ ) {
		CBasePlayer *player = (CBasePlayer *)UTIL_PlayerByIndex( i );
		const DodPlayerState *me;
		int grenades = 0, ammo_index;
		if( !player || !player->IsPlayer() || ( player->pev->flags & FL_FAKECLIENT ) )
			continue;
		me = Dod_GetPlayer( i );
		ammo_index = player->GetAmmoIndex( "Hand Grenade" );
		if( ammo_index >= 0 )
			grenades = player->m_rgAmmo[ammo_index];
		MESSAGE_BEGIN( MSG_ONE, gmsgDodHud, NULL, player->edict() );
			WRITE_SHORT( state->allies_score );
			WRITE_SHORT( state->axis_score );
			WRITE_BYTE( state->round_number );
			WRITE_BYTE( state->rounds_to_win );
			WRITE_BYTE( state->rounds_won[DOD_TEAM_ALLIES] );
			WRITE_BYTE( state->rounds_won[DOD_TEAM_AXIS] );
			WRITE_SHORT( state->round_time_remaining > 0.0f ? (int)state->round_time_remaining : 0 );
			WRITE_BYTE( (int)state->phase );
			WRITE_BYTE( points );
			WRITE_SHORT( packed );
			WRITE_BYTE( me ? (int)me->team : 0 );
			WRITE_BYTE( me ? (int)me->player_class : 0 );
			WRITE_BYTE( grenades > 255 ? 255 : grenades );
			WRITE_BYTE( ( Dod_GetAlliesCountry() == DOD_COUNTRY_BRITISH ? DOD_HUD_KIT_BRITISH : 0 )
				| ( Dod_TeamIsParatroopers( DOD_TEAM_ALLIES ) ? DOD_HUD_KIT_ALLIES_PARA : 0 )
				| ( Dod_TeamIsParatroopers( DOD_TEAM_AXIS ) ? DOD_HUD_KIT_AXIS_PARA : 0 ) );
			WRITE_BYTE( Dod_PlayerStamina( i ) );
			WRITE_BYTE( player->pev->deadflag != DEAD_NO && !player->IsObserver() ? (int)( Dod_RespawnSeconds( i ) + 0.99f ) : 0 );
		MESSAGE_END();
	}
}

/* ------------------------------------------------------------- spectators
 * The HLSDK observer code (observer.cpp) already runs the cameras and the
 * buttons; this adds what a bot match needs on top: no overview-map modes
 * (the GameCube build has no overview data), pick a target as soon as bots
 * exist, and an optional director that follows the action. */
static BOOL DodGC_SpecCandidates( CBasePlayer *pSpec, DodSpecCandidate *list, int max, int *count )
{
	int n = 0;
	for( int i = 1; i <= gpGlobals->maxClients && n < max; i++ ) {
		CBasePlayer *p = (CBasePlayer *)UTIL_PlayerByIndex( i );
		DodTeam team;
		float nearest = -1.0f;
		if( !p || p == pSpec || !p->IsPlayer() || p->IsObserver() || ( p->pev->effects & EF_NODRAW ) )
			continue;
		team = DodGC_TeamForPlayer( p );
		if( team == DOD_TEAM_ALLIES || team == DOD_TEAM_AXIS ) {
			for( int j = 1; j <= gpGlobals->maxClients; j++ ) {
				CBasePlayer *e = (CBasePlayer *)UTIL_PlayerByIndex( j );
				float d;
				if( !e || e == p || !e->IsPlayer() || e->IsObserver() || !e->IsAlive() )
					continue;
				if( DodGC_TeamForPlayer( e ) == team || DodGC_TeamForPlayer( e ) == DOD_TEAM_UNASSIGNED )
					continue;
				d = ( e->pev->origin - p->pev->origin ).Length();
				if( nearest < 0.0f || d < nearest )
					nearest = d;
			}
		}
		list[n].index = i;
		list[n].alive = p->IsAlive();
		list[n].enemy_distance = nearest;
		n++;
	}
	*count = n;
	return n > 0;
}

static void DodGC_FollowTarget( CBasePlayer *pSpec, int index )
{
	CBaseEntity *target = UTIL_PlayerByIndex( index );
	if( !target )
		return;
	pSpec->m_hObserverTarget = target;
	pSpec->pev->iuser2 = index;
	UTIL_SetOrigin( pSpec->pev, target->pev->origin );
}

static void DodGC_SpectatorThink( void )
{
	const BOOL tick = gpGlobals->time >= g_spec_next_think;
	DodSpecCandidate list[DOD_MAX_PLAYERS];
	if( tick )
		g_spec_next_think = gpGlobals->time + 0.5f;
	for( int i = 1; i <= gpGlobals->maxClients; i++ ) {
		CBasePlayer *pSpec = (CBasePlayer *)UTIL_PlayerByIndex( i );
		const int slot = i <= DOD_SPEC_SLOTS ? i : 0;
		int count, mode, pick;
		/* dod_gc_autojoin "0 ...": a spectator. The engine's own team overlay auto-confirms a join after a few seconds, so wait
		 * until the human is playing and then turn them into an observer once (the director follows the bots). */
		if( i <= DOD_MAX_PLAYERS && pSpec && pSpec->IsPlayer() && !DodBots_IsBot( pSpec ) && !g_autojoin_spec_done[i] ) {
			const char *aj = CVAR_GET_STRING( "dod_gc_autojoin" );
			int aj_team = -1, aj_class = 0, aj_bots = -1;
			const int aj_n = ( aj && aj[0] ) ? sscanf( aj, "%d %d %d", &aj_team, &aj_class, &aj_bots ) : 0;
			if( aj_n >= 2 && aj_team == 0 ) {
				if( g_autojoin_seen[i] == 0.0f ) {
					g_autojoin_seen[i] = gpGlobals->time;
					if( aj_n >= 3 && aj_bots >= 0 )
						CVAR_SET_FLOAT( "dod_bots", (float)aj_bots );
				} else if( gpGlobals->time - g_autojoin_seen[i] > 8.0f && !pSpec->IsObserver() && g_dod_joined[i] ) {
					g_autojoin_spec_done[i] = TRUE;
					g_autojoin_seen[i] = 0.0f;
					CVAR_SET_FLOAT( "dod_spectate", 1.0f );
					DodGC_JoinSpectator( pSpec );
				}
			}
		}
		if( i <= DOD_MAX_PLAYERS && pSpec && pSpec->IsPlayer() && !DodBots_IsBot( pSpec ) && !g_dod_joined[i] ) {
			const char *aj = CVAR_GET_STRING( "dod_gc_autojoin" );
			int aj_team = 0, aj_class = 0, aj_bots = -1;
			const int aj_n = ( aj && aj[0] ) ? sscanf( aj, "%d %d %d", &aj_team, &aj_class, &aj_bots ) : 0;
			if( aj_n >= 2 && ( aj_team == DOD_TEAM_ALLIES || aj_team == DOD_TEAM_AXIS ) ) {
				if( g_autojoin_seen[i] == 0.0f ) {
					g_autojoin_seen[i] = gpGlobals->time;
					/* optional third number: the bot count, set here because a cfg line for dod_bots is overwritten when
					 * the game registers the cvar (the bots join about 3 s after the human, after this point) */
					if( aj_n >= 3 && aj_bots >= 0 )
						CVAR_SET_FLOAT( "dod_bots", (float)aj_bots );
				}
				else if( gpGlobals->time - g_autojoin_seen[i] > 2.0f ) {
					g_autojoin_seen[i] = 0.0f;
					DodGC_HumanJoin( pSpec, (DodTeam)aj_team, (DodClass)aj_class );
				}
			}
		}
		if( i <= DOD_MAX_PLAYERS && pSpec && pSpec->IsPlayer() && !DodBots_IsBot( pSpec ) && g_dod_joined[i] && !pSpec->IsObserver() ) {
			const char *at = CVAR_GET_STRING( "dod_gc_autotele" );
			float tx, ty, tz;
			if( at && at[0] && sscanf( at, "%f %f %f", &tx, &ty, &tz ) == 3 ) {
				if( g_autotele_seen[i] == 0.0f )
					g_autotele_seen[i] = gpGlobals->time;
				else if( gpGlobals->time - g_autotele_seen[i] > 6.0f ) {
					pSpec->pev->movetype = MOVETYPE_NOCLIP;
					pSpec->pev->velocity = Vector( 0, 0, 0 );
					if( !g_autotele_done[i] ) {
						g_autotele_done[i] = TRUE;
						UTIL_SetOrigin( pSpec->pev, Vector( tx, ty, tz ) );
						ALERT( at_console, "dod_gc_autotele: human parked at %.0f %.0f %.0f\n", tx, ty, tz );
					}
				}
			}
		}
		if( i <= DOD_MAX_PLAYERS && g_spec_join_pending[i] ) {
			g_spec_join_pending[i] = FALSE;
			if( pSpec && pSpec->IsPlayer() && !pSpec->IsObserver() && !DodBots_IsBot( pSpec ) )
				DodGC_JoinSpectator( pSpec );
		}
		if( !pSpec || !pSpec->IsPlayer() || !pSpec->IsObserver() || DodBots_IsBot( pSpec ) )
			continue;
		if( i <= DOD_MAX_PLAYERS && !g_join_done[i] && CVAR_GET_STRING( "dod_gc_join" )[0] ) {
			float delay = 0.0f;
			int team = 0, cls = 0;
			if( sscanf( CVAR_GET_STRING( "dod_gc_join" ), "%f %d %d", &delay, &team, &cls ) >= 2 && gpGlobals->time >= delay ) {
				g_join_done[i] = TRUE;
				ALERT( at_console, "DoD join: the human joins team %d class %d at game time %.1f\n", team, cls, gpGlobals->time );
				DodGC_HumanJoin( pSpec, team == 2 ? DOD_TEAM_AXIS : DOD_TEAM_ALLIES, (DodClass)( cls >= 1 && cls <= 6 ? cls : 1 ) );
				ALERT( at_console, "DoD join: human spawned team %s alive %d at %.0f,%.0f,%.0f\n", pSpec->m_szTeamName, (int)pSpec->IsAlive(), pSpec->pev->origin.x, pSpec->pev->origin.y, pSpec->pev->origin.z );
				continue;
			}
		}
		if( slot && ( pSpec->pev->button & ( IN_ATTACK | IN_ATTACK2 | IN_JUMP ) ) )
			g_spec_hold_until[slot] = gpGlobals->time + DOD_SPEC_MANUAL_HOLD;
		mode = pSpec->pev->iuser1;
		if( mode != Dod_SpecSanitizeMode( mode ) ) {
			pSpec->Observer_SetMode( Dod_SpecSanitizeMode( mode ) );
			mode = pSpec->pev->iuser1;
		}
		if( slot ) {
			if( mode != DOD_SPEC_ROAMING && pSpec->m_hObserverTarget ) {
				CBaseEntity *tgt = pSpec->m_hObserverTarget;
				g_spec_eye[slot] = tgt->pev->origin + tgt->pev->view_ofs;
				g_spec_eye_valid[slot] = TRUE;
			} else if( mode == DOD_SPEC_ROAMING && g_spec_eye_valid[slot] ) {
				/* Just switched to free-fly: start at the followed player's eye. */
				UTIL_SetOrigin( pSpec->pev, g_spec_eye[slot] );
				g_spec_eye_valid[slot] = FALSE;
				g_spec_good_valid[slot] = FALSE;
			}
			if( mode == DOD_SPEC_ROAMING ) {
				/* Free-fly clips against the world here, where the real map is traced: a
				 * move that hits a surface (or starts inside one) is undone. */
				TraceResult tr;
				if( g_spec_good_valid[slot] ) {
					UTIL_TraceLine( g_spec_good[slot], pSpec->pev->origin, ignore_monsters, NULL, &tr );
					if( tr.flFraction < 1.0f || tr.fStartSolid || tr.fAllSolid ) {
						UTIL_SetOrigin( pSpec->pev, g_spec_good[slot] );
						pSpec->pev->velocity = g_vecZero;
					} else
						g_spec_good[slot] = pSpec->pev->origin;
				} else {
					g_spec_good[slot] = pSpec->pev->origin;
					g_spec_good_valid[slot] = TRUE;
				}
			} else
				g_spec_good_valid[slot] = FALSE;
		}
		if( !tick || !DodGC_SpecCandidates( pSpec, list, DOD_MAX_PLAYERS, &count ) )
			continue;
		if( mode == DOD_SPEC_ROAMING && UTIL_PointContents( pSpec->pev->origin + pSpec->pev->view_ofs ) == CONTENTS_SOLID ) {
			/* Free-fly inside the map (joined before anyone spawned, or a start spot in the
			 * terrain): the view is a wall of rock and noclip flying out is blind. Put the
			 * camera at the eye of the most interesting living player instead. */
			int best = -1;
			for( int k = 0; k < count; k++ )
				if( list[k].alive && ( best < 0 || Dod_SpecInterest( &list[k] ) > Dod_SpecInterest( &list[best] ) ) )
					best = k;
			if( best >= 0 ) {
				CBaseEntity *who = UTIL_PlayerByIndex( list[best].index );
				if( who ) {
					UTIL_SetOrigin( pSpec->pev, who->pev->origin + who->pev->view_ofs - pSpec->pev->view_ofs );
					pSpec->pev->angles = who->pev->v_angle;
					pSpec->pev->v_angle = who->pev->v_angle;
					pSpec->pev->fixangle = 1;
				}
			}
		}
		if( mode == DOD_SPEC_ROAMING && pSpec->m_iObserverLastMode != DOD_SPEC_ROAMING ) {
			/* Forced into roaming for lack of anyone to watch (the bots had not
			 * joined yet): go back to the camera the spectator chose. */
			pSpec->Observer_SetMode( Dod_SpecSanitizeMode( pSpec->m_iObserverLastMode ) );
			mode = pSpec->pev->iuser1;
		}
		if( mode == DOD_SPEC_ROAMING || mode == 0 || !slot || CVAR_GET_FLOAT( "dod_spec_auto" ) == 0.0f
			|| gpGlobals->time < g_spec_hold_until[slot] )
			continue;
		{
			const int current = pSpec->m_hObserverTarget ? ENTINDEX( pSpec->m_hObserverTarget->edict() ) : 0;
			if( current != g_spec_last_target[slot] )
				g_spec_target_since[slot] = gpGlobals->time;
			pick = Dod_SpecDirectorPick( list, count, current, gpGlobals->time - g_spec_target_since[slot] );
			if( pick && pick != current ) {
				DodGC_FollowTarget( pSpec, pick );
				g_spec_target_since[slot] = gpGlobals->time;
			}
			g_spec_last_target[slot] = pick ? pick : current;
		}
	}
}

void CHalfLifeDoD::ClientUserInfoChanged( CBasePlayer *pPlayer, char *infobuffer )
{
	/* Teamplay turns a changed model into a team change; a spectator has no team. */
	if( pPlayer && pPlayer->IsObserver() )
		return;
	/* The team body set by DodGC_ApplyBody is not a team change request. */
	if( pPlayer && infobuffer ) {
		const char *body = DodGC_BodyForTeam( DodGC_TeamForPlayer( pPlayer ) );
		if( body && body[0] && !strcmp( g_engfuncs.pfnInfoKeyValue( infobuffer, "model" ), body ) )
			return;
	}
	CHalfLifeTeamplay::ClientUserInfoChanged( pPlayer, infobuffer );
	DodGC_ApplyBody( pPlayer );
}

/* GC edict budget: the engine's CapFaces edict tip is 460 and a map already uses about 425, so
 * every dead player's leftovers count. Retail drops the active gun in a weapon box that lives for
 * two minutes (a box plus the weapons in it, several edicts), and a gibbed player leaves five gibs;
 * after a few deaths the pool ran out and the game stalled. Dropping is off here (the dead player's
 * weapons are just removed) and gibs are swept after a few seconds. */
/* Weapon drops: a dead player drops the gun they hold into a weapon box that others can walk over to pick up. Every box is an edict, and
 * charlie sits near 420 of 460 with 26 kept free for the bots, so a box is only dropped while at least DOD_DROP_EDICT_MARGIN are free and
 * fewer than DOD_MAX_BOXES lie about, and each one is removed DOD_BOX_SECONDS after it fell. */
#define DOD_DROP_EDICT_MARGIN 45
#define DOD_MAX_BOXES 6
#define DOD_BOX_SECONDS 20.0f
static int DodGC_CountBoxes( void )
{
	int n = 0;
	edict_t *box = NULL;
	while( !FNullEnt( box = FIND_ENTITY_BY_STRING( box, "classname", "weaponbox" ) ) )
		n++;
	return n;
}

int CHalfLifeDoD::DeadPlayerWeapons( CBasePlayer *pPlayer )
{
	if( gpGlobals->maxEntities - g_engfuncs.pfnNumberOfEntities() < DOD_DROP_EDICT_MARGIN || DodGC_CountBoxes() >= DOD_MAX_BOXES )
		return GR_PLR_DROP_GUN_NO;
	return GR_PLR_DROP_GUN_ACTIVE;
}

/* Waves: a dead player comes back when the next wave of their side falls (Dod_RespawnSeconds). */
BOOL CHalfLifeDoD::FPlayerCanRespawn( CBasePlayer *pPlayer )
{
	Dod_SetRespawnBase( CVAR_GET_FLOAT( "dod_respawn_delay" ) );
	return Dod_RespawnSeconds( DodGC_PlayerIndex( pPlayer ) ) <= 0.0f ? TRUE : FALSE;
}

int CHalfLifeDoD::DeadPlayerAmmo( CBasePlayer *pPlayer )
{
	return GR_PLR_DROP_AMMO_ACTIVE;
}

#define DOD_GIB_SECONDS 6.0f

/* Remove gibs older than DOD_GIB_SECONDS (all of them and any weapon box when `all`, at a round
 * restart). Edicts past the clients; the first sighting stamps the gib's unused fuser4. */
static void DodGC_TrimEdicts( BOOL all )
{
	int removed = 0;
	for( int i = gpGlobals->maxClients + 1; i < gpGlobals->maxEntities; i++ ) {
		edict_t *e = INDEXENT( i );
		if( !e || e->free || !e->v.classname )
			continue;
		const char *cn = STRING( e->v.classname );
		CBaseEntity *ent = NULL;
		if( FStrEq( cn, "gib" ) ) {
			if( e->v.fuser4 == 0.0f )
				e->v.fuser4 = gpGlobals->time;
			if( !all && gpGlobals->time - e->v.fuser4 <= DOD_GIB_SECONDS )
				continue;
			ent = CBaseEntity::Instance( e );
			if( ent ) { UTIL_Remove( ent ); removed++; }
		} else if( all && FStrEq( cn, "weaponbox" ) ) {
			ent = CBaseEntity::Instance( e );
			if( ent ) { ( (CWeaponBox *)ent )->Kill(); removed++; }
		}
	}
	if( all && removed )
		ALERT( at_console, "DoD round cleanup: removed %d gibs/weapon boxes\n", removed );
}

static float g_dod_next_trim;

/* Stamina and sprint for every human playing: a double tap of forward sprints, and the speed the engine's player move
 * uses is the player's maxspeed (the pad's stick sets forward; bots run their own movement). */
/* Weapon boxes on the ground: dress each new one in the retail world model of the gun it holds, remove it after DOD_BOX_SECONDS, and
 * log when somebody takes one. */
static void DodGC_ManageWeaponBoxes( void )
{
	static float next_check;
	static int seen_edict[DOD_MAX_BOXES * 2];
	static float seen_time[DOD_MAX_BOXES * 2];
	static int seen_count;
	edict_t *box = NULL;
	int i, alive[DOD_MAX_BOXES * 2], alive_count = 0;
	if( gpGlobals->time < next_check )
		return;
	next_check = gpGlobals->time + 0.5f;
	while( !FNullEnt( box = FIND_ENTITY_BY_STRING( box, "classname", "weaponbox" ) ) && alive_count < DOD_MAX_BOXES * 2 ) {
		const int ent = ENTINDEX( box );
		int slot = -1;
		for( i = 0; i < seen_count; i++ )
			if( seen_edict[i] == ent ) { slot = i; break; }
		if( slot < 0 && seen_count < DOD_MAX_BOXES * 2 ) {
			CWeaponBox *wb = (CWeaponBox *)CBaseEntity::Instance( box );
			char model[64] = "";
			if( wb ) {
				for( int t = 0; t < MAX_ITEM_TYPES && !model[0]; t++ ) {
					CBasePlayerItem *item = wb->m_rgpPlayerItems[t];
					if( item )
						Dod_WeaponWorldModel( Dod_FindWeaponDef( STRING( item->pev->classname ) ), model, sizeof( model ) );
				}
				if( model[0] ) {
					PRECACHE_MODEL( model );
					SET_MODEL( box, model );
				}
			}
			slot = seen_count++;
			seen_edict[slot] = ent;
			seen_time[slot] = gpGlobals->time;
			ALERT( at_console, "DoD weapon box %d dropped (%s), %d on the ground\n", ent, model[0] ? model : "no gun", alive_count + 1 );
		}
		if( slot >= 0 ) {
			alive[alive_count++] = slot;
			if( gpGlobals->time - seen_time[slot] > DOD_BOX_SECONDS ) {
				CWeaponBox *wb = (CWeaponBox *)CBaseEntity::Instance( box );
				ALERT( at_console, "DoD weapon box %d removed after %.0f s\n", ent, DOD_BOX_SECONDS );
				if( wb )
					wb->Kill();
			}
		}
	}
	/* a tracked box that is gone and was not timed out was picked up */
	for( i = 0; i < seen_count; i++ ) {
		int still = 0;
		for( int a = 0; a < alive_count; a++ )
			if( alive[a] == i ) { still = 1; break; }
		if( !still ) {
			if( gpGlobals->time - seen_time[i] <= DOD_BOX_SECONDS )
				ALERT( at_console, "DoD weapon box %d taken after %.1f s\n", seen_edict[i], gpGlobals->time - seen_time[i] );
			seen_edict[i] = seen_edict[seen_count - 1];
			seen_time[i] = seen_time[seen_count - 1];
			seen_count--;
			i--;
		}
	}
}

/* The newest usercmd of each player (CmdStart, patched in by the integrator): the stick's forwardmove is not a button. */
static float g_dod_cmd_forward[DOD_MAX_PLAYERS + 1];
void DodGC_UserCmd( CBasePlayer *pPlayer, const struct usercmd_s *cmd )
{
	const int index = DodGC_PlayerIndex( pPlayer );
	if( cmd && index >= 1 && index <= DOD_MAX_PLAYERS )
		g_dod_cmd_forward[index] = cmd->forwardmove;
}

static void DodGC_UpdateMovement( void )
{
	static int last_buttons[DOD_MAX_PLAYERS + 1];
	static float last_time;
	const float dt = gpGlobals->time - last_time;
	int i;
	last_time = gpGlobals->time;
	for( i = 1; i <= gpGlobals->maxClients && i <= DOD_MAX_PLAYERS; i++ ) {
		CBasePlayer *player = (CBasePlayer *)UTIL_PlayerByIndex( i );
		int buttons, jumped;
		if( !player || !player->IsPlayer() || ( player->pev->flags & FL_FAKECLIENT ) )
			continue;
		buttons = player->pev->button;
		jumped = ( buttons & IN_JUMP ) && !( last_buttons[i] & IN_JUMP ) && ( player->pev->flags & FL_ONGROUND );
		last_buttons[i] = buttons;
		if( !player->IsObserver() && player->pev->deadflag >= DEAD_DEAD ) {
			const DodTeam side = DodGC_TeamForPlayer( player );
			/* the wave brings everybody of the side back at once, the human who is not pressing fire as well */
			if( ( side == DOD_TEAM_ALLIES || side == DOD_TEAM_AXIS ) && Dod_GetState()->phase == DOD_ROUND_ACTIVE &&
				g_dod_joined[i] && Dod_RespawnSeconds( i ) <= 0.0f )
				respawn( player->pev, FALSE );
		}
		if( player->IsObserver() || player->pev->deadflag != DEAD_NO )
			continue;
		const Vector flat( player->pev->velocity.x, player->pev->velocity.y, 0.0f );
		static int was_sprinting[DOD_MAX_PLAYERS + 1];
		player->pev->maxspeed = Dod_StaminaThink( i, gpGlobals->time, dt > 0.0f && dt < 0.5f ? dt : 0.0f,
			( buttons & IN_FORWARD ) != 0 || g_dod_cmd_forward[i] > 60.0f, jumped, flat.Length() > 40.0f );
		if( Dod_PlayerSprinting( i ) != was_sprinting[i] ) {
			was_sprinting[i] = Dod_PlayerSprinting( i );
			ALERT( at_console, "DoD sprint %s: %s, stamina %d, speed %.0f\n", was_sprinting[i] ? "on" : "off", STRING( player->pev->netname ), Dod_PlayerStamina( i ), flat.Length() );
		}
	}
}

#if defined( __GAMECUBE__ )
extern "C" size_t GC_MemHeapFreeBytes( void );
extern "C" void GC_HeapCensusReport( const char *stage );
extern "C" void GC_MemReportPools( const char *stage );

/* Free engine heap, sampled twice a second; the minimum is logged every 10 s (the margin on charlie is only ~32 KB). */
static void DodGC_HeapMonitor( void )
{
	static float next_sample, next_log;
	static size_t lowest = (size_t)-1;
	if( gpGlobals->time < next_sample )
		return;
	next_sample = gpGlobals->time + 0.5f;
	const size_t now_free = GC_MemHeapFreeBytes();
	if( now_free < lowest )
		lowest = now_free;
	if( gpGlobals->time >= next_log ) {
		next_log = gpGlobals->time + 10.0f;
		ALERT( at_console, "DoD heap: free %u KB, lowest since start %u KB\n", (unsigned)( now_free / 1024 ), (unsigned)( lowest / 1024 ) );
		{
			/* Census builds only (engine XASH_GC_HEAP_CENSUS_BOOT): who holds the heap right now. A no-op otherwise. */
			GC_HeapCensusReport( "dod heap 10s" );
			GC_MemReportPools( "dod 10s" );
		}
	}
}
#else
static void DodGC_HeapMonitor( void ) {}
#endif

void CHalfLifeDoD::Think( void )
{
	DodGC_HeapMonitor();
	CHalfLifeTeamplay::Think();
	Dod_Think( gpGlobals->frametime );
	DodBots_Think();
	DodFilm_Think();
	DodGC_SpectatorThink();
	DodGC_UpdateMovement();
	DodGC_ManageWeaponBoxes();
	if( gpGlobals->time >= g_dod_next_trim ) {
		g_dod_next_trim = gpGlobals->time + 1.0f;
		DodGC_TrimEdicts( FALSE );
	}
	const DodGameRulesState *state = Dod_GetState();
	if( state && state->phase == DOD_ROUND_INTERMISSION && !g_dod_winner_announced ) {
		const char *winner = state->winner == DOD_TEAM_ALLIES ? "Allies win the round" :
			state->winner == DOD_TEAM_AXIS ? "Axis win the round" : "Round tied";
		UTIL_ClientPrintAll( HUD_PRINTCENTER, UTIL_VarArgs( "%s\nNext round in %d seconds", winner,
			(int)( state->intermission_remaining + 0.5f ) ) );
		g_dod_winner_announced = 1;
		DodGC_PlayRoundEndMusic( state->winner );
	}
	if( state && state->restart_serial != g_dod_seen_restart ) {
		/* The intermission ran out: everybody starts the new round at their
		 * spawn with a fresh loadout (bots included). */
		g_dod_seen_restart = state->restart_serial;
		g_dod_winner_announced = 0;
		ALERT( at_console, "DoD round restart: round %d, serial %d\n", state->round_number, state->restart_serial );
		DodGC_TrimEdicts( TRUE );
		if( state->match_serial != g_dod_seen_match ) {
			g_dod_seen_match = state->match_serial;
			UTIL_ClientPrintAll( HUD_PRINTCENTER, UTIL_VarArgs( "%s win the match",
				state->match_winner == DOD_TEAM_ALLIES ? "Allies" : "Axis" ) );
		} else {
			UTIL_ClientPrintAll( HUD_PRINTCENTER, UTIL_VarArgs( "Round %d", state->round_number ) );
		}
		for( int i = 1; i <= gpGlobals->maxClients; i++ ) {
			CBasePlayer *player = (CBasePlayer *)UTIL_PlayerByIndex( i );
			if( !player || !player->IsPlayer() )
				continue;
			const DodTeam team = DodGC_TeamForPlayer( player );
			if( team == DOD_TEAM_ALLIES || team == DOD_TEAM_AXIS )
				respawn( player->pev, FALSE );
		}
		/* each side's announcer opens the round; retail has two lines per side */
		DodGC_PlayTeamAnnouncement( DOD_TEAM_ALLIES, "player", RANDOM_LONG( 0, 1 ) ? "startround2" : "startround" );
		DodGC_PlayTeamAnnouncement( DOD_TEAM_AXIS, "player", RANDOM_LONG( 0, 1 ) ? "startround2" : "startround" );
	}
	/* Match status for the lean HUD, which has no VGUI scoreboard. With the client
	 * sprite HUD on (cl_dodhud, a cvar the local client registers) the server sends
	 * the numbers for it to draw; otherwise it shows the text strip, once a second. */
	DodGC_RefreshBoards();
	if( CVAR_GET_FLOAT( "cl_dodhud" ) != 0.0f ) {
		if( gpGlobals->time >= g_dod_next_hud ) {
			g_dod_next_hud = gpGlobals->time + 0.25f;
			DodGC_SendHud();
		}
	} else if( gpGlobals->time >= g_dod_next_status ) {
		char line[96];
		hudtextparms_t params;
		memset( &params, 0, sizeof( params ) );
		params.channel = 3;
		params.x = -1;
		params.y = 0.02f;
		params.effect = 0;
		params.r1 = 255; params.g1 = 220; params.b1 = 120; params.a1 = 255;
		params.holdTime = 1.5f;
		g_dod_next_status = gpGlobals->time + 1.0f;
		if( Dod_FormatStatus( line, sizeof( line ) ) > 0 )
			UTIL_HudMessageAll( params, line );
	}
}

/* Control point maps describe the point as a point entity. Keep the capture
 * volume deterministic and independent of retail VGUI: living teammates in a
 * 128-unit sphere capture it; contested points pause. */
class CDoDControlPoint : public CPointEntity
{
public:
	CDoDControlPoint();
	void Spawn( void );
	void KeyValue( KeyValueData *pkvd );
	void Think( void );
	/* A dod_capture_area drives this point (retail): the sphere test is off and the area's
	 * capture time replaces the default. */
	void SetAreaDriven( float capture_seconds );
	int PointIndex( void ) const { return m_iPointIndex; }
	BOOL IsAreaDriven( void ) const { return m_bAreaDriven; }
	/* The first area that links to this point supplies the volume bots stand in. */
	void SetAreaBounds( const Vector &mins, const Vector &maxs, const int can_cap[3], const int num_cap[3] );
	void FillView( DodGCObjectiveView *view ) const;
private:
	Vector m_vecAreaMin, m_vecAreaMax;
	int m_iAreaCanCap[3];
	int m_iAreaNumCap[3];	/* players of a team needed inside to capture */
	BOOL m_bHasBounds;
	int m_iPointIndex;
	BOOL m_bTouchKeys;	/* the map set point_can_allies_touch / point_can_axis_touch */
	int m_iDefaultOwner;
	int m_iPointsForCap;	/* point_points_for_cap: Score for each player who captures it */
	int m_iTeamPoints, m_iTimePoints;	/* point_team_points (on capture), point_pointvalue (timed) */
	char m_szPointName[64];
	BOOL m_bAreaDriven;
	/* Retail point_allies_model / point_axis_model / point_reset_model and
	 * their _bodygroup: the flag the point shows for its owner (reset =
	 * neutral). Indexed by DodTeam; 0 is the neutral (reset) flag. */
	string_t m_iszOwnerModel[3];
	int m_iOwnerBody[3];
	int m_iShownOwner;
	void ShowOwner( int owner );
};

LINK_ENTITY_TO_CLASS( dod_control_point, CDoDControlPoint )

CDoDControlPoint::CDoDControlPoint()
{
	m_iPointIndex = -1;
	m_bTouchKeys = FALSE;
	m_iDefaultOwner = DOD_TEAM_UNASSIGNED;
	m_iPointsForCap = 1;
	m_iTeamPoints = 0;
	m_iTimePoints = 1;
	m_szPointName[0] = '\0';
	m_bAreaDriven = FALSE;
	m_bHasBounds = FALSE;
	m_iAreaCanCap[0] = m_iAreaCanCap[DOD_TEAM_ALLIES] = m_iAreaCanCap[DOD_TEAM_AXIS] = 1;
	m_iAreaNumCap[0] = m_iAreaNumCap[DOD_TEAM_ALLIES] = m_iAreaNumCap[DOD_TEAM_AXIS] = 1;
	m_iszOwnerModel[0] = m_iszOwnerModel[DOD_TEAM_ALLIES] = m_iszOwnerModel[DOD_TEAM_AXIS] = 0;
	m_iOwnerBody[0] = m_iOwnerBody[DOD_TEAM_ALLIES] = m_iOwnerBody[DOD_TEAM_AXIS] = 0;
	m_iShownOwner = -1;
}

/* Show the owner's flag: its model with the map's body group value, waving. */
void CDoDControlPoint::ShowOwner( int owner )
{
	int slot = ( owner == DOD_TEAM_ALLIES || owner == DOD_TEAM_AXIS ) ? owner : 0;

	if( owner == m_iShownOwner )
		return;
	m_iShownOwner = owner;
	if( !m_iszOwnerModel[slot] ) {
		pev->effects |= EF_NODRAW;
		return;
	}
	SET_MODEL( ENT( pev ), STRING( m_iszOwnerModel[slot] ) );
	UTIL_SetSize( pev, g_vecZero, g_vecZero );
	pev->effects &= ~EF_NODRAW;
	{
		void *studio = GET_MODEL_PTR( ENT( pev ) );

		/* Without the header (lean load) group 0 with base 1 is the flags.mdl layout. */
		pev->body = studio ? 0 : m_iOwnerBody[slot];
		if( studio )
			SetBodygroup( studio, pev, 0, m_iOwnerBody[slot] );
	}
	pev->sequence = 0;
	pev->frame = 0;
	pev->framerate = 1.0f;
	pev->animtime = gpGlobals->time;
}

void CDoDControlPoint::SetAreaBounds( const Vector &mins, const Vector &maxs, const int can_cap[3], const int num_cap[3] )
{
	if( m_bHasBounds )
		return;
	m_bHasBounds = TRUE;
	m_vecAreaMin = mins;
	m_vecAreaMax = maxs;
	m_iAreaCanCap[DOD_TEAM_ALLIES] = can_cap[DOD_TEAM_ALLIES];
	m_iAreaCanCap[DOD_TEAM_AXIS] = can_cap[DOD_TEAM_AXIS];
	m_iAreaNumCap[DOD_TEAM_ALLIES] = num_cap[DOD_TEAM_ALLIES];
	m_iAreaNumCap[DOD_TEAM_AXIS] = num_cap[DOD_TEAM_AXIS];
}

void CDoDControlPoint::FillView( DodGCObjectiveView *view ) const
{
	view->can_cap[0] = view->can_cap[3] = 0;
	if( m_bHasBounds ) {
		view->mins = m_vecAreaMin;
		view->maxs = m_vecAreaMax;
		view->can_cap[DOD_TEAM_ALLIES] = m_iAreaCanCap[DOD_TEAM_ALLIES];
		view->can_cap[DOD_TEAM_AXIS] = m_iAreaCanCap[DOD_TEAM_AXIS];
		view->num_cap[DOD_TEAM_ALLIES] = m_iAreaNumCap[DOD_TEAM_ALLIES];
		view->num_cap[DOD_TEAM_AXIS] = m_iAreaNumCap[DOD_TEAM_AXIS];
	} else {
		/* sphere capture (see Think): anyone may capture within 128 units. The box bots are told to stand in must sit inside
		 * that sphere: a 128 box let them stop at its edge, up to 221 units out at a corner, and never capture (flash, donner). */
		view->mins = pev->origin - Vector( 64, 64, 64 );
		view->maxs = pev->origin + Vector( 64, 64, 64 );
		view->can_cap[DOD_TEAM_ALLIES] = view->can_cap[DOD_TEAM_AXIS] = 1;
		view->num_cap[DOD_TEAM_ALLIES] = view->num_cap[DOD_TEAM_AXIS] = 1;
	}
	view->center = ( view->mins + view->maxs ) * 0.5f;
}

void CDoDControlPoint::SetAreaDriven( float capture_seconds )
{
	if( m_bAreaDriven )
		return;
	m_bAreaDriven = TRUE;
	if( capture_seconds > 0.0f )
		Dod_RegisterObjective( m_iPointIndex, DOD_OBJ_CAPTURE_POINT, (DodTeam)m_iDefaultOwner,
			capture_seconds, DOD_POINT_SCORE_PER_MINUTE );
	Dod_SetObjectivePointsForCap( m_iPointIndex, m_iPointsForCap );
	Dod_SetObjectiveTeamPoints( m_iPointIndex, m_iTeamPoints, m_iTimePoints );
}

void CDoDControlPoint::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "point_index" ) ) {
		m_iPointIndex = atoi( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "point_can_allies_touch" ) || FStrEq( pkvd->szKeyName, "point_can_axis_touch" ) ) {
		m_bTouchKeys = TRUE; pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "point_default_owner" ) ) {
		m_iDefaultOwner = atoi( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "point_points_for_cap" ) ) {
		m_iPointsForCap = atoi( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "point_team_points" ) ) {
		m_iTeamPoints = atoi( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "point_pointvalue" ) ) {
		m_iTimePoints = atoi( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "point_name" ) ) {
		strlcpy( m_szPointName, pkvd->szValue, sizeof( m_szPointName ) ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "point_allies_model" ) ) {
		m_iszOwnerModel[DOD_TEAM_ALLIES] = ALLOC_STRING( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "point_axis_model" ) ) {
		m_iszOwnerModel[DOD_TEAM_AXIS] = ALLOC_STRING( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "point_reset_model" ) ) {
		m_iszOwnerModel[0] = ALLOC_STRING( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "point_allies_model_bodygroup" ) ) {
		m_iOwnerBody[DOD_TEAM_ALLIES] = atoi( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "point_axis_model_bodygroup" ) ) {
		m_iOwnerBody[DOD_TEAM_AXIS] = atoi( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "point_reset_model_bodygroup" ) ) {
		m_iOwnerBody[0] = atoi( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else CPointEntity::KeyValue( pkvd );
}

void CDoDControlPoint::Spawn( void )
{
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	if( m_iPointIndex < 0 ) {
		/* point_index -1 (donner: all five points) means "give me one": every such point used to land on index 0 and count as one point */
		int id = 16;
		while( Dod_GetObjective( id ) && id < 31 )
			id++;
		m_iPointIndex = id;
	}
	/* A point that starts owned and has no capture area is still taken by touch, as in retail (flash's four owned points,
	 * donner's HQ flags). Treating it as uncapturable left flash with no captures at all. */
	Dod_RegisterObjective( m_iPointIndex, DOD_OBJ_CAPTURE_POINT,
		(DodTeam)m_iDefaultOwner, 10.0f, DOD_POINT_SCORE_PER_MINUTE );
	Dod_SetObjectivePointsForCap( m_iPointIndex, m_iPointsForCap );
	Dod_SetObjectiveTeamPoints( m_iPointIndex, m_iTeamPoints, m_iTimePoints );
	for( int i = 0; i < 3; i++ )
		if( m_iszOwnerModel[i] )
			PRECACHE_MODEL( (char *)STRING( m_iszOwnerModel[i] ) );
	ShowOwner( m_iDefaultOwner );
	ALERT( at_console, "DoD control point %d flags allies=%s(%d) axis=%s(%d) reset=%s(%d) owner=%d\n", m_iPointIndex,
		m_iszOwnerModel[DOD_TEAM_ALLIES] ? STRING( m_iszOwnerModel[DOD_TEAM_ALLIES] ) : "-", m_iOwnerBody[DOD_TEAM_ALLIES],
		m_iszOwnerModel[DOD_TEAM_AXIS] ? STRING( m_iszOwnerModel[DOD_TEAM_AXIS] ) : "-", m_iOwnerBody[DOD_TEAM_AXIS],
		m_iszOwnerModel[0] ? STRING( m_iszOwnerModel[0] ) : "-", m_iOwnerBody[0], m_iDefaultOwner );
	SetThink( &CDoDControlPoint::Think );
	pev->nextthink = gpGlobals->time + 0.25f;
}

void CDoDControlPoint::Think( void )
{
	int allies = 0, axis = 0, i;
	{
		/* Captures (sphere or capture area) and round resets change the owner. */
		const DodObjectiveState *objective = Dod_GetObjective( m_iPointIndex );
		if( objective )
			ShowOwner( objective->owner );
	}
	if( m_bAreaDriven ) {
		pev->nextthink = gpGlobals->time + 0.25f;
		return;
	}
	for( i = 1; i <= gpGlobals->maxClients; ++i ) {
		CBasePlayer *player = (CBasePlayer *)UTIL_PlayerByIndex( i );
		if( !player || player->pev->deadflag != DEAD_NO ) continue;
		if( ( player->pev->origin - pev->origin ).Length() > 128.0f ) continue;
		if( DodGC_TeamForPlayer( player ) == DOD_TEAM_ALLIES ) ++allies;
		else if( DodGC_TeamForPlayer( player ) == DOD_TEAM_AXIS ) ++axis;
	}
	if( Dod_TickObjective( m_iPointIndex, allies, axis, 0.25f ) ) {
		const DodObjectiveState *objective = Dod_GetObjective( m_iPointIndex );
		if( objective && ( objective->owner == DOD_TEAM_ALLIES || objective->owner == DOD_TEAM_AXIS ) )
			DodGC_CreditCapture( objective->owner, m_iPointIndex, pev->origin, 128.0f, NULL, NULL );
		const char *owner = objective && objective->owner == DOD_TEAM_ALLIES ? "Allies" : "Axis";
		UTIL_ClientPrintAll( HUD_PRINTCENTER,
			UTIL_VarArgs( "%s captured control point %d", owner, m_iPointIndex ) );
	}
	pev->nextthink = gpGlobals->time + 0.25f;
}

/* ------------------------------------------------------------ capture areas */

/* Retail dod_capture_area: a brush trigger around a control point. Living players of a team
 * that may capture (area_<team>_cancap) and are at least area_<team>_numcap strong inside it
 * capture the point named by `target` over area_time_to_cap seconds. `master` (an env_state)
 * switches the area off. area_<team>_startcap / area_<team>_endcap are targets fired when a
 * capture starts / completes. The capture itself runs in the rules core (Dod_TickObjective). */
class CDoDCaptureArea : public CBaseEntity
{
public:
	CDoDCaptureArea();
	void Spawn( void );
	void KeyValue( KeyValueData *pkvd );
	void Think( void );
private:
	float m_flTimeToCap;
	int m_iNumCap[3];	/* indexed by DodTeam: [DOD_TEAM_ALLIES], [DOD_TEAM_AXIS] */
	int m_iCanCap[3];
	string_t m_iszTarget;
	string_t m_iszMaster;
	string_t m_iszStartCap[3];
	string_t m_iszEndCap[3];
	BOOL m_bResolved;
	BOOL m_bStarted;
	BOOL m_bContested;
	float m_flNextStateLog;	/* throttle for the capture area state log */
	CDoDControlPoint *m_pPoint;
};

LINK_ENTITY_TO_CLASS( dod_capture_area, CDoDCaptureArea )

CDoDCaptureArea::CDoDCaptureArea()
{
	m_flTimeToCap = 5.0f;
	m_iNumCap[DOD_TEAM_ALLIES] = m_iNumCap[DOD_TEAM_AXIS] = 1;
	m_iCanCap[DOD_TEAM_ALLIES] = m_iCanCap[DOD_TEAM_AXIS] = 1;
	m_iszTarget = m_iszMaster = 0;
	m_iszStartCap[DOD_TEAM_ALLIES] = m_iszStartCap[DOD_TEAM_AXIS] = 0;
	m_iszEndCap[DOD_TEAM_ALLIES] = m_iszEndCap[DOD_TEAM_AXIS] = 0;
	m_bResolved = m_bStarted = m_bContested = FALSE;
	m_pPoint = NULL;
}

void CDoDCaptureArea::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "area_time_to_cap" ) ) {
		m_flTimeToCap = (float)atof( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "area_axis_numcap" ) ) {
		m_iNumCap[DOD_TEAM_AXIS] = atoi( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "area_allies_numcap" ) ) {
		m_iNumCap[DOD_TEAM_ALLIES] = atoi( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "area_axis_cancap" ) ) {
		m_iCanCap[DOD_TEAM_AXIS] = atoi( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "area_allies_cancap" ) ) {
		m_iCanCap[DOD_TEAM_ALLIES] = atoi( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "master" ) ) {
		m_iszMaster = ALLOC_STRING( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "area_allies_startcap" ) ) {
		m_iszStartCap[DOD_TEAM_ALLIES] = ALLOC_STRING( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "area_axis_startcap" ) ) {
		m_iszStartCap[DOD_TEAM_AXIS] = ALLOC_STRING( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "area_allies_endcap" ) ) {
		m_iszEndCap[DOD_TEAM_ALLIES] = ALLOC_STRING( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "area_axis_endcap" ) ) {
		m_iszEndCap[DOD_TEAM_AXIS] = ALLOC_STRING( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "area_hud_sprite" ) || FStrEq( pkvd->szKeyName, "area_object_group" ) ) {
		pkvd->fHandled = TRUE;	/* HUD/object-group data: not used by the lean HUD */
	} else CBaseEntity::KeyValue( pkvd );
}

void CDoDCaptureArea::Spawn( void )
{
	pev->solid = SOLID_TRIGGER;
	pev->movetype = MOVETYPE_NONE;
	SET_MODEL( ENT( pev ), STRING( pev->model ) );
	pev->effects |= EF_NODRAW;
	SetThink( &CDoDCaptureArea::Think );
	pev->nextthink = gpGlobals->time + 0.5f;
}

void CDoDCaptureArea::Think( void )
{
	int inside[3] = { 0, 0, 0 }, i;
	pev->nextthink = gpGlobals->time + 0.25f;
	if( !m_bResolved ) {
		/* Every entity has spawned by the first think: find the point this area drives.
		 * `target` is a standard key the engine stores in pev->target. */
		m_iszTarget = pev->target;
		CBaseEntity *e = m_iszTarget ? UTIL_FindEntityByTargetname( NULL, STRING( m_iszTarget ) ) : NULL;
		while( e && !FClassnameIs( e->pev, "dod_control_point" ) )
			e = UTIL_FindEntityByTargetname( e, STRING( m_iszTarget ) );
		m_bResolved = TRUE;
		if( e ) {
			m_pPoint = (CDoDControlPoint *)e;
			m_pPoint->SetAreaDriven( m_flTimeToCap );
			m_pPoint->SetAreaBounds( pev->absmin, pev->absmax, m_iCanCap, m_iNumCap );
		}
		ALERT( at_console, "DoD capture area target=%s point=%d time=%.0f absmin=%.0f,%.0f,%.0f absmax=%.0f,%.0f,%.0f\n",
			m_iszTarget ? STRING( m_iszTarget ) : "-", m_pPoint ? m_pPoint->PointIndex() : -1, m_flTimeToCap,
			pev->absmin.x, pev->absmin.y, pev->absmin.z, pev->absmax.x, pev->absmax.y, pev->absmax.z );
	}
	if( !m_pPoint )
		return;	/* an area without a point (charlie's `*8`) only gates map logic */
	if( m_iszMaster && !UTIL_IsMasterTriggered( m_iszMaster, NULL ) )
	{
		static float next_master_log;
		if( gpGlobals->time >= next_master_log )
		{
			next_master_log = gpGlobals->time + 30.0f;
			ALERT( at_console, "DoD capture area point %d is closed by its master %s\n", m_pPoint->PointIndex(), STRING( m_iszMaster ) );
		}
		return;
	}
	for( i = 1; i <= gpGlobals->maxClients; ++i ) {
		CBasePlayer *player = (CBasePlayer *)UTIL_PlayerByIndex( i );
		DodTeam team;
		if( !player || player->pev->deadflag != DEAD_NO ) continue;
		if( player->pev->absmin.x > pev->absmax.x || player->pev->absmax.x < pev->absmin.x
			|| player->pev->absmin.y > pev->absmax.y || player->pev->absmax.y < pev->absmin.y
			|| player->pev->absmin.z > pev->absmax.z || player->pev->absmax.z < pev->absmin.z )
			continue;
		team = DodGC_TeamForPlayer( player );
		if( team == DOD_TEAM_ALLIES || team == DOD_TEAM_AXIS )
			++inside[team];
	}
	if( inside[DOD_TEAM_ALLIES] + inside[DOD_TEAM_AXIS] > 0 && gpGlobals->time >= m_flNextStateLog )
	{
		const DodObjectiveState *st = Dod_GetObjective( m_pPoint->PointIndex() );
		m_flNextStateLog = gpGlobals->time + 10.0f;
		ALERT( at_console, "DoD capture area point %d: inside Allies %d Axis %d, needs Allies %d Axis %d, may capture Allies %d Axis %d, owner %d, progress %.0f%%\n",
			m_pPoint->PointIndex(), inside[DOD_TEAM_ALLIES], inside[DOD_TEAM_AXIS], m_iNumCap[DOD_TEAM_ALLIES], m_iNumCap[DOD_TEAM_AXIS],
			m_iCanCap[DOD_TEAM_ALLIES], m_iCanCap[DOD_TEAM_AXIS], st ? (int)st->owner : -1, st ? st->capture_progress * 100.0f : 0.0f );
	}
	{
		int allies = ( m_iCanCap[DOD_TEAM_ALLIES] && inside[DOD_TEAM_ALLIES] >= m_iNumCap[DOD_TEAM_ALLIES] ) ? inside[DOD_TEAM_ALLIES] : 0;
		int axis = ( m_iCanCap[DOD_TEAM_AXIS] && inside[DOD_TEAM_AXIS] >= m_iNumCap[DOD_TEAM_AXIS] ) ? inside[DOD_TEAM_AXIS] : 0;
		const DodObjectiveState *before = Dod_GetObjective( m_pPoint->PointIndex() );
		const int owner_before = before ? (int)before->owner : -1;

		if( allies && axis && !m_bContested ) {
			m_bContested = TRUE;
			ALERT( at_console, "DoD capture contested point %d: Allies %d, Axis %d, capture paused at %.0f%%\n", m_pPoint->PointIndex(),
				allies, axis, before ? before->capture_progress * 100.0f : 0.0f );
		} else if( m_bContested && !( allies && axis ) ) {
			m_bContested = FALSE;
			ALERT( at_console, "DoD capture contest over at point %d (%s inside)\n", m_pPoint->PointIndex(),
				allies ? "Allies" : ( axis ? "Axis" : "nobody" ) );
		}

		if( ( allies || axis ) && !m_bStarted ) {
			const DodTeam starter = allies >= axis ? DOD_TEAM_ALLIES : DOD_TEAM_AXIS;
			if( owner_before == (int)starter )
				return;	/* already theirs: nothing to start */
			m_bStarted = TRUE;
			ALERT( at_console, "DoD capture start point %d by %s\n", m_pPoint->PointIndex(), starter == DOD_TEAM_ALLIES ? "Allies" : "Axis" );
			if( m_iszStartCap[starter] )
				FireTargets( STRING( m_iszStartCap[starter] ), this, this, USE_TOGGLE, 0 );
		} else if( !allies && !axis ) {
			m_bStarted = FALSE;
		}
		if( Dod_TickObjective( m_pPoint->PointIndex(), allies, axis, 0.25f ) ) {
			const DodObjectiveState *after = Dod_GetObjective( m_pPoint->PointIndex() );
			const DodTeam owner = after ? after->owner : DOD_TEAM_UNASSIGNED;
			if( owner == DOD_TEAM_ALLIES || owner == DOD_TEAM_AXIS )
				DodGC_CreditCapture( owner, m_pPoint->PointIndex(), pev->origin, 0.0f, &pev->absmin, &pev->absmax );
			ALERT( at_console, "DoD capture complete point %d owner %s\n", m_pPoint->PointIndex(), owner == DOD_TEAM_ALLIES ? "Allies" : "Axis" );
			UTIL_ClientPrintAll( HUD_PRINTCENTER, UTIL_VarArgs( "%s captured control point %d",
				owner == DOD_TEAM_ALLIES ? "Allies" : "Axis", m_pPoint->PointIndex() ) );
			if( ( owner == DOD_TEAM_ALLIES || owner == DOD_TEAM_AXIS ) && m_iszEndCap[owner] )
				FireTargets( STRING( m_iszEndCap[owner] ), this, this, USE_TOGGLE, 0 );
			m_bStarted = FALSE;
		}
	}
}

/* particle_shooter: the map's smoke and flame emitters (charlie 17, caen 7). Retail runs a client particle system; this build sends one
 * TE_SMOKE puff per fire_delay to the clients that can see it, from a single small sprite (gas_puff_01, 9 KB: the map's own
 * smoke_ia.spr is 197 KB and flames.spr 72 KB, too much for the GC heap), and at most DOD_PUFFS_PER_SECOND puffs per second server-wide
 * (the client has 8 temporary entities). A shooter starts on with spawnflag 32 and is toggled by use; num_particles counts puffs
 * (-1 endless). */
#define DOD_PUFF_SPRITE "sprites/gas_puff_01.spr"
#define DOD_PUFFS_PER_SECOND 6
static float g_dod_puff_window;
static int g_dod_puffs_in_window;

class CDoDParticleShooter : public CPointEntity
{
public:
	void Spawn( void );
	void KeyValue( KeyValueData *pkvd );
	void EXPORT PuffThink( void );
	void EXPORT CheckThink( void );
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
private:
	int m_iSprite;
	int m_iRemaining;	/* puffs left, -1 endless */
	int m_iCount;
	float m_flDelay;
	float m_flSize;
	int m_iFramerate;
	BOOL m_bOn;
};

LINK_ENTITY_TO_CLASS( particle_shooter, CDoDParticleShooter )

void CDoDParticleShooter::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "num_particles" ) ) {
		m_iCount = atoi( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "fire_delay" ) ) {
		m_flDelay = (float)atof( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "size" ) ) {
		m_flSize = (float)atof( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "framerate" ) ) {
		m_iFramerate = atoi( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else CPointEntity::KeyValue( pkvd );	/* the rest (velocity, colour, life...) is not used by the puff */
}

void CDoDParticleShooter::Spawn( void )
{
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	if( m_flDelay < 0.25f ) m_flDelay = 0.25f;
	if( m_iCount == 0 ) m_iCount = 10;
	if( m_flSize <= 0.0f ) m_flSize = 16.0f;
	if( m_iFramerate <= 0 ) m_iFramerate = 4;
	PRECACHE_MODEL( DOD_PUFF_SPRITE );
	m_iSprite = MODEL_INDEX( DOD_PUFF_SPRITE );
	m_iRemaining = m_iCount;
	m_bOn = FALSE;
	SetThink( &CDoDParticleShooter::CheckThink );
	pev->nextthink = gpGlobals->time + 2.0f;	/* once the whole map is loaded: does this emitter earn its edict? */
	ALERT( at_console, "DoD particle_shooter at %.0f %.0f %.0f: %s, delay %.2f, puffs %d, size %.0f\n", pev->origin.x, pev->origin.y, pev->origin.z, ( pev->spawnflags & 32 ) ? "starts on" : "waits for use", m_flDelay, m_iCount, m_flSize );
}

/* An emitter is an edict, and charlie sits at about 420 of 460 with the bots needing 26 free: an emitter that nothing can switch on
 * (no entity targets it: the map logic that would is dropped on this build) or that would leave less than DOD_PUFF_EDICT_MARGIN free
 * removes itself two seconds after the map has loaded. */
#define DOD_PUFF_EDICT_MARGIN 50
void CDoDParticleShooter::CheckThink( void )
{
	const BOOL start_on = ( pev->spawnflags & 32 ) != 0;
	const BOOL targeted = pev->targetname && !FNullEnt( FIND_ENTITY_BY_STRING( NULL, "target", STRING( pev->targetname ) ) );
	const int free_edicts = gpGlobals->maxEntities - g_engfuncs.pfnNumberOfEntities();
	if( ( !start_on && !targeted ) || free_edicts < DOD_PUFF_EDICT_MARGIN ) {
		ALERT( at_console, "DoD particle_shooter at %.0f %.0f %.0f removed (%s, %d edicts free)\n", pev->origin.x, pev->origin.y, pev->origin.z,
			( !start_on && !targeted ) ? "nothing switches it on" : "edict budget", free_edicts );
		UTIL_Remove( this );
		return;
	}
	SetThink( &CDoDParticleShooter::PuffThink );
	if( start_on ) {
		m_bOn = TRUE;
		pev->nextthink = gpGlobals->time + RANDOM_FLOAT( 0.0f, m_flDelay );
	} else {
		pev->nextthink = 0.0f;
	}
}

void CDoDParticleShooter::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	const BOOL want = useType == USE_ON ? TRUE : useType == USE_OFF ? FALSE : !m_bOn;
	if( want == m_bOn )
		return;
	m_bOn = want;
	if( m_bOn ) {
		m_iRemaining = m_iCount;
		pev->nextthink = gpGlobals->time + 0.1f;
	} else {
		pev->nextthink = 0.0f;
	}
}

void CDoDParticleShooter::PuffThink( void )
{
	if( !m_bOn )
		return;
	pev->nextthink = gpGlobals->time + m_flDelay;
	if( !Dod_PuffAllowed( gpGlobals->time, &g_dod_puff_window, &g_dod_puffs_in_window, DOD_PUFFS_PER_SECOND ) )
		return;	/* the budget for this second is spent; try again next delay */
	{
		static int logged;
		if( logged < 4 ) {
			logged++;
			ALERT( at_console, "DoD particle_shooter puff at %.0f %.0f %.0f\n", pev->origin.x, pev->origin.y, pev->origin.z );
		}
		const int scale = Dod_PuffScale( m_flSize );
		MESSAGE_BEGIN( MSG_PVS, SVC_TEMPENTITY, pev->origin );
			WRITE_BYTE( TE_SMOKE );
			WRITE_COORD( pev->origin.x );
			WRITE_COORD( pev->origin.y );
			WRITE_COORD( pev->origin.z );
			WRITE_SHORT( m_iSprite );
			WRITE_BYTE( scale );
			WRITE_BYTE( m_iFramerate > 20 ? 20 : m_iFramerate );
		MESSAGE_END();
	}
	if( m_iRemaining > 0 && --m_iRemaining == 0 ) {
		m_bOn = FALSE;
		pev->nextthink = 0.0f;
	}
}

/* info_doddetect: the map's game rules entity. The SDK links the name to a plain point entity (which keeps no keys); the integrator drops
 * that line so this class takes it. Read here: the sides' respawn factors (detect_allies_respawnfactor / detect_axis_respawnfactor, the
 * delay of the respawn waves relative to the base: charlie Allies 0.5, Axis 1.5). The country and paratrooper keys are still taken from the
 * map name (dod_weapons.c). */
class CDoDDetect : public CPointEntity
{
public:
	void Spawn( void );
	void KeyValue( KeyValueData *pkvd );
private:
	float m_flAlliesFactor, m_flAxisFactor;
};

LINK_ENTITY_TO_CLASS( info_doddetect, CDoDDetect )

void CDoDDetect::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "detect_allies_respawnfactor" ) ) {
		m_flAlliesFactor = (float)atof( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "detect_axis_respawnfactor" ) ) {
		m_flAxisFactor = (float)atof( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else CPointEntity::KeyValue( pkvd );
}

void CDoDDetect::Spawn( void )
{
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	if( m_flAlliesFactor > 0.0f )
		Dod_SetRespawnFactor( DOD_TEAM_ALLIES, m_flAlliesFactor );
	if( m_flAxisFactor > 0.0f )
		Dod_SetRespawnFactor( DOD_TEAM_AXIS, m_flAxisFactor );
	ALERT( at_console, "DoD rules entity: respawn factors Allies %.2f, Axis %.2f\n", m_flAlliesFactor > 0.0f ? m_flAlliesFactor : 1.0f,
		m_flAxisFactor > 0.0f ? m_flAxisFactor : 1.0f );
}

/* dod_point_relay: forwards a use to its target (retail relays capture events to map logic). */
class CDoDPointRelay : public CPointEntity
{
public:
	void KeyValue( KeyValueData *pkvd );
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
};

LINK_ENTITY_TO_CLASS( dod_point_relay, CDoDPointRelay )

void CDoDPointRelay::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "dod_relay_team" ) )
		pkvd->fHandled = TRUE;	/* team filter: not applied (unverified retail semantics) */
	else
		CPointEntity::KeyValue( pkvd );
}

void CDoDPointRelay::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if( pev->target )
		FireTargets( STRING( pev->target ), pActivator, this, useType, value );
}

/* Fire a map target by name. Entities the GC engine drops while loading a dense map (the
 * CapFaces optional-entity filter skips multi_manager, trigger_relay, ...) are simply absent, so
 * say so instead of failing silently: a missing win target means the round never ends. */
static void DodGC_FireMapTarget( const char *target, CBaseEntity *caller, const char *what )
{
	if( !target || !target[0] )
		return;
	CBaseEntity *found = UTIL_FindEntityByTargetname( NULL, target );
	if( !found ) {
		ALERT( at_console, "DoD %s: target '%s' does not exist (dropped by the GC entity filter?)\n", what, target );
		return;
	}
	ALERT( at_console, "DoD %s fires '%s' (%s)\n", what, target, STRING( found->pev->classname ) );
	FireTargets( target, caller, caller, USE_TOGGLE, 0 );
}

/* dod_round_timer: retail's round clock. It sets the round length (round_timer_length seconds,
 * every round) and fires its target when the clock runs out, so the map decides who wins a
 * timeout (charlie: axiswin_mm, an Axis win). The rules core counts the clock down and only
 * reports the timeout (timeout_serial); with a timer present it no longer ends the round. */
class CDoDRoundTimer : public CPointEntity
{
public:
	void Spawn( void );
	void KeyValue( KeyValueData *pkvd );
	void Think( void );
private:
	float m_flLength;
	int m_iSeenTimeout;
};

LINK_ENTITY_TO_CLASS( dod_round_timer, CDoDRoundTimer )

void CDoDRoundTimer::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "round_timer_length" ) ) {
		m_flLength = (float)atof( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else CPointEntity::KeyValue( pkvd );
}

void CDoDRoundTimer::Spawn( void )
{
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	if( m_flLength > 0.0f ) {
		Dod_SetTimerDriven( 1 );
		Dod_SetRoundClock( m_flLength );
		m_iSeenTimeout = Dod_GetState()->timeout_serial;
		SetThink( &CDoDRoundTimer::Think );
		pev->nextthink = gpGlobals->time + 0.25f;
		ALERT( at_console, "DoD round timer %.0f s target=%s\n", m_flLength, pev->target ? STRING( pev->target ) : "-" );
	}
}

void CDoDRoundTimer::Think( void )
{
	const DodGameRulesState *state = Dod_GetState();
	pev->nextthink = gpGlobals->time + 0.25f;
	if( state->timeout_serial == m_iSeenTimeout )
		return;
	m_iSeenTimeout = state->timeout_serial;
	ALERT( at_console, "DoD round timer expired (round %d)\n", state->round_number );
	if( pev->target )
		DodGC_FireMapTarget( STRING( pev->target ), this, "round timer" );
}

/* dod_control_point_master: when a capture leaves one team holding every control point it fires
 * that team's capture target once (charlie: allies_capture_target = allieswin_mm). Capture events
 * come from the rules core (capture_serial); a team that already held every point at round start
 * (Axis on charlie) does not trigger anything, as in retail where only a capture completes it.
 * point_give_delay_time is the delay of the timed award: every delay each point gives its owner its point_pointvalue
 * (Dod_SetPointGiveDelay). */
class CDoDControlPointMaster : public CPointEntity
{
public:
	CDoDControlPointMaster();
	void Spawn( void );
	void KeyValue( KeyValueData *pkvd );
	void Think( void );
private:
	string_t m_iszTarget[3];	/* indexed by DodTeam */
	int m_iSeenCapture;
};

LINK_ENTITY_TO_CLASS( dod_control_point_master, CDoDControlPointMaster )

CDoDControlPointMaster::CDoDControlPointMaster()
{
	m_iszTarget[0] = m_iszTarget[DOD_TEAM_ALLIES] = m_iszTarget[DOD_TEAM_AXIS] = 0;
	m_iSeenCapture = 0;
}

void CDoDControlPointMaster::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "allies_capture_target" ) ) {
		m_iszTarget[DOD_TEAM_ALLIES] = ALLOC_STRING( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "axis_capture_target" ) ) {
		m_iszTarget[DOD_TEAM_AXIS] = ALLOC_STRING( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "point_give_delay_time" ) ) {
		Dod_SetPointGiveDelay( (float)atof( pkvd->szValue ) ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "cpm_group" ) ) {
		pkvd->fHandled = TRUE;
	} else CPointEntity::KeyValue( pkvd );
}

void CDoDControlPointMaster::Spawn( void )
{
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	Dod_SetMasterDriven( 1 );
	m_iSeenCapture = Dod_GetState()->capture_serial;
	SetThink( &CDoDControlPointMaster::Think );
	pev->nextthink = gpGlobals->time + 0.25f;
	ALERT( at_console, "DoD control point master allies=%s axis=%s\n",
		m_iszTarget[DOD_TEAM_ALLIES] ? STRING( m_iszTarget[DOD_TEAM_ALLIES] ) : "-",
		m_iszTarget[DOD_TEAM_AXIS] ? STRING( m_iszTarget[DOD_TEAM_AXIS] ) : "-" );
}

void CDoDControlPointMaster::Think( void )
{
	const DodGameRulesState *state = Dod_GetState();
	pev->nextthink = gpGlobals->time + 0.25f;
	if( state->capture_serial == m_iSeenCapture )
		return;
	m_iSeenCapture = state->capture_serial;
	ALERT( at_console, "DoD master: capture #%d by %s, Allies hold %d / Axis hold %d of %d points\n", state->capture_serial,
		state->last_capture_team == DOD_TEAM_ALLIES ? "Allies" : "Axis", state->controlled_objectives[DOD_TEAM_ALLIES],
		state->controlled_objectives[DOD_TEAM_AXIS], state->objective_count );
	if( state->phase != DOD_ROUND_ACTIVE || !Dod_TeamOwnsAllObjectives( state->last_capture_team ) )
		return;
	const DodTeam team = state->last_capture_team;
	ALERT( at_console, "DoD master: %s hold every control point, target=%s\n", team == DOD_TEAM_ALLIES ? "Allies" : "Axis",
		m_iszTarget[team] ? STRING( m_iszTarget[team] ) : "-" );
	if( m_iszTarget[team] )
		DodGC_FireMapTarget( STRING( m_iszTarget[team] ), this, "master" );
}

/* dod_score_ent: gives its team score_points when used. With score_resetplayerstime > 0 it is a
 * round win (retail resets the players after that many seconds): the round ends with that team as
 * the winner and restarts after the delay. score_nextmapdelay (a map change) is not implemented;
 * charlie sets it to 0. message is a localisation key (MAP_AXIS_VICTORY2), which the lean HUD
 * cannot resolve, so the round announcement text comes from CHalfLifeDoD::Think. */
class CDoDScoreEnt : public CPointEntity
{
public:
	CDoDScoreEnt();
	void Spawn( void );
	void KeyValue( KeyValueData *pkvd );
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
private:
	int m_iTeam;
	int m_iPoints;
	float m_flResetPlayersTime;
};

LINK_ENTITY_TO_CLASS( dod_score_ent, CDoDScoreEnt )

CDoDScoreEnt::CDoDScoreEnt()
{
	m_iTeam = DOD_TEAM_UNASSIGNED;
	m_iPoints = 0;
	m_flResetPlayersTime = 0.0f;
}

void CDoDScoreEnt::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "team" ) ) {
		m_iTeam = atoi( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "score_points" ) ) {
		m_iPoints = atoi( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "score_resetplayerstime" ) ) {
		m_flResetPlayersTime = (float)atof( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "score_nextmapdelay" ) || FStrEq( pkvd->szKeyName, "score_resetitemtime" )
		|| FStrEq( pkvd->szKeyName, "noise" ) || FStrEq( pkvd->szKeyName, "message" ) ) {
		pkvd->fHandled = TRUE;
	} else CPointEntity::KeyValue( pkvd );
}

void CDoDScoreEnt::Spawn( void )
{
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	ALERT( at_console, "DoD score ent '%s' team=%d points=%d restart=%.0f\n", pev->targetname ? STRING( pev->targetname ) : "-",
		m_iTeam ? m_iTeam : pev->team, m_iPoints, m_flResetPlayersTime );
}

void CDoDScoreEnt::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	/* The engine stores the `team` key in pev->team itself, so KeyValue never sees it. */
	const DodTeam team = (DodTeam)( m_iTeam ? m_iTeam : pev->team );
	if( team != DOD_TEAM_ALLIES && team != DOD_TEAM_AXIS )
		return;
	const DodGameRulesState *state = Dod_GetState();
	if( state->phase != DOD_ROUND_ACTIVE )
		return;
	Dod_AddTeamScore( team, m_iPoints );
	ALERT( at_console, "DoD score ent '%s' used: %s +%d%s\n", pev->targetname ? STRING( pev->targetname ) : "-", team == DOD_TEAM_ALLIES ? "Allies" : "Axis", m_iPoints,
		m_flResetPlayersTime > 0.0f ? " (round win)" : "" );
	if( m_flResetPlayersTime > 0.0f ) {
		Dod_EndRoundDelayed( team, m_flResetPlayersTime );
		ALERT( at_console, "DoD round %d won by %s, restart in %.0f s (rounds won allies=%d axis=%d)\n", state->round_number,
			team == DOD_TEAM_ALLIES ? "Allies" : "Axis", m_flResetPlayersTime,
			state->rounds_won[DOD_TEAM_ALLIES], state->rounds_won[DOD_TEAM_AXIS] );
	}
}

int DodGC_ListObjectives( DodGCObjectiveView *out, int max )
{
	int count = 0;
	CBaseEntity *entity = NULL;
	while( count < max && ( entity = UTIL_FindEntityByClassname( entity, "dod_control_point" ) ) != NULL ) {
		const CDoDControlPoint *point = (const CDoDControlPoint *)entity;
		const DodObjectiveState *objective = Dod_GetObjective( point->PointIndex() );
		if( !objective )
			continue;
		DodGCObjectiveView *view = &out[count++];
		view->id = objective->id;
		view->owner = objective->owner;
		view->capturing = objective->capturing_team;
		point->FillView( view );
	}
	return count;
}

void DodGC_ResetMapObjectives( void )
{
	Dod_ClearObjectives();
}
