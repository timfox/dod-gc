/* HLSDK bridge for the Day of Defeat rules core. */
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "gamerules.h"
#include "teamplay_gamerules.h"
#include "dod_runtime.h"
#include "client.h"
#include "dod_hud.h"
#include "dod_game.h"
#include "dod_weapons.h"
#include "dod_bot.h"

#include <stdlib.h>
#include <string.h>

/* Round length when mp_timelimit is 0, and how many rounds win a match. */
static cvar_t dod_roundtime = { "dod_roundtime", "600", FCVAR_SERVER };
static cvar_t dod_rounds = { "dod_rounds", "2", FCVAR_SERVER };
static BOOL g_dod_cvars_registered;
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
	DodGC_SendMenu( pPlayer, 0x0003,
		"\\yDAY OF DEFEAT - CHOOSE A SIDE\n\\w\n"
		"1. Allies\n2. Axis\n\n"
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

static void DodGC_ApplyClass( CBasePlayer *pPlayer, DodClass player_class )
{
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
	DodGC_ClearMenu( pPlayer );
}

CHalfLifeDoD::CHalfLifeDoD()
{
	DodBots_Init();
	ALERT( at_console, "BOTINIT CHalfLifeDoD constructed\n" );
	float round_seconds;
	if( !g_dod_cvars_registered ) {
		CVAR_REGISTER( &dod_roundtime );
		CVAR_REGISTER( &dod_rounds );
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
	DodGC_ApplyClass( pPlayer, player_class );
}

BOOL CHalfLifeDoD::ClientCommand( CBasePlayer *pPlayer, const char *pcmd )
{
	const int index = DodGC_PlayerIndex( pPlayer );
	if( FStrEq( pcmd, "dod_team" ) ) {
		const DodTeam team = (DodTeam)Dod_TeamFromName( CMD_ARGV( 1 ) );
		if( team == DOD_TEAM_ALLIES || team == DOD_TEAM_AXIS ) {
			if( stricmp( pPlayer->m_szTeamName, Dod_TeamName( team ) ) )
				ChangePlayerTeam( pPlayer, Dod_TeamName( team ), TRUE, FALSE );
			Dod_AssignPlayerTeam( index, team );
			DodGC_ShowClassMenu( pPlayer );
		}
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
			if( slot == 1 || slot == 2 ) {
				const DodTeam team = slot == 1 ? DOD_TEAM_ALLIES : DOD_TEAM_AXIS;
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
	DodGC_ShowTeamMenu( pPlayer );
}

void CHalfLifeDoD::PlayerSpawn( CBasePlayer *pPlayer )
{
	CHalfLifeTeamplay::PlayerSpawn( pPlayer );
	DodGC_EnsurePlayer( pPlayer );
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

void CHalfLifeDoD::Think( void )
{
	CHalfLifeTeamplay::Think();
	Dod_Think( gpGlobals->frametime );
	DodBots_Think();
	const DodGameRulesState *state = Dod_GetState();
	if( state && state->phase == DOD_ROUND_INTERMISSION && !g_dod_winner_announced ) {
		const char *winner = state->winner == DOD_TEAM_ALLIES ? "Allies win the round" :
			state->winner == DOD_TEAM_AXIS ? "Axis win the round" : "Round tied";
		UTIL_ClientPrintAll( HUD_PRINTCENTER, UTIL_VarArgs( "%s\nNext round in %d seconds", winner,
			(int)DOD_INTERMISSION_SECONDS ) );
		g_dod_winner_announced = 1;
	}
	if( state && state->restart_serial != g_dod_seen_restart ) {
		/* The intermission ran out: everybody starts the new round at their
		 * spawn with a fresh loadout (bots included). */
		g_dod_seen_restart = state->restart_serial;
		g_dod_winner_announced = 0;
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
private:
	int m_iPointIndex;
	int m_iDefaultOwner;
	char m_szPointName[64];
};

LINK_ENTITY_TO_CLASS( dod_control_point, CDoDControlPoint )

CDoDControlPoint::CDoDControlPoint()
{
	m_iPointIndex = -1;
	m_iDefaultOwner = DOD_TEAM_UNASSIGNED;
	m_szPointName[0] = '\0';
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

void DodGC_ResetMapObjectives( void )
{
	Dod_ClearObjectives();
}
