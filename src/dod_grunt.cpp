/* Grunts in the game DLL: one prop edict per grunt, driven from the gamerules Think by the plain arrays in
 * dod_grunts.c. Opt-in: dod_grunts N (default 0) puts N of them in the match, on the side opposite the humans.
 * They sit beside the bots and share nothing with them (no client slot, no weapons, no nav graph).
 *
 *   dod_grunts N        how many (0 off), capped by the pool (48) and the free edicts minus a reserve
 *   dod_grunt_team T    0 opposite the humans (default), 1 Allies, 2 Axis
 *   dod_grunt_rush R    1 run forward and at the enemy (default), 0 wander
 *   dod_grunt_skill S   0-3 shot cadence and accuracy (default 1)
 *   dod_grunt_log L     1 prints a cost line every 10 s while grunts exist (default 1)
 *
 * The body is the side's player model, which a match precaches for its players already. The engine's lean route
 * draws studio props through an allowlist; the film actors' tag (DODGC_FILM_ACTOR_TAG) admits the body and the
 * animation uses the same layering (rifle aim on the upper body, a gait on the legs). */
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "monsters.h"
#include "gamerules.h"
#include "dod_runtime.h"
#include "dod_bot.h"
#include "dod_game.h"
#include "dod_film.h"
#include "dod_weapons.h"
#include "dod_grunts.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#if defined( __GAMECUBE__ )
/* The PowerPC time base (40.5 MHz), read directly as dod_bot.cpp does: libogc's headers redefine BOOL. */
static inline unsigned long long GruntClock( void )
{
	unsigned int hi, lo, hi2;
	do {
		__asm__ volatile( "mftbu %0" : "=r"( hi ) );
		__asm__ volatile( "mftb %0" : "=r"( lo ) );
		__asm__ volatile( "mftbu %0" : "=r"( hi2 ) );
	} while( hi != hi2 );
	return ( ( (unsigned long long)hi << 32 ) | lo ) * 1000ULL / 40500ULL;	/* microseconds */
}
#else
static inline unsigned long long GruntClock( void ) { return 0; }
#endif

static cvar_t dod_grunts = { "dod_grunts", "0", FCVAR_SERVER };
static cvar_t dod_grunt_team = { "dod_grunt_team", "0", FCVAR_SERVER };
static cvar_t dod_grunt_rush = { "dod_grunt_rush", "1", FCVAR_SERVER };
static cvar_t dod_grunt_skill = { "dod_grunt_skill", "1", FCVAR_SERVER };
static cvar_t dod_grunt_log = { "dod_grunt_log", "1", FCVAR_SERVER };

class CDodGrunt : public CBaseAnimating
{
public:
	int m_slot;
	int m_pose;		/* 0 stand-aim, 1 walk, 2 run, 3 fallen; -1 not set yet */
	int m_gait;		/* legs' sequence under the upper-body aim, 0 none */
	float m_phase;
	virtual int ObjectCaps( void ) { return FCAP_DONT_SAVE; }
	virtual int BloodColor( void ) { return BLOOD_COLOR_RED; }
	virtual int TakeDamage( entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType );
};

static DodGruntPool g_pool;
static CDodGrunt *g_ents[DOD_GRUNT_MAX];
static int g_registered;
static float g_next_log;
static unsigned long long g_us_total, g_us_max, g_us_frames;
static DodGruntStats g_last_stats;
static float g_last_log_time;

static int GruntLive( int i )
{
	CDodGrunt *e = g_ents[i];
	return e && e->pev && !FNullEnt( e->edict() ) && !e->edict()->free && e->pev->iuser4 == DODGC_FILM_ACTOR_TAG;
}

static void GruntHide( int i )
{
	if( GruntLive( i ) ) {
		g_ents[i]->pev->effects |= EF_NODRAW;
		g_ents[i]->pev->solid = SOLID_NOT;
		g_ents[i]->pev->takedamage = DAMAGE_NO;
	}
	g_pool.state[i] = DOD_GRUNT_FREE;
}

void DodGrunts_Init( void )
{
	if( !g_registered ) {
		Dod_CvarRegisterKeep( &dod_grunts );
		Dod_CvarRegisterKeep( &dod_grunt_team );
		Dod_CvarRegisterKeep( &dod_grunt_rush );
		Dod_CvarRegisterKeep( &dod_grunt_skill );
		Dod_CvarRegisterKeep( &dod_grunt_log );
		g_registered = 1;
	}
	/* a new map: the old map's entities are gone with it */
	memset( g_ents, 0, sizeof( g_ents ) );
	DodGrunt_Init( &g_pool, (unsigned)( (int)( gpGlobals->time * 1000.0f ) ^ 0x5bd1e995 ) );
	memset( &g_last_stats, 0, sizeof( g_last_stats ) );
	g_next_log = 0.0f;
	g_us_total = g_us_max = g_us_frames = 0;
	g_last_log_time = 0.0f;
	if( CVAR_GET_FLOAT( "dod_grunts" ) > 0.0f ) {
		/* A match precaches its players' bodies; do it here too, so a grunts-only side (no player on it) has one. */
		for( int team = DOD_TEAM_ALLIES; team <= DOD_TEAM_AXIS; team++ ) {
			const char *body = DodGC_BodyModelForTeam( (DodTeam)team );
			char path[96];
			if( !body || !body[0] )
				continue;
			snprintf( path, sizeof( path ), "models/player/%s/%s.mdl", body, body );
			PRECACHE_MODEL( (char *)STRING( ALLOC_STRING( path ) ) );
		}
		const DodWeaponDef *d = Dod_ClassWeaponDef( DOD_TEAM_AXIS, DOD_CLASS_RIFLEMAN );
		if( d && d->fire_sound && d->fire_sound[0] )
			PRECACHE_SOUND( (char *)d->fire_sound );
		d = Dod_ClassWeaponDef( DOD_TEAM_ALLIES, DOD_CLASS_RIFLEMAN );
		if( d && d->fire_sound && d->fire_sound[0] )
			PRECACHE_SOUND( (char *)d->fire_sound );
	}
}

int CDodGrunt::TakeDamage( entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType )
{
	(void)pevInflictor;
	(void)bitsDamageType;
	if( m_slot < 0 || m_slot >= DOD_GRUNT_MAX || pev->takedamage == DAMAGE_NO )
		return 0;
	/* Friendly fire does not hurt a grunt: it is fodder for the other side. */
	CBaseEntity *who = pevAttacker ? CBaseEntity::Instance( pevAttacker ) : NULL;
	if( who && who->IsPlayer() && DodGC_PlayerTeam( (CBasePlayer *)who ) == (int)g_pool.team[m_slot] )
		return 0;
	if( DodGrunt_Hurt( &g_pool, m_slot, (int)ceilf( flDamage ), gpGlobals->time ) ) {
		pev->takedamage = DAMAGE_NO;
		pev->solid = SOLID_NOT;
		m_pose = -1;	/* Think sets the death sequence */
		if( who && who->IsPlayer() )
			( (CBasePlayer *)who )->AddPoints( 1, TRUE );
	}
	return 1;
}

/* The pose's sequences, set only when the pose changes. */
static void GruntPose( CDodGrunt *e, int pose, int axis )
{
	if( e->m_pose == pose )
		return;
	e->m_pose = pose;
	e->m_gait = 0;
	if( pose == 3 ) {
		/* A death: one of the candidates the body has, picked by the slot. */
		const char *const *names = DodFilm_PoseSequences( DOD_POSE_FALLEN );
		int have[16], n = 0;
		for( int k = 0; names[k] && n < 16; k++ ) {
			int s = e->LookupSequence( names[k] );
			if( s >= 0 )
				have[n++] = s;
		}
		e->pev->sequence = n ? have[( e->m_slot * 7 ) % n] : 0;
		e->ResetSequenceInfo();
		e->m_phase = 0.0f;
		e->pev->controller[2] = e->pev->controller[3] = 0;
		return;
	}
	const char *upper = axis ? "stand_bolt_aim" : "stand_rifle_aim";
	const char *legs = pose == 1 ? "dod_walk" : ( pose == 2 ? "dod_jog" : NULL );
	int u = e->LookupSequence( upper );
	if( u < 0 )
		u = 0;
	if( legs ) {
		int g = e->LookupSequence( legs );
		e->m_gait = g > 0 && g < 0x8000 ? g : 0;
	}
	if( e->m_gait ) {
		e->pev->sequence = e->m_gait;
		e->ResetSequenceInfo();	/* the legs set the pace */
	}
	e->pev->sequence = u;
	if( !e->m_gait )
		e->ResetSequenceInfo();
	/* The gait for the engine, as the film actors send it: controller[2] and the low bits of [3], flagged by bit 7. */
	e->pev->controller[2] = (byte)( e->m_gait & 0xff );
	e->pev->controller[3] = e->m_gait ? (byte)( 0x80 | ( ( e->m_gait >> 8 ) & 0x7f ) ) : 0;
	e->pev->blending[0] = 127;
	e->pev->blending[1] = 127;
}

static CBasePlayer *GruntPlayer( int index )
{
	CBaseEntity *e = UTIL_PlayerByIndex( index );
	if( !e || !e->IsPlayer() || !e->IsAlive() || ( e->pev->flags & FL_NOTARGET ) )
		return NULL;
	return (CBasePlayer *)e;
}

static int GruntSideForSpawn( void )
{
	const int forced = (int)CVAR_GET_FLOAT( "dod_grunt_team" );
	if( forced == DOD_TEAM_ALLIES || forced == DOD_TEAM_AXIS )
		return forced;
	int allies = 0, axis = 0;
	for( int i = 1; i <= gpGlobals->maxClients; i++ ) {
		CBaseEntity *e = UTIL_PlayerByIndex( i );
		if( !e || !e->IsPlayer() || ( e->pev->flags & FL_FAKECLIENT ) )
			continue;
		const int t = DodGC_PlayerTeam( (CBasePlayer *)e );
		if( t == DOD_TEAM_ALLIES ) allies++;
		else if( t == DOD_TEAM_AXIS ) axis++;
	}
	return allies > axis ? DOD_TEAM_AXIS : ( axis > allies ? DOD_TEAM_ALLIES : DOD_TEAM_AXIS );
}

static int GruntHumans( void )
{
	int n = 0;
	for( int i = 1; i <= gpGlobals->maxClients; i++ ) {
		CBaseEntity *e = UTIL_PlayerByIndex( i );
		if( e && e->IsPlayer() && !( e->pev->flags & FL_FAKECLIENT ) && !( (CBasePlayer *)e )->IsObserver() )
			n++;
	}
	return n;
}

/* Put grunt i into the world: make or reuse its edict, place it at a spawn point of its side. */
static bool GruntSpawn( int i, int team )
{
	Vector origin;
	float yaw;
	const char *body = DodGC_BodyModelForTeam( (DodTeam)team );
	char model[96];
	if( !body || !body[0] || !DodGC_TeamSpawnPoint( (DodTeam)team, &origin, &yaw ) )
		return false;
	snprintf( model, sizeof( model ), "models/player/%s/%s.mdl", body, body );
	CDodGrunt *e = GruntLive( i ) ? g_ents[i] : NULL;
	if( !e ) {
		/* A full edict table hands the game NULL, and GetClassPtr does not survive it. */
		if( NUMBER_OF_ENTITIES() + DOD_GRUNT_EDICT_RESERVE >= gpGlobals->maxEntities )
			return false;
		e = GetClassPtr( (CDodGrunt *)NULL );
		if( !e )
			return false;
		g_ents[i] = e;
	}
	/* a spread around the spawn so a wave does not stand in one spot */
	origin.x += RANDOM_FLOAT( -64.0f, 64.0f );
	origin.y += RANDOM_FLOAT( -64.0f, 64.0f );
	e->pev->classname = MAKE_STRING( "dod_grunt" );
	e->pev->effects &= ~EF_NODRAW;
	e->pev->movetype = MOVETYPE_NONE;
	e->pev->solid = SOLID_BBOX;
	e->pev->takedamage = DAMAGE_YES;
	e->pev->health = DOD_GRUNT_HEALTH;
	e->pev->iuser4 = DODGC_FILM_ACTOR_TAG;
	SET_MODEL( e->edict(), model );
	UTIL_SetSize( e->pev, Vector( -16, -16, -36 ), Vector( 16, 16, 36 ) );
	e->pev->body = Dod_PlayerBodyValue( body, DOD_CLASS_RIFLEMAN, i + 1 );
	const DodWeaponDef *gun = Dod_ClassWeaponDef( (DodTeam)team, DOD_CLASS_RIFLEMAN );
	e->pev->weaponmodel = gun && gun->player_model && gun->player_model[0] ? MAKE_STRING( gun->player_model ) : 0;
	e->m_slot = i;
	e->m_pose = -1;
	e->m_gait = 0;
	e->m_phase = 0.0f;
	DodGrunt_Spawn( &g_pool, i, team, origin.x, origin.y, origin.z + 36.0f, yaw, gpGlobals->time,
		(int)CVAR_GET_FLOAT( "dod_grunt_rush" ) != 0 );
	UTIL_SetOrigin( e->pev, Vector( origin.x, origin.y, origin.z + 36.0f ) );
	e->pev->angles = Vector( 0, yaw, 0 );
	return true;
}

/* A look: the nearest living player of the other side that is in front of the grunt and in plain sight. */
static void GruntLook( int i, int rush )
{
	CDodGrunt *e = g_ents[i];
	const Vector eye( g_pool.x[i], g_pool.y[i], g_pool.z[i] + 24.0f );
	int best = 0;
	float best_d2 = DOD_GRUNT_SIGHT * DOD_GRUNT_SIGHT;
	Vector best_pos;
	const float fwd = g_pool.yaw[i] * 0.017453293f;
	for( int k = 1; k <= gpGlobals->maxClients; k++ ) {
		CBasePlayer *pl = GruntPlayer( k );
		if( !pl )
			continue;
		const int t = DodGC_PlayerTeam( pl );
		if( ( t != DOD_TEAM_ALLIES && t != DOD_TEAM_AXIS ) || t == (int)g_pool.team[i] )
			continue;
		const Vector d = pl->pev->origin - eye;
		const float d2 = d.x * d.x + d.y * d.y + d.z * d.z;
		if( d2 >= best_d2 )
			continue;
		/* in front: within about 70 degrees, or close (they hear him) */
		const float dot = ( d.x * cosf( fwd ) + d.y * sinf( fwd ) );
		if( d2 > 300.0f * 300.0f && dot < 0.34f * sqrtf( d2 ) )
			continue;
		best = k;
		best_d2 = d2;
		best_pos = pl->pev->origin;
	}
	if( !best ) {
		DodGrunt_Looked( &g_pool, i, gpGlobals->time, 0, 0, 0, 0, 0, rush );
		return;
	}
	TraceResult tr;
	CBasePlayer *pl = GruntPlayer( best );
	UTIL_TraceLine( eye, pl->pev->origin + pl->pev->view_ofs * 0.5f, ignore_monsters, e->edict(), &tr );
	const int visible = tr.flFraction >= 1.0f || tr.pHit == pl->edict();
	DodGrunt_Looked( &g_pool, i, gpGlobals->time, visible, best, best_pos.x, best_pos.y, sqrtf( best_d2 ), rush );
}

static void GruntShoot( int i )
{
	CDodGrunt *e = g_ents[i];
	CBasePlayer *pl = GruntPlayer( g_pool.target[i] );
	if( !pl ) {
		g_pool.target[i] = 0;
		return;
	}
	const int skill = (int)CVAR_GET_FLOAT( "dod_grunt_skill" );
	const Vector from( g_pool.x[i], g_pool.y[i], g_pool.z[i] + 24.0f );
	const Vector aim = pl->pev->origin + pl->pev->view_ofs * 0.5f;
	const Vector dir = ( aim - from ).Normalize();
	const float dist = ( aim - from ).Length();
	const DodWeaponDef *gun = Dod_ClassWeaponDef( (DodTeam)g_pool.team[i], DOD_CLASS_RIFLEMAN );
	if( gun && gun->fire_sound && gun->fire_sound[0] )
		EMIT_SOUND_DYN( e->edict(), CHAN_WEAPON, gun->fire_sound, 1.0f, ATTN_NORM, 0, 100 );
	if( !DodGrunt_ShotHits( dist, skill, DodGrunt_Rand( &g_pool ) ) )
		return;
	TraceResult tr;
	UTIL_TraceLine( from, aim + dir * 8.0f, dont_ignore_monsters, e->edict(), &tr );
	if( tr.pHit != pl->edict() )
		return;	/* something stepped in between */
	g_pool.stats.hits++;
	ClearMultiDamage();
	pl->TraceAttack( e->pev, ( gun ? gun->damage : 30.0f ) * 0.6f, dir, &tr, DMG_BULLET | DMG_NEVERGIB );
	ApplyMultiDamage( e->pev, e->pev );
}

/* The world check for the next stretch of travel: one hull trace along the heading, and the floor under the new spot. */
static void GruntCheck( int i )
{
	CDodGrunt *e = g_ents[i];
	const float r = g_pool.heading[i] * 0.017453293f;
	const float ahead = DodGrunt_ProbeDistance( &g_pool, i );
	const Vector from( g_pool.x[i], g_pool.y[i], g_pool.z[i] );
	const Vector to = from + Vector( cosf( r ) * ahead, sinf( r ) * ahead, 0 );
	TraceResult tr;
	UTIL_TraceHull( from, to, dont_ignore_monsters, head_hull, e->edict(), &tr );
	const int clear = !tr.fStartSolid && tr.flFraction >= 1.0f;
	DodGrunt_MoveChecked( &g_pool, i, clear );
	/* The floor under where it stands now: it follows slopes in steps, up to 160 units a second. */
	UTIL_TraceLine( Vector( from.x, from.y, from.z ), Vector( from.x, from.y, from.z - 96.0f ), ignore_monsters, e->edict(), &tr );
	g_pool.stats.floor_traces++;
	if( tr.flFraction < 1.0f && !tr.fStartSolid ) {
		const float want = tr.vecEndPos.z + 36.0f;
		const float dz = want - g_pool.z[i];
		g_pool.z[i] += dz > 12.0f ? 12.0f : ( dz < -12.0f ? -12.0f : dz );
	} else if( tr.flFraction >= 1.0f ) {
		g_pool.z[i] -= 6.0f;	/* a drop: let it fall to the next floor */
	}
}

static void GruntLogCost( int live )
{
	if( (int)CVAR_GET_FLOAT( "dod_grunt_log" ) == 0 || gpGlobals->time < g_next_log )
		return;
	const float dt = gpGlobals->time - g_last_log_time;
	g_next_log = gpGlobals->time + 10.0f;
	if( g_last_log_time > 0.0f && dt > 0.5f && g_us_frames > 0 ) {
		const DodGruntStats &s = g_pool.stats;
		const DodGruntStats &o = g_last_stats;
		ALERT( at_console, "GRUNT cost: %d live of %d, frame avg %llu us max %llu us (console time), per s: looks %.1f move %.1f floor %.1f shots %.1f hits %.1f deferred %.1f, edicts %d of %d, row %d B\n",
			live, g_pool.cap, g_us_total / g_us_frames, g_us_max, ( s.looks - o.looks ) / dt,
			( s.move_traces - o.move_traces ) / dt, ( s.floor_traces - o.floor_traces ) / dt, ( s.shots - o.shots ) / dt,
			( s.hits - o.hits ) / dt, ( s.deferred - o.deferred ) / dt, NUMBER_OF_ENTITIES(), gpGlobals->maxEntities,
			DodGrunt_RowBytes() );
	}
	g_last_stats = g_pool.stats;
	g_last_log_time = gpGlobals->time;
	g_us_total = g_us_max = g_us_frames = 0;
}

void DodGrunts_Think( void )
{
	const int requested = (int)CVAR_GET_FLOAT( "dod_grunts" );
	if( requested <= 0 && g_pool.cap == 0 )
		return;	/* off, and nothing ever spawned: costs this one test per frame */
	const unsigned long long t0 = GruntClock();
	const float now = gpGlobals->time;
	const float dt = gpGlobals->frametime;
	int live = 0, in_use = 0;
	for( int i = 0; i < DOD_GRUNT_MAX; i++ )
		if( g_pool.state[i] != DOD_GRUNT_FREE )
			in_use++;
	/* Edicts the grunts hold are not free for the target count: add them back. */
	const int free_edicts = gpGlobals->maxEntities - NUMBER_OF_ENTITIES() + in_use;
	int want = Dod_GruntTargetCount( requested, free_edicts );
	if( !GruntHumans() )
		want = 0;	/* like the bots: nobody to fight, nobody here */
	/* grow one grunt a frame; shrink by hiding the highest slots */
	int spawned_this_frame = 0;
	for( int i = 0; i < DOD_GRUNT_MAX; i++ ) {
		if( i >= want ) {
			if( g_pool.state[i] != DOD_GRUNT_FREE )
				GruntHide( i );
			continue;
		}
		if( g_pool.state[i] == DOD_GRUNT_FREE && !spawned_this_frame ) {
			if( GruntSpawn( i, GruntSideForSpawn() ) )
				spawned_this_frame = 1;
		} else if( DodGrunt_RespawnDue( &g_pool, i, now ) && !spawned_this_frame ) {
			g_pool.stats.respawns++;
			if( GruntSpawn( i, GruntSideForSpawn() ) )
				spawned_this_frame = 1;
		}
	}
	const int rush = (int)CVAR_GET_FLOAT( "dod_grunt_rush" ) != 0;
	int due[DOD_GRUNT_SENSE_BUDGET];
	const int n = DodGrunt_DueLooks( &g_pool, now, DOD_GRUNT_SENSE_BUDGET, due );
	for( int k = 0; k < n; k++ )
		if( GruntLive( due[k] ) )
			GruntLook( due[k], rush );
	for( int i = 0; i < DOD_GRUNT_MAX; i++ ) {
		if( !GruntLive( i ) || g_pool.state[i] == DOD_GRUNT_FREE )
			continue;
		CDodGrunt *e = g_ents[i];
		const int axis = g_pool.team[i] == DOD_TEAM_AXIS;
		if( g_pool.state[i] == DOD_GRUNT_DEAD ) {
			GruntPose( e, 3, axis );
			e->m_phase += dt * e->m_flFrameRate;
			if( e->m_phase > 255.0f )
				e->m_phase = 255.0f;
			e->pev->frame = e->m_phase;
			e->pev->framerate = 0.0f;
			e->pev->animtime = now;
			continue;
		}
		live++;
		DodGrunt_Move( &g_pool, i, now, dt );
		if( DodGrunt_CheckDue( &g_pool, i, now ) )
			GruntCheck( i );
		if( DodGrunt_ShotDue( &g_pool, i, now, (int)CVAR_GET_FLOAT( "dod_grunt_skill" ) ) )
			GruntShoot( i );
		const int moving = ( g_pool.state[i] == DOD_GRUNT_WANDER || g_pool.state[i] == DOD_GRUNT_ADVANCE ) && !g_pool.blocked[i];
		const int pose = !moving ? 0 : ( g_pool.state[i] == DOD_GRUNT_ADVANCE ? 2 : 1 );
		GruntPose( e, pose, axis );
		UTIL_SetOrigin( e->pev, Vector( g_pool.x[i], g_pool.y[i], g_pool.z[i] ) );
		e->pev->angles = Vector( 0, g_pool.yaw[i], 0 );
		/* the pose's own cycle: the pool keeps the stride phase; a standing grunt breathes on the stand cycle */
		e->pev->frame = g_pool.phase[i];
		e->pev->framerate = 0.0f;
		e->pev->animtime = now;
	}
	const unsigned long long us = GruntClock() - t0;
	g_us_total += us;
	if( us > g_us_max )
		g_us_max = us;
	g_us_frames++;
	GruntLogCost( live );
}
