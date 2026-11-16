/* Day of Defeat bots (fake clients); see dod_bot.h and dod_bots.c. */
#include <stdlib.h>
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
	/* objective goal (see PickGoal) */
	int goal_id;			/* control point id, -1 none */
	int goal_kind;			/* DodBotGoalKind */
	int arrived;			/* inside the capture volume */
	float steer_yaw;		/* obstacle-avoiding heading towards the goal */
	float next_steer;
	float steer_off;		/* offset from the straight line the last pick used */
	short cell_x[16], cell_y[16];	/* recently visited 128-unit cells, newest at cell_n - 1 */
	int cell_n;
	float goal_best_dist;		/* closest approach so far */
	float goal_progress_time;	/* when it last got closer */
	Vector goal_mins, goal_maxs;
	int block_id[3];		/* goals it could not reach, and until when */
	float block_until[3];
} DodBotState;

/* Per-bot state is heap allocated only while bots exist (the BSS it used to
 * take is MEM1 the menu and map load want). Bounded by a byte budget, freed on
 * map change and when the last bot leaves; if the heap cannot give it, no bot
 * is added rather than failing later. */
#if defined( __GAMECUBE__ )
#define DOD_BOT_MEM_BUDGET 1024
#else
#define DOD_BOT_MEM_BUDGET 8192
#endif
static DodBotState *g_bot;
static int g_bot_slots;		/* entries allocated, indexed by entity index */

static DodBotState *BotState( int index )
{
	return g_bot && index >= 0 && index < g_bot_slots ? &g_bot[index] : NULL;
}

static void FreeBotState( void )
{
	if( g_bot )
		free( g_bot );
	g_bot = NULL;
	g_bot_slots = 0;
}

/* Bytes the bot state holds right now (for budget reporting). */
int DodBots_MemoryBytes( void )
{
	return g_bot_slots * (int)sizeof( DodBotState );
}

static BOOL EnsureBotState( void )
{
	int slots = gpGlobals->maxClients + 1;
	if( g_bot )
		return TRUE;
	if( slots > DOD_BOT_MAX_SLOTS + 1 )
		slots = DOD_BOT_MAX_SLOTS + 1;
	if( slots * (int)sizeof( DodBotState ) > DOD_BOT_MEM_BUDGET )
		slots = DOD_BOT_MEM_BUDGET / (int)sizeof( DodBotState );
	if( slots < 2 )
		return FALSE;
	g_bot = (DodBotState *)calloc( slots, sizeof( DodBotState ) );
	if( !g_bot )
		return FALSE;
	g_bot_slots = slots;
	ALERT( at_console, "DoD bots: state %d bytes (%d slots, budget %d)\n", DodBots_MemoryBytes(), slots, DOD_BOT_MEM_BUDGET );
	return TRUE;
}
static BOOL g_registered;
static BOOL g_granting;
static float g_next_plan;
static float g_first_human_time;

void DodBots_SetGranting( BOOL granting ) { g_granting = granting; }
BOOL DodBots_Granting( void ) { return g_granting; }

BOOL DodBots_IsBot( CBasePlayer *pPlayer )
{
	DodBotState *state;
	if( !pPlayer || !pPlayer->pev )
		return FALSE;
	if( pPlayer->pev->flags & FL_FAKECLIENT )
		return TRUE;
	/* CBasePlayer::Spawn() (every respawn) resets pev->flags to FL_CLIENT, which drops
	 * FL_FAKECLIENT; the bot's own slot record is the reliable marker. */
	state = BotState( ENTINDEX( pPlayer->edict() ) );
	if( state && state->active )
	{
		pPlayer->pev->flags |= FL_FAKECLIENT;
		return TRUE;
	}
	return FALSE;
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
	FreeBotState();
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

static void CountPlayers( int *humans, int *bots, int team_counts[4], int bot_team_counts[4], int special_counts[4][DOD_CLASS_COUNT] )
{
	int i;
	*humans = *bots = 0;
	memset( team_counts, 0, sizeof( int ) * 4 );
	memset( bot_team_counts, 0, sizeof( int ) * 4 );
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
	bot_team_counts[team]++;
			if( DodBots_IsBot( pPlayer ) )
				bot_team_counts[team]++;
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

static void AddBot( int team_counts[4], int bot_team_counts[4], int special_counts[4][DOD_CLASS_COUNT] )
{
	static int name_index;
	const char *name = g_bot_names[name_index++ % ( sizeof( g_bot_names ) / sizeof( g_bot_names[0] ) )];
	edict_t *pEdict;
	CBasePlayer *pBot;
	char reject[128];
	DodTeam team;
	DodClass player_class;

	if( !EnsureBotState() )
		return;	/* out of memory or budget: no bot */
	pEdict = ( *g_engfuncs.pfnCreateFakeClient )( name );
	if( FNullEnt( pEdict ) )
		return;	/* no free client slot after all */
	reject[0] = '\0';
	{
		/* Mark the slot a bot before ClientPutInServer spawns it: the first PlayerSpawn must already
		 * know it is a bot (a human waits at the side menu, a bot does not). */
		DodBotState *state = BotState( ENTINDEX( pEdict ) );
		if( state )
		{
			memset( state, 0, sizeof( DodBotState ) );
			state->active = 1;
		}
	}
	ClientConnect( pEdict, name, "127.0.0.1", reject );
	ClientPutInServer( pEdict );

	pBot = (CBasePlayer *)CBaseEntity::Instance( pEdict );
	if( !pBot )
		return;
	team = Dod_BotPickTeamBalanced( team_counts[DOD_TEAM_ALLIES], team_counts[DOD_TEAM_AXIS],
		bot_team_counts[DOD_TEAM_ALLIES], bot_team_counts[DOD_TEAM_AXIS] );
	player_class = Dod_BotPickClass( RANDOM_LONG( 0, 1000 ), special_counts[team] );
	DodGC_BotJoin( pBot, team, player_class );
	pBot->pev->flags |= FL_FAKECLIENT;	/* Spawn() inside BotJoin resets flags to FL_CLIENT */
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
			if( BotState( i ) )
				BotState( i )->active = 0;
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
	int humans, bots, team_counts[4], bot_team_counts[4], special_counts[4][DOD_CLASS_COUNT], target;

	CountPlayers( &humans, &bots, team_counts, bot_team_counts, special_counts );
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
	if( bots == 0 && target == 0 )
		FreeBotState();
	if( bots > target )
		KickOneBot();
	else if( bots < target && gpGlobals->time - g_first_human_time > 3.0f )
		AddBot( team_counts, bot_team_counts, special_counts );	/* one per plan tick: no spawn burst */
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

/* Objective goals, retail style (see dod_bots.h): attackers take the nearest point their team may
 * capture and does not own and stay inside its capture volume until it flips; defenders guard
 * owned points and the ones under attack. Re-evaluated every couple of seconds, but the current
 * target is kept while it is still a valid choice. A bot that gets no closer for GOAL_PATIENCE
 * seconds (a wall in the way: there is no path graph) blacklists the point for a while. */
#define DOD_BOT_GOAL_PATIENCE 30.0f
#define DOD_BOT_GOAL_BLOCK_TIME 45.0f
#define DOD_BOT_IN_MARGIN 16.0f

static BOOL InsideGoal( const DodBotState *state, const Vector &origin )
{
	return origin.x >= state->goal_mins.x - DOD_BOT_IN_MARGIN && origin.x <= state->goal_maxs.x + DOD_BOT_IN_MARGIN
		&& origin.y >= state->goal_mins.y - DOD_BOT_IN_MARGIN && origin.y <= state->goal_maxs.y + DOD_BOT_IN_MARGIN
		&& origin.z >= state->goal_mins.z - 72.0f && origin.z <= state->goal_maxs.z + 72.0f;
}

static void PickGoal( CBasePlayer *pBot, DodBotState *state )
{
	DodGCObjectiveView views[DOD_BOT_MAX_OBJECTIVES];
	DodBotObjectiveInfo info[DOD_BOT_MAX_OBJECTIVES];
	const float now = gpGlobals->time;
	int count, i, kind = DOD_BOT_GOAL_NONE, id;

	state->next_goal = now + 2.0f;
	count = DodGC_ListObjectives( views, DOD_BOT_MAX_OBJECTIVES );
	for( i = 0; i < count; i++ )
	{
		info[i].id = views[i].id;
		info[i].owner = views[i].owner;
		info[i].capturing_team = views[i].capturing;
		memcpy( info[i].can_cap, views[i].can_cap, sizeof( info[i].can_cap ) );
		info[i].distance = ( views[i].center - pBot->pev->origin ).Length2D();
		info[i].blocked = Dod_BotBlockHas( state->block_id, state->block_until, 3, views[i].id, now );
	}
	/* No progress towards the goal for a long while: give it up for a bit. */
	if( state->has_goal && state->goal_id >= 0 && !state->arrived && now - state->goal_progress_time > DOD_BOT_GOAL_PATIENCE )
	{
		ALERT( at_console, "DoD bot %s: cannot reach point %d, trying another\n", STRING( pBot->pev->netname ), state->goal_id );
		Dod_BotBlockAdd( state->block_id, state->block_until, 3, state->goal_id, now + DOD_BOT_GOAL_BLOCK_TIME, now );
		for( i = 0; i < count; i++ )
			if( info[i].id == state->goal_id )
				info[i].blocked = 1;
		state->goal_id = -1;
	}
	id = Dod_BotChooseObjective( info, count, TeamOf( pBot ), state->has_goal ? state->goal_id : -1, &kind );
	if( id < 0 )
	{
		state->has_goal = 0;
		state->goal_id = -1;
		state->arrived = 0;
		return;
	}
	for( i = 0; i < count; i++ )
		if( views[i].id == id )
			break;
	if( i == count )
		return;
	if( !state->has_goal || state->goal_id != id || state->goal_kind != kind )
	{
		ALERT( at_console, "DoD bot %s: %s point %d (%.0f units)\n", STRING( pBot->pev->netname ),
			kind == DOD_BOT_GOAL_ATTACK ? "attack" : "defend", id, info[i].distance );
		state->goal_id = id;
		state->goal_kind = kind;
		state->arrived = 0;
		state->goal_best_dist = info[i].distance;
		state->goal_progress_time = now;
		state->cell_n = 0;
		state->steer_off = 0.0f;
	}
	state->goal = views[i].center;
	state->goal_mins = views[i].mins;
	state->goal_maxs = views[i].maxs;
	state->has_goal = 1;
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

/* Obstacle avoidance without a path graph: a greedy look-ahead with memory. Probe 18 headings with
 * the player hull (raised to step height, so stairs and kerbs pass) up to DOD_BOT_LOOKAHEAD units,
 * with a floor check at the end, and take the one whose end point is nearest the goal, penalising
 * end points in cells the bot has been to lately (so it leaves a dead end instead of re-entering it)
 * and a little for turning. It will not find every route, but it gets round walls and buildings;
 * a bot that still makes no progress gives the point up (see PickGoal). */
/* A drop this far or less is fine to walk off (about 15 damage at 260); a pit deeper than that is a ledge. */
#define DOD_BOT_MAX_DROP 260.0f
#define DOD_BOT_LOOKAHEAD 480.0f
#define DOD_BOT_CELL 128.0f
#define DOD_BOT_VISIT_PENALTY 250.0f

static short CellOf( float v )
{
	return (short)floorf( v / DOD_BOT_CELL );
}

static void RecordCell( DodBotState *state, const Vector &origin )
{
	const short cx = CellOf( origin.x ), cy = CellOf( origin.y );
	const int last = ( state->cell_n - 1 ) & 15;
	if( state->cell_n > 0 && state->cell_x[last] == cx && state->cell_y[last] == cy )
		return;
	state->cell_x[state->cell_n & 15] = cx;
	state->cell_y[state->cell_n & 15] = cy;
	state->cell_n++;
}

static int Visits( const DodBotState *state, float x, float y )
{
	const short cx = CellOf( x ), cy = CellOf( y );
	const int n = state->cell_n < 16 ? state->cell_n : 16;
	int i, count = 0;
	for( i = 0; i < n; i++ )
		if( state->cell_x[i] == cx && state->cell_y[i] == cy )
			count++;
	return count;
}

/* Longest free walk along yaw, up to max_dist (0 when blocked or there is no floor). */
static float FreeRun( CBasePlayer *pBot, float yaw, float max_dist, BOOL floor_trust )
{
	TraceResult tr;
	const float r = yaw * (float)M_PI / 180.0f;
	const Vector dir( cos( r ), sin( r ), 0.0f );
	const Vector start = pBot->pev->origin + Vector( 0, 0, 18 );
	float dist = max_dist;
	int tries;
	UTIL_TraceHull( start, start + dir * dist, ignore_monsters, human_hull, pBot->edict(), &tr );
	if( tr.fStartSolid )
		return 0.0f;
	dist *= tr.flFraction;
	if( !floor_trust )
		return dist;
	/* Shorten the run until the end point has floor under it (a drop or a pit ends the run). */
	for( tries = 0; tries < 3 && dist >= 64.0f; tries++ )
	{
		const Vector end = start + dir * dist;
		UTIL_TraceLine( end, end - Vector( 0, 0, DOD_BOT_MAX_DROP ), ignore_monsters, pBot->edict(), &tr );
		if( tr.flFraction < 1.0f )
			return dist;
		dist *= 0.5f;
	}
	return 0.0f;
}

static float SteerYaw( CBasePlayer *pBot, DodBotState *state, const Vector &goal, BOOL floor_trust )
{
	const Vector origin = pBot->pev->origin;
	const float want = YawTo( origin, goal );
	float best_score = 1e9f, best_off = 0.0f;
	int off;
	for( off = -180; off < 180; off += 20 )
	{
		const float yaw = want + (float)off;
		const float run = FreeRun( pBot, yaw, DOD_BOT_LOOKAHEAD, floor_trust );
		const float r = yaw * (float)M_PI / 180.0f;
		float score;
		Vector end;
		if( run < 96.0f )
			continue;
		end = origin + Vector( cos( r ), sin( r ), 0.0f ) * run;
		score = ( end - goal ).Length2D() + (float)Visits( state, end.x, end.y ) * DOD_BOT_VISIT_PENALTY + fabsf( (float)off ) * 0.5f;
		if( fabsf( (float)off - state->steer_off ) < 15.0f )
			score -= 60.0f;	/* keep going the way it was going */
		if( score < best_score )
		{
			best_score = score;
			best_off = (float)off;
		}
	}
	state->steer_off = best_off;
	return want + best_off;
}

static void RunBot( int index, CBasePlayer *pBot )
{
	DodBotState *state = BotState( index );
	if( !state )
		return;
	const float dt = gpGlobals->frametime > 0.0f ? gpGlobals->frametime : 0.016f;
	int msec_value = (int)( dt * 1000.0f );
	if( msec_value < 1 ) msec_value = 1;
	if( msec_value > 100 ) msec_value = 100;
	const byte msec = (byte)msec_value;
	/* pmove clamps to pev->maxspeed; the engine leaves it 0 on some builds, which froze the bots
	 * in place (fwd=220 commanded, velocity 0). Give the bot a walking speed of its own. */
	if( pBot->pev->maxspeed <= 0.0f )
		pBot->pev->maxspeed = 220.0f;
	const float speed = pBot->pev->maxspeed;
	float viewangles[3], aim_error = 0.0f, enemy_distance = 0.0f;
	unsigned short buttons = 0;
	CBasePlayer *enemy;
	DodBotInput in;
	DodBotMove move;
	Vector eye, ahead, floor_end;
	TraceResult tr;
	BOOL floor_trust = TRUE;

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
		state->goal_id = -1;
		state->arrived = 0;
		state->cell_n = 0;
		return;
	}
	if( state->yaw == 0.0f && state->pitch == 0.0f )
		state->yaw = pBot->pev->v_angle.y;
	if( gpGlobals->time >= state->next_goal )
		PickGoal( pBot, state );
	RecordCell( state, pBot->pev->origin );
	if( state->has_goal && state->goal_id >= 0 )
	{
		const BOOL was_arrived = state->arrived;
		const float dist = ( state->goal - pBot->pev->origin ).Length2D();
		state->arrived = InsideGoal( state, pBot->pev->origin );
		if( state->arrived )
		{
			state->goal_progress_time = gpGlobals->time;
			if( !was_arrived )
				ALERT( at_console, "DoD bot %s: arrived at point %d (%s)\n", STRING( pBot->pev->netname ), state->goal_id,
					state->goal_kind == DOD_BOT_GOAL_ATTACK ? "attack" : "defend" );
		}
		else if( dist < state->goal_best_dist - 64.0f )
		{
			state->goal_best_dist = dist;
			state->goal_progress_time = gpGlobals->time;
		}
	}
	else
		state->arrived = 0;

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
		floor_end = ahead - Vector( 0, 0, DOD_BOT_MAX_DROP );
		UTIL_TraceLine( ahead, floor_end, ignore_monsters, pBot->edict(), &tr );
		in.ledge = tr.flFraction >= 1.0f;
		{
			/* The lean GameCube server has no line-trace hull for the world: a trace straight
			 * down from the bot's own feet misses. Then ledge/blocked mean nothing and would
			 * pin the bot in place (forward 0 forever), so distrust them. */
			TraceResult own;
			UTIL_TraceLine( pBot->pev->origin, pBot->pev->origin - Vector( 0, 0, 100 ), ignore_monsters, pBot->edict(), &own );
			if( own.flFraction >= 1.0f )
			{
				in.ledge = in.blocked = 0;
				floor_trust = FALSE;
			}
		}
	}

	enemy = FindEnemy( pBot, &enemy_distance );
	{
		/* No path graph: if the bot is walking but going nowhere, break the
		 * straight-line loop with a growing detour. */
		const DodBotEscape esc = Dod_BotStuckUpdate( &state->stuck, pBot->pev->origin.x, pBot->pev->origin.y,
			gpGlobals->time, enemy == NULL && !state->arrived, RANDOM_FLOAT( 0.0f, 1.0f ) );
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
		/* Head for the goal, or keep the current heading. Inside the capture volume stand and look
		 * around instead (an attacker stays until the point flips, a defender guards it). */
		if( state->arrived )
		{
			if( gpGlobals->time >= state->hold_heading_until )
			{
				state->heading = RANDOM_FLOAT( -180.0f, 180.0f );
				state->hold_heading_until = gpGlobals->time + RANDOM_FLOAT( 2.0f, 4.0f );
			}
		}
		else if( state->has_goal && gpGlobals->time >= state->hold_heading_until )
		{
			if( gpGlobals->time >= state->next_steer )
			{
				state->steer_yaw = SteerYaw( pBot, state, state->goal, floor_trust );
				state->next_steer = gpGlobals->time + 0.25f;
			}
			state->heading = state->steer_yaw;
		}
		state->yaw = Dod_BotTurn( state->yaw, state->heading, 120.0f * dt );
		state->pitch = Dod_BotTurn( state->pitch, 0.0f, 120.0f * dt );
	}

	move = Dod_BotDecide( &in );
	if( state->has_goal && !enemy && !state->arrived )
		move.turn_away = 0;	/* the steering picks the way round; a random turn only undoes it */
	if( state->arrived && !enemy )
	{
		move.forward = 0.0f;
		move.side = 0.0f;
		move.jump = 0;
		move.turn_away = 0;
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
	/* The engine does not copy a fake client's command angles into pev->v_angle on every build
	 * (Allies bots kept v_angle 0,0 and every shot flew along +X). Weapons fire along v_angle, so
	 * set it from the command, with the same sign convention (positive pitch = looking down). */
	pBot->pev->v_angle.x = viewangles[0];
	pBot->pev->v_angle.y = viewangles[1];
	pBot->pev->v_angle.z = 0.0f;
	pBot->pev->angles.x = viewangles[0] * ( 1.0f / 3.0f );
	pBot->pev->angles.y = viewangles[1];
	( *g_engfuncs.pfnRunPlayerMove )( pBot->edict(), viewangles, move.forward * speed, move.side * speed, 0, buttons, 0, msec );
}


void DodBots_Think( void )
{
	int i;
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
	/* TEMP bot objective probe (not for merge) */
	{
		const DodGameRulesState *rs = Dod_GetState();
		CBasePlayer *h = PlayerAt( 1 );
		static float next = 0.0f;
		if( h && !DodBots_IsBot( h ) )
			h->pev->flags |= FL_GODMODE | FL_NOTARGET;
		if( gpGlobals->time >= next )
		{
			next = gpGlobals->time + 5.0f;
			ALERT( at_console, "BOTOBJ t=%.0f round=%d phase=%d allies=%d axis=%d capserial=%d rounds=%d/%d ents=%d\n", gpGlobals->time, rs->round_number, (int)rs->phase,
				rs->controlled_objectives[1], rs->controlled_objectives[2], rs->capture_serial, rs->rounds_won[1], rs->rounds_won[2], (int)NUMBER_OF_ENTITIES() );
			for( i = 1; i <= gpGlobals->maxClients && i <= DOD_BOT_MAX_SLOTS; i++ )
			{
				CBasePlayer *b = PlayerAt( i );
				DodBotState *st = BotState( i );
				if( b && st && DodBots_IsBot( b ) )
					ALERT( at_console, "BOTOBJ   %s team=%d alive=%d hp=%.0f pos=%.0f,%.0f,%.0f goal=%d kind=%d arrived=%d\n", STRING( b->pev->netname ), (int)TeamOf( b ), b->IsAlive() ? 1 : 0,
						b->pev->health, b->pev->origin.x, b->pev->origin.y, b->pev->origin.z, st->goal_id, st->goal_kind, st->arrived );
			}
		}
	}
}
