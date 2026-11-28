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
#include "studio.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

class CDodFilmActor : public CBaseAnimating
{
public:
	int m_actor;		/* index into g_film_cast.actors */
	int m_pose;		/* the pose whose sequence is set, -1 for none yet */
	float m_phase;		/* sequence frame, 0..256 (the gait's, when there is one) */
	int m_gait;		/* the legs' sequence under an upper-body aim sequence, 0 for none */
	int m_seq;		/* the cast's named sequence being played (DodFilmKey.seq), 0 for the pose's */
	int m_hands;		/* what his hands hold (DodFilmHands), -1 before the first key */
};

static DodFilmCast g_film_cast;
static CDodFilmActor *g_film_ents[DOD_FILM_MAX_ACTORS];
/* Actor entities kept from shot to shot. Removing a cast and spawning the next one cost
 * twice the edicts: a freed edict is not handed out again for half a second, and on
 * dod_charlie the map leaves a cast only ~30 of the GameCube's edicts. */
static CDodFilmActor *g_film_pool[DOD_FILM_MAX_ACTORS];
#define DOD_FILM_EDICT_RESERVE 8	/* left free for the camera player, gibs and temp entities */
static int g_film_seq[DOD_FILM_MAX_ACTORS][DOD_POSE_COUNT];	/* looked-up sequence + 1, 0 unknown */
/* The cast's named sequences in each team's model: sequence + 1, -1 not in the model, 0 unknown. */
static int g_film_named[4][DOD_FILM_MAX_SEQS];
static float g_film_start;
static float g_film_last;
static float g_film_fx_clock;	/* the shot time fx have been fired up to */
static char g_film_shot[32];
static int g_film_commands_added;
static int g_film_precached[4];	/* by DodTeam */
/* The class guns cine/cast.txt asked for ("gun <team> <class>" lines), by DodTeam and DodClass.
 * Each costs a file read and a resident mesh, so a side carries at most DOD_FILM_GUNS_PER_SIDE
 * models (its rifle counts as one); a class whose gun was not precached carries the rifle. */
#define DOD_FILM_GUNS_PER_SIDE 4
static int g_film_gun[4][DOD_CLASS_COUNT];
static int g_film_sidearm[4];	/* the side's pistol is precached: cast.txt "sidearm <team>" */
/* The vehicles cine/cast.txt asked for ("vehicle <model>" lines): a model can only be set on an
 * entity once it was precached with the map, so a cast's vehicle not on this list is left out. */
#define DOD_FILM_MAX_VEHICLE_MODELS 8	/* tanks, and set pieces: a box on the ground, a chair */
static char g_film_vehicle[DOD_FILM_MAX_VEHICLE_MODELS][DOD_FILM_NAME];
/* The held props cine/cast.txt asked for ("prop <name>" lines: models/film/p_<name>.mdl). A key that
 * holds one that was not precached plays empty handed, never with the gun through the clip. */
#define DOD_FILM_MAX_PROP_MODELS 16
static char g_film_prop[DOD_FILM_MAX_PROP_MODELS][DOD_FILM_NAME];
static string_t g_film_prop_path[DOD_FILM_MAX_PROP_MODELS];	/* its model path, allocated once */

/* The precached model path of a held prop, 0 when it was not precached. */
static string_t DodFilm_PropModel( const char *name )
{
	for( int i = 0; name && i < DOD_FILM_MAX_PROP_MODELS; i++ )
		if( g_film_prop[i][0] && !strcmp( g_film_prop[i], name ) )
			return g_film_prop_path[i];
	return 0;
}

static int DodFilm_VehiclePrecached( const char *name )
{
	for( int i = 0; i < DOD_FILM_MAX_VEHICLE_MODELS; i++ )
		if( g_film_vehicle[i][0] && !strcmp( g_film_vehicle[i], name ) )
			return 1;
	return 0;
}
/* The shot's fireball. zerogxplode.spr (g_sModelIndexFireball) is 193 KB on disc, more than
 * the GameCube heap has free once a map is up, so it never loaded and no burst drew;
 * explode1.spr is a fraction of that. 0 falls back to g_sModelIndexFireball. */
static int g_film_fireball;
/* Gibs: a man who falls next to a shell burst comes apart, in his own side's gibs (DoD's
 * allied_gibs/axis_gibs, about 54 KB each). A side's model is loaded when its body is and the file is on
 * the disc (a film disc stages both); 0 means no gibs for that side, only the heavier blood. Each piece
 * is a client tempent, and a capture has few to spare, so a burst throws only a handful. */
static int g_film_gibs[3];
#define DOD_FILM_BURSTS 8
#define DOD_FILM_GIB_RANGE 240.0f	/* a fall this close to a burst ... */
#define DOD_FILM_GIB_WINDOW 0.8f	/* ... this soon after it */
static struct { Vector at; float t; } g_film_burst[DOD_FILM_BURSTS];
static int g_film_burst_next;
static void DodFilm_Gibs( const Vector &o, const Vector &dir, int team );

static int DodFilm_PoolLive( int i )
{
	CDodFilmActor *e = g_film_pool[i];
	return e && e->pev && !FNullEnt( e->edict() ) && !e->edict()->free
		&& e->pev->iuser4 == DODGC_FILM_ACTOR_TAG;
}

/* Out of the shot: hidden and kept for the next cast. */
static void DodFilm_Clear( void )
{
	for( int i = 0; i < DOD_FILM_MAX_ACTORS; i++ ) {
		if( DodFilm_PoolLive( i ) )
			g_film_pool[i]->pev->effects |= EF_NODRAW;
		g_film_ents[i] = NULL;
	}
	memset( &g_film_cast, 0, sizeof( g_film_cast ) );
	memset( g_film_seq, 0, sizeof( g_film_seq ) );
	memset( g_film_named, 0, sizeof( g_film_named ) );
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
static unsigned DodFilm_ActorHash( int i );
static void DodFilm_Blood( const Vector &o, const Vector &dir, float scale );

static int DodFilm_Sequence( CDodFilmActor *ent, int pose )
{
	int *slot = &g_film_seq[ent->m_actor][pose];
	if( !*slot ) {
		const char *const *names = DodFilm_PoseSequences( (DodFilmPose)pose );
		int seq = -1;
		const char *found = "-";
		if( pose == DOD_POSE_FALLEN ) {
			/* A death: one of every candidate the model has, picked by the actor's slot. */
			int have[16], n = 0;
			const char *have_name[16];
			for( int i = 0; names[i] && n < 16; i++ ) {
				int s = ent->LookupSequence( names[i] );
				if( s >= 0 ) {
					have[n] = s;
					have_name[n++] = names[i];
				}
			}
			if( n > 0 ) {
				int pick = (int)( ( DodFilm_ActorHash( ent->m_actor ) >> 16 ) % (unsigned)n );
				seq = have[pick];
				found = have_name[pick];
			}
		}
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

/* The model's sequence for the cast's named sequence n (1-based), or -1 when the model has none
 * by that name: the actor then plays its pose. Looked up once per team body and logged. */
static int DodFilm_Named( CDodFilmActor *ent, int n )
{
	const int team = g_film_cast.actors[ent->m_actor].team & 3;
	if( n <= 0 || n > g_film_cast.seq_count )
		return -1;
	int *slot = &g_film_named[team][n - 1];
	if( !*slot ) {
		const char *name = g_film_cast.seq_names[n - 1];
		const int seq = ent->LookupSequence( name );
		*slot = seq >= 0 ? seq + 1 : -1;
		ALERT( at_console, "FILM ACTOR %s: named sequence %s is %d%s\n", g_film_cast.actors[ent->m_actor].name,
			name, seq, seq < 0 ? " (not in this model: the pose plays instead)" : "" );
	}
	return *slot > 0 ? *slot - 1 : -1;
}

/* A brush entity the renderer never draws: no-draw, or a render mode other than normal with amount 0. */
static bool DodFilm_UnseenBrush( edict_t *hit )
{
	if( !hit || ENTINDEX( hit ) <= 0 )
		return false;
	const entvars_t *v = &hit->v;
	if( v->effects & EF_NODRAW )
		return true;
	return v->rendermode != kRenderNormal && v->renderamt < 1.0f;
}

/* Feet on the floor under (x, y): trace down from a little above the keyed point. The key's
 * z is a standing player's origin (from the waypoints), the fallback when nothing is hit.
 * Between keys an actor moves in a straight line, so over a crest the line runs a little
 * under the ground: there the trace starts higher, up to 64 units (a man is not lifted onto
 * anything taller, such as a beach obstacle). Over a dip the line runs above the ground, so
 * the floor is looked for up to 320 units down, not left hanging in the air. */
static float DodFilm_OriginZ( const float *origin, int pose )
{
	static const float ups[] = { 24.0f, 64.0f };
	for( unsigned i = 0; i < sizeof( ups ) / sizeof( ups[0] ); i++ ) {
		TraceResult tr;
		Vector start( origin[0], origin[1], origin[2] + ups[i] );
		Vector end( origin[0], origin[1], origin[2] - 320.0f );
		UTIL_TraceLine( start, end, ignore_monsters, NULL, &tr );
		/* Mappers fence obstacles and wire with brush entities that are never drawn (dod_charlie's
		 * beach obstacles and the wire on the slope each sit in one). A line trace hits them like any
		 * wall, and an actor crossing one stood on its top, in the air. Look through them for the
		 * floor a viewer sees. */
		for( int pass = 0; pass < 4 && DodFilm_UnseenBrush( tr.pHit ) && ( tr.flFraction < 1.0f || tr.fStartSolid ); pass++ ) {
			edict_t *unseen = tr.pHit;
			if( !tr.fStartSolid )
				start = tr.vecEndPos;
			UTIL_TraceLine( start, end, ignore_monsters, unseen, &tr );
		}
		if( tr.fStartSolid || tr.fAllSolid )
			continue;
		if( tr.flFraction < 1.0f )
			return tr.vecEndPos.z + DodFilm_PoseHeight( (DodFilmPose)pose );
		break;
	}
	return origin[2] - 36.0f + DodFilm_PoseHeight( (DodFilmPose)pose );
}

/* How far his origin sits above his soles in the sequence his legs play: the lowest point of that
 * sequence's box, as the model compiler measured it over every frame. The player models are not all
 * built round one origin height (a crouch or prone set can sit lower or higher than the stand's 36), and
 * a fixed height per pose sank whole boots into the ground. A box out of reason keeps the pose's height. */
static float DodFilm_FootHeight( CDodFilmActor *ent, int seq, int pose )
{
	float mins[3], maxs[3];
	if( seq >= 0 && ent->ExtractBbox( seq, mins, maxs ) ) {
		const float h = -mins[2];
		if( h >= 4.0f && h <= 48.0f )
			return h;
	}
	return DodFilm_PoseHeight( (DodFilmPose)pose );
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

/* The third-person weapon an actor carries (p_*.mdl), or NULL: his class's own gun when the map
 * precached it for the film (a BAR, a Thompson, an MG42), else the side's rifle. Each gun model
 * costs the GameCube a file read and a resident mesh; a cast of 24 asking for every class's gun
 * ran the file pool dry, so cast.txt names only the classes the plan uses, capped per side. */
static const char *DodFilm_ClassGun( int team, int player_class )
{
	const DodWeaponDef *def = Dod_ClassWeaponDef( (DodTeam)team, (DodClass)player_class );
	return def && def->player_model && def->player_model[0] ? def->player_model : NULL;
}

/* The side's pistol (the Colt, the Luger), for a key that plays a pistol clip. */
static const char *DodFilm_Sidearm( int team )
{
	const DodWeaponDef *def = Dod_GetWeaponDef( team == DOD_TEAM_AXIS ? DOD_WEAPON_LUGER : DOD_WEAPON_COLT );
	return def && def->player_model && def->player_model[0] ? def->player_model : NULL;
}

static const char *DodFilm_WeaponModel( int team, int player_class )
{
	if( team >= 0 && team < 4 && player_class > DOD_CLASS_NONE && player_class < DOD_CLASS_COUNT
		&& g_film_gun[team][player_class] ) {
		const char *gun = DodFilm_ClassGun( team, player_class );
		if( gun )
			return gun;
	}
	return DodFilm_ClassGun( team, DOD_CLASS_RIFLEMAN );
}

/* The upper-body aim grid that fits the gun in his hands: DoD names them stand_<kind>_aim and
 * crouch_<kind>_aim. NULL is the side's rifle grid. */
static const char *DodFilm_AimKind( int team, int player_class )
{
	const char *gun = DodFilm_WeaponModel( team, player_class );
	if( !gun ) return NULL;
	if( strstr( gun, "p_tommy" ) ) return "tommy";
	if( strstr( gun, "p_mp40" ) ) return "mp40";
	if( strstr( gun, "p_stg44" ) ) return "stg44";
	if( strstr( gun, "p_mg42" ) ) return "mg";
	if( strstr( gun, "p_30cal" ) ) return "30cal";
	if( strstr( gun, "p_bazooka" ) ) return "bazooka";
	if( strstr( gun, "p_pschreck" ) ) return "pschreck";
	if( strstr( gun, "p_spring" ) || strstr( gun, "p_k98" ) ) return "bolt";
	return NULL;	/* Garand, BAR, carbine: the rifle grid */
}

/* A rifleman as DoD draws a player: the legs walk a gait sequence under the rifle's upper-body
 * aim grid (both hands on the gun, head and chest following the grid's pitch). The gait
 * sequences' own upper body is a placeholder slump: played whole, every actor looked at his
 * boots and his left hand hung off the stock. Returns the upper sequence and sets *gait (0 for
 * none); -1 keeps the pose's own single sequence (lying down, prone, falling). */
static int DodFilm_Layered( CDodFilmActor *ent, int pose, int *gait )
{
	const DodFilmActor *who = &g_film_cast.actors[ent->m_actor];
	const int axis = who->team == DOD_TEAM_AXIS;
	const char *upper = NULL, *legs = NULL;
	*gait = 0;
	switch( pose ) {
	case DOD_POSE_STAND:       upper = axis ? "stand_bolt_aim" : "stand_rifle_aim";   legs = "dod_idle1"; break;
	case DOD_POSE_WALK:        upper = axis ? "stand_bolt_aim" : "stand_rifle_aim";   legs = "dod_walk"; break;
	case DOD_POSE_RUN:         upper = axis ? "stand_bolt_aim" : "stand_rifle_aim";   legs = "dod_jog"; break;
	case DOD_POSE_AIM:         upper = axis ? "stand_bolt_aim" : "stand_rifle_aim";   break;
	case DOD_POSE_CROUCH:      upper = axis ? "crouch_bolt_aim" : "crouch_rifle_aim"; legs = "dod_crouch_idle"; break;
	case DOD_POSE_CROUCH_WALK: upper = axis ? "crouch_bolt_aim" : "crouch_rifle_aim"; legs = "dod_crawl"; break;
	case DOD_POSE_CROUCH_AIM:  upper = axis ? "crouch_bolt_aim" : "crouch_rifle_aim"; break;
	default: return -1;
	}
	/* his own gun's grid when the model has it (both hands on an MG42 or a Thompson, not a rifle hold) */
	int u = -1;
	const char *kind = DodFilm_AimKind( who->team, who->player_class > DOD_CLASS_NONE ? who->player_class
		: DOD_CLASS_RIFLEMAN );
	if( kind ) {
		char name[32];
		snprintf( name, sizeof( name ), "%s_%s_aim", strncmp( upper, "crouch", 6 ) ? "stand" : "crouch", kind );
		u = ent->LookupSequence( name );
	}
	if( u < 0 )
		u = ent->LookupSequence( upper );
	if( u < 0 )
		return -1;
	if( legs ) {
		const int g = ent->LookupSequence( legs );
		*gait = g > 0 && g < 0x8000 ? g : 0;
	}
	return u;
}

/* A vehicle: on the floor under its keys, facing their yaw, its first sequence looping. */
static void DodFilm_PlaceVehicle( CDodFilmActor *ent, const DodFilmSample *s, float t, float dt )
{
	Vector origin( s->origin[0], s->origin[1],
		DodFilm_OriginZ( s->origin, DOD_POSE_STAND ) - DodFilm_PoseHeight( DOD_POSE_STAND ) );
	UTIL_SetOrigin( ent->pev, origin );
	ent->pev->angles = Vector( 0.0f, s->yaw, 0.0f );
	/* The turret: bone controller 0 of a film vehicle model (tools/vehicle/obj_to_mdl.py: -180..180 degrees
	 * about the turret ring). It holds its gun on the cast's aim point as the hull turns and drives; with no
	 * aim it points ahead. A model without a controller ignores it. */
	{
		const DodFilmActor *a = &g_film_cast.actors[ent->m_actor];
		float turret = 0.0f;
		if( a->has_aim ) {
			turret = atan2f( a->aim[1] - origin.y, a->aim[0] - origin.x ) * 57.29578f - s->yaw;
			turret = fmodf( turret + 540.0f, 360.0f ) - 180.0f;
		}
		/* The byte itself, not SetBoneController: for a -180..180 controller the SDK's wrap (start + 359 <
		 * end, so it folds into 0..360) turned every angle to the right of the hull into 180..360 and
		 * clamped it to 255, the gun held straight back (a turret aimed 74-106 degrees right). */
		ent->pev->controller[0] = (byte)( ( turret + 180.0f ) * ( 255.0f / 360.0f ) + 0.5f );
	}
	if( t >= 0.0f )
		ent->m_phase = fmodf( ent->m_phase + dt * ent->m_flFrameRate, 256.0f );
	ent->pev->frame = ent->m_phase;
	ent->pev->framerate = 0.0f;
	ent->pev->animtime = gpGlobals->time;
	/* Where the vehicle really is, once a second: a test shot's Panzer was off its path for most of the
	 * shot and drew tilted at the end, and nothing in the log said where the game had put it. */
	{
		/* by the shot's clock: in capture the game's clock runs several times faster than the film's */
		static int last_sec[DOD_FILM_MAX_ACTORS];
		const int i = ent->m_actor;
		const int sec = t < 0.0f ? -1 : (int)t;
		if( i >= 0 && i < DOD_FILM_MAX_ACTORS && ( sec < 0 || sec != last_sec[i] ) ) {
			last_sec[i] = sec;
			/* and what the floor trace under it hits: a tank sat ~20 units under cobbles that the map's
			 * BSP puts at the keyed height */
			TraceResult tr;
			UTIL_TraceLine( Vector( s->origin[0], s->origin[1], s->origin[2] + 24.0f ),
				Vector( s->origin[0], s->origin[1], s->origin[2] - 320.0f ), ignore_monsters, NULL, &tr );
			ALERT( at_console, "FILM VEHICLE %s t=%.2f org=(%.0f %.0f %.0f) ang=(%.1f %.1f %.1f) turret=%d abs=(%.0f %.0f %.0f)-(%.0f %.0f %.0f)"
				" key z %.1f floor %.1f (fraction %.3f, start solid %d, all solid %d, hit %d %s)\n",
				g_film_cast.actors[i].name, t, origin.x, origin.y, origin.z, ent->pev->angles.x, ent->pev->angles.y,
				ent->pev->angles.z, ent->pev->controller[0], ent->pev->absmin.x, ent->pev->absmin.y, ent->pev->absmin.z,
				ent->pev->absmax.x, ent->pev->absmax.y, ent->pev->absmax.z, s->origin[2], tr.vecEndPos.z, tr.flFraction,
				tr.fStartSolid, tr.fAllSolid, tr.pHit ? ENTINDEX( tr.pHit ) : -1,
				tr.pHit ? STRING( tr.pHit->v.classname ) : "-" );
		}
	}
}

/* A vehicle's box. The engine gives a studio model no size (SV_SetModel), so a film vehicle's absmin and
 * absmax were a 2-unit box at its tracks; the GameCube client culls a lean model by that box against the
 * world's visible leaves, and the ground point under a tank sat in leaves the camera did not see. The
 * model's own bounds when the game has its header; a Panzer-sized box (obj_to_mdl.py: about 230 units
 * long) when the lean load left the game without it. */
static void DodFilm_VehicleSize( CDodFilmActor *ent )
{
	const studiohdr_t *hdr = (const studiohdr_t *)GET_MODEL_PTR( ent->edict() );
	Vector mins( -144.0f, -144.0f, 0.0f ), maxs( 144.0f, 144.0f, 128.0f );
	if( hdr && hdr->bbmax[0] - hdr->bbmin[0] > 16.0f && hdr->bbmax[2] - hdr->bbmin[2] > 16.0f ) {
		/* the hull turns with its yaw: a square box as wide as the model is long */
		float r = 0.0f;
		for( int k = 0; k < 2; k++ ) {
			r = fmaxf( r, fabsf( hdr->bbmin[k] ) );
			r = fmaxf( r, fabsf( hdr->bbmax[k] ) );
		}
		mins = Vector( -r, -r, hdr->bbmin[2] );
		maxs = Vector( r, r, hdr->bbmax[2] );
	}
	UTIL_SetSize( ent->pev, mins, maxs );
}

static void DodFilm_Place( int i, float t, float dt )
{
	CDodFilmActor *ent = g_film_ents[i];
	DodFilmSample s;
	if( !ent )
		return;
	DodFilm_Sample( &g_film_cast.actors[i], t, &s );
	if( g_film_cast.actors[i].model[0] ) {
		DodFilm_PlaceVehicle( ent, &s, t, dt );
		/* a set piece out of its show window (a box not yet put down, one picked up) is not drawn */
		if( DodFilm_Shown( &g_film_cast.actors[i], t ) )
			ent->pev->effects &= ~EF_NODRAW;
		else
			ent->pev->effects |= EF_NODRAW;
		return;
	}
	s.pose = DodFilm_GaitPose( s.pose, s.moving, s.speed );
	const int named = s.seq ? DodFilm_Named( ent, s.seq ) : -1;
	if( named < 0 )
		s.seq = 0;
	Vector origin( s.origin[0], s.origin[1], DodFilm_OriginZ( s.origin, s.pose ) );
	UTIL_SetOrigin( ent->pev, origin );
	/* Where he looks. The player models' sequences blend on the view pitch, and a blend left at 0 is
	 * the bottom of that range: every actor stared at his boots. Level,
	 * drifting a few degrees on his own slow cycle; a man standing or crouching at rest also turns his
	 * body a little to look round. Aiming and moving men keep their line. */
	const float own = DodFilm_ActorPhase( i ) * 0.0245f;	/* 0..2pi from his 0..256 phase */
	float pitch = 4.0f * sinf( t * 0.37f + own );
	float yaw = s.yaw;
	if( !s.moving && ( s.pose == DOD_POSE_STAND || s.pose == DOD_POSE_CROUCH ) )
		yaw += 14.0f * sinf( t * 0.21f + own * 1.7f );
	ent->pev->angles = Vector( 0.0f, yaw, 0.0f );
	if( ent->m_pose != (int)s.pose || ent->m_seq != s.seq ) {
		/* Hit: an actor that goes down during the shot bleeds as it falls. One that
		 * starts the shot lying there does not, nor one already down that moves on to
		 * another sequence where he lies. */
		if( s.pose == DOD_POSE_FALLEN && ent->m_pose >= 0 && ent->m_pose != DOD_POSE_FALLEN && t > 0.0f ) {
			const float yaw = s.yaw * 0.017453293f;
			const Vector back( -cosf( yaw ), -sinf( yaw ), 0.35f );
			/* a lot of it: the chest wound, a spray from higher up, and a pool where he lands */
			DodFilm_Blood( origin + Vector( 0, 0, 20.0f ), back, 1.8f );
			DodFilm_Blood( origin + Vector( 0, 0, 34.0f ), back + Vector( 0, 0, 0.4f ), 1.2f );
			DodFilm_Blood( origin - Vector( 0, 0, 12.0f ), Vector( 0, 0, 0.2f ), 0.8f );
			for( int b = 0; b < DOD_FILM_BURSTS; b++ ) {
				if( g_film_burst[b].t > 0.0f && t - g_film_burst[b].t >= 0.0f && t - g_film_burst[b].t <= DOD_FILM_GIB_WINDOW
					&& ( g_film_burst[b].at - origin ).Length() <= DOD_FILM_GIB_RANGE ) {
					DodFilm_Gibs( origin, ( origin - g_film_burst[b].at ).Normalize(),
						g_film_cast.actors[ent->m_actor].team & 3 );
					break;
				}
			}
		}
		int gait = 0;
		const int upper = s.seq ? -1 : DodFilm_Layered( ent, s.pose, &gait );
		if( s.seq ) {
			/* A named sequence: the whole body, or the upper body over the pose's standing
			 * legs. Its own cycle rate and ground speed set the pace. */
			ent->pev->sequence = named;
			ent->ResetSequenceInfo();
			if( s.play == DOD_FILM_PLAY_UPPER || s.play == DOD_FILM_PLAY_UPPER_LOOP )
				DodFilm_Layered( ent, s.pose, &gait );
		} else if( upper >= 0 && gait > 0 ) {
			/* The legs set the pace: their cycle rate and ground speed. */
			ent->pev->sequence = gait;
			ent->ResetSequenceInfo();
			ent->pev->sequence = upper;
		} else {
			ent->pev->sequence = upper >= 0 ? upper : DodFilm_Sequence( ent, s.pose );
			ent->ResetSequenceInfo();
		}
		ent->m_gait = ( s.seq || upper >= 0 ) ? gait : 0;
		ent->m_pose = s.pose;
		ent->m_seq = s.seq;
		/* A looping pose starts at the actor's own point in the cycle, so a squad that
		 * starts walking together does not step in lockstep. One-shot poses start at 0. */
		ent->m_phase = DodFilm_SampleLoops( &s ) ? DodFilm_ActorPhase( i ) : 0.0f;
	}
	/* What his hands hold, set whenever it changes: with a new sequence, or on a key that goes on playing
	 * the same clip with something else in them (a box taken off the ground halfway through a pick-up). */
	{
		/* A bare key's sequence is played empty handed, a pistol key's with the side's pistol (when
		 * the cast file had it precached); any other puts his own gun back. */
		const DodFilmActor *who = &g_film_cast.actors[i];
		const int hands = s.seq ? s.bare : DOD_FILM_HANDS_GUN;
		if( hands != ent->m_hands ) {
			ent->m_hands = hands;
			const char *prop = DodFilm_HandsProp( &g_film_cast, hands );
			if( prop ) {
				ent->pev->weaponmodel = DodFilm_PropModel( prop );
				if( !ent->pev->weaponmodel )
					ALERT( at_console, "FILM ACTOR %s: prop %s was not precached (cine/cast.txt), empty handed\n",
						who->name, prop );
			}
			const char *gun = prop || hands == DOD_FILM_HANDS_BARE ? NULL
				: hands == DOD_FILM_HANDS_PISTOL && who->team >= 0 && who->team < 4 && g_film_sidearm[who->team]
				? DodFilm_Sidearm( who->team )
				: DodFilm_WeaponModel( who->team, who->player_class > DOD_CLASS_NONE ? who->player_class : DOD_CLASS_RIFLEMAN );
			if( !prop )
				ent->pev->weaponmodel = gun ? MAKE_STRING( gun ) : 0;
		}
	}
	/* His soles on the floor under him: the pose's height above it, corrected to his legs' sequence. */
	{
		const int legs = ent->m_gait ? ent->m_gait : ent->pev->sequence;
		const float lift = DodFilm_FootHeight( ent, legs, s.pose ) - DodFilm_PoseHeight( (DodFilmPose)s.pose );
		if( lift != 0.0f ) {
			origin.z += lift;
			UTIL_SetOrigin( ent->pev, origin );
		}
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
	if( DodFilm_SampleLoops( &s ) )
		ent->m_phase = fmodf( ent->m_phase, 256.0f );
	else if( ent->m_phase > 255.0f )
		ent->m_phase = 255.0f;
	/* The aim grid: torso yaw along the row (blending[0]), pitch down the column (blending[1]),
	 * 127 the middle of each; +45 degrees up is 0, -45 down is 255. Level, drifting a few degrees. */
	ent->pev->blending[0] = 127;
	ent->pev->blending[1] = (byte)( 127.0f - pitch / 90.0f * 255.0f );
	/* The legs' gait for the engine (r_studio.c, film actors): controller[2] and the low bits of
	 * controller[3], flagged by bit 7. The player models have no bone controllers. */
	ent->pev->controller[2] = (byte)( ent->m_gait & 0xff );
	ent->pev->controller[3] = ent->m_gait ? (byte)( 0x80 | ( ( ent->m_gait >> 8 ) & 0x7f ) ) : 0;
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

/* A smoke puff. TE_SMOKE draws its sprite in a near-black grey at full alpha (the
 * engine's R_Sprite_Smoke), so a big one is an opaque black blob: daylight beach
 * shots, with puffs at scale 5 to 11 every second or so, hid up to a third of the
 * frame. The dark core stays small, and a lit, see-through puff (TE_SPRITE draws
 * additive) gives the column its size. */
#define DOD_FILM_SMOKE_CORE 0.35f
static void DodFilm_Smoke( const Vector &o, float scale, int framerate )
{
	MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY );
		WRITE_BYTE( TE_SPRITE );
		WRITE_COORD( o.x );
		WRITE_COORD( o.y );
		WRITE_COORD( o.z + 0.8f * scale );
		WRITE_SHORT( g_sModelIndexSmoke );
		WRITE_BYTE( DodFilm_Byte( 0.9f * scale ) );
		WRITE_BYTE( 70 );
	MESSAGE_END();
	scale *= DOD_FILM_SMOKE_CORE;
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

/* Blood: a spurt along dir and a spray around it, both particles (no sprite to load,
 * which MEM1 has no room for). 70 is the red of the HL palette's blood ramp. */
/* Gibs flung from a man caught by a burst (TE_BREAKMODEL, flesh), with a burst of blood round them. */
static void DodFilm_Gibs( const Vector &o, const Vector &dir, int team )
{
	DodFilm_Blood( o + Vector( 0, 0, 24.0f ), Vector( 0, 0, 1.0f ), 2.2f );
	DodFilm_Blood( o + Vector( 0, 0, 24.0f ), dir + Vector( 0, 0, 0.6f ), 2.0f );
	const int model = team >= 0 && team < 3 ? g_film_gibs[team] : 0;
	if( !model )
		return;
	MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY );
		WRITE_BYTE( TE_BREAKMODEL );
		WRITE_COORD( o.x );
		WRITE_COORD( o.y );
		WRITE_COORD( o.z + 24.0f );
		WRITE_COORD( 24.0f );	/* size */
		WRITE_COORD( 24.0f );
		WRITE_COORD( 36.0f );
		WRITE_COORD( dir.x * 220.0f );	/* velocity: away from the burst, and up */
		WRITE_COORD( dir.y * 220.0f );
		WRITE_COORD( 240.0f );
		WRITE_BYTE( 20 );	/* random velocity, in tens */
		WRITE_SHORT( model );
		WRITE_BYTE( 4 );	/* count: each piece is a tempent */
		WRITE_BYTE( 35 );	/* life, tenths of a second */
		WRITE_BYTE( BREAK_FLESH );
	MESSAGE_END();
}

static void DodFilm_Blood( const Vector &o, const Vector &dir, float scale )
{
	const int color = 70;
	MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY );
		WRITE_BYTE( TE_BLOODSTREAM );
		WRITE_COORD( o.x );
		WRITE_COORD( o.y );
		WRITE_COORD( o.z );
		WRITE_COORD( dir.x );
		WRITE_COORD( dir.y );
		WRITE_COORD( dir.z );
		WRITE_BYTE( color );
		WRITE_BYTE( DodFilm_Byte( 90.0f * scale ) );
	MESSAGE_END();
	MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY );
		WRITE_BYTE( TE_BLOOD );
		WRITE_COORD( o.x );
		WRITE_COORD( o.y );
		WRITE_COORD( o.z );
		WRITE_COORD( dir.x );
		WRITE_COORD( dir.y );
		WRITE_COORD( dir.z );
		WRITE_BYTE( color );
		WRITE_BYTE( DodFilm_Byte( 60.0f * scale ) );
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
			WRITE_SHORT( g_film_fireball ? g_film_fireball : g_sModelIndexFireball );
			WRITE_BYTE( DodFilm_Byte( 30.0f * s ) );	/* scale * 10, a grenade's */
			WRITE_BYTE( 15 );
			WRITE_BYTE( TE_EXPLFLAG_NOSOUND );
		MESSAGE_END();
		DodFilm_Streaks( o, 1.0f, 5, (int)( 12 * s ), (int)( 220 * s ) );
		DodFilm_Smoke( o + Vector( 0, 0, 40.0f * s ), 40.0f * s, 8 );
		break;
	case DOD_FX_KIND_WATER:
		/* a tall white column: two showers of white streaks and a spray puff */
		DodFilm_Streaks( o, 1.0f, 0, (int)( 20 * s ), (int)( 420 * s ) );
		DodFilm_Streaks( o, 1.0f, 0, (int)( 12 * s ), (int)( 220 * s ) );
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
		DodFilm_Streaks( o, 1.0f, 4, (int)( 10 * s ), (int)( 260 * s ) );
		DodFilm_Smoke( o + Vector( 0, 0, 16.0f * s ), 18.0f * s, 12 );
		break;
	case DOD_FX_KIND_SMOKE:
		/* the dark puff alone vanished against a night street (ep02: not one of 50 smoke
		 * cues read), and a big one is a black blob by day; DodFilm_Smoke draws both */
		DodFilm_Smoke( o, 30.0f * s, 6 );
		break;
	case DOD_FX_KIND_BLOOD:
		DodFilm_Blood( o, Vector( 0, 0, 1.0f ), 1.6f * s );
		break;
	case DOD_FX_KIND_GIBS: {
		/* a scripted body blown apart: that side's gibs (the other side's if only those loaded) */
		int team = f->team == DOD_TEAM_AXIS ? DOD_TEAM_AXIS : DOD_TEAM_ALLIES;
		if( !g_film_gibs[team] )
			team = DOD_TEAM_AXIS + DOD_TEAM_ALLIES - team;
		DodFilm_Gibs( o, Vector( 0, 0, 0 ), team );
		DodFilm_Blood( o + Vector( 0, 0, 16.0f ), Vector( 0.7f, 0.7f, 0.3f ), 1.5f * s );
		DodFilm_Blood( o + Vector( 0, 0, 16.0f ), Vector( -0.7f, -0.7f, 0.3f ), 1.5f * s );
		break;
	}
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
		if( g_film_cast.fx[i].kind == DOD_FX_KIND_EXPLOSION ) {
			g_film_burst[g_film_burst_next].at = Vector( g_film_cast.fx[i].origin[0], g_film_cast.fx[i].origin[1],
				g_film_cast.fx[i].origin[2] );
			g_film_burst[g_film_burst_next].t = t > 0.0f ? t : 0.001f;
			g_film_burst_next = ( g_film_burst_next + 1 ) % DOD_FILM_BURSTS;
		}
		if( logs < 24 ) {
			logs++;
			ALERT( at_console, "FILM FX %s: %d of %d, %s at %.2f s (%.0f %.0f %.0f), fireball %d smoke %d\n", g_film_shot,
				i + 1, g_film_cast.fx_count, DodFilm_FxName( (DodFilmFxKind)g_film_cast.fx[i].kind ), t,
				g_film_cast.fx[i].origin[0], g_film_cast.fx[i].origin[1], g_film_cast.fx[i].origin[2],
				g_film_fireball ? g_film_fireball : g_sModelIndexFireball, g_sModelIndexSmoke );
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
	/* Parsed in place: LOAD_FILE_FOR_ME allocates length + 1 and null-terminates. No copy
	 * buffer: a static 32 KB one cost the GameCube enough MEM1 that later config reads failed. */
	if( length <= 0 || text[length] != '\0' ) {
		ALERT( at_console, "FILM CAST %s: %s is %d bytes and not terminated, no actors\n", g_film_shot, path, length );
		FREE_FILE( text );
		return;
	}
	ALERT( at_console, "FILM CAST %s: read %s, %d bytes\n", g_film_shot, path, length );
	int count = DodFilm_ParseCast( &g_film_cast, (const char *)text, err, sizeof( err ) );
	FREE_FILE( text );
	if( count < 0 ) {
		ALERT( at_console, "FILM CAST %s: %s: %s\n", g_film_shot, path, err );
		memset( &g_film_cast, 0, sizeof( g_film_cast ) );
		return;
	}
	float preroll = CMD_ARGC() > 2 ? (float)atof( CMD_ARGV( 2 ) ) : 0.0f;
	g_film_start = gpGlobals->time + ( preroll > 0.0f ? preroll : 0.0f );
	g_film_last = gpGlobals->time - g_film_start;	/* shot time, as DodFilm_Think keeps it */
	g_film_fx_clock = gpGlobals->time - g_film_start;	/* minus the preroll: an fx at 0 fires on the first frame */
	if( g_film_fx_clock >= 0.0f )
		g_film_fx_clock = -0.001f;
	int placed = 0;
	for( int i = 0; i < g_film_cast.actor_count; i++ ) {
		const DodFilmActor *a = &g_film_cast.actors[i];
		char model[96];
		const int vehicle = a->model[0] != '\0';
		if( vehicle ) {
			if( !DodFilm_VehiclePrecached( a->model ) ) {
				ALERT( at_console, "FILM VEHICLE %s: %s was not precached (cine/cast.txt)\n", a->name, a->model );
				continue;
			}
			DodFilm_VehiclePath( a->model, model, sizeof( model ) );
		} else
			DodFilm_BodyPath( a->team, model, sizeof( model ) );
		if( !vehicle && ( !model[0] || !g_film_precached[a->team] ) ) {
			ALERT( at_console, "FILM ACTOR %s: the %s body was not precached (cine/cast.txt)\n", a->name,
				a->team == DOD_TEAM_AXIS ? "axis" : "allies" );
			continue;
		}
		CDodFilmActor *ent = DodFilm_PoolLive( i ) ? g_film_pool[i] : NULL;
		if( !ent ) {
			/* A full edict table hands the game NULL, and GetClassPtr does not survive it. */
			if( NUMBER_OF_ENTITIES() + DOD_FILM_EDICT_RESERVE >= gpGlobals->maxEntities ) {
				ALERT( at_console, "FILM CAST %s: edicts full (%d of %d in use), %d actor(s) left out\n",
					g_film_shot, NUMBER_OF_ENTITIES(), gpGlobals->maxEntities, g_film_cast.actor_count - i );
				break;
			}
			ent = GetClassPtr( (CDodFilmActor *)NULL );
			if( !ent ) {
				ALERT( at_console, "FILM ACTOR %s: out of edicts\n", a->name );
				break;
			}
			g_film_pool[i] = ent;
		}
		ent->pev->effects &= ~EF_NODRAW;
		ent->pev->classname = MAKE_STRING( ( vehicle ? "film_vehicle" : "film_actor" ) );
		ent->pev->movetype = MOVETYPE_NONE;
		ent->pev->solid = SOLID_NOT;
		ent->pev->takedamage = DAMAGE_NO;
		ent->pev->iuser4 = DODGC_FILM_ACTOR_TAG;
		SET_MODEL( ent->edict(), model );
		if( vehicle ) {
			ent->pev->body = 0;
			ent->pev->weaponmodel = 0;
			ent->pev->sequence = 0;
			ent->pev->controller[2] = ent->pev->controller[3] = 0;
			ent->ResetSequenceInfo();
			ent->m_actor = i;
			ent->m_pose = -1;
			ent->m_seq = 0;
		ent->m_hands = -1;
			ent->m_hands = -1;
			ent->m_gait = 0;
			ent->m_phase = 0.0f;
			DodFilm_VehicleSize( ent );
			g_film_ents[i] = ent;
			DodFilm_Place( i, gpGlobals->time - g_film_start, 0.0f );
			ALERT( at_console, "FILM VEHICLE %s: %s, box (%.0f %.0f %.0f)-(%.0f %.0f %.0f)\n", a->name, model,
				ent->pev->mins.x, ent->pev->mins.y, ent->pev->mins.z, ent->pev->maxs.x, ent->pev->maxs.y, ent->pev->maxs.z );
			placed++;
			continue;
		}
		const char *body = DodGC_BodyModelForTeam( (DodTeam)a->team );
		DodClass cls = a->player_class > DOD_CLASS_NONE ? (DodClass)a->player_class : DOD_CLASS_RIFLEMAN;
		ent->pev->body = Dod_PlayerBodyValue( body, cls, i + 1 );
		/* The engine draws weaponmodel on the actor's hand bones, as it does for a player. */
		const char *gun = DodFilm_WeaponModel( a->team, cls );
		ent->pev->weaponmodel = gun ? MAKE_STRING( gun ) : 0;	/* a reused actor may have carried another */
		ent->m_actor = i;
		ent->m_pose = -1;
		ent->m_seq = 0;
		ent->m_hands = -1;
		ent->m_gait = 0;
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
	memset( g_film_pool, 0, sizeof( g_film_pool ) );
	memset( &g_film_cast, 0, sizeof( g_film_cast ) );
	memset( g_film_precached, 0, sizeof( g_film_precached ) );
	memset( g_film_gun, 0, sizeof( g_film_gun ) );
	memset( g_film_sidearm, 0, sizeof( g_film_sidearm ) );
	memset( g_film_vehicle, 0, sizeof( g_film_vehicle ) );
	memset( g_film_prop, 0, sizeof( g_film_prop ) );
	memset( g_film_prop_path, 0, sizeof( g_film_prop_path ) );
	g_film_fireball = 0;
	memset( g_film_gibs, 0, sizeof( g_film_gibs ) );
	memset( g_film_burst, 0, sizeof( g_film_burst ) );
	g_film_burst_next = 0;
	byte *text = LOAD_FILE_FOR_ME( "cine/cast.txt", &length );
	if( !text )
		return;
	g_film_fireball = PRECACHE_MODEL( (char *)"sprites/explode1.spr" );
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
		g_film_gun[team][DOD_CLASS_RIFLEMAN] = 1;
		ALERT( at_console, "FILM: precached %s for actors\n", model );
		const char *gibs = team == DOD_TEAM_AXIS ? "models/axis_gibs.mdl" : "models/allied_gibs.mdl";
		int gib_len = 0;
		byte *gib = LOAD_FILE_FOR_ME( (char *)gibs, &gib_len );
		if( gib ) {
			FREE_FILE( gib );
			g_film_gibs[team] = PRECACHE_MODEL( (char *)gibs );
		}
		ALERT( at_console, "FILM: %s gibs %s (%d bytes)\n", name, g_film_gibs[team] ? "on" : "off, not on the disc", gib_len );
	}
	/* "gun <team> <class>" lines: the class guns the plan's casts carry, in the plan's order of need */
	{
		const char *p = (const char *)text, *end = (const char *)text + length;
		int per_side[4] = { 0, 1, 1, 0 };	/* the rifle is one */
		while( p < end ) {
			char line[64], tname[16], cname[16];
			int n = 0;
			while( p < end && *p != '\n' && n < (int)sizeof( line ) - 1 )
				line[n++] = *p++;
			line[n] = '\0';
			while( p < end && *p != '\n' )
				p++;
			if( p < end )
				p++;
			char vname[DOD_FILM_NAME];
			if( sscanf( line, "vehicle %31s", vname ) == 1 ) {
				int slot = -1;
				for( int v = 0; v < DOD_FILM_MAX_VEHICLE_MODELS && slot < 0; v++ )
					if( !g_film_vehicle[v][0] || !strcmp( g_film_vehicle[v], vname ) )
						slot = v;
				if( slot < 0 ) {
					ALERT( at_console, "FILM: vehicle %s left out (%d models at most)\n", vname, DOD_FILM_MAX_VEHICLE_MODELS );
					continue;
				}
				if( !g_film_vehicle[slot][0] ) {
					char path[96];
					DodFilm_VehiclePath( vname, path, sizeof( path ) );
					PRECACHE_MODEL( (char *)STRING( ALLOC_STRING( path ) ) );
					snprintf( g_film_vehicle[slot], sizeof( g_film_vehicle[slot] ), "%s", vname );
					ALERT( at_console, "FILM: precached %s for vehicles\n", path );
				}
				continue;
			}
			if( sscanf( line, "prop %31s", vname ) == 1 ) {
				int slot = -1;
				for( int v = 0; v < DOD_FILM_MAX_PROP_MODELS && slot < 0; v++ )
					if( !g_film_prop[v][0] || !strcmp( g_film_prop[v], vname ) )
						slot = v;
				if( slot < 0 ) {
					ALERT( at_console, "FILM: prop %s left out (%d props at most)\n", vname, DOD_FILM_MAX_PROP_MODELS );
					continue;
				}
				if( !g_film_prop[slot][0] ) {
					char path[96];
					int plen = 0;
					DodFilm_PropPath( vname, path, sizeof( path ) );
					/* only a prop that is on the disc: a precache of a missing model ends the map */
					byte *pf = LOAD_FILE_FOR_ME( path, &plen );
					if( !pf ) {
						ALERT( at_console, "FILM: prop %s is not on the disc, left out\n", path );
						continue;
					}
					FREE_FILE( pf );
					g_film_prop_path[slot] = ALLOC_STRING( path );
					PRECACHE_MODEL( (char *)STRING( g_film_prop_path[slot] ) );
					snprintf( g_film_prop[slot], sizeof( g_film_prop[slot] ), "%s", vname );
					ALERT( at_console, "FILM: precached %s for held props (%d bytes)\n", path, plen );
				}
				continue;
			}
			if( sscanf( line, "sidearm %15s", tname ) == 1 ) {
				const int team = !strcmp( tname, "axis" ) ? DOD_TEAM_AXIS : !strcmp( tname, "allies" ) ? DOD_TEAM_ALLIES : 0;
				const char *pistol = team ? DodFilm_Sidearm( team ) : NULL;
				if( pistol && g_film_precached[team] && !g_film_sidearm[team] ) {
					PRECACHE_MODEL( (char *)pistol );
					g_film_sidearm[team] = 1;
					ALERT( at_console, "FILM: precached %s for %s pistol clips\n", pistol, tname );
				}
				continue;
			}
			if( sscanf( line, "gun %15s %15s", tname, cname ) != 2 )
				continue;
			const int team = !strcmp( tname, "axis" ) ? DOD_TEAM_AXIS : !strcmp( tname, "allies" ) ? DOD_TEAM_ALLIES : 0;
			int cls = -1;
			static const char *const names[DOD_CLASS_COUNT] = { "none", "rifleman", "assault", "support", "sniper", "mg", "rocket" };
			for( int c = 1; c < DOD_CLASS_COUNT; c++ )
				if( !strcmp( cname, names[c] ) )
					cls = c;
			if( !team || cls < 0 || !g_film_precached[team] || g_film_gun[team][cls] )
				continue;
			const char *gun = DodFilm_ClassGun( team, cls );
			if( !gun || per_side[team] >= DOD_FILM_GUNS_PER_SIDE ) {
				ALERT( at_console, "FILM: %s %s carry the rifle (%s)\n", tname, cname, gun ? "gun cap" : "no gun" );
				continue;
			}
			PRECACHE_MODEL( (char *)gun );
			g_film_gun[team][cls] = 1;
			per_side[team]++;
			ALERT( at_console, "FILM: precached %s for %s %s\n", gun, tname, cname );
		}
	}
	FREE_FILE( text );
}

int DodFilm_IsFilmMap( void )
{
	return g_film_precached[DOD_TEAM_ALLIES] || g_film_precached[DOD_TEAM_AXIS];
}

/* Seconds into the shot. The engine's cine_clock cvar is the camera's locked film clock
 * (negative in preroll); server time can fall behind it when frames render slowly, which
 * left effects unfired and actors behind the camera. Server time when no shot plays. */
static float DodFilm_ShotTime( void )
{
	const float clock = CVAR_GET_FLOAT( "cine_clock" );
	if( clock > -50000.0f )
		return clock;
	return gpGlobals->time - g_film_start;
}

void DodFilm_Think( void )
{
	if( !g_film_cast.actor_count && !g_film_cast.fx_count )
		return;
	const float t = DodFilm_ShotTime();
	float dt = t - g_film_last;
	g_film_last = t;
	if( dt < 0.0f || dt > 0.5f )
		dt = 0.0f;
	for( int i = 0; i < g_film_cast.actor_count; i++ )
		DodFilm_Place( i, t, dt );
	DodFilm_RunFx( t );
}
