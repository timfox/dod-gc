/* Day of Defeat bots (fake clients); see dod_bot.h and dod_bots.c. */
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "gamerules.h"
#include "client.h"
#include "dod_game.h"
#include "dod_weapons.h"
#include "dod_bots.h"
#include "dod_bot.h"
#include "dod_runtime.h"

#include <math.h>
#include <string.h>

/* dod_bots: bots on top of the humans, filling free slots (0 = none). On
 * GameCube the engine decides how many slots there are (sv_gc_slots, 2 by
 * default), so 3 means "fill the server" without ever asking for more than the
 * slots allow. dod_bot_skill: aim 0-3. dod_bots_online: allow bots while the
 * network link is up (otherwise the free slot is kept for the peer). */
#if defined( __GAMECUBE__ )
static cvar_t dod_bots = { "dod_bots", "3", FCVAR_SERVER };
#else
static cvar_t dod_bots = { "dod_bots", "1", FCVAR_SERVER };
#endif
static cvar_t dod_bot_skill = { "dod_bot_skill", "1", FCVAR_SERVER };
static cvar_t dod_bots_online = { "dod_bots_online", "0", FCVAR_SERVER };

/* The most bots ever; the free slots are the real limit. */
#define DOD_BOT_HARD_CAP 7

#if defined( __GAMECUBE__ )
#define DOD_BOT_MAX_SLOTS 4	/* the listen server has 2 client slots */
#else
#define DOD_BOT_MAX_SLOTS 32
#endif

typedef struct DodBotState {
	float yaw, pitch;		/* current view */
	float hold_heading_until;	/* after a turn-away, keep going */
	float heading;			/* wander/goal yaw while nothing is in sight */
	float next_noise;		/* aim offset refresh */
	float noise_yaw, noise_pitch;
	float next_goal;
	Vector goal;
	int has_goal;
	int fire_phase;
	float next_respawn_press;
	int active;
	DodBotStuck stuck;
} DodBotState;

static DodBotState g_bot[DOD_BOT_MAX_SLOTS + 1];
static BOOL g_registered;
static BOOL g_granting;
static float g_next_plan;
static float g_first_human_time;

void DodBots_SetGranting( BOOL granting ) { g_granting = granting; }
BOOL DodBots_Granting( void ) { return g_granting; }

BOOL DodBots_IsBot( CBasePlayer *pPlayer )
{
	return pPlayer && pPlayer->pev && ( pPlayer->pev->flags & FL_FAKECLIENT ) ? TRUE : FALSE;
}

void DodBots_Init( void )
{
	if( !g_registered )
	{
		CVAR_REGISTER( &dod_bots );
		CVAR_REGISTER( &dod_bot_skill );
		CVAR_REGISTER( &dod_bots_online );
		g_registered = TRUE;
	}
	memset( g_bot, 0, sizeof( g_bot ) );
	g_next_plan = 0.0f;
	g_first_human_time = 0.0f;
}

static DodTeam TeamOf( CBasePlayer *pPlayer )
{
	return (DodTeam)Dod_TeamFromName( pPlayer->m_szTeamName );
}

static CBasePlayer *PlayerAt( int index )
{
	CBaseEntity *pEntity = UTIL_PlayerByIndex( index );
	if( !pEntity || !pEntity->IsPlayer() || !pEntity->pev )
		return NULL;
	return (CBasePlayer *)pEntity;
}

/* ------------------------------------------------------------- bot count */

static void CountPlayers( int *humans, int *bots, int team_counts[4], int special_counts[4][DOD_CLASS_COUNT] )
{
	int i;
	*humans = *bots = 0;
	memset( team_counts, 0, sizeof( int ) * 4 );
	memset( special_counts, 0, sizeof( int ) * 4 * DOD_CLASS_COUNT );
	for( i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = PlayerAt( i );
		const DodPlayerState *state;
		DodTeam team;
		if( !pPlayer )
			continue;
		if( DodBots_IsBot( pPlayer ) )
			( *bots )++;
		else
			( *humans )++;
		team = TeamOf( pPlayer );
		if( team == DOD_TEAM_ALLIES || team == DOD_TEAM_AXIS )
		{
			team_counts[team]++;
			state = Dod_GetPlayer( i );
			if( state && state->player_class > DOD_CLASS_NONE && state->player_class < DOD_CLASS_COUNT )
				special_counts[team][state->player_class]++;
		}
	}
}

static const char *const g_bot_names[] = {
	"Pvt. Miller", "Cpl. Jones", "Pvt. Weber", "Gefr. Keller",
	"Sgt. Walker", "Pvt. Brandt", "Cpl. Hayes", "Ofw. Vogel"
};

static void AddBot( int team_counts[4], int special_counts[4][DOD_CLASS_COUNT] )
{
	static int name_index;
	const char *name = g_bot_names[name_index++ % ( sizeof( g_bot_names ) / sizeof( g_bot_names[0] ) )];
	edict_t *pEdict;
	CBasePlayer *pBot;
	char reject[128];
	DodTeam team;
	DodClass player_class;

	pEdict = ( *g_engfuncs.pfnCreateFakeClient )( name );
	if( FNullEnt( pEdict ) )
		return;	/* no free client slot after all */
	reject[0] = '\0';
	ClientConnect( pEdict, name, "127.0.0.1", reject );
	ClientPutInServer( pEdict );

	pBot = (CBasePlayer *)CBaseEntity::Instance( pEdict );
	if( !pBot )
		return;
	team = Dod_BotPickTeam( team_counts[DOD_TEAM_ALLIES], team_counts[DOD_TEAM_AXIS] );
	player_class = Dod_BotPickClass( RANDOM_LONG( 0, 1000 ), special_counts[team] );
	memset( &g_bot[ENTINDEX( pEdict )], 0, sizeof( DodBotState ) );
	g_bot[ENTINDEX( pEdict )].active = 1;
	DodGC_BotJoin( pBot, team, player_class );
	team_counts[team]++;
	special_counts[team][player_class]++;
	ALERT( at_console, "DoD bot %s joined %s as %s\n", name, Dod_TeamName( team ), Dod_ClassName( player_class ) );
}

static void KickOneBot( void )
{
	int i;
	for( i = gpGlobals->maxClients; i >= 1; i-- )
	{
		CBasePlayer *pPlayer = PlayerAt( i );
		if( pPlayer && DodBots_IsBot( pPlayer ) )
		{
			SERVER_COMMAND( UTIL_VarArgs( "kick \"%s\"\n", STRING( pPlayer->pev->netname ) ) );
			g_bot[i].active = 0;
			return;
		}
	}
}

/* The GameCube network stack publishes its address in gc_net_status once the
 * link is up ("not started" / "no link ..." before). */
static BOOL NetworkUp( void )
{
	const char *status = CVAR_GET_STRING( "gc_net_status" );
	return status && status[0] >= '0' && status[0] <= '9';
}

static void PlanBots( void )
{
	int humans, bots, team_counts[4], special_counts[4][DOD_CLASS_COUNT], target;

	CountPlayers( &humans, &bots, team_counts, special_counts );
	if( humans > 0 && g_first_human_time == 0.0f )
		g_first_human_time = gpGlobals->time;
	if( humans == 0 )
	{
		g_first_human_time = 0.0f;
		return;	/* no bots on an empty server */
	}
	target = Dod_BotTargetCount(
		Dod_BotRequestedCount( (int)CVAR_GET_FLOAT( "dod_bots" ), NetworkUp(), CVAR_GET_FLOAT( "dod_bots_online" ) != 0.0f ),
		gpGlobals->maxClients, humans, DOD_BOT_HARD_CAP );
	if( bots > target )
		KickOneBot();
	else if( bots < target && gpGlobals->time - g_first_human_time > 3.0f )
		AddBot( team_counts, special_counts );	/* one per plan tick: no spawn burst */
}

/* -------------------------------------------------------------------- AI */

static BOOL CanSee( CBasePlayer *pBot, CBasePlayer *pTarget )
{
	TraceResult tr;
	Vector eye = pBot->pev->origin + pBot->pev->view_ofs;
	Vector aim = pTarget->pev->origin + pTarget->pev->view_ofs;
	UTIL_TraceLine( eye, aim, ignore_monsters, pBot->edict(), &tr );
	return tr.flFraction >= 0.99f || tr.pHit == pTarget->edict();
}

static CBasePlayer *FindEnemy( CBasePlayer *pBot, float *distance )
{
	CBasePlayer *best = NULL;
	float best_dist = 4000.0f;
	const DodTeam my_team = TeamOf( pBot );
	int i;
	for( i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pOther = PlayerAt( i );
		float dist;
		if( !pOther || pOther == pBot || !pOther->IsAlive() || ( pOther->pev->flags & FL_NOTARGET ) )
			continue;
		if( TeamOf( pOther ) == my_team || TeamOf( pOther ) == DOD_TEAM_UNASSIGNED )
			continue;
		dist = ( pOther->pev->origin - pBot->pev->origin ).Length();
		if( dist < best_dist && CanSee( pBot, pOther ) )
		{
			best = pOther;
			best_dist = dist;
		}
	}
	if( best )
		*distance = best_dist;
	return best;
}

static void PickGoal( DodBotState *state )
{
	/* Roam between the map's control points; with none, wander. */
	int count = 0, pick;
	CBaseEntity *point = NULL;
	state->has_goal = 0;
	while( ( point = UTIL_FindEntityByClassname( point, "dod_control_point" ) ) != NULL )
		count++;
	if( count > 0 )
	{
		pick = RANDOM_LONG( 0, count - 1 );
		point = NULL;
		while( ( point = UTIL_FindEntityByClassname( point, "dod_control_point" ) ) != NULL )
		{
			if( pick-- == 0 )
			{
				state->goal = point->pev->origin;
				state->has_goal = 1;
				break;
			}
		}
	}
	state->next_goal = gpGlobals->time + RANDOM_FLOAT( 15.0f, 30.0f );
}

static float YawTo( const Vector &from, const Vector &to )
{
	return (float)( atan2( to.y - from.y, to.x - from.x ) * 180.0 / M_PI );
}

static float PitchTo( const Vector &from, const Vector &to )
{
	const float dx = to.x - from.x, dy = to.y - from.y;
	/* v_angle pitch is positive looking down */
	return (float)( -atan2( to.z - from.z, sqrt( dx * dx + dy * dy ) ) * 180.0 / M_PI );
}

static void RunBot( int index, CBasePlayer *pBot )
{
	DodBotState *state = &g_bot[index];
	const float dt = gpGlobals->frametime > 0.0f ? gpGlobals->frametime : 0.016f;
	int msec_value = (int)( dt * 1000.0f );
	if( msec_value < 1 ) msec_value = 1;
	if( msec_value > 100 ) msec_value = 100;
	const byte msec = (byte)msec_value;
	const float speed = pBot->pev->maxspeed > 0.0f ? pBot->pev->maxspeed : 220.0f;
	float viewangles[3], aim_error = 0.0f, enemy_distance = 0.0f;
	unsigned short buttons = 0;
	CBasePlayer *enemy;
	DodBotInput in;
	DodBotMove move;
	Vector eye, ahead, floor_end;
	TraceResult tr;

	if( !pBot->IsAlive() )
	{
		/* A dead player respawns on a fire press after the death delay. */
		if( gpGlobals->time >= state->next_respawn_press )
		{
			buttons = IN_ATTACK;
			state->next_respawn_press = gpGlobals->time + 0.5f;
		}
		viewangles[0] = pBot->pev->v_angle.x;
		viewangles[1] = pBot->pev->v_angle.y;
		viewangles[2] = 0.0f;
		( *g_engfuncs.pfnRunPlayerMove )( pBot->edict(), viewangles, 0, 0, 0, buttons, 0, msec );
		state->yaw = pBot->pev->v_angle.y;
		state->pitch = pBot->pev->v_angle.x;
		state->has_goal = 0;
		{ static float nd = 0.0f; if( gpGlobals->time >= nd ) { nd = gpGlobals->time + 1.0f; ALERT( at_console, "BOTPROBE t=%.1f dead/waiting hp=%.0f\n", gpGlobals->time, pBot->pev->health ); } }
		return;
	}
	if( state->yaw == 0.0f && state->pitch == 0.0f )
		state->yaw = pBot->pev->v_angle.y;
	if( gpGlobals->time >= state->next_goal )
		PickGoal( state );

	eye = pBot->pev->origin + pBot->pev->view_ofs;
	memset( &in, 0, sizeof( in ) );
	in.time = gpGlobals->time;
	in.fire_phase = state->fire_phase++;

	/* Obstacles: a wall at knee height, or no floor a little ahead. */
	{
		const float yaw_rad = state->yaw * (float)M_PI / 180.0f;
		const Vector dir( cos( yaw_rad ), sin( yaw_rad ), 0.0f );
		UTIL_TraceLine( pBot->pev->origin + Vector( 0, 0, 18 ), pBot->pev->origin + Vector( 0, 0, 18 ) + dir * 48.0f,
			ignore_monsters, pBot->edict(), &tr );
		in.blocked = tr.flFraction < 1.0f;
		ahead = pBot->pev->origin + dir * 48.0f;
		floor_end = ahead - Vector( 0, 0, 100 );
		UTIL_TraceLine( ahead, floor_end, ignore_monsters, pBot->edict(), &tr );
		in.ledge = tr.flFraction >= 1.0f;
	}

	enemy = FindEnemy( pBot, &enemy_distance );
	{
		/* No path graph: if the bot is walking but going nowhere, break the
		 * straight-line loop with a growing detour. */
		const DodBotEscape esc = Dod_BotStuckUpdate( &state->stuck, pBot->pev->origin.x, pBot->pev->origin.y,
			gpGlobals->time, enemy == NULL, RANDOM_FLOAT( 0.0f, 1.0f ) );
		if( esc.stuck )
		{
			state->heading = Dod_BotAngleDelta( 0.0f, state->yaw + esc.turn );
			state->yaw = state->heading;
			state->hold_heading_until = gpGlobals->time + esc.hold;
			if( esc.jump )
				buttons |= IN_JUMP;
		}
	}
	{
		CBasePlayerItem *item = pBot->m_pActiveItem;
		CBasePlayerWeapon *weapon = item ? (CBasePlayerWeapon *)item->GetWeaponPtr() : NULL;
		const DodWeaponDef *def = weapon ? Dod_FindWeaponDef( STRING( weapon->pev->classname ) ) : NULL;
		in.semi_auto = def ? def->mode != DOD_FIRE_AUTO : 0;
		in.clip_empty = weapon && weapon->m_iClip == 0;
	}

	if( enemy )
	{
		const Vector target = enemy->pev->origin + enemy->pev->view_ofs * 0.9f;
		const float want_yaw = YawTo( eye, target ), want_pitch = PitchTo( eye, target );
		in.has_target = 1;
		in.target_distance = enemy_distance;
		if( gpGlobals->time >= state->next_noise )
		{
			const float err = Dod_BotAimError( enemy_distance, (int)CVAR_GET_FLOAT( "dod_bot_skill" ) );
			state->noise_yaw = RANDOM_FLOAT( -err, err );
			state->noise_pitch = RANDOM_FLOAT( -err, err ) * 0.5f;
			state->next_noise = gpGlobals->time + 0.8f;
		}
		state->yaw = Dod_BotTurn( state->yaw, want_yaw + state->noise_yaw, 300.0f * dt );
		state->pitch = Dod_BotTurn( state->pitch, want_pitch + state->noise_pitch, 300.0f * dt );
		{
			const float dy = fabsf( Dod_BotAngleDelta( state->yaw, want_yaw ) );
			const float dp = fabsf( Dod_BotAngleDelta( state->pitch, want_pitch ) );
			aim_error = dy > dp ? dy : dp;
		}
		in.aim_error = aim_error;
	}
	else
	{
		/* Head for the goal, or keep the current heading. */
		if( state->has_goal && gpGlobals->time >= state->hold_heading_until )
			state->heading = YawTo( pBot->pev->origin, state->goal );
		state->yaw = Dod_BotTurn( state->yaw, state->heading, 120.0f * dt );
		state->pitch = Dod_BotTurn( state->pitch, 0.0f, 120.0f * dt );
	}

	move = Dod_BotDecide( &in );
	{	/* local probe (not for merge): once a second per bot */
		static float next_probe = 0.0f;
		if( gpGlobals->time >= next_probe )
		{
			next_probe = gpGlobals->time + 1.0f;
			ALERT( at_console, "BOTPROBE t=%.1f pos=%.0f,%.0f,%.0f hp=%.0f enemy=%d dist=%.0f fwd=%.1f fire=%d jump=%d blocked=%d\n",
				gpGlobals->time, pBot->pev->origin.x, pBot->pev->origin.y, pBot->pev->origin.z, pBot->pev->health,
				enemy != NULL, enemy ? enemy_distance : 0.0f, move.forward, move.fire, move.jump, in.blocked );
		}
	}
	if( move.turn_away && gpGlobals->time >= state->hold_heading_until )
	{
		const float turn = RANDOM_FLOAT( 70.0f, 140.0f ) * ( RANDOM_LONG( 0, 1 ) ? 1.0f : -1.0f );
		state->heading = Dod_BotAngleDelta( 0.0f, state->yaw + turn );
		state->yaw = state->heading;
		state->hold_heading_until = gpGlobals->time + 0.7f;
	}
	if( move.fire )
		buttons |= IN_ATTACK;
	if( move.reload )
		buttons |= IN_RELOAD;
	if( move.jump )
		buttons |= IN_JUMP;

	viewangles[0] = state->pitch;
	viewangles[1] = state->yaw;
	viewangles[2] = 0.0f;
	( *g_engfuncs.pfnRunPlayerMove )( pBot->edict(), viewangles, move.forward * speed, move.side * speed, 0, buttons, 0, msec );
}

void DodBots_Think( void )
{
	int i;
	{	/* local probe: is Think running at all */
		static int calls = 0;
		if( ++calls % 40 == 1 )
			ALERT( at_console, "BOTCALLS n=%d t=%.3f frametime=%.4f\n", calls, gpGlobals->time, gpGlobals->frametime );
	}
	if( !g_registered )
		return;
	if( gpGlobals->time >= g_next_plan )
	{
		g_next_plan = gpGlobals->time + 1.0f;
		PlanBots();
	}
	for( i = 1; i <= gpGlobals->maxClients && i <= DOD_BOT_MAX_SLOTS; i++ )
	{
		CBasePlayer *pBot = PlayerAt( i );
		if( pBot && DodBots_IsBot( pBot ) )
			RunBot( i, pBot );
	}
}
