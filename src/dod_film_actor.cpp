/* Film actors in the game DLL: props wearing the team player models that walk the
 * keyed paths of a shot's cast file (see dod_film.h).
 *
 *   film_cast <shot> [preroll]   load cine/<shot>.act, put its actors in place, start their
 *                                clock after preroll seconds (the camera's cine_play preroll)
 *   film_cast_clear              remove every actor
 *
 * The player models have to be precached while the map loads, before any command can
 * run, so the gamerules constructor reads cine/cast.txt (one team name per line, written
 * by a film plan) and precaches those teams' bodies. A disc built without
 * --film has no cine/ folder and precaches nothing extra. Actors are driven from the
 * gamerules Think every server frame, with no entity think of their own, so their clock
 * steps with the engine's locked cinematic frame time exactly as the camera's does. */
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "gamerules.h"
#include "weapons.h"
#include "dod_runtime.h"
#include "dod_game.h"
#include "dod_film.h"
#include "dod_weapons.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

class CDodFilmActor : public CBaseAnimating
{
public:
	int m_actor;		/* index into g_film_cast.actors */
	int m_pose;		/* the pose whose sequence is set, -1 for none yet */
	float m_phase;		/* sequence frame, 0..256 */
};

static DodFilmCast g_film_cast;
static CDodFilmActor *g_film_ents[DOD_FILM_MAX_ACTORS];
static int g_film_seq[DOD_FILM_MAX_ACTORS][DOD_POSE_COUNT];	/* looked-up sequence + 1, 0 unknown */
static float g_film_start;
static float g_film_last;
static float g_film_fx_clock;	/* the shot time fx have been fired up to */
static char g_film_shot[32];
static int g_film_commands_added;
static int g_film_precached[4];	/* by DodTeam */

static void DodFilm_Clear( void )
{
	for( int i = 0; i < DOD_FILM_MAX_ACTORS; i++ ) {
		if( g_film_ents[i] )
			UTIL_Remove( g_film_ents[i] );
		g_film_ents[i] = NULL;
	}
	memset( &g_film_cast, 0, sizeof( g_film_cast ) );
	memset( g_film_seq, 0, sizeof( g_film_seq ) );
	g_film_shot[0] = '\0';
}

static void DodFilm_BodyPath( int team, char *out, size_t size )
{
	const char *body = DodGC_BodyModelForTeam( (DodTeam)team );
	out[0] = '\0';
	if( body && body[0] )
		snprintf( out, size, "models/player/%s/%s.mdl", body, body );
}

/* The first of the pose's candidate sequences the model has. */
static int DodFilm_Sequence( CDodFilmActor *ent, int pose )
{
	int *slot = &g_film_seq[ent->m_actor][pose];
	if( !*slot ) {
		const char *const *names = DodFilm_PoseSequences( (DodFilmPose)pose );
		int seq = -1;
		const char *found = "-";
		for( int i = 0; names[i] && seq < 0; i++ ) {
			seq = ent->LookupSequence( names[i] );
			if( seq >= 0 )
				found = names[i];
		}
		if( seq < 0 )
			seq = 0;
		*slot = seq + 1;
		ALERT( at_console, "FILM ACTOR %s pose %s: sequence %d (%s)\n", g_film_cast.actors[ent->m_actor].name,
			DodFilm_PoseName( (DodFilmPose)pose ), seq, found );
	}
	return *slot - 1;
}

/* Feet on the floor under (x, y): trace down from a little above the keyed point. The key's
 * z is a standing player's origin (from the waypoints), the fallback when nothing is hit. */
static float DodFilm_OriginZ( const float *origin, int pose )
{
	TraceResult tr;
	Vector start( origin[0], origin[1], origin[2] + 24.0f );
	Vector end( origin[0], origin[1], origin[2] - 160.0f );
	UTIL_TraceLine( start, end, ignore_monsters, NULL, &tr );
	if( !tr.fAllSolid && !tr.fStartSolid && tr.flFraction < 1.0f )
		return tr.vecEndPos.z + DodFilm_PoseHeight( (DodFilmPose)pose );
	return origin[2] - 36.0f + DodFilm_PoseHeight( (DodFilmPose)pose );
}

/* Per-actor variety from the actor's slot: a start phase in the cycle (0..256) and a
 * stride factor (0.92..1.08). Fixed per slot, so a re-render of a shot is identical. */
static unsigned DodFilm_ActorHash( int i )
{
	unsigned h = (unsigned)( i + 1 ) * 2654435761u;
	h ^= h >> 15;
	h *= 2246822519u;
	h ^= h >> 13;
	return h;
}

static float DodFilm_ActorPhase( int i )
{
	return (float)( DodFilm_ActorHash( i ) & 255u );
}

static float DodFilm_ActorStride( int i )
{
	return 0.92f + 0.16f * (float)( ( DodFilm_ActorHash( i ) >> 8 ) & 255u ) / 255.0f;
}

/* The third-person weapon an actor carries (p_*.mdl), or NULL: the side's rifle for every
 * class. Each gun model costs the GameCube a file read and a resident mesh, and a cast of 24
 * asking for every class's gun ran the file pool dry; one per side fits. */
static const char *DodFilm_WeaponModel( int team, int player_class )
{
	(void)player_class;
	const DodWeaponDef *def = Dod_ClassWeaponDef( (DodTeam)team, DOD_CLASS_RIFLEMAN );
	return def && def->player_model && def->player_model[0] ? def->player_model : NULL;
}

static void DodFilm_Place( int i, float t, float dt )
{
	CDodFilmActor *ent = g_film_ents[i];
	DodFilmSample s;
	if( !ent )
		return;
	DodFilm_Sample( &g_film_cast.actors[i], t, &s );
	Vector origin( s.origin[0], s.origin[1], DodFilm_OriginZ( s.origin, s.pose ) );
	UTIL_SetOrigin( ent->pev, origin );
	ent->pev->angles = Vector( 0.0f, s.yaw, 0.0f );
	if( ent->m_pose != (int)s.pose ) {
		ent->pev->sequence = DodFilm_Sequence( ent, s.pose );
		ent->ResetSequenceInfo();
		ent->m_pose = s.pose;
		/* A looping pose starts at the actor's own point in the cycle, so a squad that
		 * starts walking together does not step in lockstep. One-shot poses start at 0. */
		ent->m_phase = DodFilm_PoseLoops( s.pose ) ? DodFilm_ActorPhase( i ) : 0.0f;
	}
	/* Walk cycles run at the speed the actor covers ground, so the feet do not slide much,
	 * give or take the actor's own stride (a few percent), so the cycles drift apart. */
	float rate = DodFilm_ActorStride( i );
	if( s.moving && ent->m_flGroundSpeed > 1.0f ) {
		rate *= s.speed / ent->m_flGroundSpeed;
		rate = rate < 0.5f ? 0.5f : ( rate > 2.0f ? 2.0f : rate );
	}
	if( t >= 0.0f )
		ent->m_phase += dt * ent->m_flFrameRate * rate;
	if( DodFilm_PoseLoops( s.pose ) )
		ent->m_phase = fmodf( ent->m_phase, 256.0f );
	else if( ent->m_phase > 255.0f )
		ent->m_phase = 255.0f;
	ent->pev->frame = ent->m_phase;
	ent->pev->framerate = 0.0f;	/* the frame is set here every server frame */
	ent->pev->animtime = gpGlobals->time;
}

static int DodFilm_Byte( float v )
{
	return v < 1.0f ? 1 : ( v > 255.0f ? 255 : (int)v );
}

static void DodFilm_Streaks( const Vector &o, float up, int color, int count, int speed )
{
	MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY );
		WRITE_BYTE( TE_STREAK_SPLASH );
		WRITE_COORD( o.x );
		WRITE_COORD( o.y );
		WRITE_COORD( o.z );
		WRITE_COORD( 0.0f );
		WRITE_COORD( 0.0f );
		WRITE_COORD( up );
		WRITE_BYTE( color );
		WRITE_SHORT( count );
		WRITE_SHORT( speed );
		WRITE_SHORT( speed / 2 );
	MESSAGE_END();
}

static void DodFilm_Smoke( const Vector &o, float scale, int framerate )
{
	MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY );
		WRITE_BYTE( TE_SMOKE );
		WRITE_COORD( o.x );
		WRITE_COORD( o.y );
		WRITE_COORD( o.z );
		WRITE_SHORT( g_sModelIndexSmoke );
		WRITE_BYTE( DodFilm_Byte( scale ) );
		WRITE_BYTE( framerate );
	MESSAGE_END();
}

/* One effect. Broadcast, not by PVS or PAS: the camera is often far from the player.
 * The sound is left to the film's own mix (TE_EXPLFLAG_NOSOUND). */
static void DodFilm_FireFx( const DodFilmFx *f )
{
	Vector o( f->origin[0], f->origin[1], f->origin[2] );
	float s = f->scale;
	/* A burst, a spout or a puff sits on the ground under its point: the plan's heights
	 * come from the nearest waypoint, which on a slope or a bluff can be well off. Water
	 * keeps its height (a trace goes through water to the bottom). */
	if( f->kind == DOD_FX_KIND_EXPLOSION || f->kind == DOD_FX_KIND_DIRT || f->kind == DOD_FX_KIND_SMOKE ) {
		TraceResult tr;
		UTIL_TraceLine( o + Vector( 0, 0, 512 ), o - Vector( 0, 0, 512 ), ignore_monsters, NULL, &tr );
		if( !tr.fStartSolid && !tr.fAllSolid && tr.flFraction < 1.0f )
			o.z = tr.vecEndPos.z;
	}
	switch( f->kind ) {
	case DOD_FX_KIND_EXPLOSION:
		MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY );
			WRITE_BYTE( TE_EXPLOSION );
			WRITE_COORD( o.x );
			WRITE_COORD( o.y );
			WRITE_COORD( o.z + 24.0f * s );
			WRITE_SHORT( g_sModelIndexFireball );
			WRITE_BYTE( DodFilm_Byte( 30.0f * s ) );	/* scale * 10, a grenade's */
			WRITE_BYTE( 15 );
			WRITE_BYTE( TE_EXPLFLAG_NOSOUND );
		MESSAGE_END();
		DodFilm_Streaks( o, 1.0f, 5, (int)( 24 * s ), (int)( 220 * s ) );
		DodFilm_Smoke( o + Vector( 0, 0, 40.0f * s ), 40.0f * s, 8 );
		break;
	case DOD_FX_KIND_WATER:
		/* a tall white column: two showers of white streaks and a spray puff */
		DodFilm_Streaks( o, 1.0f, 0, (int)( 40 * s ), (int)( 420 * s ) );
		DodFilm_Streaks( o, 1.0f, 0, (int)( 24 * s ), (int)( 220 * s ) );
		MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY );
			WRITE_BYTE( TE_SPRITE );
			WRITE_COORD( o.x );
			WRITE_COORD( o.y );
			WRITE_COORD( o.z + 48.0f * s );
			WRITE_SHORT( g_sModelIndexSmoke );
			WRITE_BYTE( DodFilm_Byte( 30.0f * s ) );
			WRITE_BYTE( 200 );
		MESSAGE_END();
		break;
	case DOD_FX_KIND_DIRT:
		DodFilm_Streaks( o, 1.0f, 4, (int)( 20 * s ), (int)( 260 * s ) );
		DodFilm_Smoke( o + Vector( 0, 0, 16.0f * s ), 18.0f * s, 12 );
		break;
	case DOD_FX_KIND_SMOKE:
		DodFilm_Smoke( o, 30.0f * s, 6 );
		break;
	case DOD_FX_KIND_TRACER:
		MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY );
			WRITE_BYTE( TE_TRACER );
			WRITE_COORD( o.x );
			WRITE_COORD( o.y );
			WRITE_COORD( o.z );
			WRITE_COORD( f->end[0] );
			WRITE_COORD( f->end[1] );
			WRITE_COORD( f->end[2] );
		MESSAGE_END();
		break;
	default:
		break;
	}
}

static void DodFilm_RunFx( float t )
{
	static int logs;
	int first, end = DodFilm_FxDue( &g_film_cast, g_film_fx_clock, t, &first );
	for( int i = first; i < end; i++ ) {
		DodFilm_FireFx( &g_film_cast.fx[i] );
		if( logs < 24 ) {
			logs++;
			ALERT( at_console, "FILM FX %s: %d of %d, %s at %.2f s (%.0f %.0f %.0f), fireball %d smoke %d\n", g_film_shot,
				i + 1, g_film_cast.fx_count, DodFilm_FxName( (DodFilmFxKind)g_film_cast.fx[i].kind ), t,
				g_film_cast.fx[i].origin[0], g_film_cast.fx[i].origin[1], g_film_cast.fx[i].origin[2],
				g_sModelIndexFireball, g_sModelIndexSmoke );
		}
	}
	if( t > g_film_fx_clock )
		g_film_fx_clock = t;
}

static void DodFilm_CastCommand( void )
{
	char path[96], err[96];
	int length = 0;
	if( CMD_ARGC() < 2 ) {
		ALERT( at_console, "usage: film_cast <shot> [preroll]\n" );
		return;
	}
	DodFilm_Clear();
	snprintf( g_film_shot, sizeof( g_film_shot ), "%s", CMD_ARGV( 1 ) );
	snprintf( path, sizeof( path ), "cine/%s.act", g_film_shot );
	byte *text = LOAD_FILE_FOR_ME( path, &length );
	if( !text ) {
		ALERT( at_console, "FILM CAST %s: no %s, no actors\n", g_film_shot, path );
		return;
	}
	/* LOAD_FILE_FOR_ME null-terminates, but the parser should not trust that. A static
	 * buffer, not the heap: a failed allocation used to drop the whole cast silently. */
	static char copy[32768];
	if( length <= 0 || length >= (int)sizeof( copy ) ) {
		ALERT( at_console, "FILM CAST %s: %s is %d bytes (the limit is %d), no actors\n", g_film_shot, path,
			length, (int)sizeof( copy ) - 1 );
		FREE_FILE( text );
		return;
	}
	memcpy( copy, text, (size_t)length );
	copy[length] = '\0';
	FREE_FILE( text );
	ALERT( at_console, "FILM CAST %s: read %s, %d bytes\n", g_film_shot, path, length );
	int count = DodFilm_ParseCast( &g_film_cast, copy, err, sizeof( err ) );
	if( count < 0 ) {
		ALERT( at_console, "FILM CAST %s: %s: %s\n", g_film_shot, path, err );
		memset( &g_film_cast, 0, sizeof( g_film_cast ) );
		return;
	}
	float preroll = CMD_ARGC() > 2 ? (float)atof( CMD_ARGV( 2 ) ) : 0.0f;
	g_film_start = gpGlobals->time + ( preroll > 0.0f ? preroll : 0.0f );
	g_film_last = gpGlobals->time;
	g_film_fx_clock = gpGlobals->time - g_film_start;	/* minus the preroll: an fx at 0 fires on the first frame */
	if( g_film_fx_clock >= 0.0f )
		g_film_fx_clock = -0.001f;
	int placed = 0;
	for( int i = 0; i < g_film_cast.actor_count; i++ ) {
		const DodFilmActor *a = &g_film_cast.actors[i];
		char model[96];
		DodFilm_BodyPath( a->team, model, sizeof( model ) );
		if( !model[0] || !g_film_precached[a->team] ) {
			ALERT( at_console, "FILM ACTOR %s: the %s body was not precached (cine/cast.txt)\n", a->name,
				a->team == DOD_TEAM_AXIS ? "axis" : "allies" );
			continue;
		}
		CDodFilmActor *ent = GetClassPtr( (CDodFilmActor *)NULL );
		if( !ent ) {
			ALERT( at_console, "FILM ACTOR %s: out of edicts\n", a->name );
			break;
		}
		ent->pev->classname = MAKE_STRING( "film_actor" );
		ent->pev->movetype = MOVETYPE_NONE;
		ent->pev->solid = SOLID_NOT;
		ent->pev->takedamage = DAMAGE_NO;
		ent->pev->iuser4 = DODGC_FILM_ACTOR_TAG;
		SET_MODEL( ent->edict(), model );
		const char *body = DodGC_BodyModelForTeam( (DodTeam)a->team );
		DodClass cls = a->player_class > DOD_CLASS_NONE ? (DodClass)a->player_class : DOD_CLASS_RIFLEMAN;
		ent->pev->body = Dod_PlayerBodyValue( body, cls, i + 1 );
		/* The engine draws weaponmodel on the actor's hand bones, as it does for a player. */
		const char *gun = DodFilm_WeaponModel( a->team, cls );
		if( gun )
			ent->pev->weaponmodel = MAKE_STRING( gun );
		ent->m_actor = i;
		ent->m_pose = -1;
		ent->m_phase = 0.0f;
		g_film_ents[i] = ent;
		DodFilm_Place( i, gpGlobals->time - g_film_start, 0.0f );
		placed++;
	}
	ALERT( at_console, "FILM CAST %s: %d of %d actor(s), %d fx, clock starts in %.2f s\n", g_film_shot, placed,
		g_film_cast.actor_count, g_film_cast.fx_count, g_film_start - gpGlobals->time );
}

static void DodFilm_ClearCommand( void )
{
	DodFilm_Clear();
	ALERT( at_console, "FILM CAST cleared\n" );
}

void DodFilm_Init( void )
{
	int length = 0;
	if( !g_film_commands_added ) {
		g_engfuncs.pfnAddServerCommand( "film_cast", DodFilm_CastCommand );
		g_engfuncs.pfnAddServerCommand( "film_cast_clear", DodFilm_ClearCommand );
		g_film_commands_added = 1;
	}
	/* a new map: the old map's entities are gone with it */
	memset( g_film_ents, 0, sizeof( g_film_ents ) );
	memset( &g_film_cast, 0, sizeof( g_film_cast ) );
	memset( g_film_precached, 0, sizeof( g_film_precached ) );
	byte *text = LOAD_FILE_FOR_ME( "cine/cast.txt", &length );
	if( !text )
		return;
	for( int team = DOD_TEAM_ALLIES; team <= DOD_TEAM_AXIS; team++ ) {
		const char *name = team == DOD_TEAM_AXIS ? "axis" : "allies";
		const char *hit = strstr( (const char *)text, name );
		char model[96];
		if( !hit || hit - (const char *)text >= length )
			continue;
		DodFilm_BodyPath( team, model, sizeof( model ) );
		if( !model[0] )
			continue;
		PRECACHE_MODEL( (char *)STRING( ALLOC_STRING( model ) ) );
		const char *gun = DodFilm_WeaponModel( team, DOD_CLASS_RIFLEMAN );
		if( gun )
			PRECACHE_MODEL( (char *)gun );
		g_film_precached[team] = 1;
		ALERT( at_console, "FILM: precached %s for actors\n", model );
	}
	FREE_FILE( text );
}

int DodFilm_IsFilmMap( void )
{
	return g_film_precached[DOD_TEAM_ALLIES] || g_film_precached[DOD_TEAM_AXIS];
}

void DodFilm_Think( void )
{
	if( !g_film_cast.actor_count && !g_film_cast.fx_count )
		return;
	float dt = gpGlobals->time - g_film_last;
	g_film_last = gpGlobals->time;
	if( dt < 0.0f || dt > 0.5f )
		dt = 0.0f;
	for( int i = 0; i < g_film_cast.actor_count; i++ )
		DodFilm_Place( i, gpGlobals->time - g_film_start, dt );
	DodFilm_RunFx( gpGlobals->time - g_film_start );
}
