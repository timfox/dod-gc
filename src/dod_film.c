/* Film actors: cast file parsing and sampling (see dod_film.h). */
#include "dod_film.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const pose_names[DOD_POSE_COUNT] = {
	"stand", "walk", "run", "crouch", "crouch_walk", "prone", "crawl", "aim", "crouch_aim", "fallen",
};

/* Candidates, best first. HL player models name their gait sequences idle, walk, run,
 * crouch_idle, crouchrun; DoD adds prone and aiming sequences. */
static const char *const seq_stand[] = { "idle", "standing", "ref_aim_garand", 0 };
static const char *const seq_walk[] = { "walk", "walking", "run", 0 };
static const char *const seq_run[] = { "run", "running", "walk", 0 };
static const char *const seq_crouch[] = { "crouch_idle", "crouch", "crouching", "idle", 0 };
static const char *const seq_crouch_walk[] = { "crouchrun", "crouch_walk", "crouchwalk", "crouch_idle", 0 };
static const char *const seq_prone[] = { "prone_idle", "prone", "proneidle", "crouch_idle", 0 };
static const char *const seq_crawl[] = { "prone_forward", "prone_walk", "crawl", "prone_idle", "crouchrun", 0 };
static const char *const seq_aim[] = { "ref_aim_garand", "ref_aim_k98", "ref_aim_mp40", "idle", 0 };
static const char *const seq_crouch_aim[] = { "crouch_aim_garand", "crouch_aim_k98", "crouch_aim_mp40", "crouch_idle", 0 };
static const char *const seq_fallen[] = { "death_simple", "death_backwards", "die_simple", "die_backwards", 0 };

static const char *const *const pose_seqs[DOD_POSE_COUNT] = {
	seq_stand, seq_walk, seq_run, seq_crouch, seq_crouch_walk, seq_prone, seq_crawl, seq_aim, seq_crouch_aim, seq_fallen,
};

const char *DodFilm_PoseName(DodFilmPose pose)
{
	return pose >= 0 && pose < DOD_POSE_COUNT ? pose_names[pose] : "?";
}

int DodFilm_PoseFromName(const char *name)
{
	int i;
	for (i = 0; i < DOD_POSE_COUNT; ++i)
		if (!strcmp(name, pose_names[i]))
			return i;
	return -1;
}

float DodFilm_PoseHeight(DodFilmPose pose)
{
	switch (pose) {
	case DOD_POSE_CROUCH:
	case DOD_POSE_CROUCH_WALK:
	case DOD_POSE_CROUCH_AIM:
	case DOD_POSE_PRONE:
	case DOD_POSE_CRAWL:
	case DOD_POSE_FALLEN:
		return 18.0f;
	default:
		return 36.0f;
	}
}

const char *const *DodFilm_PoseSequences(DodFilmPose pose)
{
	return pose >= 0 && pose < DOD_POSE_COUNT ? pose_seqs[pose] : seq_stand;
}

int DodFilm_PoseLoops(DodFilmPose pose)
{
	return pose != DOD_POSE_FALLEN;
}

static int set_err(char *err, int size, int line, const char *what)
{
	if (err && size > 0)
		snprintf(err, (size_t)size, "line %d: %s", line, what);
	return -1;
}

static int team_from_name(const char *s)
{
	if (!strcmp(s, "allies") || !strcmp(s, "1"))
		return 1;
	if (!strcmp(s, "axis") || !strcmp(s, "2"))
		return 2;
	return 0;
}

static int class_from_name(const char *s)
{
	/* DodClass order (dod_game.h): none, rifleman, assault, support, sniper, mg, rocket */
	static const char *const names[] = { "none", "rifleman", "assault", "support", "sniper", "mg", "rocket" };
	int i;
	for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); ++i)
		if (!strcmp(s, names[i]))
			return i;
	return -1;
}

int DodFilm_ParseCast(DodFilmCast *cast, const char *text, char *err, int err_size)
{
	char line[256];
	int lineno = 0, have_header = 0;
	DodFilmActor *cur = 0;
	const char *p = text;

	memset(cast, 0, sizeof(*cast));
	if (err && err_size > 0)
		err[0] = '\0';
	if (!text)
		return set_err(err, err_size, 0, "no text");
	while (*p) {
		const char *nl = strchr(p, '\n');
		size_t n = nl ? (size_t)(nl - p) : strlen(p);
		char tok[8][32];
		int ntok;
		++lineno;
		if (n >= sizeof(line))
			return set_err(err, err_size, lineno, "line too long");
		memcpy(line, p, n);
		line[n] = '\0';
		p = nl ? nl + 1 : p + n;
		{
			char *c = strstr(line, "//");
			if (c)
				*c = '\0';
		}
		ntok = sscanf(line, "%31s %31s %31s %31s %31s %31s %31s %31s",
			tok[0], tok[1], tok[2], tok[3], tok[4], tok[5], tok[6], tok[7]);
		if (ntok <= 0)
			continue;
		if (!have_header) {
			if (strcmp(tok[0], "act") || ntok < 2 || atoi(tok[1]) != 1)
				return set_err(err, err_size, lineno, "expected 'act 1'");
			have_header = 1;
			continue;
		}
		if (!strcmp(tok[0], "actor")) {
			int team, cls = 0;
			if (ntok < 3)
				return set_err(err, err_size, lineno, "actor needs a name and a team");
			if (cast->actor_count >= DOD_FILM_MAX_ACTORS)
				return set_err(err, err_size, lineno, "too many actors");
			team = team_from_name(tok[2]);
			if (!team)
				return set_err(err, err_size, lineno, "team must be allies or axis");
			if (ntok >= 4 && (cls = class_from_name(tok[3])) < 0)
				return set_err(err, err_size, lineno, "unknown class");
			cur = &cast->actors[cast->actor_count++];
			snprintf(cur->name, sizeof(cur->name), "%s", tok[1]);
			cur->team = team;
			cur->player_class = cls;
			continue;
		}
		if (!strcmp(tok[0], "key")) {
			DodFilmKey *k;
			int pose;
			if (!cur)
				return set_err(err, err_size, lineno, "key before any actor");
			if (ntok < 7)
				return set_err(err, err_size, lineno, "key needs t x y z yaw pose");
			if (cur->key_count >= DOD_FILM_MAX_KEYS)
				return set_err(err, err_size, lineno, "too many keys for one actor");
			pose = DodFilm_PoseFromName(tok[6]);
			if (pose < 0)
				return set_err(err, err_size, lineno, "unknown pose");
			k = &cur->keys[cur->key_count];
			k->t = (float)atof(tok[1]);
			k->origin[0] = (float)atof(tok[2]);
			k->origin[1] = (float)atof(tok[3]);
			k->origin[2] = (float)atof(tok[4]);
			k->yaw = strcmp(tok[5], "-") ? (float)atof(tok[5]) : DOD_FILM_YAW_TRAVEL;
			k->pose = (DodFilmPose)pose;
			if (cur->key_count > 0 && k->t < cur->keys[cur->key_count - 1].t)
				return set_err(err, err_size, lineno, "keys out of time order");
			cur->key_count++;
			continue;
		}
		return set_err(err, err_size, lineno, "unknown directive");
	}
	if (!have_header)
		return set_err(err, err_size, lineno, "empty cast file");
	{
		int i;
		for (i = 0; i < cast->actor_count; ++i)
			if (cast->actors[i].key_count == 0) {
				char what[64];
				snprintf(what, sizeof(what), "actor %s has no keys", cast->actors[i].name);
				return set_err(err, err_size, lineno, what);
			}
	}
	return cast->actor_count;
}

static float travel_yaw(const DodFilmKey *a, const DodFilmKey *b, float fallback)
{
	float dx = b->origin[0] - a->origin[0], dy = b->origin[1] - a->origin[1];
	if (dx * dx + dy * dy < 1.0f)
		return fallback;
	return (float)(atan2(dy, dx) * 180.0 / 3.14159265358979323846);
}

/* The facing an actor has at key i when it says "-": the leg that leaves it, or the
 * one that arrives, or 0 for a single standing key. */
static float key_yaw(const DodFilmActor *a, int i)
{
	int j;
	if (a->keys[i].yaw != DOD_FILM_YAW_TRAVEL)
		return a->keys[i].yaw;
	for (j = i; j + 1 < a->key_count; ++j) {
		float y = travel_yaw(&a->keys[j], &a->keys[j + 1], DOD_FILM_YAW_TRAVEL);
		if (y != DOD_FILM_YAW_TRAVEL)
			return y;
	}
	for (j = i; j > 0; --j) {
		float y = travel_yaw(&a->keys[j - 1], &a->keys[j], DOD_FILM_YAW_TRAVEL);
		if (y != DOD_FILM_YAW_TRAVEL)
			return y;
	}
	return 0.0f;
}

void DodFilm_Sample(const DodFilmActor *actor, float t, DodFilmSample *out)
{
	int i;
	memset(out, 0, sizeof(*out));
	if (!actor || actor->key_count <= 0)
		return;
	if (t <= actor->keys[0].t || actor->key_count == 1) {
		const DodFilmKey *k = &actor->keys[0];
		memcpy(out->origin, k->origin, sizeof(out->origin));
		out->yaw = key_yaw(actor, 0);
		out->pose = k->pose;
		return;
	}
	for (i = 0; i + 1 < actor->key_count; ++i) {
		const DodFilmKey *a = &actor->keys[i], *b = &actor->keys[i + 1];
		if (t < b->t) {
			float span = b->t - a->t;
			float u = span > 0.0f ? (t - a->t) / span : 1.0f;
			float dx = b->origin[0] - a->origin[0], dy = b->origin[1] - a->origin[1], dz = b->origin[2] - a->origin[2];
			float dist = (float)sqrt(dx * dx + dy * dy + dz * dz);
			int c;
			for (c = 0; c < 3; ++c)
				out->origin[c] = a->origin[c] + (b->origin[c] - a->origin[c]) * u;
			out->pose = a->pose;
			out->moving = dist >= 1.0f && span > 0.0f;
			out->speed = out->moving ? dist / span : 0.0f;
			if (a->yaw != DOD_FILM_YAW_TRAVEL)
				out->yaw = a->yaw;
			else
				out->yaw = travel_yaw(a, b, key_yaw(actor, i));
			return;
		}
	}
	{
		const DodFilmKey *k = &actor->keys[actor->key_count - 1];
		memcpy(out->origin, k->origin, sizeof(out->origin));
		out->yaw = key_yaw(actor, actor->key_count - 1);
		out->pose = k->pose;
	}
}
