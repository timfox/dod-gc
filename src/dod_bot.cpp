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
#if defined( __GAMECUBE__ )
/* The PowerPC time base (40.5 MHz: 40500 ticks per millisecond), the GameCube's own clock, so the cost below is in
 * console time, not host time. Read directly: libogc's headers redefine BOOL and its gettime() is an inline. */
static inline unsigned long long gettime( void )
{
	unsigned int hi, lo, hi2;
	do
	{
		__asm__ volatile( "mftbu %0" : "=r"( hi ) );
		__asm__ volatile( "mftb %0" : "=r"( lo ) );
		__asm__ volatile( "mftbu %0" : "=r"( hi2 ) );
	} while( hi != hi2 );
	return ( (unsigned long long)hi << 32 ) | lo;
}
#define DOD_BOT_TICKS_TO_US( t ) ( (t) * 1000ULL / 40500ULL )
#endif

/* Where a long bot frame goes: the worst single call of each phase since the last log line (console time; zero
 * off the GameCube). */
enum { DOD_PH_PLAN, DOD_PH_PERCEIVE, DOD_PH_THINK, DOD_PH_GOAL, DOD_PH_MOVE, DOD_PH_STEER, DOD_PH_COUNT };
static unsigned long long g_phase_max_us[DOD_PH_COUNT];

static inline unsigned long long BotClockUs( void )
{
#if defined( __GAMECUBE__ )
	return DOD_BOT_TICKS_TO_US( gettime() );
#else
	return 0;
#endif
}

/* One RunBot call in four segments: A start..obstacle traces, B perception and planning, C decisions and steering,
 * D the rest (the player move). The worst of each, and of the whole call, since the last log line. */
static unsigned long long g_seg_max_us[5];

/* How fast the bots' view turned, per bot-frame, since the last log line: the largest rates and how many frames
 * turned faster than a human wrist can (a smooth turn is a few hundred degrees per second). */
static float g_turn_max_yaw_rate, g_turn_max_pitch_rate;
static unsigned g_turn_frames, g_turn_over_540, g_turn_over_1080;

struct SegTimer
{
	unsigned long long t[4];
	int n;
	SegTimer() : n( 0 ) { t[0] = BotClockUs(); }
	void mark( void ) { if( n < 3 ) t[++n] = BotClockUs(); }
	~SegTimer()
	{
		const unsigned long long end = BotClockUs();
		int i;
		for( i = 0; i < 4; i++ )
		{
			const unsigned long long a = t[i], b = i < n ? t[i + 1] : ( i == n ? end : 0 );
			if( i <= n && b - a > g_seg_max_us[i] )
				g_seg_max_us[i] = b - a;
		}
		if( end - t[0] > g_seg_max_us[4] )
			g_seg_max_us[4] = end - t[0];
	}
};

struct PhaseTimer
{
	int phase;
	unsigned long long t0;
	explicit PhaseTimer( int p ) : phase( p ), t0( BotClockUs() ) {}
	~PhaseTimer()
	{
		const unsigned long long d = BotClockUs() - t0;
		if( d > g_phase_max_us[phase] )
			g_phase_max_us[phase] = d;
	}
};
#include <string.h>

/* dod_bots: bots on top of the humans, filling free slots (0 = none). On
 * GameCube the engine decides how many slots there are (sv_gc_slots, 5 for a DoD
 * build: the human and four bots), so 7 means "fill the server" without ever asking
 * for more than the slots allow (and PlanBots stops short when edicts run low). dod_bot_skill: aim 0-3. dod_bots_online: allow bots while the
 * network link is up (otherwise the free slot is kept for the peer). */
#if defined( __GAMECUBE__ )
static cvar_t dod_bots = { "dod_bots", "7", FCVAR_SERVER };
#else
static cvar_t dod_bots = { "dod_bots", "1", FCVAR_SERVER };
#endif
static cvar_t dod_bot_skill = { "dod_bot_skill", "1", FCVAR_SERVER };
static cvar_t dod_bots_online = { "dod_bots_online", "0", FCVAR_SERVER };
/* Test scenarios (dev only): 1 = an enemy behind a wall inside the view cone, then in the open; 2 = a contested capture;
 * 3 = put the human on the Allies as a rifleman (weapon animation checks). */
static cvar_t dod_bot_stage = { "dod_bot_stage", "0", FCVAR_SERVER };

/* The most bots ever; the free slots are the real limit. */
#define DOD_BOT_HARD_CAP 12

/* Free edicts that must remain before another bot is added (see PlanBots). */
#define DOD_BOT_EDICT_RESERVE 22

#if defined( __GAMECUBE__ )
#define DOD_BOT_MAX_SLOTS 10	/* bot state slots; the engine's slot count (XASH_GC_DEFAULT_SLOTS, 3 by default) is the real limit */
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
	/* waypoint path (see NavTarget) */
	short path[DOD_NAV_MAX_PATH];
	int path_len, path_idx;		/* nodes planned, and the one being walked to */
	int path_goal;			/* goal id the path was planned for */
	float path_replan;		/* earliest time to plan again */
	float path_best, path_progress_time;	/* closest approach to the current node, and when it improved */
	int nav_fails;
	int ladder_dir;			/* +1 climbing up, -1 down, 0 not on a ladder */
	float ladder_until;
	int want_jump;			/* the edge being walked is a jump */
	int stall_jumped;		/* a stalled route got its one jump before it is replanned */
	float jump_until;
	/* GOAP (see Think) */
	unsigned ws;			/* world-state facts at the last think */
	DodGoapPlan plan;
	int role, team_bots;		/* DodBotRole, and the team size, set when the objective is picked */
	int goal_held, goal_contested;	/* the objective's ownership, from the last PickGoal */
	float next_think, last_health;
	Vector known_pos;		/* last place an enemy was seen */
	float known_until;
	int behavior;			/* DodGoapAction being run */
	/* where the bot is walking to: the objective, or a plan step's own destination */
	Vector nav_pos;
	int nav_id, nav_has, nav_arrived;
	int nav_serial;
	/* perception and aim (see Perceive and the fight code in RunBot) */
	float next_percept, seen_time;
	int tgt;			/* entity index of the enemy in view, 0 none (cached between perception ticks) */
	float tgt_dist;
	int last_tgt;
	float track_since, last_fight_time, react_until;
	float burst_until, pause_until;
	float next_unseen_log, last_fire;
	/* The view: yaw/pitch are the angles the bot looks along (and what v_angle and the body yaw are written from); the
	 * rest of the code only says where it wants to look (want_*) and one eased step per frame (Dod_BotEase) turns
	 * the view towards it, so nothing ever snaps. */
	float want_yaw, want_pitch, yaw_vel, pitch_vel;
	float prev_yaw, prev_pitch;	/* last written view angles, for the turn log */
	int have_prev_angles;
	float prev_health, hurt_at;	/* health at the last frame and when it last dropped (low health only counts for a while after a hit) */
	int was_alive;			/* for the death log */
	int stage_role;			/* test scenario: 1 watcher (holds still, perceives), 2 idle and blind */
	float stage_yaw;
	int stage_sightings, stage_fire_frames;
	int fire_frames, hit_frames;	/* this burst: frames the trigger was held, and how many were on target */
	/* team play (see Dod_IntelReport): what the bot reported and answered, its roam and its search around a lost target */
	float aggr;			/* 0..1, fixed per bot */
	float next_report, intel_checked;
	float patrol_until;
	int aggr_set, search_left, patrol_has;
	short patrol_recent[3];
	int patrol_recent_n;
	Vector patrol_pos;
} DodBotState;

/* Per-bot state is heap allocated only while bots exist (the BSS it used to
 * take is MEM1 the menu and map load want). Bounded by a byte budget, freed on
 * map change and when the last bot leaves; if the heap cannot give it, no bot
 * is added rather than failing later. */
#if defined( __GAMECUBE__ )
#define DOD_BOT_MEM_BUDGET 8192
#else
#define DOD_BOT_MEM_BUDGET 8192
#endif
static DodBotState *g_bot;
static int g_bot_slots;		/* entries allocated, indexed by entity index */

static DodBotState *BotState( int index )
{
	return g_bot && index >= 0 && index < g_bot_slots ? &g_bot[index] : NULL;
}

/* The map's waypoint graph (data/waypoints/<map>.wpt, next to the BSP on the disc): loaded when the first bot
 * runs on a map, freed with the bot state or when the map changes. Without it bots steer with the look-ahead. */
static DodNav g_nav;
static DodTeamIntel g_intel;	/* what each team knows about where the enemy is (shared by its bots) */
static byte *g_nav_file;
static char g_nav_map[32];

static void FreeNav( void )
{
	Dod_NavFree( &g_nav );
	if( g_nav_file )
		FREE_FILE( g_nav_file );
	g_nav_file = NULL;
	g_nav_map[0] = '\0';
}

/* How long an edge that bots got stuck on stays penalised: doubles with every report (a table of the worst ones). */
#define DOD_BOT_TRAP_TABLE 64
static struct { int from, to, hits; } g_traps[DOD_BOT_TRAP_TABLE];
static int g_trap_count;
static float EdgeTrapSeconds( int from, int to )
{
	int i, hits;
	for( i = 0; i < g_trap_count; i++ )
		if( g_traps[i].from == from && g_traps[i].to == to )
			break;
	if( i == g_trap_count )
	{
		if( g_trap_count == DOD_BOT_TRAP_TABLE )
			i = g_trap_count - 1;	/* full: reuse the last */
		else
			g_trap_count++;
		g_traps[i].from = from;
		g_traps[i].to = to;
		g_traps[i].hits = 0;
	}
	hits = ++g_traps[i].hits;
	return hits > 5 ? 900.0f : 30.0f * (float)( 1 << ( hits - 1 ) );
}

/* The map's damage zones for route planning: every trigger_hurt that is live from the start and hurts someone
 * (spawn-protection zones, minefields), with the teams it hurts (trigger_hurt's DoD team flags). A bot's route
 * avoids the ones that would hurt its own team. */
static void LoadHazards( void )
{
	edict_t *pent = NULL;
	int zones = 0;
	while( !FNullEnt( pent = FIND_ENTITY_BY_STRING( pent, "classname", "trigger_hurt" ) ) )
	{
		const entvars_t *v = &pent->v;
		int mask = 0, team;
		if( ( v->spawnflags & ( 1 | 2 ) ) || v->dmg < 20.0f )
			continue;	/* target-only or start-off (switched on later), or too weak to matter */
		for( team = DOD_TEAM_ALLIES; team <= DOD_TEAM_AXIS; team++ )
			if( Dod_HurtAppliesToTeam( v->spawnflags, (DodTeam)team ) )
				mask |= 1 << team;
		if( !mask || v->absmax == v->absmin )
			continue;
		{
			const float mins[3] = { v->absmin.x, v->absmin.y, v->absmin.z };
			const float maxs[3] = { v->absmax.x, v->absmax.y, v->absmax.z };
			Dod_NavAddHazard( &g_nav, mins, maxs, mask );
			ALERT( at_console, "DoD nav: damage zone %d: (%.0f %.0f %.0f) to (%.0f %.0f %.0f) hurts mask %d, dmg %.0f, flags %d\n", zones, mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2], mask, v->dmg, v->spawnflags );
			zones++;
		}
	}
	if( zones )
		ALERT( at_console, "DoD nav: %d damage zones to route around\n", zones );
}

static void EnsureNav( void )
{
	const char *map = STRING( gpGlobals->mapname );
	char path[64];
	int len = 0;
	/* a failed load (the engine heap can be nearly full when the first bot joins: ~20 KB free on charlie) is retried every 5 s for a
	 * minute, otherwise the bots steer by look-ahead for the whole round and cannot get across the map */
	static float next_retry;
	static int tries;
	if( g_nav_map[0] && !strcmp( g_nav_map, map ) )
	{
		if( g_nav_file || tries == 0 || tries >= 12 || gpGlobals->time < next_retry )
			return;
	}
	else
	{
		FreeNav();
		g_trap_count = 0;
		tries = 0;
		strncpy( g_nav_map, map, sizeof( g_nav_map ) - 1 );
		g_nav_map[sizeof( g_nav_map ) - 1] = '\0';
	}
	snprintf( path, sizeof( path ), "maps/%s.wpt", map );
	g_nav_file = LOAD_FILE_FOR_ME( path, &len );
	if( !g_nav_file )
	{
		tries++;
		next_retry = gpGlobals->time + 5.0f;
		ALERT( at_console, "DoD nav: could not load %s (try %d of 12), bots steer without a graph until it loads\n", path, tries );
		return;
	}
	if( !Dod_NavLoad( &g_nav, g_nav_file, len ) )
	{
		ALERT( at_console, "DoD nav: %s is not a valid waypoint file\n", path );
		FREE_FILE( g_nav_file );
		g_nav_file = NULL;
		return;
	}
	ALERT( at_console, "DoD nav: %s %d waypoints, %d edges (%d bytes)\n", path, g_nav.node_count, g_nav.edge_count, len );
	LoadHazards();
}

static void FreeBotState( void )
{
	FreeNav();
	Dod_IntelReset( &g_intel );
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

/* One-line diagnostics for the silent "no bot" paths (see the log, not the screen). */
static void BotNote( const char *why, int a, int b )
{
	ALERT( at_console, "DoD bot: %s (%d, %d)\n", why, a, b );
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
	if( slots < 2 ) {
		BotNote( "no bot state: too few slots", slots, DOD_BOT_MEM_BUDGET );
		return FALSE;
	}
	g_bot = (DodBotState *)calloc( slots, sizeof( DodBotState ) );
	if( !g_bot ) {
		BotNote( "no bot state: out of memory", slots, (int)sizeof( DodBotState ) );
		return FALSE;
	}
	Dod_IntelReset( &g_intel );
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
		CVAR_REGISTER( &dod_bot_stage );
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
	if( FNullEnt( pEdict ) ) {
		BotNote( "no free client slot", (int)gpGlobals->maxClients, 0 );
		return;	/* no free client slot after all */
	}
	reject[0] = '\0';
	ClientConnect( pEdict, name, "127.0.0.1", reject );
	ClientPutInServer( pEdict );

	pBot = (CBasePlayer *)CBaseEntity::Instance( pEdict );
	if( !pBot ) {
		BotNote( "no player entity for fake client", ENTINDEX( pEdict ), 0 );
		return;
	}
	team = Dod_BotPickTeam( team_counts[DOD_TEAM_ALLIES], team_counts[DOD_TEAM_AXIS] );
	player_class = Dod_BotPickClass( RANDOM_LONG( 0, 1000 ), special_counts[team] );
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
	player_class = DodGC_OpenClass( pBot, team, player_class );
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

/* The bots' share of a frame (every RunBot incl. the player move each one costs the engine), in microseconds of
 * console time, averaged and worst over the last log interval. */
static unsigned long long g_think_us_sum, g_think_us_max;
static unsigned g_think_frames;

static const char *const g_action_names[DOD_ACT_COUNT] = { "none", "reload", "attack", "seek", "cover", "regroup", "to-point", "capture", "guard" };

static void PlanBots( void )
{
	int humans, bots, team_counts[4], bot_team_counts[4], special_counts[4][DOD_CLASS_COUNT], target;

	static float next_edict_log;
	CountPlayers( &humans, &bots, team_counts, bot_team_counts, special_counts );
	if( gpGlobals->time >= next_edict_log )
	{
		/* entities are the real ceiling for a crowded match (a respawn drops a weapon box); worth watching */
		next_edict_log = gpGlobals->time + 10.0f;
		ALERT( at_console, "DoD edicts: %d in use of %d (%d humans, %d bots)\n", g_engfuncs.pfnNumberOfEntities(), gpGlobals->maxEntities, humans, bots );
		{
			/* where each bot stands against its goal: the quickest way to see a bot that cannot get anywhere */
			int bi;
			for( bi = 1; bi <= gpGlobals->maxClients && bi <= DOD_BOT_MAX_SLOTS; bi++ )
			{
				CBasePlayer *pb = PlayerAt( bi );
				DodBotState *sb = BotState( bi );
				if( !pb || !sb || !DodBots_IsBot( pb ) )
					continue;
				if( !pb->IsAlive() )
				{
					ALERT( at_console, "DoD bot %s: dead\n", STRING( pb->pev->netname ) );
					continue;
				}
				if( sb->has_goal && sb->goal_id >= 0 )
					ALERT( at_console, "DoD bot %s: %s point %d, %.0f units (dz %.0f), route %d/%d, %s, health %.0f, at %.0f,%.0f,%.0f\n",
						STRING( pb->pev->netname ), sb->goal_kind == DOD_BOT_GOAL_ATTACK ? "attacking" : "defending", sb->goal_id,
						( sb->goal - pb->pev->origin ).Length2D(), sb->goal.z - pb->pev->origin.z, sb->path_idx, sb->path_len,
						g_action_names[sb->behavior], pb->pev->health, pb->pev->origin.x, pb->pev->origin.y, pb->pev->origin.z );
			}
		}
		if( g_think_frames )
		{
			ALERT( at_console, "DoD bots: think %.2f ms average, %.2f ms worst over %u frames (%d bots)\n",
				(double)g_think_us_sum / (double)g_think_frames / 1000.0, (double)g_think_us_max / 1000.0, g_think_frames, bots );
			ALERT( at_console, "DoD bots: worst single call (ms): plan %.1f, perceive %.1f, think %.1f, goal %.1f, player move %.1f, steer %.1f\n",
				(double)g_phase_max_us[DOD_PH_PLAN] / 1000.0, (double)g_phase_max_us[DOD_PH_PERCEIVE] / 1000.0,
				(double)g_phase_max_us[DOD_PH_THINK] / 1000.0, (double)g_phase_max_us[DOD_PH_GOAL] / 1000.0,
				(double)g_phase_max_us[DOD_PH_MOVE] / 1000.0, (double)g_phase_max_us[DOD_PH_STEER] / 1000.0 );
			ALERT( at_console, "DoD bots: worst RunBot segments (ms): start-to-obstacles %.1f, perceive+think %.1f, decisions %.1f, player move %.1f; whole call %.1f\n",
				(double)g_seg_max_us[0] / 1000.0, (double)g_seg_max_us[1] / 1000.0, (double)g_seg_max_us[2] / 1000.0,
				(double)g_seg_max_us[3] / 1000.0, (double)g_seg_max_us[4] / 1000.0 );
			ALERT( at_console, "DoD bots: view turn: max yaw %.0f deg/s, max pitch %.0f deg/s; %u of %u bot-frames over 540 deg/s, %u over 1080\n",
				g_turn_max_yaw_rate, g_turn_max_pitch_rate, g_turn_over_540, g_turn_frames, g_turn_over_1080 );
			g_turn_max_yaw_rate = g_turn_max_pitch_rate = 0.0f;
			g_turn_frames = g_turn_over_540 = g_turn_over_1080 = 0;
			memset( g_seg_max_us, 0, sizeof( g_seg_max_us ) );
			memset( g_phase_max_us, 0, sizeof( g_phase_max_us ) );
			g_think_us_sum = g_think_us_max = 0;
			g_think_frames = 0;
		}
	}
	if( humans > 0 && g_first_human_time == 0.0f )
		g_first_human_time = gpGlobals->time;
	if( humans == 0 )
	{
		g_first_human_time = 0.0f;
		return;	/* no bots on an empty server */
	}
	{
		static float next_note;
		if( gpGlobals->time >= next_note ) {
			next_note = gpGlobals->time + 20.0f;
			BotNote( "plan: time x10, humans*100+bots", (int)( gpGlobals->time * 10.0f ), humans * 100 + bots );
		}
	}
	target = Dod_BotTargetCount(
		Dod_BotRequestedCount( (int)CVAR_GET_FLOAT( "dod_bots" ), NetworkUp(), CVAR_GET_FLOAT( "dod_bots_online" ) != 0.0f ),
		gpGlobals->maxClients, humans, DOD_BOT_HARD_CAP );
	if( bots == 0 && target == 0 )
		FreeBotState();
	{
		static int last_target = -1;
		if( target != last_target ) {
			last_target = target;
			BotNote( "target count (maxClients)", target, (int)gpGlobals->maxClients );
		}
	}
	if( bots > target )
		KickOneBot();
	else if( bots < target && gpGlobals->time - g_first_human_time > 3.0f )
	{
		/* Entities are the ceiling, not memory: a bot costs about 3 edicts and every respawn drops a weapon box, and
		 * a map like charlie already uses 420 of the 460. Keep a reserve so a crowd never runs the table dry. */
		if( gpGlobals->maxEntities - g_engfuncs.pfnNumberOfEntities() < DOD_BOT_EDICT_RESERVE )
		{
			static float next_note;
			if( gpGlobals->time >= next_note )
			{
				next_note = gpGlobals->time + 30.0f;
				ALERT( at_console, "DoD bots: %d of %d wanted, not adding more (%d of %d edicts in use)\n", bots, target,
					g_engfuncs.pfnNumberOfEntities(), gpGlobals->maxEntities );
			}
			return;
		}
		AddBot( team_counts, bot_team_counts, special_counts );	/* one per plan tick: no spawn burst */
	}
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

static float YawTo( const Vector &from, const Vector &to );

static DodBotSkill BotSkill( void )
{
	return Dod_BotSkillParams( (int)CVAR_GET_FLOAT( "dod_bot_skill" ) );
}

/* What the bot perceives, on its own 10 Hz tick (staggered by when each bot started, with a little jitter; the
 * result is cached between ticks so the traces cost the same however many frames run).
 * Sight: inside the view cone (or very close) and in line of sight, nearest first; no x-ray and no
 * all-round vision. Hearing, when nothing is in sight: gunfire and running footsteps within a radius give a
 * rough position, never a clean one, and only make the bot go and look. */
static CBasePlayer *Perceive( CBasePlayer *pBot, DodBotState *state, float *distance )
{
	PhaseTimer timer( DOD_PH_PERCEIVE );
	const float now = gpGlobals->time;
	if( now >= state->next_percept )
	{
		const DodBotSkill sk = BotSkill();
		const DodTeam my_team = TeamOf( pBot );
		CBasePlayer *best = NULL;
		float best_dist = 4000.0f, unseen_dist = 400.0f;
		const char *unseen_why = NULL;
		int i;
		state->next_percept = now + 0.1f + RANDOM_FLOAT( 0.0f, 0.03f );
		state->tgt = 0;
		for( i = 1; i <= gpGlobals->maxClients; i++ )
		{
			CBasePlayer *pOther = PlayerAt( i );
			float dist;
			if( !pOther || pOther == pBot || !pOther->IsAlive() || ( pOther->pev->flags & FL_NOTARGET ) )
				continue;
			if( TeamOf( pOther ) == my_team || TeamOf( pOther ) == DOD_TEAM_UNASSIGNED )
				continue;
			dist = ( pOther->pev->origin - pBot->pev->origin ).Length();
			if( dist >= best_dist )
				continue;
			if( dist > DOD_BOT_CLOSE_NOTICE && !Dod_BotInFov( state->yaw, YawTo( pBot->pev->origin, pOther->pev->origin ), sk.fov_half ) )
			{
				if( dist < unseen_dist )
				{
					unseen_dist = dist;
					unseen_why = "outside the view cone";
				}
				continue;
			}
			if( CanSee( pBot, pOther ) )
			{
				best = pOther;
				best_dist = dist;
			}
			else if( dist < unseen_dist )
			{
				unseen_dist = dist;
				unseen_why = "no line of sight";
			}
		}
		if( !best && unseen_why && now >= state->next_unseen_log )
		{
			state->next_unseen_log = now + 2.0f;
			ALERT( at_console, "DoD bot %s: an enemy is %.0f units away but not seen (%s)\n", STRING( pBot->pev->netname ), unseen_dist, unseen_why );
		}
		if( best )
		{
			state->tgt = best->entindex();
			state->tgt_dist = best_dist;
			state->seen_time = now;
		}
		else
		{
			CBasePlayer *heard = NULL;
			float heard_dist = 1e9f;
			for( i = 1; i <= gpGlobals->maxClients; i++ )
			{
				CBasePlayer *pOther = PlayerAt( i );
				float dist, speed, radius;
				if( !pOther || pOther == pBot || !pOther->IsAlive() || ( pOther->pev->flags & FL_NOTARGET ) )
					continue;
				if( TeamOf( pOther ) == my_team || TeamOf( pOther ) == DOD_TEAM_UNASSIGNED )
					continue;
				dist = ( pOther->pev->origin - pBot->pev->origin ).Length();
				speed = ( pOther->pev->flags & FL_ONGROUND ) ? pOther->pev->velocity.Length2D() : 0.0f;
				radius = Dod_BotHearRadius( &sk, ( pOther->pev->button & IN_ATTACK ) != 0, speed, 1 );
				if( radius <= 0.0f || dist >= radius || dist >= heard_dist )
					continue;
				if( ( pOther->pev->button & IN_ATTACK ) == 0 && dist > Dod_BotHearRadius( &sk, 0, speed, 0 ) && !CanSee( pBot, pOther ) )
					continue;	/* footsteps through a wall carry less far */
				heard = pOther;
				heard_dist = dist;
			}
			if( heard && Dod_NavInHazard( &g_nav, my_team, heard->pev->origin.x, heard->pev->origin.y, heard->pev->origin.z ) )
				heard = NULL;	/* a sound from a zone that would kill us is not followed */
			if( heard && now > state->seen_time + 1.0f )
			{
				float dx, dy;
				Dod_BotHeardOffset( heard_dist, RANDOM_FLOAT( 0.0f, 1.0f ), RANDOM_FLOAT( 0.0f, 1.0f ), &dx, &dy );
				if( now >= state->known_until )
					state->next_think = 0.0f;	/* something new to react to */
				state->known_pos = heard->pev->origin + Vector( dx, dy, 0.0f );
				if( now >= state->next_unseen_log )
				{
					state->next_unseen_log = now + 2.0f;
					ALERT( at_console, "DoD bot %s: hears an enemy %.0f units away, guesses %.0f units off\n", STRING( pBot->pev->netname ), heard_dist,
						sqrtf( dx * dx + dy * dy ) );
				}
				state->known_until = now + fminf( sk.memory, 4.0f );
				if( now >= state->next_report )
				{
					const float hp[3] = { heard->pev->origin.x, heard->pev->origin.y, heard->pev->origin.z };
					state->next_report = now + 1.5f;
					Dod_IntelReport( &g_intel, my_team, hp, DOD_INTEL_HEARD, pBot->entindex(), now, 5.0f );
				}
			}
		}
	}
	if( state->tgt )
	{
		CBasePlayer *t = PlayerAt( state->tgt );
		if( t && t->IsAlive() )
		{
			*distance = ( t->pev->origin - pBot->pev->origin ).Length();
			return t;
		}
		state->tgt = 0;
	}
	return NULL;
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
	PhaseTimer timer( DOD_PH_GOAL );
	DodGCObjectiveView views[DOD_BOT_MAX_OBJECTIVES];
	DodBotObjectiveInfo info[DOD_BOT_MAX_OBJECTIVES];
	const float now = gpGlobals->time;
	int count, i, kind = DOD_BOT_GOAL_NONE, id;

	int rank = 0, team_bots = 0, held = 0, team_alive = 0;
	const DodTeam my_team = TeamOf( pBot );
	state->next_goal = now + 2.0f;
	count = DodGC_ListObjectives( views, DOD_BOT_MAX_OBJECTIVES );
	/* the team split: this bot's place among the team's bots, and where the others are heading */
	for( i = 0; i < count; i++ )
		if( views[i].owner == my_team )
			held++;
	for( i = 1; i <= gpGlobals->maxClients && i <= DOD_BOT_MAX_SLOTS; i++ )
	{
		CBasePlayer *pOther = PlayerAt( i );
		if( !pOther || !DodBots_IsBot( pOther ) || TeamOf( pOther ) != my_team )
			continue;
		if( pOther == pBot )
			rank = team_bots;
		team_bots++;
	}
	state->role = Dod_BotRole( rank, team_bots, held );
	for( i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pAlive = PlayerAt( i );
		if( pAlive && pAlive->IsAlive() && TeamOf( pAlive ) == my_team )
			team_alive++;
	}
	state->team_bots = team_bots;
	for( i = 0; i < count; i++ )
	{
		info[i].id = views[i].id;
		info[i].owner = views[i].owner;
		info[i].capturing_team = views[i].capturing;
		memcpy( info[i].can_cap, views[i].can_cap, sizeof( info[i].can_cap ) );
		info[i].distance = ( views[i].center - pBot->pev->origin ).Length2D();
		info[i].blocked = Dod_BotBlockHas( state->block_id, state->block_until, 3, views[i].id, now );
		info[i].crowd = 0;
		info[i].need = views[i].num_cap[my_team] > 0 ? views[i].num_cap[my_team] : 1;
		/* a point whose capture volume lies in a damage zone that would hurt this bot's team (the enemy's spawn protection
		 * on a map like dod_flash) cannot be taken: the walk there ends in a trigger_hurt kill, so it is not a goal */
		if( g_nav.zone_count )
		{
			const Vector &c = views[i].center;
			if( Dod_NavInHazardMargin( &g_nav, my_team, c.x, c.y, c.z, 0.0f ) ||
				Dod_NavInHazardMargin( &g_nav, my_team, views[i].mins.x, views[i].mins.y, c.z, 0.0f ) ||
				Dod_NavInHazardMargin( &g_nav, my_team, views[i].maxs.x, views[i].maxs.y, c.z, 0.0f ) )
			{
				if( !info[i].blocked )
					ALERT( at_console, "DoD bot %s: point %d lies in a damage zone that would hurt its team, not a goal\n", STRING( pBot->pev->netname ), views[i].id );
				info[i].blocked = 1;
			}
		}
		/* a point that needs more players than the team has alive cannot be taken: leave it alone */
		if( info[i].need > team_alive )
			info[i].blocked = 1;
		{
			int j;
			for( j = 1; j <= gpGlobals->maxClients && j <= DOD_BOT_MAX_SLOTS; j++ )
			{
				CBasePlayer *pOther = PlayerAt( j );
				DodBotState *other = BotState( j );
				if( pOther && pOther != pBot && other && DodBots_IsBot( pOther ) && TeamOf( pOther ) == my_team &&
					other->has_goal && other->goal_id == views[i].id && other->goal_kind == DOD_BOT_GOAL_ATTACK )
					info[i].crowd++;
			}
		}
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
	id = Dod_BotChooseObjectiveRole( info, count, my_team, state->has_goal ? state->goal_id : -1, state->role, &kind );
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
	/* a pick of a point the team already holds is a guard job whatever the team split said */
	if( kind == DOD_BOT_GOAL_DEFEND )
		state->role = DOD_ROLE_DEFENDER;
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
	{
		const int held_now = views[i].owner == my_team;
		const int contested_now = views[i].capturing != DOD_TEAM_UNASSIGNED && views[i].capturing != my_team;
		if( held_now != state->goal_held || contested_now != state->goal_contested )
			state->next_think = 0.0f;	/* the capture state changed: replan now */
		state->goal_held = held_now;
		state->goal_contested = contested_now;
	}
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

/* The local obstacle-avoiding steer (no usable route): up to a dozen heading probes of a few traces each, which is
 * tens of milliseconds on the console. Only one bot steers in a given frame (the others keep their last heading and
 * try next frame), and each bot only every DOD_BOT_STEER_PERIOD seconds. */
#define DOD_BOT_STEER_PERIOD 0.4f
static float g_steer_stamp = -1.0f;

static float SteerYaw( CBasePlayer *pBot, DodBotState *state, const Vector &goal, BOOL floor_trust )
{
	PhaseTimer timer( DOD_PH_STEER );
	const Vector origin = pBot->pev->origin;
	const float want = YawTo( origin, goal );
	/* Never look further than the target: a heading that runs straight past it must not lose to one that
	 * stops short at a wall (the bot used to orbit a node 100 units away). */
	const float cap = fminf( DOD_BOT_LOOKAHEAD, ( goal - origin ).Length2D() + 16.0f );
	float best_score = 1e9f, best_off = 0.0f;
	int off;
	for( off = -180; off < 180; off += 30 )
	{
		const float yaw = want + (float)off;
		const float run = FreeRun( pBot, yaw, cap, floor_trust );
		const float r = yaw * (float)M_PI / 180.0f;
		float score;
		Vector end;
		if( run < fminf( 96.0f, cap * 0.6f ) )
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

/* Waypoint path following. The graph (tools/gen_waypoints.py) gives a route of nodes from the bot's floor to
 * the goal's; the bot walks to each node in turn with the local steering above, jumps where the edge says
 * so, and climbs ladders (look up or down and push forward). A route that stops making progress is replanned;
 * a goal that keeps failing is left to the patience in PickGoal, which gives the point up for a while. */
#define DOD_BOT_NODE_REACH 40.0f
#define DOD_BOT_NODE_STALL 2.5f

static BOOL NodeVisible( CBasePlayer *pBot, int node )
{
	TraceResult tr;
	float x, y, z;
	Dod_NavNodePos( &g_nav, node, &x, &y, &z );
	if( fabsf( z - pBot->pev->origin.z ) > 64.0f )
		return FALSE;
	UTIL_TraceLine( pBot->pev->origin + Vector( 0, 0, 18 ), Vector( x, y, z + 18.0f ), ignore_monsters, pBot->edict(), &tr );
	return tr.flFraction >= 0.98f;
}

/* Planning is the one expensive thing a bot does (a search over the whole graph), so it is rationed: at most
 * DOD_BOT_PLAN_SEARCHES searches per plan, and only one bot plans in a given server frame. */
#define DOD_BOT_PLAN_SEARCHES 4
static float g_plan_stamp = -1.0f;

static BOOL PlanPath( CBasePlayer *pBot, DodBotState *state )
{
	PhaseTimer timer( DOD_PH_PLAN );
	int starts[6], goals[4], tmp[DOD_NAV_MAX_PATH], ns, ng, i, j, k, pass, len, searches = 0;
	const Vector origin = pBot->pev->origin;
	if( gpGlobals->time <= g_plan_stamp )
	{
		state->path_replan = gpGlobals->time + 0.05f;	/* another bot planned this frame: try again in a moment */
		return FALSE;
	}
	g_plan_stamp = gpGlobals->time;
	Dod_NavSetTime( &g_nav, gpGlobals->time );
	Dod_NavSetAvoidTeam( &g_nav, TeamOf( pBot ) );
	ns = Dod_NavNearest( &g_nav, origin.x, origin.y, origin.z, starts, 6 );
	ng = 0;
	if( state->has_goal && state->nav_id == state->goal_id )
	{
		/* an objective: route to a waypoint inside its capture volume when there is one (the node nearest the
		 * centre can be well outside a big or odd-shaped volume) */
		const float mins[3] = { state->goal_mins.x, state->goal_mins.y, state->goal_mins.z };
		const float maxs[3] = { state->goal_maxs.x, state->goal_maxs.y, state->goal_maxs.z };
		ng = Dod_NavInside( &g_nav, mins, maxs, DOD_BOT_IN_MARGIN, goals, 4 );
	}
	if( ng == 0 )
		ng = Dod_NavNearest( &g_nav, state->nav_pos.x, state->nav_pos.y, state->nav_pos.z, goals, 4 );
	for( pass = 0; pass < 2; pass++ )	/* a start node in sight first, then any */
	{
		for( i = 0; i < ns; i++ )
		{
			if( pass == 0 && ( !NodeVisible( pBot, starts[i] ) || Dod_NavPenalized( &g_nav, -1, starts[i] ) ) )
				continue;
			for( j = 0; j < ng; j++ )
			{
				if( searches++ >= DOD_BOT_PLAN_SEARCHES )
					return FALSE;
				len = Dod_NavPath( &g_nav, starts[i], goals[j], tmp, DOD_NAV_MAX_PATH );
				if( len <= 0 )
					continue;
				for( k = 0; k < len; k++ )
					state->path[k] = (short)tmp[k];
				state->path_len = len;
				state->path_idx = 0;
				if( len > 1 && Dod_NavEdgeKind( &g_nav, tmp[0], tmp[1] ) == DOD_NAV_WALK )
				{
					/* already past the first node: start at the second (never skip the foot of a ladder or a jump) */
					float x0, y0, z0, x1, y1, z1;
					Dod_NavNodePos( &g_nav, tmp[0], &x0, &y0, &z0 );
					Dod_NavNodePos( &g_nav, tmp[1], &x1, &y1, &z1 );
					if( ( Vector( x1, y1, 0 ) - Vector( origin.x, origin.y, 0 ) ).Length() < ( Vector( x1, y1, 0 ) - Vector( x0, y0, 0 ) ).Length() )
						state->path_idx = 1;
				}
				state->path_goal = state->nav_id;
				state->path_best = 1e9f;
				state->path_progress_time = gpGlobals->time;
				state->stall_jumped = 0;
				state->cell_n = 0;
				state->steer_off = 0.0f;
				ALERT( at_console, "DoD bot %s: path to %d, %d waypoints (%d -> %d)\n", STRING( pBot->pev->netname ),
					state->nav_id, len, tmp[0], tmp[len - 1] );
				return TRUE;
			}
		}
	}
	return FALSE;
}

/* The position to walk towards while following the path; FALSE when there is no path to follow (no graph,
 * no goal, arrived, or no route), in which case the caller heads for the goal itself. */
static BOOL NavTarget( CBasePlayer *pBot, DodBotState *state, Vector *target )
{
	const float now = gpGlobals->time;
	const Vector origin = pBot->pev->origin;
	state->want_jump = now < state->jump_until;
	if( !g_nav.node_count || !state->nav_has || state->nav_arrived )
	{
		state->path_len = 0;
		state->ladder_dir = 0;
		return FALSE;
	}
	if( state->path_len == 0 || state->path_goal != state->nav_id )
	{
		state->ladder_dir = 0;
		if( now < state->path_replan )
			return FALSE;
		state->path_replan = now + 2.0f;
		if( !PlanPath( pBot, state ) )
			return FALSE;
	}
	for( ;; )
	{
		float nx, ny, nz, dist;
		const BOOL last = state->path_idx + 1 >= state->path_len;
		Dod_NavNodePos( &g_nav, state->path[state->path_idx], &nx, &ny, &nz );
		dist = ( Vector( nx, ny, 0 ) - Vector( origin.x, origin.y, 0 ) ).Length();
		if( state->ladder_dir )
		{
			/* on a ladder: the node being climbed to is the next one */
			float lx, ly, lz;
			Dod_NavNodePos( &g_nav, state->path[state->path_idx + 1], &lx, &ly, &lz );
			if( fabsf( lz - origin.z ) < 36.0f || now > state->ladder_until )
			{
				const BOOL timed_out = fabsf( lz - origin.z ) >= 36.0f;
				state->ladder_dir = 0;
				if( timed_out )
				{
					ALERT( at_console, "DoD bot %s: ladder climb timed out\n", STRING( pBot->pev->netname ) );
					state->path_len = 0;
					state->nav_fails++;
					return FALSE;
				}
				state->path_idx++;
				state->goal_progress_time = now;
				state->path_best = 1e9f;
				state->path_progress_time = now;
				state->cell_n = 0;
				continue;
			}
			{
				static float next_log;	/* ladder progress, once a second */
				if( now >= next_log )
				{
					next_log = now + 1.0f;
					ALERT( at_console, "DoD bot %s: on ladder dir=%d pos %.0f,%.0f,%.0f vel %.0f,%.0f,%.0f movetype %d to %.0f,%.0f,%.0f flags 0x%x\n",
						STRING( pBot->pev->netname ), state->ladder_dir, origin.x, origin.y, origin.z, pBot->pev->velocity.x,
						pBot->pev->velocity.y, pBot->pev->velocity.z, (int)pBot->pev->movetype, lx, ly, lz, (int)pBot->pev->flags );
				}
			}
			*target = Vector( lx, ly, lz );
			return TRUE;
		}
		if( dist < DOD_BOT_NODE_REACH && fabsf( nz - origin.z ) < 72.0f )
		{
			if( last )
			{
				state->path_len = 0;	/* the route is done: walk the last stretch to the goal itself */
				return FALSE;
			}
			if( Dod_NavEdgeKind( &g_nav, state->path[state->path_idx], state->path[state->path_idx + 1] ) == DOD_NAV_LADDER )
			{
				float lx, ly, lz;
				Dod_NavNodePos( &g_nav, state->path[state->path_idx + 1], &lx, &ly, &lz );
				state->ladder_dir = lz > nz ? 1 : -1;
				state->ladder_until = now + 12.0f;
				ALERT( at_console, "DoD bot %s: %s a ladder\n", STRING( pBot->pev->netname ), state->ladder_dir > 0 ? "climbs" : "descends" );
				continue;
			}
			state->path_idx++;
			state->goal_progress_time = now;
			state->path_best = 1e9f;
			state->path_progress_time = now;
			state->cell_n = 0;
			continue;
		}
		if( dist < state->path_best - 24.0f )
		{
			state->path_best = dist;
			state->path_progress_time = now;
			state->goal_progress_time = now;
			state->stall_jumped = 0;
		}
		else if( now - state->path_progress_time > DOD_BOT_NODE_STALL && !state->stall_jumped )
		{
			/* no progress for a moment: hop once (a low lip, a door frame), then give it another go */
			state->stall_jumped = 1;
			state->jump_until = now + 0.7f;
			state->want_jump = 1;
			state->path_progress_time = now;
		}
		else if( now - state->path_progress_time > DOD_BOT_NODE_STALL )
		{
			TraceResult line, hull;
			UTIL_TraceLine( origin + Vector( 0, 0, 18 ), Vector( nx, ny, nz + 18.0f ), ignore_monsters, pBot->edict(), &line );
			UTIL_TraceHull( origin + Vector( 0, 0, 18 ), Vector( nx, ny, nz + 18.0f ), ignore_monsters, human_hull, pBot->edict(), &hull );
			ALERT( at_console, "DoD bot %s: stuck on the way to waypoint %d (node %d/%d, edge %d), replanning: bot %.0f,%.0f,%.0f node %.0f,%.0f,%.0f dist %.0f line %.2f hull %.2f vel %.0f movetype %d\n",
				STRING( pBot->pev->netname ), state->path[state->path_idx], state->path_idx, state->path_len,
				state->path_idx > 0 ? Dod_NavEdgeKind( &g_nav, state->path[state->path_idx - 1], state->path[state->path_idx] ) : -1,
				origin.x, origin.y, origin.z, nx, ny, nz, dist, line.flFraction, hull.flFraction, pBot->pev->velocity.Length(), pBot->pev->movetype );
			{
				/* where exactly is the way blocked: the player hull from the bot's own origin towards the node on the flat, then the same
				 * straight along the move direction; hit entity, plane and whether the start is already solid */
				static int diag_logs;
				if( diag_logs < 24 )
				{
					TraceResult th, tf;
					Vector to( nx, ny, origin.z );
					UTIL_TraceHull( origin, to, dont_ignore_monsters, human_hull, pBot->edict(), &th );
					UTIL_TraceHull( origin, origin + gpGlobals->v_forward * 0.0f + ( to - origin ), ignore_monsters, head_hull, pBot->edict(), &tf );
					diag_logs++;
					ALERT( at_console, "DoD stuck trace %s: hull frac %.2f startsolid %d allsolid %d hit %s (ent %d) normal %.2f %.2f %.2f; crouch hull frac %.2f startsolid %d; origin %.0f %.0f %.0f to %.0f %.0f %.0f\n",
						STRING( pBot->pev->netname ), th.flFraction, (int)th.fStartSolid, (int)th.fAllSolid,
						th.pHit ? STRING( th.pHit->v.classname ) : "none", th.pHit ? ENTINDEX( th.pHit ) : -1, th.vecPlaneNormal.x, th.vecPlaneNormal.y, th.vecPlaneNormal.z,
						tf.flFraction, (int)tf.fStartSolid, origin.x, origin.y, origin.z, to.x, to.y, to.z );
				}
			}
			{
				/* an edge that traps bots again and again stays avoided longer each time (30 s, 60 s, ... up to 15 minutes), for every bot */
				const int from = state->path_idx > 0 ? state->path[state->path_idx - 1] : -1;
				const int to = state->path[state->path_idx > 0 ? state->path_idx : 0];
				Dod_NavPenalize( &g_nav, from, to, now + EdgeTrapSeconds( from, to ) );
			}
			state->path_len = 0;
			state->path_replan = now;
			state->nav_fails++;
			return FALSE;
		}
		if( state->path_idx > 0 && dist < 96.0f &&
			Dod_NavEdgeKind( &g_nav, state->path[state->path_idx - 1], state->path[state->path_idx] ) == DOD_NAV_JUMP )
			state->want_jump = 1;
		*target = Vector( nx, ny, nz );
		return TRUE;
	}
}

/* ---------------------------------------------------------------- GOAP
 * Every ~0.5 s (and at once when an enemy appears or goes, the bot is hurt, or its objective changes hands) the
 * bot reads the world into a bitmask, picks the goal it wants most for its role and plans the cheapest actions
 * to reach it (dod_bots.c). The first step is what it does now: shoot, reload, seek the last-known enemy, take
 * cover, join a teammate, go to / hold the objective. Cost per bot: one scan of the players, two traces, one
 * search over a few hundred integers. */
#define DOD_BOT_MAX_FIRE_RANGE 2000.0f
#define DOD_BOT_THINK_PERIOD 0.5f
#define DOD_BOT_LOW_HEALTH 35.0f
#define DOD_BOT_HURT_MEMORY 12.0f	/* seconds after a hit that low health keeps a bot in cover; nobody heals in DoD, so it must not last for ever */
#define DOD_BOT_STAND_RANGE 400.0f	/* beyond this a bot keeps advancing along its route while it fires; inside it stands and fights */
#define DOD_BOT_NEAR_TEAM 450.0f

static const char *const g_goal_names[DOD_GOAL_COUNT] = { "capture", "defend", "hunt", "cover", "reload", "regroup" };

static void SetDest( DodBotState *state, const Vector &pos, int id )
{
	if( !state->nav_has || state->nav_id != id || ( pos - state->nav_pos ).Length() > 150.0f )
	{
		state->path_len = 0;
		state->path_replan = 0.0f;
	}
	state->nav_pos = pos;
	state->nav_id = id;
	state->nav_has = 1;
}

/* A waypoint near the bot that the last-known enemy cannot see; failing that, straight away from the enemy. */
static Vector PickCover( CBasePlayer *pBot, DodBotState *state )
{
	const Vector eye_off( 0, 0, 17.0f );
	const Vector from = state->known_pos + eye_off;
	int near_nodes[8], count, i;
	if( g_nav.node_count )
	{
		count = Dod_NavNearest( &g_nav, pBot->pev->origin.x, pBot->pev->origin.y, pBot->pev->origin.z, near_nodes, 8 );
		for( i = 0; i < count; i++ )
		{
			TraceResult tr;
			float x, y, z;
			Dod_NavNodePos( &g_nav, near_nodes[i], &x, &y, &z );
			if( fabsf( z - pBot->pev->origin.z ) > 64.0f || ( Vector( x, y, z ) - pBot->pev->origin ).Length2D() < 80.0f )
				continue;
			UTIL_TraceLine( from, Vector( x, y, z ) + eye_off, ignore_monsters, pBot->edict(), &tr );
			if( tr.flFraction < 0.95f )
				return Vector( x, y, z );
		}
	}
	{
		Vector away = pBot->pev->origin - state->known_pos;
		away.z = 0.0f;
		if( away.Length() < 1.0f )
			away = Vector( 1, 0, 0 );
		return pBot->pev->origin + away.Normalize() * 300.0f;
	}
}

/* Where fights are: the freshest contact the team knows, else the nearest objective this team does not hold, else the
 * middle of the waypoint graph. */
static void FightRef( CBasePlayer *pBot, DodTeam team, float ref[3] )
{
	const float from[3] = { pBot->pev->origin.x, pBot->pev->origin.y, pBot->pev->origin.z };
	DodGCObjectiveView views[DOD_BOT_MAX_OBJECTIVES];
	float placed, best = 1e9f;
	int count, i, found = 0;
	if( Dod_IntelPick( &g_intel, team, from, 0, -1.0f, gpGlobals->time, 0.0f, 1e9f, ref, &placed ) )
		return;
	count = DodGC_ListObjectives( views, DOD_BOT_MAX_OBJECTIVES );
	for( i = 0; i < count; i++ )
	{
		const float d = ( views[i].center - pBot->pev->origin ).Length();
		if( views[i].owner != team && d < best )
		{
			best = d;
			ref[0] = views[i].center.x;
			ref[1] = views[i].center.y;
			ref[2] = views[i].center.z;
			found = 1;
		}
	}
	if( !found )
	{
		static float centre[3];
		static int centre_nodes;
		if( centre_nodes != g_nav.node_count )
		{
			float sx = 0, sy = 0, sz = 0;
			for( i = 0; i < g_nav.node_count; i++ )
			{
				float x, y, z;
				Dod_NavNodePos( &g_nav, i, &x, &y, &z );
				sx += x;
				sy += y;
				sz += z;
			}
			centre[0] = sx / g_nav.node_count;
			centre[1] = sy / g_nav.node_count;
			centre[2] = sz / g_nav.node_count;
			centre_nodes = g_nav.node_count;
		}
		ref[0] = centre[0];
		ref[1] = centre[1];
		ref[2] = centre[2];
	}
}

/* Roam: with no objective and no lead, walk to a waypoint towards the fight that this bot has not been to lately and
 * no mate is heading for, so a team spreads over the map looking for the enemy. Returns whether it has a destination. */
static BOOL PatrolDest( CBasePlayer *pBot, DodBotState *state, DodTeam team )
{
	const float now = gpGlobals->time;
	if( g_nav.node_count < 8 )
		return FALSE;
	if( !state->patrol_has || now >= state->patrol_until || ( state->patrol_pos - pBot->pev->origin ).Length2D() < 100.0f )
	{
		const float own[3] = { pBot->pev->origin.x, pBot->pev->origin.y, pBot->pev->origin.z };
		float ref[3], avoid[DOD_BOT_MAX_SLOTS][3];
		int cand[8], recent[3], i, n, avoid_count = 0, pick;
		FightRef( pBot, team, ref );
		for( i = 0; i < 8; i++ )
			cand[i] = RANDOM_LONG( 0, g_nav.node_count - 1 );
		for( i = 0; i < state->patrol_recent_n; i++ )
			recent[i] = state->patrol_recent[i];
		for( i = 1; i <= gpGlobals->maxClients && i <= DOD_BOT_MAX_SLOTS; i++ )
		{
			CBasePlayer *pOther = PlayerAt( i );
			DodBotState *os = BotState( i );
			if( !pOther || pOther == pBot || !os || !os->active || !pOther->IsAlive() || TeamOf( pOther ) != team || !os->patrol_has )
				continue;
			avoid[avoid_count][0] = os->patrol_pos.x;
			avoid[avoid_count][1] = os->patrol_pos.y;
			avoid[avoid_count][2] = os->patrol_pos.z;
			avoid_count++;
		}
		pick = Dod_BotPickPatrol( &g_nav, own, 350.0f, ref, 0.0f, recent, state->patrol_recent_n, avoid, avoid_count, cand, 8 );
		if( pick < 0 )
			return FALSE;
		{
			float x, y, z;
			Dod_NavNodePos( &g_nav, pick, &x, &y, &z );
			state->patrol_pos = Vector( x, y, z );
		}
		n = state->patrol_recent_n < 3 ? state->patrol_recent_n++ : 2;
		for( i = n; i > 0; i-- )
			state->patrol_recent[i] = state->patrol_recent[i - 1];
		state->patrol_recent[0] = (short)pick;
		state->patrol_has = 1;
		state->patrol_until = now + 25.0f;
	}
	SetDest( state, state->patrol_pos, 1004 );
	return TRUE;
}

/* A lost target: look at a waypoint near where it was, as far as it could have got in a few seconds. */
static BOOL SearchSweep( CBasePlayer *pBot, DodBotState *state, DodTeam team )
{
	int near_nodes[8], count;
	const float own[3] = { pBot->pev->origin.x, pBot->pev->origin.y, pBot->pev->origin.z };
	const float ref[3] = { state->known_pos.x, state->known_pos.y, state->known_pos.z };
	int pick;
	( void )team;
	count = Dod_NavNearest( &g_nav, ref[0], ref[1], ref[2], near_nodes, 8 );
	pick = Dod_BotPickPatrol( &g_nav, own, 150.0f, ref, 700.0f, NULL, 0, NULL, 0, near_nodes, count );
	if( pick < 0 )
		return FALSE;
	{
		float x, y, z;
		Dod_NavNodePos( &g_nav, pick, &x, &y, &z );
		state->known_pos = Vector( x, y, z );
	}
	state->known_until = gpGlobals->time + 5.0f;
	return TRUE;
}

static void BotThink( CBasePlayer *pBot, DodBotState *state, CBasePlayer *enemy )
{
	PhaseTimer timer( DOD_PH_THINK );
	const float now = gpGlobals->time;
	const DodTeam team = TeamOf( pBot );
	unsigned ws = 0;
	int i, old_goal = state->plan.goal, old_behavior = state->behavior;
	BOOL have_mate = FALSE;
	Vector mate_pos;
	float mate_dist = DOD_BOT_NEAR_TEAM;

	if( !state->aggr_set )
	{
		state->aggr = Dod_BotAggression( pBot->entindex(), (int)CVAR_GET_FLOAT( "dod_bot_skill" ) );
		state->aggr_set = 1;
	}
	if( enemy )
	{
		state->known_pos = enemy->pev->origin;
		state->known_until = now + BotSkill().memory;
		state->search_left = 2;	/* if it slips away, sweep the area round where it was before giving up */
		if( now >= state->next_report )
		{
			/* a call-out to the team: where an enemy was just seen */
			const float ep[3] = { enemy->pev->origin.x, enemy->pev->origin.y, enemy->pev->origin.z };
			state->next_report = now + 1.0f;
			Dod_IntelReport( &g_intel, team, ep, DOD_INTEL_SEEN, pBot->entindex(), now, 8.0f );
		}
	}
	if( ( enemy != NULL ) != ( ( state->ws & DOD_WS_ENEMY_VISIBLE ) != 0 ) )
		state->next_think = 0.0f;	/* an enemy appeared or went out of sight */
	if( pBot->pev->health < state->prev_health - 0.5f )
		state->next_think = 0.0f;	/* hurt */
	state->prev_health = pBot->pev->health;
	if( now < state->next_think )
		return;
	state->next_think = now + DOD_BOT_THINK_PERIOD + RANDOM_FLOAT( 0.0f, 0.1f );

	if( !enemy && now >= state->known_until )
	{
		/* nothing known of its own: answer a mate's call-out (or a mate's death), if this bot is the sort to go and look */
		const float from[3] = { pBot->pev->origin.x, pBot->pev->origin.y, pBot->pev->origin.z };
		float pos[3], placed = 0.0f;
		const int kind = Dod_IntelPick( &g_intel, team, from, pBot->entindex(), state->intel_checked, now, 300.0f, 2400.0f, pos, &placed );
		if( kind )
		{
			const float d = ( Vector( pos[0], pos[1], pos[2] ) - pBot->pev->origin ).Length();
			state->intel_checked = placed;	/* one decision per report, not one per think */
			if( Dod_BotAnswersIntel( state->aggr, state->role, d, kind, RANDOM_FLOAT( 0.0f, 1.0f ) ) )
			{
				state->known_pos = Vector( pos[0], pos[1], pos[2] );
				state->known_until = now + 5.0f;
				state->search_left = 2;
				ALERT( at_console, "DoD bot %s: answers a team %s call-out %.0f units away\n", STRING( pBot->pev->netname ),
					kind == DOD_INTEL_DEATH ? "death" : "contact", d );
			}
		}
	}

	{
		CBasePlayerItem *item = pBot->m_pActiveItem;
		CBasePlayerWeapon *weapon = item ? (CBasePlayerWeapon *)item->GetWeaponPtr() : NULL;
		BOOL has_ammo = TRUE;
		/* an empty clip with a spare magazine to load; with none, reloading is no use, so it counts as armed */
		if( weapon && weapon->m_iClip == 0 && weapon->m_iPrimaryAmmoType >= 0 && pBot->m_rgAmmo[weapon->m_iPrimaryAmmoType] > 0 )
			has_ammo = FALSE;
		if( has_ammo )
			ws |= DOD_WS_HAS_AMMO;
	}
	if( enemy )
		ws |= DOD_WS_ENEMY_VISIBLE | DOD_WS_ENEMY_KNOWN;
	else if( now < state->known_until && !Dod_NavInHazard( &g_nav, team, state->known_pos.x, state->known_pos.y, state->known_pos.z ) )
		ws |= DOD_WS_ENEMY_KNOWN;	/* (an enemy last seen or heard inside a zone that would kill us is not worth the trip) */
	if( pBot->pev->health < state->prev_health - 0.5f )
		state->hurt_at = now;
	state->prev_health = pBot->pev->health;
	if( pBot->pev->health < DOD_BOT_LOW_HEALTH && now - state->hurt_at < DOD_BOT_HURT_MEMORY )
		ws |= DOD_WS_LOW_HEALTH;
	for( i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pOther = PlayerAt( i );
		float d;
		if( !pOther || pOther == pBot || !pOther->IsAlive() || TeamOf( pOther ) != team )
			continue;
		d = ( pOther->pev->origin - pBot->pev->origin ).Length();
		if( d < mate_dist )
		{
			mate_dist = d;
			mate_pos = pOther->pev->origin;
			have_mate = TRUE;
		}
	}
	if( have_mate )
		ws |= DOD_WS_NEAR_TEAM;
	if( !enemy && now < state->known_until )
	{
		TraceResult tr;
		UTIL_TraceLine( state->known_pos + Vector( 0, 0, 17 ), pBot->pev->origin + pBot->pev->view_ofs, ignore_monsters, pBot->edict(), &tr );
		if( tr.flFraction < 0.95f )
			ws |= DOD_WS_IN_COVER;
	}
	if( state->has_goal && state->goal_id >= 0 )
	{
		if( state->goal_held )
			ws |= DOD_WS_POINT_HELD;
		if( state->goal_contested )
			ws |= DOD_WS_POINT_CONTESTED;
		if( state->arrived )
			ws |= DOD_WS_AT_POINT;
	}
	state->ws = ws;
	state->plan = Dod_GoapThink( ws, state->role, state->team_bots, old_goal );
	state->behavior = state->plan.count ? state->plan.steps[0] : DOD_ACT_NONE;

	switch( state->behavior )
	{
	case DOD_ACT_SEEK:
		if( !enemy && ( state->known_pos - pBot->pev->origin ).Length2D() < 110.0f )
		{
			/* got there, nobody here: sweep a spot nearby (where it may have gone), then give the lead up */
			if( state->search_left > 0 && SearchSweep( pBot, state, team ) )
				state->search_left--;
			else
			{
				const float kp[3] = { state->known_pos.x, state->known_pos.y, state->known_pos.z };
				Dod_IntelClearNear( &g_intel, team, kp, 400.0f );
				state->known_until = 0.0f;
				state->search_left = 0;
				state->next_think = 0.0f;
			}
		}
		SetDest( state, state->known_pos, 1000 );
		break;
	case DOD_ACT_TAKE_COVER:
		SetDest( state, PickCover( pBot, state ), 1001 );
		break;
	case DOD_ACT_REGROUP:
		if( have_mate )
			SetDest( state, mate_pos, 1002 );
		break;
	default:
		if( state->has_goal && state->goal_id >= 0 )
			SetDest( state, state->goal, state->goal_id );
		else if( state->behavior == DOD_ACT_ATTACK && now < state->known_until )
			SetDest( state, state->known_pos, 1003 );	/* a hunt with an enemy in sight: close in on it along the route while firing */
		else if( state->behavior == DOD_ACT_NONE && PatrolDest( pBot, state, team ) )
			break;	/* nothing asked of this bot: roam towards where the fight is */
		else
			state->nav_has = 0;
		break;
	}
	if( state->plan.goal != old_goal || state->behavior != old_behavior )
		ALERT( at_console, "DoD bot %s: goal %s, doing %s (%d step plan, role %s)\n", STRING( pBot->pev->netname ),
			state->plan.goal >= 0 ? g_goal_names[state->plan.goal] : "none", g_action_names[state->behavior], state->plan.count,
			state->role == DOD_ROLE_DEFENDER ? "defender" : "attacker" );
}

/* Whether the bot is where its current step wants it (checked every frame). */
static void UpdateArrival( CBasePlayer *pBot, DodBotState *state )
{
	switch( state->behavior )
	{
	case DOD_ACT_SEEK:
	case DOD_ACT_TAKE_COVER:
	case DOD_ACT_REGROUP:
		state->nav_arrived = state->nav_has &&
			( state->nav_pos - pBot->pev->origin ).Length2D() < 72.0f && fabsf( state->nav_pos.z - pBot->pev->origin.z ) < 72.0f;
		break;
	default:
		state->nav_arrived = state->nav_has && state->has_goal && state->arrived;
		break;
	}
}

/* ---------------------------------------------------------------- test scenarios (dod_bot_stage)
 * Dev-only, off by default. With two bots on opposing teams the harness puts them where a test needs them and
 * holds them still, so what the perception and capture rules do can be read from the log:
 *  1: the Allies bot watches; the Axis bot idles at a floor waypoint that is inside the Allies bot's view cone
 *     but behind a wall (it must neither see nor shoot it), then in the open (it must see it, after a delay);
 *  2: both bots idle in a capture area: Allies alone (the capture runs), then Axis joins (contested, paused),
 *     then Axis leaves (the capture finishes). */
static int g_stage_phase;
static float g_stage_until;
static int g_stage_node_a, g_stage_node_c;
static float g_stage_yaw;
static int g_stage_point;

static void StagePlace( CBasePlayer *pBot, const Vector &pos, float yaw )
{
	DodBotState *state = BotState( pBot->entindex() );
	pBot->pev->velocity = Vector( 0, 0, 0 );
	UTIL_SetOrigin( pBot->pev, pos );
	if( state )
	{
		state->yaw = state->want_yaw = yaw;	/* a teleport, not a turn */
		state->pitch = state->want_pitch = 0.0f;
		state->yaw_vel = state->pitch_vel = 0.0f;
		state->stage_yaw = yaw;
		state->path_len = 0;
	}
}

static BOOL StageNodeEye( int node, Vector *eye )
{
	float x, y, z;
	if( node < 0 || node >= g_nav.node_count )
		return FALSE;
	Dod_NavNodePos( &g_nav, node, &x, &y, &z );
	*eye = Vector( x, y, z + 17.0f );
	return TRUE;
}

/* A waypoint pair on one floor, 100..260 units apart, with no line of sight between them (a wall), and a third
 * waypoint in the same direction from A (within 35 degrees) that A can see. Searches a slice per frame. */
static BOOL StageFindWall( CBasePlayer *pBot, int *node_a, int *node_b, int *node_c )
{
	static int next_a;
	int tries;
	for( tries = 0; tries < 24 && next_a < g_nav.node_count; tries++, next_a++ )
	{
		float ax, ay, az;
		int b;
		Dod_NavNodePos( &g_nav, next_a, &ax, &ay, &az );
		for( b = 0; b < g_nav.node_count; b++ )
		{
			float bx, by, bz, d;
			Vector ea, eb;
			TraceResult tr;
			if( b == next_a )
				continue;
			Dod_NavNodePos( &g_nav, b, &bx, &by, &bz );
			d = sqrtf( ( bx - ax ) * ( bx - ax ) + ( by - ay ) * ( by - ay ) );
			if( d < 100.0f || d > 260.0f || fabsf( bz - az ) > 12.0f )
				continue;
			StageNodeEye( next_a, &ea );
			StageNodeEye( b, &eb );
			UTIL_TraceLine( ea, eb, ignore_monsters, pBot->edict(), &tr );
			if( tr.flFraction > 0.9f )
				continue;
			{
				const float yaw_b = YawTo( ea, eb );
				int c;
				for( c = 0; c < g_nav.node_count; c++ )
				{
					float cx, cy, cz, dc;
					Vector ec;
					if( c == next_a || c == b )
						continue;
					Dod_NavNodePos( &g_nav, c, &cx, &cy, &cz );
					dc = sqrtf( ( cx - ax ) * ( cx - ax ) + ( cy - ay ) * ( cy - ay ) );
					if( dc < 150.0f || dc > 350.0f || fabsf( cz - az ) > 12.0f )
						continue;
					StageNodeEye( c, &ec );
					if( fabsf( Dod_BotAngleDelta( yaw_b, YawTo( ea, ec ) ) ) > 35.0f )
						continue;
					UTIL_TraceLine( ea, ec, ignore_monsters, pBot->edict(), &tr );
					if( tr.flFraction < 0.98f )
						continue;
					*node_a = next_a;
					*node_b = b;
					*node_c = c;
					return TRUE;
				}
			}
		}
	}
	return FALSE;
}

static void StageStep( void )
{
	const int stage = (int)CVAR_GET_FLOAT( "dod_bot_stage" );
	const float now = gpGlobals->time;
	CBasePlayer *allies = NULL, *axis = NULL;
	DodBotState *sa, *sx;
	int i;
	static float armed_at;
	static int g_track_victim;
	static float g_track_until, g_track_next;
	if( stage == 7 && g_stage_phase >= 9 )
	{
		/* test scenario 7: the Allies win the round 20 s after the human joined, the Axis win the next one 40 s after that (round-end fanfares) */
		static float t7;
		static int ended;
		if( !t7 )
			t7 = now;
		if( ended == 0 && now - t7 > 20.0f )
		{
			ended = 1;
			ALERT( at_console, "DoD STAGE rounds: ending the round, Allies win\n" );
			Dod_EndRound( DOD_TEAM_ALLIES );
		}
		else if( ended == 1 && now - t7 > 60.0f && Dod_GetState()->phase == DOD_ROUND_ACTIVE )
		{
			ended = 2;
			ALERT( at_console, "DoD STAGE rounds: ending the round, Axis win\n" );
			Dod_EndRound( DOD_TEAM_AXIS );
		}
		return;
	}
	if( stage == 6 && g_stage_phase >= 9 )
	{
		/* test scenario 6 (dod_caen): the human stands 140 units from the particle_shooter at 1280 2928 159, looking at it, for smoke close-ups */
		static int placed6;
		CBasePlayer *human = NULL;
		for( int k = 1; k <= gpGlobals->maxClients; k++ )
		{
			CBasePlayer *p = PlayerAt( k );
			if( p && !DodBots_IsBot( p ) )
				human = p;
		}
		if( human && human->IsAlive() && !placed6 )
		{
			placed6 = 1;
			UTIL_SetOrigin( human->pev, Vector( 1140.0f, 2928.0f, 140.0f ) );
			human->pev->velocity = g_vecZero;
			human->pev->v_angle.x = 0.0f;
			human->pev->v_angle.y = 0.0f;
			human->pev->angles = human->pev->v_angle;
			human->pev->fixangle = 1;
			ALERT( at_console, "DoD STAGE particles: human placed 140 units from the shooter at 1280 2928 159\n" );
		}
		return;
	}
	if( stage == 5 && g_stage_phase >= 9 )
	{
		/* test scenario 5: an Axis bot stands 130 units in front of the human, blind and still, and tilts its view 30 degrees down, 30 up and
		 * level in turn (6 s each), to look at how the body follows the pitch */
		static float t0;
		static int placed;
		CBasePlayer *human = NULL, *victim = NULL;
		for( int k = 1; k <= gpGlobals->maxClients; k++ )
		{
			CBasePlayer *p = PlayerAt( k );
			if( !p )
				continue;
			if( !DodBots_IsBot( p ) )
				human = p;
			else if( p->IsAlive() && TeamOf( p ) == DOD_TEAM_AXIS && !victim )
				victim = p;
		}
		if( human && victim && human->IsAlive() )
		{
			static const float pitches[3] = { 30.0f, -30.0f, 0.0f };
			DodBotState *vs = BotState( victim->entindex() );
			if( !placed )
			{
				placed = 1;
				t0 = now;
			}
			UTIL_MakeVectors( Vector( 0.0f, human->pev->v_angle.y, 0.0f ) );
			UTIL_SetOrigin( victim->pev, human->pev->origin + gpGlobals->v_forward * 130.0f );
			victim->pev->velocity = g_vecZero;
			if( vs )
			{
				const int phase = (int)( ( now - t0 ) / 6.0f ) % 3;
				vs->stage_role = 2;
				vs->yaw = vs->want_yaw = human->pev->v_angle.y + 180.0f;
				vs->pitch = vs->want_pitch = pitches[phase];
				if( (int)( now - t0 ) != g_track_victim )
				{
					g_track_victim = (int)( now - t0 );
					ALERT( at_console, "DoD STAGE aim: victim pitch %.0f at %d s\n", vs->pitch, g_track_victim );
				}
			}
		}
		return;
	}
	if( stage == 4 && g_stage_phase >= 9 )
	{
		/* test scenario 4: the human has joined; every 14 s a bot is stood 160 units in front of the human and killed with the next hit of a
		 * cycle (head, stomach, chest from the front, chest from behind, explosion, crouching), to look at the death animation and the corpse */
		static float next_kill;
		static int round, round_started;
		CBasePlayer *human = NULL, *victim = NULL;
		if( !next_kill )
			next_kill = now + 10.0f;
		for( int k = 1; k <= gpGlobals->maxClients; k++ )
		{
			CBasePlayer *p = PlayerAt( k );
			if( !p )
				continue;
			if( !DodBots_IsBot( p ) )
				human = p;
			else if( p->IsAlive() && TeamOf( p ) == DOD_TEAM_AXIS && !victim )
				victim = p;
		}
		if( human && victim && human->IsAlive() && now >= next_kill )
		{
			static const int hitgroups[6] = { 1, 3, 2, 2, 2, 2 };
			static const char *const names[6] = { "headshot", "gutshot", "chest from the front", "chest from behind", "explosion", "crouching" };
			const int r = round++ % 6;
			if( !round_started )
			{
				/* the join spot sits where the lighting pack has no data (corpses and players draw black there); stand where live bots are lit */
				round_started = 1;
				UTIL_SetOrigin( human->pev, Vector( -417.0f, 2080.0f, 60.0f ) );
				human->pev->velocity = g_vecZero;
			}
			UTIL_MakeVectors( human->pev->v_angle );
			Vector org = human->pev->origin + gpGlobals->v_forward * 200.0f - gpGlobals->v_right * 70.0f;
			UTIL_SetOrigin( victim->pev, org );
			victim->pev->velocity = g_vecZero;
			victim->pev->angles.y = human->pev->v_angle.y + ( r == 3 ? 0.0f : 180.0f );	/* facing the human, or turned away for the one from behind */
			victim->pev->v_angle.y = victim->pev->angles.y;
			if( r == 5 )
				victim->pev->flags |= FL_DUCKING;
			victim->m_LastHitGroup = hitgroups[r];
			ALERT( at_console, "DoD STAGE execute: %s, %s\n", STRING( victim->pev->netname ), names[r] );
			victim->TakeDamage( human->pev, human->pev, 100.0f + victim->pev->health * 0.0f + 10.0f, r == 4 ? DMG_BLAST : DMG_BULLET );
			next_kill = now + 14.0f;
			g_track_victim = victim->entindex();
			g_track_until = now + 9.0f;
			g_track_next = now;
		}
		if( g_track_victim && now >= g_track_next )
		{
			CBasePlayer *tv = PlayerAt( g_track_victim );
			CBaseEntity *bq = NULL;
			int shown = 0;
			g_track_next = now + 0.5f;
			if( tv )
				ALERT( at_console, "DoD STAGE track: dead %d health %.0f model %d fx %d move %d seq %d fin %d\n", tv->pev->deadflag, tv->pev->health,
					tv->pev->modelindex, tv->pev->effects, tv->pev->movetype, tv->pev->sequence, (int)tv->m_fSequenceFinished );
			while( ( bq = UTIL_FindEntityByClassname( bq, "bodyque" ) ) != NULL )
				if( bq->pev->modelindex )
				{
					ALERT( at_console, "DoD STAGE track: bodyque %d model %d owner-slot %.0f at %.0f %.0f %.0f\n", bq->entindex(), bq->pev->modelindex, bq->pev->renderamt,
						bq->pev->origin.x, bq->pev->origin.y, bq->pev->origin.z );
					shown++;
				}
			if( now >= g_track_until )
				g_track_victim = 0;
		}
		return;
	}
	if( stage == 3 && g_stage_phase >= 9 )
	{
		/* diagnostic: what state is the human in after the join */
		static float next_log;
		if( now >= next_log )
		{
			CBasePlayer *p;
			next_log = now + 5.0f;
			for( int k = 1; k <= gpGlobals->maxClients; k++ )
			{
				p = PlayerAt( k );
				if( p && !DodBots_IsBot( p ) )
					ALERT( at_console, "DoD STAGE human: health %.0f deadflag %d observer %d team %d weapon %s origin %.0f,%.0f,%.0f\n", p->pev->health, p->pev->deadflag,
						p->IsObserver(), (int)TeamOf( p ), p->m_pActiveItem ? STRING( p->m_pActiveItem->pev->classname ) : "-", p->pev->origin.x, p->pev->origin.y, p->pev->origin.z );
			}
		}
		return;
	}
	if( !stage || g_stage_phase >= 9 )
		return;
	if( stage == 3 || stage == 4 || stage == 5 || stage == 6 || stage == 7 )
	{
		/* put the human on the Allies as a rifleman so the pad can fire, reload and cycle weapons (view model checks) */
		static float arm;
		if( !arm )
			arm = now + 14.0f;
		if( now >= arm )
		{
			for( i = 1; i <= gpGlobals->maxClients; i++ )
			{
				CBasePlayer *p = PlayerAt( i );
				if( p && !DodBots_IsBot( p ) )
				{
					DodGC_HumanJoin( p, DOD_TEAM_ALLIES, DOD_CLASS_RIFLEMAN );
					ALERT( at_console, "DoD STAGE weapons: the human joined the Allies as a rifleman\n" );
					g_stage_phase = 9;
					break;
				}
			}
		}
		return;
	}
	for( i = 1; i <= gpGlobals->maxClients && i <= DOD_BOT_MAX_SLOTS; i++ )
	{
		CBasePlayer *p = PlayerAt( i );
		if( !p || !DodBots_IsBot( p ) || !p->IsAlive() )
			continue;
		if( TeamOf( p ) == DOD_TEAM_ALLIES && !allies )
			allies = p;
		else if( TeamOf( p ) == DOD_TEAM_AXIS && !axis )
			axis = p;
	}
	if( !allies || !axis || !g_nav.node_count )
	{
		armed_at = 0.0f;
		return;
	}
	if( armed_at == 0.0f )
		armed_at = now + 12.0f;
	if( now < armed_at )
		return;
	sa = BotState( allies->entindex() );
	sx = BotState( axis->entindex() );
	if( !sa || !sx )
		return;
	if( stage == 1 )
	{
		int a, b, c;
		Vector pos;
		float x, y, z;
		switch( g_stage_phase )
		{
		case 0:
			if( !StageFindWall( allies, &a, &b, &c ) )
			{
				static BOOL warned;
				if( !warned )
				{
					ALERT( at_console, "DoD STAGE wall: still searching for a wall geometry\n" );
					warned = TRUE;
				}
				return;
			}
			g_stage_node_a = a;
			g_stage_node_c = c;
			Dod_NavNodePos( &g_nav, a, &x, &y, &z );
			{
				Vector ea, eb;
				StageNodeEye( a, &ea );
				StageNodeEye( b, &eb );
				g_stage_yaw = YawTo( ea, eb );
				ALERT( at_console, "DoD STAGE wall: watcher at waypoint %d (%.0f,%.0f,%.0f) facing %.0f; target waypoint %d behind a wall, %.0f units away\n",
					a, x, y, z, g_stage_yaw, b, ( eb - ea ).Length2D() );
				sa->stage_role = 1;
				sx->stage_role = 2;
				StagePlace( allies, Vector( x, y, z ), g_stage_yaw );
				Dod_NavNodePos( &g_nav, b, &x, &y, &z );
				StagePlace( axis, Vector( x, y, z ), 0.0f );
			}
			sa->stage_sightings = sa->stage_fire_frames = 0;
			g_stage_until = now + 8.0f;
			g_stage_phase = 1;
			break;
		case 1:
			if( now < g_stage_until )
				break;
			ALERT( at_console, "DoD STAGE wall: 8 s behind the wall, view cone on it: frames with the target in view %d, trigger frames %d\n",
				sa->stage_sightings, sa->stage_fire_frames );
			Dod_NavNodePos( &g_nav, g_stage_node_c, &x, &y, &z );
			pos = Vector( x, y, z );
			StagePlace( axis, pos, 0.0f );
			sa->stage_sightings = sa->stage_fire_frames = 0;
			ALERT( at_console, "DoD STAGE wall: target steps into the open at waypoint %d (%.0f units from the watcher)\n", g_stage_node_c,
				( pos - allies->pev->origin ).Length2D() );
			g_stage_until = now + 6.0f;
			g_stage_phase = 2;
			break;
		case 2:
			if( now < g_stage_until )
				break;
			ALERT( at_console, "DoD STAGE wall: 6 s in the open: frames with the target in view %d, trigger frames %d\n", sa->stage_sightings,
				sa->stage_fire_frames );
			sa->stage_role = sx->stage_role = 0;
			g_stage_phase = 9;
			break;
		}
	}
	else if( stage == 2 )
	{
		static float next_status;
		DodGCObjectiveView views[DOD_BOT_MAX_OBJECTIVES];
		if( g_stage_phase > 0 && now >= next_status )
		{
			next_status = now + 1.0f;
			ALERT( at_console, "DoD STAGE capture status: Allies bot %s at %.0f,%.0f,%.0f; Axis bot %s at %.0f,%.0f,%.0f\n", allies->IsAlive() ? "alive" : "DEAD",
				allies->pev->origin.x, allies->pev->origin.y, allies->pev->origin.z, axis->IsAlive() ? "alive" : "DEAD", axis->pev->origin.x,
				axis->pev->origin.y, axis->pev->origin.z );
		}
		const int count = DodGC_ListObjectives( views, DOD_BOT_MAX_OBJECTIVES );
		int pick = -1, node[2], n;
		float x, y, z;
		switch( g_stage_phase )
		{
		case 0:
			for( i = 0; i < count; i++ )
				ALERT( at_console, "DoD STAGE capture: point %d owner %d, Allies may capture %d (need %d), Axis may capture %d (need %d)\n", views[i].id,
					(int)views[i].owner, views[i].can_cap[DOD_TEAM_ALLIES], views[i].num_cap[DOD_TEAM_ALLIES], views[i].can_cap[DOD_TEAM_AXIS],
					views[i].num_cap[DOD_TEAM_AXIS] );
			for( i = 0; i < count; i++ )	/* a point both may capture, one player each, shows a contest; else any the Allies may take */
				if( views[i].owner != DOD_TEAM_ALLIES && views[i].can_cap[DOD_TEAM_ALLIES] && views[i].can_cap[DOD_TEAM_AXIS] &&
					views[i].num_cap[DOD_TEAM_ALLIES] <= 1 && views[i].num_cap[DOD_TEAM_AXIS] <= 1 )
				{
					pick = i;
					break;
				}
			if( pick < 0 )
				for( i = 0; i < count; i++ )
					if( views[i].owner != DOD_TEAM_ALLIES && views[i].can_cap[DOD_TEAM_ALLIES] )
					{
						pick = i;
						break;
					}
			if( pick < 0 )
				return;
			ALERT( at_console, "DoD STAGE capture: using point %d (%s)\n", views[pick].id,
				views[pick].can_cap[DOD_TEAM_AXIS] ? "both teams may capture: a contest is possible" : "Axis may not capture here: a defender inside must not block" );
			{
				const float mins[3] = { views[pick].mins.x, views[pick].mins.y, views[pick].mins.z };
				const float maxs[3] = { views[pick].maxs.x, views[pick].maxs.y, views[pick].maxs.z };
				n = Dod_NavInside( &g_nav, mins, maxs, 0.0f, node, 2 );	/* a waypoint inside the area, not just near its centre */
			}
			if( n < 1 )
				n = Dod_NavNearest( &g_nav, views[pick].center.x, views[pick].center.y, views[pick].center.z, node, 2 );
			if( n < 1 )
				return;
			g_stage_point = views[pick].id;
			g_stage_node_a = node[0];
			sa->stage_role = sx->stage_role = 2;
			Dod_NavNodePos( &g_nav, node[0], &x, &y, &z );
			StagePlace( allies, Vector( x, y, z ), 0.0f );
			StagePlace( axis, Vector( x + 900.0f, y, z ), 0.0f );	/* parked away until it is needed */
			ALERT( at_console, "DoD STAGE capture: Allies bot placed in point %d's capture area alone\n", g_stage_point );
			g_stage_until = now + 2.5f;
			g_stage_phase = 1;
			break;
		case 1:
			if( now < g_stage_until )
				break;
			Dod_NavNodePos( &g_nav, g_stage_node_a, &x, &y, &z );
			StagePlace( axis, Vector( x, y + 40.0f, z ), 180.0f );
			ALERT( at_console, "DoD STAGE capture: Axis bot steps into the same area (contest)\n" );
			g_stage_until = now + 8.0f;
			g_stage_phase = 2;
			break;
		case 2:
			if( now < g_stage_until )
				break;
			Dod_NavNodePos( &g_nav, g_stage_node_a, &x, &y, &z );
			StagePlace( axis, Vector( x + 900.0f, y, z ), 0.0f );
			ALERT( at_console, "DoD STAGE capture: Axis bot leaves; Allies carry on\n" );
			g_stage_until = now + 20.0f;
			g_stage_phase = 3;
			break;
		case 3:
			if( now < g_stage_until )
				break;
			sa->stage_role = sx->stage_role = 0;
			g_stage_phase = 9;
			break;
		}
	}
}

static void RunBot( int index, CBasePlayer *pBot )
{
	SegTimer seg;
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

	if( !pBot->IsAlive() && state->was_alive )
	{
		/* once per death: where, what hit it last, and the damage kinds (to tell a firefight from a hazard or a fall) */
		edict_t *inflictor = pBot->pev->dmg_inflictor;
		ALERT( at_console, "DoD bot %s: died at %.0f,%.0f,%.0f, last hit by %s (damage bits 0x%x, last damage %.0f)\n", STRING( pBot->pev->netname ),
			pBot->pev->origin.x, pBot->pev->origin.y, pBot->pev->origin.z,
			inflictor && !FNullEnt( inflictor ) ? STRING( inflictor->v.classname ) : "nothing", pBot->m_bitsDamageType, pBot->pev->dmg_take );
	}
	if( !pBot->IsAlive() && state->was_alive )
	{
		/* a mate died here: the team now believes the enemy is near (the killer is not named, so it is a weak lead) */
		const float dp[3] = { pBot->pev->origin.x, pBot->pev->origin.y, pBot->pev->origin.z };
		Dod_IntelReport( &g_intel, TeamOf( pBot ), dp, DOD_INTEL_DEATH, pBot->entindex(), gpGlobals->time, 20.0f );
	}
	state->was_alive = pBot->IsAlive();
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
		{
			/* the fire press that respawns a dead bot runs the whole player spawn (spawn spot, map targets, weapons,
			 * model binds): worth a line, it is the longest frame a bot causes */
			const unsigned long long t0 = BotClockUs();
			( *g_engfuncs.pfnRunPlayerMove )( pBot->edict(), viewangles, 0, 0, 0, buttons, 0, msec );
			if( buttons && pBot->IsAlive() )
				ALERT( at_console, "DoD bot %s: respawned as team %d at %.0f %.0f %.0f, the spawn took %.0f ms\n", STRING( pBot->pev->netname ), (int)TeamOf( pBot ),
					pBot->pev->origin.x, pBot->pev->origin.y, pBot->pev->origin.z, (double)( BotClockUs() - t0 ) / 1000.0 );
		}
		state->yaw = state->want_yaw = pBot->pev->v_angle.y;
		state->pitch = state->want_pitch = pBot->pev->v_angle.x;
		state->yaw_vel = state->pitch_vel = 0.0f;
		state->has_goal = 0;
		state->goal_id = -1;
		state->arrived = 0;
		state->search_left = 0;
		state->patrol_has = 0;
		state->nav_has = state->nav_arrived = 0;
		state->behavior = DOD_ACT_NONE;
		state->plan.goal = -1;
		state->plan.count = 0;
		state->ws = 0;
		state->next_think = 0.0f;
		state->known_until = 0.0f;
		state->tgt = 0;
		state->burst_until = 0.0f;
		state->have_prev_angles = 0;
		state->cell_n = 0;
		state->path_len = 0;
		state->ladder_dir = 0;
		return;
	}
	if( state->stage_role == 2 )
	{
		/* test scenario: stand still, blind and deaf, no buttons */
		viewangles[0] = state->pitch;
		viewangles[1] = state->yaw;
		viewangles[2] = 0.0f;
		pBot->pev->v_angle.x = viewangles[0];
		pBot->pev->v_angle.y = viewangles[1];
		pBot->pev->angles.x = Dod_BotBodyPitch( viewangles[0] );
		pBot->pev->angles.y = viewangles[1];
		( *g_engfuncs.pfnRunPlayerMove )( pBot->edict(), viewangles, 0, 0, 0, 0, 0, msec );
		pBot->pev->angles.x = Dod_BotBodyPitch( viewangles[0] );	/* after the move: the player move rewrites the angles */
		{
			static float next_stage_log[DOD_MAX_PLAYERS + 1];
			const int bi = pBot->entindex();
			if( bi >= 1 && bi <= DOD_MAX_PLAYERS && gpGlobals->time >= next_stage_log[bi] )
			{
				next_stage_log[bi] = gpGlobals->time + 1.0f;
				ALERT( at_console, "DoD bot aim %s: staged, pitch %.0f, v_angle %.0f %.0f, body pitch %.0f, gaitsequence %d, sequence %d\n", STRING( pBot->pev->netname ),
					viewangles[0], pBot->pev->v_angle.x, pBot->pev->v_angle.y, pBot->pev->angles.x, pBot->pev->gaitsequence, pBot->pev->sequence );
			}
		}
		return;
	}
	EnsureNav();
	if( state->yaw == 0.0f && state->pitch == 0.0f )
		state->yaw = state->want_yaw = pBot->pev->v_angle.y;
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

	seg.mark();
	enemy = Perceive( pBot, state, &enemy_distance );
	BotThink( pBot, state, enemy );
	UpdateArrival( pBot, state );
	seg.mark();
	/* shoot only when the plan says so (or there is no plan yet); a bot taking cover or reloading does not stand and trade */
	const BOOL fight = enemy != NULL && ( state->behavior == DOD_ACT_ATTACK || state->behavior == DOD_ACT_NONE );
	{
		/* No path graph: if the bot is walking but going nowhere, break the
		 * straight-line loop with a growing detour. */
		const DodBotEscape esc = Dod_BotStuckUpdate( &state->stuck, pBot->pev->origin.x, pBot->pev->origin.y,
			gpGlobals->time, !fight && !state->nav_arrived && !state->ladder_dir && state->behavior != DOD_ACT_RELOAD, RANDOM_FLOAT( 0.0f, 1.0f ) );
		if( esc.stuck )
		{
			state->heading = Dod_BotAngleDelta( 0.0f, state->yaw + esc.turn );	/* the view eases round to it; the legs go at once */
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

	BOOL path_follow = FALSE;
	BOOL advance = FALSE;	/* a fight at range: keep going along the route, view on the enemy */
	if( fight )
	{
		const DodBotSkill sk = BotSkill();
		const float now = gpGlobals->time;
		const Vector target = enemy->pev->origin + enemy->pev->view_ofs * 0.9f;
		const float want_yaw = YawTo( eye, target ), want_pitch = PitchTo( eye, target );
		float err;
		in.has_target = 1;
		in.target_distance = enemy_distance;
		if( enemy->entindex() != state->last_tgt || now - state->last_fight_time > 1.0f )
		{
			/* a new target: a moment to take it in before the first shot, and aim from scratch */
			state->last_tgt = enemy->entindex();
			state->track_since = now;
			state->react_until = now + Dod_BotReactionDelay( &sk, RANDOM_FLOAT( 0.0f, 1.0f ) );
			state->next_noise = 0.0f;
			ALERT( at_console, "DoD bot %s: sees an enemy at %.0f units, reacts in %.0f ms\n", STRING( pBot->pev->netname ), enemy_distance,
				( state->react_until - now ) * 1000.0f );
			DodGC_VoiceAuto( pBot, "enemyahead" );
		}
		state->last_fight_time = now;
		err = Dod_BotAimErrorEx( &sk, enemy_distance, enemy->pev->velocity.Length2D(), pBot->pev->velocity.Length2D(), now - state->track_since );
		if( now >= state->next_noise )
		{
			state->noise_yaw = RANDOM_FLOAT( -err, err );
			state->noise_pitch = RANDOM_FLOAT( -err, err ) * 0.5f;
			state->next_noise = now + 0.3f;
		}
		state->want_yaw = want_yaw + state->noise_yaw;
		state->want_pitch = want_pitch + state->noise_pitch;
		{
			/* fire when the view is on where the bot is trying to aim; the noise is what makes shots miss */
			const float dy = fabsf( Dod_BotAngleDelta( state->yaw, want_yaw + state->noise_yaw ) );
			const float dp = fabsf( Dod_BotAngleDelta( state->pitch, want_pitch + state->noise_pitch ) );
			aim_error = dy > dp ? dy : dp;
		}
		in.aim_error = aim_error;
		advance = state->nav_has && !state->nav_arrived && !state->ladder_dir && enemy_distance > DOD_BOT_STAND_RANGE;
		if( advance && gpGlobals->time >= state->hold_heading_until )
		{
			Vector node = state->nav_pos;
			const BOOL on_path = NavTarget( pBot, state, &node );
			state->heading = YawTo( pBot->pev->origin, on_path ? node : state->nav_pos );
			path_follow = on_path;
		}
	}
	else
	{
		/* Head for the goal, or keep the current heading. Inside the capture volume stand and look
		 * around instead (an attacker stays until the point flips, a defender guards it). */
		if( state->nav_arrived )
		{
			if( gpGlobals->time >= state->hold_heading_until )
			{
				state->heading = RANDOM_FLOAT( -180.0f, 180.0f );
				state->hold_heading_until = gpGlobals->time + RANDOM_FLOAT( 2.0f, 4.0f );
			}
		}
		else if( state->nav_has )
		{
			Vector target = state->nav_pos;
			const BOOL on_path = NavTarget( pBot, state, &target );
			path_follow = on_path && !state->ladder_dir;
			if( state->ladder_dir )
			{
				/* climbing: face the ladder's far end, look up (or down) and push forward */
				state->heading = YawTo( pBot->pev->origin, target );
			}
			else if( on_path && gpGlobals->time >= state->hold_heading_until )
			{
				/* the graph's edges are walkable in a straight line (tools/gen_waypoints.py checks that), so go
				 * straight at the node: the free-run probing below mistakes steps and stairs for walls */
				state->heading = YawTo( pBot->pev->origin, target );
				state->steer_off = 0.0f;
			}
			else if( gpGlobals->time >= state->hold_heading_until )
			{
				if( gpGlobals->time >= state->next_steer && gpGlobals->time > g_steer_stamp )
				{
					g_steer_stamp = gpGlobals->time;
					state->steer_yaw = SteerYaw( pBot, state, on_path ? target : state->nav_pos, floor_trust );
					state->next_steer = gpGlobals->time + DOD_BOT_STEER_PERIOD;
				}
				state->heading = state->steer_yaw;
			}
		}
		state->want_yaw = state->heading;
		state->want_pitch = state->ladder_dir > 0 ? -50.0f : ( state->ladder_dir < 0 ? 50.0f : 0.0f );
	}

	move = Dod_BotDecide( &in );
	if( advance )
	{
		move.forward = 1.0f;	/* the route, not the enemy, sets the way; the view stays on the enemy */
		move.side = 0.0f;
	}
	if( ( !fight || advance ) && state->nav_has && !state->ladder_dir && ( move.forward != 0.0f || move.side != 0.0f ) )
	{
		/* The movement command is relative to the view, which eases round to the heading over a few frames: rotate
		 * the command by the difference so the legs go the way the bot means to at once, as a person running while
		 * turning does, instead of running off along the old view. */
		const float d = Dod_BotAngleDelta( state->yaw, state->heading ) * (float)M_PI / 180.0f;
		const float c = cos( d ), sn = sin( d ), f = move.forward, sd = move.side;
		float len;
		move.forward = f * c + sd * sn;
		move.side = sd * c - f * sn;
		len = sqrtf( move.forward * move.forward + move.side * move.side );
		if( len > 1.0f )
		{
			move.forward /= len;
			move.side /= len;
		}
	}
	seg.mark();
	BOOL zone_stop = FALSE;	/* the zone guard is holding the bot back this frame: no jump either */
	if( g_nav.zone_count )
	{
		/* Never walk into a damage zone that hurts this team (spawn-protection zones, minefields): the route costs
		 * only discourage it, and chasing an enemy or a heard sound ignores them. Standing already inside one is left alone. */
		const DodTeam gteam = TeamOf( pBot );
		const float yr = state->yaw * (float)M_PI / 180.0f;
		const Vector fwd( cos( yr ), sin( yr ), 0.0f ), right( sin( yr ), -cos( yr ), 0.0f );
		const Vector dir = fwd * move.forward + right * move.side;
		const Vector o = pBot->pev->origin;
		const Vector vel( pBot->pev->velocity.x, pBot->pev->velocity.y, 0.0f );
		const float speed = vel.Length();
		/* strictly inside already: nothing left to prevent. Otherwise look where the command leads (a body length
		 * and a half) and where momentum carries the bot in the next 0.4 s: the zone kills on touch. */
		if( ( dir.Length() > 0.1f || speed > 40.0f ) && !Dod_NavInHazardMargin( &g_nav, gteam, o.x, o.y, o.z, 0.0f ) )
		{
			const Vector ahead = o + ( dir.Length() > 0.1f ? dir.Normalize() * 72.0f : Vector( 0, 0, 0 ) );
			const Vector coast = o + vel * 0.4f;
			if( Dod_NavInHazard( &g_nav, gteam, ahead.x, ahead.y, o.z ) || ( speed > 40.0f && Dod_NavInHazard( &g_nav, gteam, coast.x, coast.y, o.z ) ) )
			{
				static float next_note;
				/* stand still, and brake against any momentum towards the zone */
				const Vector vdir = speed > 1.0f ? vel * ( 1.0f / speed ) : Vector( 0, 0, 0 );
				move.forward = -( vdir.x * fwd.x + vdir.y * fwd.y );
				move.side = -( vdir.x * right.x + vdir.y * right.y );
				move.jump = 0;
				zone_stop = TRUE;
				if( gpGlobals->time >= next_note )
				{
					next_note = gpGlobals->time + 20.0f;
					ALERT( at_console, "DoD bot %s: stops at a damage zone that would hurt its team\n", STRING( pBot->pev->netname ) );
				}
			}
		}
	}
	if( state->nav_has && !fight && !state->nav_arrived )
		move.turn_away = 0;	/* the steering picks the way round; a random turn only undoes it */
	if( state->ladder_dir && !fight )
	{
		move.forward = 1.0f;
		move.side = 0.0f;
		move.jump = 0;
		move.turn_away = 0;
	}
	if( state->want_jump && !fight && !zone_stop )
		buttons |= IN_JUMP;
	if( ( state->nav_arrived && !fight ) || ( state->behavior == DOD_ACT_RELOAD && !state->ladder_dir ) )
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
		state->hold_heading_until = gpGlobals->time + 0.7f;
	}
	if( fight )
	{
		const DodBotSkill sk = BotSkill();
		const float now = gpGlobals->time;
		const BOOL automatic = !in.semi_auto;
		if( now < state->react_until || enemy_distance > DOD_BOT_MAX_FIRE_RANGE )
			move.fire = 0;	/* still taking the target in, or too far to bother */
		else if( move.fire )
		{
			/* trigger discipline: short bursts, shorter at range, with a pause between */
			if( state->burst_until == 0.0f )
			{
				if( now < state->pause_until )
					move.fire = 0;
				else
					state->burst_until = now + Dod_BotBurstLength( automatic, enemy_distance );
			}
			else if( now >= state->burst_until )
			{
				state->burst_until = 0.0f;
				state->pause_until = now + Dod_BotBurstPause( automatic, enemy_distance, RANDOM_FLOAT( 0.0f, 1.0f ) );
				move.fire = 0;
			}
		}
		if( move.fire && automatic )
			state->pitch -= sk.recoil * dt;	/* the view climbs while the gun is held down; tracking pulls it back */
		if( move.fire )
		{
			/* bookkeeping for the log: is the view within a body's width of the target? */
			const Vector tc = enemy->pev->origin + enemy->pev->view_ofs * 0.5f;
			const float off_yaw = fabsf( Dod_BotAngleDelta( state->yaw, YawTo( eye, tc ) ) );
			const float off_pitch = fabsf( Dod_BotAngleDelta( state->pitch, PitchTo( eye, tc ) ) );
			const float body = atanf( 16.0f / ( enemy_distance > 16.0f ? enemy_distance : 16.0f ) ) * 180.0f / (float)M_PI;
			state->fire_frames++;
			state->last_fire = now;
			if( off_yaw < body && off_pitch < body * 2.0f )
				state->hit_frames++;
		}
		else if( state->fire_frames && now - state->last_fire >= 0.5f )
		{
			ALERT( at_console, "DoD bot %s: burst at %.0f units: %d of %d frames on target\n", STRING( pBot->pev->netname ), enemy_distance,
				state->hit_frames, state->fire_frames );
			state->fire_frames = state->hit_frames = 0;
		}
	}
	else
		state->burst_until = 0.0f;
	if( state->stage_role == 1 )
	{
		/* test scenario: the watcher stays where it was put, looking where it was put until something draws its aim */
		move.forward = move.side = 0.0f;
		move.jump = 0;
		buttons &= ~IN_JUMP;
		if( !fight )
			state->yaw = state->want_yaw = state->heading = state->stage_yaw;
			state->yaw_vel = 0.0f;
		if( fight )
		{
			state->stage_sightings++;
			if( move.fire )
				state->stage_fire_frames++;
		}
	}
	if( state->behavior == DOD_ACT_RELOAD )
	{
		move.fire = 0;
		move.reload = 1;
	}
	if( move.fire )
		buttons |= IN_ATTACK;
	if( move.reload )
		buttons |= IN_RELOAD;
	if( move.jump )
		buttons |= IN_JUMP;

	{
		/* The one place the view turns: an eased step towards where the bot wants to look. Human-like limits
		 * (skill: peak speed and acceleration; the head pitches slower than it yaws). */
		const DodBotSkill turn_sk = BotSkill();
		Dod_BotEase( &state->yaw, &state->yaw_vel, state->want_yaw, turn_sk.turn_rate, turn_sk.turn_accel, dt );
		Dod_BotEase( &state->pitch, &state->pitch_vel, state->want_pitch, turn_sk.turn_rate * 0.6f, turn_sk.turn_accel * 0.6f, dt );
	}
	viewangles[0] = state->pitch;
	viewangles[1] = state->yaw;
	viewangles[2] = 0.0f;
	/* The engine does not copy a fake client's command angles into pev->v_angle on every build
	 * (Allies bots kept v_angle 0,0 and every shot flew along +X). Weapons fire along v_angle, so
	 * set it from the command, with the same sign convention (positive pitch = looking down). */
	pBot->pev->v_angle.x = viewangles[0];
	pBot->pev->v_angle.y = viewangles[1];
	pBot->pev->v_angle.z = 0.0f;
	pBot->pev->angles.x = Dod_BotBodyPitch( viewangles[0] );
	{
		/* every 5 s per bot: where it wants to look, where it looks, what the body is given */
		static float next_aim_log[DOD_MAX_PLAYERS + 1];
		const int bi = pBot->entindex();
		if( bi >= 1 && bi <= DOD_MAX_PLAYERS && gpGlobals->time >= next_aim_log[bi] )
		{
			next_aim_log[bi] = gpGlobals->time + 2.0f;
			ALERT( at_console, "DoD bot aim %s: %s, want yaw %.0f pitch %.0f, v_angle %.0f %.0f, body pitch %.0f; move fwd %.2f side %.2f zone_stop %d vel %.0f\n", STRING( pBot->pev->netname ),
				fight ? "target" : "no target", state->want_yaw, state->want_pitch, pBot->pev->v_angle.x, pBot->pev->v_angle.y, pBot->pev->angles.x,
				move.forward, move.side, (int)zone_stop, pBot->pev->velocity.Length2D() );
		}
	}
	pBot->pev->angles.y = viewangles[1];
	if( state->have_prev_angles && dt > 0.0f )
	{
		const float yr = fabsf( Dod_BotAngleDelta( state->prev_yaw, viewangles[1] ) ) / dt;
		const float pr = fabsf( Dod_BotAngleDelta( state->prev_pitch, viewangles[0] ) ) / dt;
		g_turn_frames++;
		if( yr > g_turn_max_yaw_rate )
			g_turn_max_yaw_rate = yr;
		if( pr > g_turn_max_pitch_rate )
			g_turn_max_pitch_rate = pr;
		if( yr > 540.0f )
			g_turn_over_540++;
		if( yr > 1080.0f )
			g_turn_over_1080++;
	}
	state->prev_yaw = viewangles[1];
	state->prev_pitch = viewangles[0];
	state->have_prev_angles = 1;
	{
		PhaseTimer timer( DOD_PH_MOVE );
		( *g_engfuncs.pfnRunPlayerMove )( pBot->edict(), viewangles, move.forward * speed, move.side * speed, 0, buttons, 0, msec );
		pBot->pev->angles.x = Dod_BotBodyPitch( viewangles[0] );	/* after the move: the player move rewrites the angles */
	}
}


/* func_wall_toggle brushes that are solid right now (charlie: shingle1, shingle2, cp4_boom3 start solid and the map opens them) stop the
 * walk along the waypoint edges they really hit: the edges crossing a wall's box are traced with the player hull and the ones that hit a
 * toggle wall are blocked for A*. Checked once a second; a wall that opens gives its shortcut back, one that closes takes it away. */
#define DOD_BOT_MAX_TOGGLE_WALLS 16
static void RefreshToggleWalls( void )
{
	static float next_check;
	static int last_sig = -1;
	static int candidates[1536];
	float mins[DOD_BOT_MAX_TOGGLE_WALLS][3], maxs[DOD_BOT_MAX_TOGGLE_WALLS][3];
	int n = 0, sig = 0;
	CBaseEntity *wall = NULL;
	if( gpGlobals->time < next_check || g_nav.node_count < 1 )
		return;
	next_check = gpGlobals->time + 1.0f;
	while( n < DOD_BOT_MAX_TOGGLE_WALLS && ( wall = UTIL_FindEntityByClassname( wall, "func_wall_toggle" ) ) != NULL )
	{
		if( wall->pev->solid == SOLID_NOT || ( wall->pev->effects & EF_NODRAW ) )
			continue;
		mins[n][0] = wall->pev->absmin.x; mins[n][1] = wall->pev->absmin.y; mins[n][2] = wall->pev->absmin.z;
		maxs[n][0] = wall->pev->absmax.x; maxs[n][1] = wall->pev->absmax.y; maxs[n][2] = wall->pev->absmax.z;
		sig = sig * 31 + wall->entindex();
		n++;
	}
	if( sig == last_sig )
		return;
	{
		/* what do the bots stand on where they stall on charlie: player-hull and line traces straight down from z 300 */
		static int probed;
		if( !probed && !strcmp( STRING( gpGlobals->mapname ), "dod_charlie" ) )
		{
			static const float xy[3][2] = { { 500.0f, -100.0f }, { 700.0f, -120.0f }, { 840.0f, -80.0f } };
			probed = 1;
			for( int k = 0; k < 3; k++ )
			{
				TraceResult th, tl;
				UTIL_TraceHull( Vector( xy[k][0], xy[k][1], 300.0f ), Vector( xy[k][0], xy[k][1], -300.0f ), ignore_monsters, human_hull, NULL, &th );
				UTIL_TraceLine( Vector( xy[k][0], xy[k][1], 300.0f ), Vector( xy[k][0], xy[k][1], -300.0f ), ignore_monsters, NULL, &tl );
				ALERT( at_console, "DoD floor probe (%.0f %.0f): hull hits %s (ent %d, model %s) normal %.2f %.2f %.2f end z %.0f (origin height; floor %.0f); line hits %s normal z %.2f end z %.0f\n",
					xy[k][0], xy[k][1], th.pHit ? STRING( th.pHit->v.classname ) : "none", th.pHit ? ENTINDEX( th.pHit ) : -1, th.pHit ? STRING( th.pHit->v.model ) : "-",
					th.vecPlaneNormal.x, th.vecPlaneNormal.y, th.vecPlaneNormal.z, th.vecEndPos.z, th.vecEndPos.z - 36.0f,
					tl.pHit ? STRING( tl.pHit->v.classname ) : "none", tl.vecPlaneNormal.z, tl.vecEndPos.z );
			}
		}
	}
	last_sig = sig;
	Dod_NavClearEdgeBlocks( &g_nav );
	int crossing = 0;
	if( n > 0 )
	{
		crossing = Dod_NavEdgesCrossing( &g_nav, mins, maxs, n, candidates, (int)( sizeof( candidates ) / sizeof( candidates[0] ) ) );
		for( int i = 0; i < crossing; i++ )
		{
			int from, to;
			float ax, ay, az, bx, by, bz;
			TraceResult tr;
			Dod_NavEdgeNodes( &g_nav, candidates[i], &from, &to );
			Dod_NavNodePos( &g_nav, from, &ax, &ay, &az );
			Dod_NavNodePos( &g_nav, to, &bx, &by, &bz );
			UTIL_TraceHull( Vector( ax, ay, az ), Vector( bx, by, bz ), ignore_monsters, human_hull, NULL, &tr );
			if( tr.flFraction < 1.0f && tr.pHit && FClassnameIs( tr.pHit, "func_wall_toggle" ) )
				Dod_NavSetEdgeBlocked( &g_nav, candidates[i], 1 );
		}
	}
	ALERT( at_console, "DoD nav: %d solid toggle walls, %d edges cross them, %d blocked (of %d edges)\n", n, crossing, g_nav.edge_blocked_count, g_nav.edge_count );
}

void DodBots_Think( void )
{
	int i;
	if( !g_registered )
		return;
	/* the waypoint graph is read as early as the map runs, while the engine heap still has room: by the time the first bot joins
	 * (a few seconds later) the heap is fragmented and a 22 KB read failed in about 4 runs of 10 */
	EnsureNav();
	RefreshToggleWalls();
	if( gpGlobals->time >= g_next_plan )
	{
		g_next_plan = gpGlobals->time + 1.0f;
		PlanBots();
	}
	StageStep();
#if defined( __GAMECUBE__ )
	const unsigned long long t0 = gettime();
#endif
	for( i = 1; i <= gpGlobals->maxClients && i <= DOD_BOT_MAX_SLOTS; i++ )
	{
		CBasePlayer *pBot = PlayerAt( i );
		if( pBot && DodBots_IsBot( pBot ) )
			RunBot( i, pBot );
	}
#if defined( __GAMECUBE__ )
	{
		const unsigned long long us = DOD_BOT_TICKS_TO_US( gettime() - t0 );
		g_think_us_sum += us;
		if( us > g_think_us_max )
			g_think_us_max = us;
		g_think_frames++;
	}
#endif
}
