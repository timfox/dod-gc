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
#include "dod_runtime.h"
#include "dod_game.h"
#include "dod_film.h"
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
		ent->m_phase = 0.0f;
	}
	/* Walk cycles run at the speed the actor covers ground, so the feet do not slide much. */
	float rate = 1.0f;
	if( s.moving && ent->m_flGroundSpeed > 1.0f ) {
		rate = s.speed / ent->m_flGroundSpeed;
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
	/* LOAD_FILE_FOR_ME null-terminates, but the parser should not trust that */
	char *copy = (char *)malloc( (size_t)length + 1 );
	if( copy ) {
		memcpy( copy, text, (size_t)length );
		copy[length] = '\0';
	}
	FREE_FILE( text );
	if( !copy )
		return;
	int count = DodFilm_ParseCast( &g_film_cast, copy, err, sizeof( err ) );
	free( copy );
	if( count < 0 ) {
		ALERT( at_console, "FILM CAST %s: %s: %s\n", g_film_shot, path, err );
		memset( &g_film_cast, 0, sizeof( g_film_cast ) );
		return;
	}
	float preroll = CMD_ARGC() > 2 ? (float)atof( CMD_ARGV( 2 ) ) : 0.0f;
	g_film_start = gpGlobals->time + ( preroll > 0.0f ? preroll : 0.0f );
	g_film_last = gpGlobals->time;
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
		SET_MODEL( ent->edict(), model );
		const char *body = DodGC_BodyModelForTeam( (DodTeam)a->team );
		DodClass cls = a->player_class > DOD_CLASS_NONE ? (DodClass)a->player_class : DOD_CLASS_RIFLEMAN;
		ent->pev->body = Dod_PlayerBodyValue( body, cls, i + 1 );
		ent->m_actor = i;
		ent->m_pose = -1;
		ent->m_phase = 0.0f;
		g_film_ents[i] = ent;
		DodFilm_Place( i, gpGlobals->time - g_film_start, 0.0f );
		placed++;
	}
	ALERT( at_console, "FILM CAST %s: %d of %d actor(s), clock starts in %.2f s\n", g_film_shot, placed,
		g_film_cast.actor_count, g_film_start - gpGlobals->time );
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
		g_film_precached[team] = 1;
		ALERT( at_console, "FILM: precached %s for actors\n", model );
	}
	FREE_FILE( text );
}

void DodFilm_Think( void )
{
	if( !g_film_cast.actor_count )
		return;
	float dt = gpGlobals->time - g_film_last;
	g_film_last = gpGlobals->time;
	if( dt < 0.0f || dt > 0.5f )
		dt = 0.0f;
	for( int i = 0; i < g_film_cast.actor_count; i++ )
		DodFilm_Place( i, gpGlobals->time - g_film_start, dt );
}
