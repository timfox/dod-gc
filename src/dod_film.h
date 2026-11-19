/* Film actors: scripted props for camera shots.
 *
 * An actor is a prop entity wearing a team's player model, not a bot: it costs one
 * edict and no client slot, so a shot can have a dozen soldiers on the GameCube where
 * the listen server has room for exactly one bot. Each shot has a cast file,
 * cine/<shot>.act, written by a film plan:
 *
 *   act 1
 *   actor <name> <allies|axis> [class]
 *   key <t> <x> <y> <z> <yaw|-> <pose>
 *   ...
 *
 * Keys belong to the actor above them and run in time order. Between keys the actor
 * moves in a straight line at constant speed; yaw "-" faces the way it is moving (or
 * keeps the last facing when standing still). Times are seconds from the first frame
 * of the shot, the same clock as the camera's .cam path. This file is engine-free so
 * the parsing and sampling are host-tested; the entity side is dod_film_actor.cpp. */
#ifndef DODGC_DOD_FILM_H
#define DODGC_DOD_FILM_H

#ifdef __cplusplus
extern "C" {
#endif

#define DOD_FILM_MAX_ACTORS 12
#define DOD_FILM_MAX_KEYS 16
#define DOD_FILM_NAME 32
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

typedef struct {
	float t;
	float origin[3];
	float yaw;		/* degrees, or DOD_FILM_YAW_TRAVEL */
	DodFilmPose pose;
} DodFilmKey;

typedef struct {
	char name[DOD_FILM_NAME];
	int team;		/* 1 allies, 2 axis (DodTeam) */
	int player_class;	/* DodClass, 0 when the cast file names none; picks the uniform and the gun
				 * the retail model carries in its gear bodygroup */
	int key_count;
	DodFilmKey keys[DOD_FILM_MAX_KEYS];
} DodFilmActor;

typedef struct {
	int actor_count;
	DodFilmActor actors[DOD_FILM_MAX_ACTORS];
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
} DodFilmSample;

/* Parse a cast file. Returns the number of actors (0 is a valid, empty cast) or -1
 * with a reason in err. Keys out of time order, unknown poses or teams, and too many
 * actors or keys are errors: a cast that is wrong should fail loudly, not play wrong. */
int DodFilm_ParseCast(DodFilmCast *cast, const char *text, char *err, int err_size);

/* Sample an actor at time t (seconds from the shot's first frame). */
void DodFilm_Sample(const DodFilmActor *actor, float t, DodFilmSample *out);

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

#ifdef __cplusplus
}
#endif

#endif
