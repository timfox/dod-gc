/* Film actors: scripted props for camera shots.
 *
 * An actor is a prop entity wearing a team's player model, not a bot: it costs one
 * edict and no client slot, so a shot can have up to 24 soldiers on the GameCube where
 * the listen server has room for exactly one bot. Each shot has a cast file,
 * cine/<shot>.act, written by a film plan:
 *
 *   act 1
 *   actor <name> <allies|axis> [class]
 *   key <t> <x> <y> <z> <yaw|-> <pose> [<sequence> [loop|once|upper|upperloop]]
 *   ...
 *   fx <t> <kind> <x> <y> <z> [scale]
 *   fx <t> tracer <x> <y> <z> <x2> <y2> <z2>
 *
 * Keys belong to the actor above them and run in time order. Between keys the actor
 * moves in a straight line at constant speed; yaw "-" faces the way it is moving (or
 * keeps the last facing when standing still). Times are seconds from the first frame
 * of the shot, the same clock as the camera's .cam path. An fx line is a one-off effect
 * at a point (a shell burst, a splash, a smoke puff, a tracer from one point to another),
 * fired by the game when the shot's clock passes its time; fx lines can come anywhere
 * after the header and in any order.
 *
 * A key can name a sequence of the actor's model to play instead of the pose's own: a
 * hand signal, a flinch, a death, anything the model has. The pose still sets how high
 * the actor stands. "loop" (the default) cycles the sequence, "once" plays it from its
 * first frame when the key is reached and holds the last; "upper" and "upperloop" play
 * it over the pose's standing legs (an upper-body sequence such as a hand signal or a
 * reload). Consecutive keys naming the same sequence keep playing it, so a key that
 * holds a one-shot does not restart it. A model without the sequence plays the pose.
 * This file is engine-free so
 * the parsing and sampling are host-tested; the entity side is dod_film_actor.cpp. */
#ifndef DODGC_DOD_FILM_H
#define DODGC_DOD_FILM_H

#ifdef __cplusplus
extern "C" {
#endif

#define DOD_FILM_MAX_ACTORS 64	/* the camera player plus 64 actors stays well under the 460-edict cap */
#define DOD_FILM_MAX_KEYS 16
#define DOD_FILM_NAME 32
#define DOD_FILM_MAX_FX 192
#define DOD_FILM_MAX_SEQS 64	/* distinct named sequences in one cast file */
#define DOD_FILM_YAW_TRAVEL 1000.0f	/* yaw "-": face the direction of travel */
/* Set in an actor's pev->iuser4. The GameCube engine draws studio props on its lean listen
 * route through a model allowlist that leaves out player bodies; this tag admits the actors
 * and puts them ahead of the map's props (xash3d-gc engine/client/cl_frame.c, GC_FILM_ACTOR_TAG). */
#define DODGC_FILM_ACTOR_TAG 0x46494C4D	/* 'FILM' */

typedef enum {
	DOD_POSE_STAND = 0,
	DOD_POSE_WALK,
	DOD_POSE_RUN,
	DOD_POSE_CROUCH,
	DOD_POSE_CROUCH_WALK,
	DOD_POSE_PRONE,
	DOD_POSE_CRAWL,
	DOD_POSE_AIM,		/* standing, weapon up */
	DOD_POSE_CROUCH_AIM,
	DOD_POSE_FALLEN,	/* lying still, the last frame of a death sequence */
	DOD_POSE_COUNT
} DodFilmPose;

typedef enum {
	DOD_FILM_PLAY_LOOP = 0,		/* the whole body, cycling */
	DOD_FILM_PLAY_ONCE,		/* the whole body, once, holding the last frame */
	DOD_FILM_PLAY_UPPER,		/* over the pose's standing legs, once */
	DOD_FILM_PLAY_UPPER_LOOP,	/* over the pose's standing legs, cycling */
	DOD_FILM_PLAY_COUNT
} DodFilmPlay;

typedef struct {
	float t;
	float origin[3];
	float yaw;		/* degrees, or DOD_FILM_YAW_TRAVEL */
	unsigned char pose;	/* DodFilmPose */
	unsigned char seq;	/* 0: the pose's own sequence; n: the cast's seq_names[n - 1] */
	unsigned char play;	/* DodFilmPlay, when seq is set */
	unsigned char pad;	/* the key stays 24 bytes: 64 actors of 16 keys live in MEM1 */
} DodFilmKey;

typedef struct {
	char name[DOD_FILM_NAME];
	int team;		/* 1 allies, 2 axis (DodTeam) */
	int player_class;	/* DodClass, 0 when the cast file names none; picks the uniform and the gun
				 * the retail model carries in its gear bodygroup */
	int key_count;
	DodFilmKey keys[DOD_FILM_MAX_KEYS];
} DodFilmActor;

typedef enum {
	DOD_FX_KIND_EXPLOSION = 0,	/* a shell burst on land: fireball, sparks, thrown dirt, smoke */
	DOD_FX_KIND_WATER,		/* a shell in the water: a white column of spray */
	DOD_FX_KIND_DIRT,		/* a near miss: a spout of sand or earth, no fire */
	DOD_FX_KIND_SMOKE,		/* a rising puff of smoke */
	DOD_FX_KIND_TRACER,		/* a tracer round from origin to end */
	DOD_FX_KIND_BLOOD,		/* a hit: a spurt and a spray of blood particles */
	DOD_FX_KIND_COUNT
} DodFilmFxKind;

typedef struct {
	float t;
	DodFilmFxKind kind;
	float origin[3];
	float end[3];		/* tracer only */
	float scale;		/* 1 is a grenade-sized blast */
} DodFilmFx;

typedef struct {
	int actor_count;
	DodFilmActor actors[DOD_FILM_MAX_ACTORS];
	int seq_count;
	char seq_names[DOD_FILM_MAX_SEQS][DOD_FILM_NAME];
	int fx_count;
	DodFilmFx fx[DOD_FILM_MAX_FX];	/* sorted by time once parsed */
} DodFilmCast;

/* Where an actor is at time t: origin, facing and pose. Before the first key it
 * holds the first, after the last it holds the last. moving is 1 while the actor
 * travels between two keys that are apart (the pose then is the earlier key's). */
typedef struct {
	float origin[3];
	float yaw;
	DodFilmPose pose;
	int moving;
	float speed;	/* units per second along the current leg */
	int seq;	/* the key's named sequence (DodFilmKey.seq), 0 for the pose's own */
	DodFilmPlay play;
} DodFilmSample;

/* Parse a cast file. Returns the number of actors (0 is a valid, empty cast) or -1
 * with a reason in err. Keys out of time order, unknown poses or teams, and too many
 * actors or keys are errors: a cast that is wrong should fail loudly, not play wrong. */
int DodFilm_ParseCast(DodFilmCast *cast, const char *text, char *err, int err_size);

/* The fx due when the shot's clock moves from t0 to t1: indices [*first, return) of
 * cast->fx with t0 < t <= t1 (an fx at exactly 0 fires on the first step past it). */
int DodFilm_FxDue(const DodFilmCast *cast, float t0, float t1, int *first);

/* The kind's name in cast files ("explosion", "water", ...) and back; -1 when unknown. */
const char *DodFilm_FxName(DodFilmFxKind kind);
int DodFilm_FxFromName(const char *name);

/* Sample an actor at time t (seconds from the shot's first frame). */
void DodFilm_Sample(const DodFilmActor *actor, float t, DodFilmSample *out);

/* The play mode's name in cast files ("loop", "once", "upper", "upperloop") and back; -1 when unknown. */
const char *DodFilm_PlayName(DodFilmPlay play);
int DodFilm_PlayFromName(const char *name);

/* 1 when a sample's sequence cycles: a named sequence by its play mode, otherwise the pose's. */
int DodFilm_SampleLoops(const DodFilmSample *s);

/* The pose's name in cast files ("stand", "run", ...) and back; -1 when unknown. */
const char *DodFilm_PoseName(DodFilmPose pose);
int DodFilm_PoseFromName(const char *name);

/* Height of the actor's origin above the floor for a pose. A player model is drawn
 * from the origin of the player's hull: 36 above the floor standing, 18 crouched. */
float DodFilm_PoseHeight(DodFilmPose pose);

/* Sequence names to look up in the player model for a pose, best first, NULL-ended.
 * The retail model's names are not documented anywhere we can cite, so the entity
 * tries these in order and logs the one it found. */
const char *const *DodFilm_PoseSequences(DodFilmPose pose);

/* 1 when the pose's sequence should loop (walking, idling); 0 when it plays once
 * and holds its last frame (falling). */
int DodFilm_PoseLoops(DodFilmPose pose);
DodFilmPose DodFilm_GaitPose(DodFilmPose pose, int moving, float speed);

#ifdef __cplusplus
}
#endif

#endif
