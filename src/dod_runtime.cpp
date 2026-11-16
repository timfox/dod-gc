/* HLSDK bridge for the Day of Defeat rules core. */
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "gamerules.h"
#include "teamplay_gamerules.h"
#include "dod_runtime.h"
#include "client.h"
#include "dod_hud.h"
#include "dod_game.h"
#include "dod_weapons.h"
#include "dod_bot.h"
#include "dod_bots.h"

#include <stdlib.h>
#include <string.h>

/* Round length when mp_timelimit is 0, and how many rounds win a match. */
static cvar_t dod_roundtime = { "dod_roundtime", "600", FCVAR_SERVER };
static cvar_t dod_rounds = { "dod_rounds", "2", FCVAR_SERVER };
/* dod_spectate 1: the local human joins as a spectator instead of picking a side,
 * to watch the bots fight. dod_spec_auto 1: a director moves the camera between
 * the bots by itself (any manual target pick pauses it for a while). */
static cvar_t dod_spectate = { "dod_spectate", "0", FCVAR_SERVER };
static cvar_t dod_spec_auto = { "dod_spec_auto", "1", FCVAR_SERVER };
/* Team body models (models/player/<name>/<name>.mdl, split onto the disc as
 * gc_studio/<name>.mdl). A map cfg can switch to us-para/axis-para/brit-inf. */
static cvar_t dod_allies_body = { "dod_allies_body", "us-inf", FCVAR_SERVER };
static cvar_t dod_axis_body = { "dod_axis_body", "axis-inf", FCVAR_SERVER };
static BOOL g_dod_cvars_registered;
#define DOD_SPEC_SLOTS 8	/* spectator director state for client slots 1..8 */
static float g_spec_hold_until[DOD_SPEC_SLOTS + 1];
static float g_spec_target_since[DOD_SPEC_SLOTS + 1];
static int g_spec_last_target[DOD_SPEC_SLOTS + 1];
static float g_spec_next_think;
/* dod_spectate 1: InitHUD asks for the join, SpectatorThink makes it. The first
 * InitHUD runs inside CBasePlayer::Spawn, whose tail (game_playerspawn, the kit,
 * observer flags reset) undid a join made there, and bots are not yet marked as
 * bots at that point. */
static BOOL g_spec_join_pending[DOD_MAX_PLAYERS + 1];
static int gmsgDodHud;
static float g_dod_next_hud;
static int g_dod_seen_restart;
static int g_dod_seen_match;

#define DOD_POINT_SCORE_PER_MINUTE 2.0f

extern int gmsgShowMenu;
extern int gmsgScoreInfo;

static int g_dod_menu_state[DOD_MAX_PLAYERS + 1];
static int g_dod_winner_announced;
static float g_dod_next_status;

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

static void DodGC_ShowClassMenu( CBasePlayer *pPlayer )
{
	const int index = DodGC_PlayerIndex( pPlayer );
	const DodTeam team = DodGC_TeamForPlayer( pPlayer );
	char text[256];
	int len;
	if( index > 0 && index <= DOD_MAX_PLAYERS )
		g_dod_menu_state[index] = 2;
	len = snprintf( text, sizeof( text ), "\\y%s - CHOOSE A CLASS\n\\w\n",
		team == DOD_TEAM_AXIS ? "AXIS" : "ALLIES" );
	for( int c = DOD_CLASS_RIFLEMAN; c < DOD_CLASS_COUNT && len < (int)sizeof( text ); c++ )
		len += snprintf( text + len, sizeof( text ) - len, "%d. %s (%s)\n", c,
			Dod_TeamClassName( team, (DodClass)c ), Dod_TeamClassWeapon( team, (DodClass)c ) );
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

/* Become a spectator: no class, no gun, no spawn, no collision. Only the local
 * human does this (a bot never watches), and no death or frag is recorded. */
static void DodGC_JoinSpectator( CBasePlayer *pPlayer )
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
	ClientPrint( pPlayer->pev, HUD_PRINTTALK, "You are spectating. Fire/Aim: next/previous player, Jump: camera mode.\n" );
}

/* Leave spectating to play: the HLSDK respawns the player as an ordinary one. */
static void DodGC_LeaveSpectator( CBasePlayer *pPlayer )
{
	if( pPlayer && pPlayer->IsObserver() ) {
		pPlayer->StopObserver();
		pPlayer->pev->iuser1 = pPlayer->pev->iuser2 = 0;
	}
}

/* The body the engine draws for a player comes from the "model" userinfo key
 * (models/player/<model>/<model>.mdl); the server entity stays models/player.mdl
 * for hulls. Bots are fake clients with their own infobuffer, so this covers them. */
static const char *DodGC_BodyForTeam( DodTeam team )
{
	if( team == DOD_TEAM_AXIS )
		return CVAR_GET_STRING( "dod_axis_body" );
	if( team == DOD_TEAM_ALLIES )
		return CVAR_GET_STRING( "dod_allies_body" );
	return NULL;
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
		pPlayer->GiveNamedItem( "weapon_crowbar" );
		if( sidearm ) {
			pPlayer->GiveNamedItem( sidearm->classname );
			pPlayer->GiveAmmo( sidearm->reserve, sidearm->ammo, sidearm->ammo_max );
		}
		if( weapon ) {
			/* Last, so it is the best weapon and gets drawn. */
			pPlayer->GiveNamedItem( weapon->classname );
			pPlayer->GiveAmmo( weapon->reserve, weapon->ammo, weapon->ammo_max );
		}
		if( Dod_GetLoadout( team, player_class, &load ) && load.grenades > 0 ) {
			/* The weapon spawns with one grenade; top up to the loadout. */
			pPlayer->GiveNamedItem( "weapon_handgrenade" );
			if( load.grenades > 1 && Dod_GetGrenade( team, &grenade ) )
				pPlayer->GiveAmmo( load.grenades - 1, "Hand Grenade", grenade.max_carry );
		}
		DodBots_SetGranting( FALSE );
	}
	DodGC_ApplyBody( pPlayer );
	DodGC_ClearMenu( pPlayer );
}

CHalfLifeDoD::CHalfLifeDoD()
{
	DodBots_Init();
	float round_seconds;
	if( !g_dod_cvars_registered ) {
		CVAR_REGISTER( &dod_roundtime );
		CVAR_REGISTER( &dod_rounds );
		CVAR_REGISTER( &dod_spectate );
		CVAR_REGISTER( &dod_spec_auto );
		CVAR_REGISTER( &dod_allies_body );
		CVAR_REGISTER( &dod_axis_body );
		g_dod_cvars_registered = TRUE;
	}
	Dod_GameInit();
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
	memset( g_dod_menu_state, 0, sizeof( g_dod_menu_state ) );
	g_dod_winner_announced = 0;
	g_dod_next_status = 0.0f;
	memset( g_spec_hold_until, 0, sizeof( g_spec_hold_until ) );
	memset( g_spec_target_since, 0, sizeof( g_spec_target_since ) );
	memset( g_spec_last_target, 0, sizeof( g_spec_last_target ) );
	g_spec_next_think = 0.0f;
	memset( g_spec_join_pending, 0, sizeof( g_spec_join_pending ) );
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

BOOL CHalfLifeDoD::ClientCommand( CBasePlayer *pPlayer, const char *pcmd )
{
	const int index = DodGC_PlayerIndex( pPlayer );
	if( FStrEq( pcmd, "dod_team" ) ) {
		const DodTeam team = (DodTeam)Dod_TeamFromName( CMD_ARGV( 1 ) );
		if( team == DOD_TEAM_SPECTATOR ) {
			DodGC_JoinSpectator( pPlayer );
			return TRUE;
		}
		if( team == DOD_TEAM_ALLIES || team == DOD_TEAM_AXIS ) {
			DodGC_LeaveSpectator( pPlayer );
			if( stricmp( pPlayer->m_szTeamName, Dod_TeamName( team ) ) )
				ChangePlayerTeam( pPlayer, Dod_TeamName( team ), TRUE, FALSE );
			Dod_AssignPlayerTeam( index, team );
			DodGC_ShowClassMenu( pPlayer );
		}
		return TRUE;
	}
	if( FStrEq( pcmd, "spectate" ) ) {
		DodGC_JoinSpectator( pPlayer );
		return TRUE;
	}
	if( FStrEq( pcmd, "dod_score" ) ) {
		/* Text scoreboard for the lean HUD: one HUD text block for the asker. */
		char board[512];
		char line[96];
		int len = snprintf( board, sizeof( board ), "SCOREBOARD\n" );
		hudtextparms_t params;
		memset( &params, 0, sizeof( params ) );
		params.channel = 4;
		params.x = 0.08f;
		params.y = 0.25f;
		params.r1 = 255; params.g1 = 255; params.b1 = 255; params.a1 = 255;
		params.holdTime = 6.0f;
		for( int i = 1; i <= gpGlobals->maxClients && len < (int)sizeof( board ) - 1; i++ ) {
			CBasePlayer *other = (CBasePlayer *)UTIL_PlayerByIndex( i );
			if( !other || !other->IsPlayer() )
				continue;
			if( Dod_FormatPlayerLine( i, STRING( other->pev->netname ), line, sizeof( line ) ) > 0 )
				len += snprintf( board + len, sizeof( board ) - len, "%s\n", line );
		}
		UTIL_HudMessage( pPlayer, params, board );
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
			DodGC_ApplyClass( pPlayer, player_class );
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
			if( slot == 1 || slot == 2 ) {
				const DodTeam team = slot == 1 ? DOD_TEAM_ALLIES : DOD_TEAM_AXIS;
				DodGC_LeaveSpectator( pPlayer );
				if( stricmp( pPlayer->m_szTeamName, Dod_TeamName( team ) ) )
					ChangePlayerTeam( pPlayer, Dod_TeamName( team ), TRUE, FALSE );
				Dod_AssignPlayerTeam( index, team );
				DodGC_ShowClassMenu( pPlayer );
			}
			return TRUE;
		}
		if( g_dod_menu_state[index] == 2 && slot >= 1 && slot <= 6 ) {
			DodGC_ApplyClass( pPlayer, (DodClass)( DOD_CLASS_RIFLEMAN + slot - 1 ) );
			return TRUE;
		}
	}
	return CHalfLifeTeamplay::ClientCommand( pPlayer, pcmd );
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

void CHalfLifeDoD::PlayerSpawn( CBasePlayer *pPlayer )
{
	/* A round restart respawns living players, who still hold their items. The base spawn gives a
	 * crowbar again, and a duplicate crowbar (no ammo to extract) stays lying in the world: one
	 * leaked edict per player per round until the edict pool ran out. Start from nothing. */
	pPlayer->RemoveAllItems( FALSE );
	CHalfLifeTeamplay::PlayerSpawn( pPlayer );
	DodGC_EnsurePlayer( pPlayer );
	if( pPlayer->IsObserver() )
		return;
	const int index = DodGC_PlayerIndex( pPlayer );
	const DodPlayerState *state = Dod_GetPlayer( index );
	if( state && state->player_class != DOD_CLASS_NONE )
		DodGC_ApplyClass( pPlayer, state->player_class );
	else
		DodGC_ApplyClass( pPlayer, DOD_CLASS_RIFLEMAN );
}

void CHalfLifeDoD::PlayerKilled( CBasePlayer *pVictim, entvars_t *pKiller, entvars_t *pInflictor )
{
	const int attacker = pKiller ? ENTINDEX( ENT( pKiller ) ) : 0;
	Dod_RecordKill( attacker, DodGC_PlayerIndex( pVictim ) );
	CHalfLifeTeamplay::PlayerKilled( pVictim, pKiller, pInflictor );
}

void CHalfLifeDoD::ClientDisconnected( edict_t *pClient )
{
	if( pClient )
		Dod_PlayerDisconnect( ENTINDEX( pClient ) );
	CHalfLifeMultiplay::ClientDisconnected( pClient );
}

edict_t *CHalfLifeDoD::GetPlayerSpawnSpot( CBasePlayer *pPlayer )
{
	const DodTeam team = DodGC_TeamForPlayer( pPlayer );
	const char *classname = team == DOD_TEAM_AXIS ? "info_player_axis" : "info_player_allies";
	int count = 0;
	CBaseEntity *spot = NULL;
	CBaseEntity *chosen = NULL;
	while( ( spot = UTIL_FindEntityByClassname( spot, classname ) ) != NULL )
		++count;
	if( count > 0 ) {
		int pick = RANDOM_LONG( 0, count - 1 );
		spot = NULL;
		while( ( spot = UTIL_FindEntityByClassname( spot, classname ) ) != NULL ) {
			if( pick-- == 0 ) { chosen = spot; break; }
		}
	}
	if( !chosen )
		return CHalfLifeTeamplay::GetPlayerSpawnSpot( pPlayer );
	pPlayer->pev->origin = chosen->pev->origin + Vector( 0, 0, 1 );
	pPlayer->pev->v_angle = g_vecZero;
	pPlayer->pev->velocity = g_vecZero;
	pPlayer->pev->angles = chosen->pev->angles;
	pPlayer->pev->punchangle = g_vecZero;
	pPlayer->pev->fixangle = TRUE;
	return chosen->edict();
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
		if( i <= DOD_MAX_PLAYERS && g_spec_join_pending[i] ) {
			g_spec_join_pending[i] = FALSE;
			if( pSpec && pSpec->IsPlayer() && !pSpec->IsObserver() && !DodBots_IsBot( pSpec ) )
				DodGC_JoinSpectator( pSpec );
		}
		if( !pSpec || !pSpec->IsPlayer() || !pSpec->IsObserver() || DodBots_IsBot( pSpec ) )
			continue;
		if( slot && ( pSpec->pev->button & ( IN_ATTACK | IN_ATTACK2 | IN_JUMP ) ) )
			g_spec_hold_until[slot] = gpGlobals->time + DOD_SPEC_MANUAL_HOLD;
		mode = pSpec->pev->iuser1;
		if( mode != Dod_SpecSanitizeMode( mode ) ) {
			pSpec->Observer_SetMode( Dod_SpecSanitizeMode( mode ) );
			mode = pSpec->pev->iuser1;
		}
		if( !tick || !DodGC_SpecCandidates( pSpec, list, DOD_MAX_PLAYERS, &count ) )
			continue;
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
int CHalfLifeDoD::DeadPlayerWeapons( CBasePlayer *pPlayer )
{
	return GR_PLR_DROP_GUN_NO;
}

int CHalfLifeDoD::DeadPlayerAmmo( CBasePlayer *pPlayer )
{
	return GR_PLR_DROP_AMMO_NO;
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

void CHalfLifeDoD::Think( void )
{
	CHalfLifeTeamplay::Think();
	Dod_Think( gpGlobals->frametime );
	DodBots_Think();
	DodGC_SpectatorThink();
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
	}
	/* Match status for the lean HUD, which has no VGUI scoreboard. With the client
	 * sprite HUD on (cl_dodhud, a cvar the local client registers) the server sends
	 * the numbers for it to draw; otherwise it shows the text strip, once a second. */
	if( CVAR_GET_FLOAT( "cl_dodhud" ) != 0.0f ) {
		if( gpGlobals->time >= g_dod_next_hud ) {
			g_dod_next_hud = gpGlobals->time + 0.5f;
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
private:
	int m_iPointIndex;
	int m_iDefaultOwner;
	char m_szPointName[64];
	BOOL m_bAreaDriven;
};

LINK_ENTITY_TO_CLASS( dod_control_point, CDoDControlPoint )

CDoDControlPoint::CDoDControlPoint()
{
	m_iPointIndex = -1;
	m_iDefaultOwner = DOD_TEAM_UNASSIGNED;
	m_szPointName[0] = '\0';
	m_bAreaDriven = FALSE;
}

void CDoDControlPoint::SetAreaDriven( float capture_seconds )
{
	if( m_bAreaDriven )
		return;
	m_bAreaDriven = TRUE;
	if( capture_seconds > 0.0f )
		Dod_RegisterObjective( m_iPointIndex, DOD_OBJ_CAPTURE_POINT, (DodTeam)m_iDefaultOwner,
			capture_seconds, DOD_POINT_SCORE_PER_MINUTE );
}

void CDoDControlPoint::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "point_index" ) ) {
		m_iPointIndex = atoi( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "point_default_owner" ) ) {
		m_iDefaultOwner = atoi( pkvd->szValue ); pkvd->fHandled = TRUE;
	} else if( FStrEq( pkvd->szKeyName, "point_name" ) ) {
		strlcpy( m_szPointName, pkvd->szValue, sizeof( m_szPointName ) ); pkvd->fHandled = TRUE;
	} else CPointEntity::KeyValue( pkvd );
}

void CDoDControlPoint::Spawn( void )
{
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	if( m_iPointIndex < 0 ) m_iPointIndex = 0;
	Dod_RegisterObjective( m_iPointIndex, DOD_OBJ_CAPTURE_POINT,
		(DodTeam)m_iDefaultOwner, 10.0f, DOD_POINT_SCORE_PER_MINUTE );
	SetThink( &CDoDControlPoint::Think );
	pev->nextthink = gpGlobals->time + 0.25f;
}

void CDoDControlPoint::Think( void )
{
	int allies = 0, axis = 0, i;
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
	m_bResolved = m_bStarted = FALSE;
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
		}
		ALERT( at_console, "DoD capture area target=%s point=%d time=%.0f absmin=%.0f,%.0f,%.0f absmax=%.0f,%.0f,%.0f\n",
			m_iszTarget ? STRING( m_iszTarget ) : "-", m_pPoint ? m_pPoint->PointIndex() : -1, m_flTimeToCap,
			pev->absmin.x, pev->absmin.y, pev->absmin.z, pev->absmax.x, pev->absmax.y, pev->absmax.z );
	}
	if( !m_pPoint )
		return;	/* an area without a point (charlie's `*8`) only gates map logic */
	if( m_iszMaster && !UTIL_IsMasterTriggered( m_iszMaster, NULL ) )
		return;
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
	{
		int allies = ( m_iCanCap[DOD_TEAM_ALLIES] && inside[DOD_TEAM_ALLIES] >= m_iNumCap[DOD_TEAM_ALLIES] ) ? inside[DOD_TEAM_ALLIES] : 0;
		int axis = ( m_iCanCap[DOD_TEAM_AXIS] && inside[DOD_TEAM_AXIS] >= m_iNumCap[DOD_TEAM_AXIS] ) ? inside[DOD_TEAM_AXIS] : 0;
		const DodObjectiveState *before = Dod_GetObjective( m_pPoint->PointIndex() );
		const int owner_before = before ? (int)before->owner : -1;

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
			ALERT( at_console, "DoD capture complete point %d owner %s\n", m_pPoint->PointIndex(), owner == DOD_TEAM_ALLIES ? "Allies" : "Axis" );
			UTIL_ClientPrintAll( HUD_PRINTCENTER, UTIL_VarArgs( "%s captured control point %d",
				owner == DOD_TEAM_ALLIES ? "Allies" : "Axis", m_pPoint->PointIndex() ) );
			if( ( owner == DOD_TEAM_ALLIES || owner == DOD_TEAM_AXIS ) && m_iszEndCap[owner] )
				FireTargets( STRING( m_iszEndCap[owner] ), this, this, USE_TOGGLE, 0 );
			m_bStarted = FALSE;
		}
	}
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
 * point_give_delay_time / point_pointvalue (periodic point score) are not used: charlie sets the
 * point value to 0, and the core awards the per-point score continuously. */
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
	} else if( FStrEq( pkvd->szKeyName, "cpm_group" ) || FStrEq( pkvd->szKeyName, "point_give_delay_time" ) ) {
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

void DodGC_ResetMapObjectives( void )
{
	Dod_ClearObjectives();
}
