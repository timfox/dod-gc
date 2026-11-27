/* Film actors: cast file parsing and sampling (see dod_film.h). */
#include "dod_film.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const pose_names[DOD_POSE_COUNT] = {
	"stand", "walk", "run", "crouch", "crouch_walk", "prone", "crawl", "aim", "crouch_aim", "fallen",
};

/* Candidates, best first. The retail DoD player models (all five, and player.mdl) share one 345-sequence
 * table: dod_walk 10, dod_jog 11, dod_sprint 12, dod_crawl 13 (DoD's crouched walk), dod_crouch_idle 14,
 * prone_idle 15, prone_forward 16 (the prone crawl), die_* 19-29, stand/crouch_rifle_aim 112/113.
 * HL names follow as fallbacks for other models. */
static const char *const seq_stand[] = { "dod_idle1", "look_idle", "idle", "standing", 0 };
static const char *const seq_walk[] = { "dod_walk", "walk", "walking", 0 };
static const char *const seq_run[] = { "dod_jog", "dod_sprint", "run", "running", "walk", 0 };
static const char *const seq_crouch[] = { "dod_crouch_idle", "crouch_idle", "crouch", 0 };
static const char *const seq_crouch_walk[] = { "dod_crawl", "crouchrun", "crouch_walk", "dod_crouch_idle", 0 };
static const char *const seq_prone[] = { "prone_idle", "prone", 0 };
static const char *const seq_crawl[] = { "prone_forward", "prone_walk", "crawl", "prone_idle", 0 };
static const char *const seq_aim[] = { "stand_rifle_aim", "ref_aim_garand", "dod_idle1", "idle", 0 };
static const char *const seq_crouch_aim[] = { "crouch_rifle_aim", "crouch_aim_garand", "dod_crouch_idle", 0 };
/* Every death the model has is a candidate: the actor takes one of those found, picked by its
 * slot, so a line of men going down does not fall the same way. */
static const char *const seq_fallen[] = { "die_simple", "die_forwards", "die_backwards", "die_backwards1",
	"die_spin", "die_headshot", "die_gutshot", "headshot", "gutshot", "death_simple", 0 };

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

static const char *const play_names[DOD_FILM_PLAY_COUNT] = { "loop", "once", "upper", "upperloop" };

const char *DodFilm_PlayName(DodFilmPlay play)
{
	return play >= 0 && play < DOD_FILM_PLAY_COUNT ? play_names[play] : "?";
}

int DodFilm_PlayFromName(const char *name)
{
	int i;
	for (i = 0; i < DOD_FILM_PLAY_COUNT; ++i)
		if (!strcmp(name, play_names[i]))
			return i;
	return -1;
}

int DodFilm_SampleLoops(const DodFilmSample *s)
{
	if (s->seq)
		return s->play == DOD_FILM_PLAY_LOOP || s->play == DOD_FILM_PLAY_UPPER_LOOP;
	return DodFilm_PoseLoops(s->pose);
}

/* The cast's number for a sequence name (1-based), adding it when new; 0 when the table is full. */
static int seq_slot(DodFilmCast *cast, const char *name)
{
	int i;
	for (i = 0; i < cast->seq_count; ++i)
		if (!strcmp(cast->seq_names[i], name))
			return i + 1;
	if (cast->seq_count >= DOD_FILM_MAX_SEQS)
		return 0;
	snprintf(cast->seq_names[cast->seq_count], sizeof(cast->seq_names[0]), "%s", name);
	return ++cast->seq_count;
}

/* The pose an actor is shown in: the keyed stance, with the gait its motion needs. A leg keyed
 * "crouch" that covers ground plays the crouch walk, not a still crouch gliding over the floor; a
 * stop keyed "run" stands. Fast legs run whatever the stance (a crouch walk at run pace slides). */
DodFilmPose DodFilm_GaitPose(DodFilmPose pose, int moving, float speed)
{
	if (pose == DOD_POSE_FALLEN)
		return pose;
	if (!moving || speed < 8.0f) {
		switch (pose) {
		case DOD_POSE_WALK:
		case DOD_POSE_RUN:
			return DOD_POSE_STAND;
		case DOD_POSE_CROUCH_WALK:
			return DOD_POSE_CROUCH;
		case DOD_POSE_CRAWL:
			return DOD_POSE_PRONE;
		default:
			return pose;
		}
	}
	switch (pose) {
	case DOD_POSE_PRONE:
	case DOD_POSE_CRAWL:
		return speed > 90.0f ? DOD_POSE_RUN : DOD_POSE_CRAWL;
	case DOD_POSE_CROUCH:
	case DOD_POSE_CROUCH_AIM:
	case DOD_POSE_CROUCH_WALK:
		return speed > 130.0f ? DOD_POSE_RUN : DOD_POSE_CROUCH_WALK;
	default:	/* standing stances */
		return speed > 135.0f ? DOD_POSE_RUN : DOD_POSE_WALK;
	}
}

static const char *const fx_names[DOD_FX_KIND_COUNT] = {
	"explosion", "water", "dirt", "smoke", "tracer", "blood", "gibs",
};

const char *DodFilm_FxName(DodFilmFxKind kind)
{
	return kind >= 0 && kind < DOD_FX_KIND_COUNT ? fx_names[kind] : "?";
}

int DodFilm_FxFromName(const char *name)
{
	int i;
	for (i = 0; i < DOD_FX_KIND_COUNT; ++i)
		if (!strcmp(name, fx_names[i]))
			return i;
	return -1;
}

static int fx_cmp(const void *a, const void *b)
{
	float ta = ((const DodFilmFx *)a)->t, tb = ((const DodFilmFx *)b)->t;
	return ta < tb ? -1 : ta > tb ? 1 : 0;
}

int DodFilm_FxDue(const DodFilmCast *cast, float t0, float t1, int *first)
{
	int i = 0, j;
	if (!cast || t1 <= t0) {
		if (first)
			*first = 0;
		return 0;
	}
	while (i < cast->fx_count && cast->fx[i].t <= t0)
		++i;
	j = i;
	while (j < cast->fx_count && cast->fx[j].t <= t1)
		++j;
	if (first)
		*first = i;
	return j;
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

void DodFilm_VehiclePath(const char *name, char *out, int size)
{
	const size_t n = strlen(name);
	if (n > 4 && !strcmp(name + n - 4, ".mdl"))
		snprintf(out, (size_t)size, "%s", name);
	else
		snprintf(out, (size_t)size, "models/mapmodels/%s.mdl", name);
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
		char tok[10][32];
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
		ntok = sscanf(line, "%31s %31s %31s %31s %31s %31s %31s %31s %31s %31s",
			tok[0], tok[1], tok[2], tok[3], tok[4], tok[5], tok[6], tok[7], tok[8], tok[9]);
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
		if (!strcmp(tok[0], "vehicle")) {
			if (ntok < 3)
				return set_err(err, err_size, lineno, "vehicle needs a name and a model");
			if (cast->actor_count >= DOD_FILM_MAX_ACTORS)
				return set_err(err, err_size, lineno, "too many actors");
			cur = &cast->actors[cast->actor_count++];
			snprintf(cur->name, sizeof(cur->name), "%s", tok[1]);
			snprintf(cur->model, sizeof(cur->model), "%s", tok[2]);
			cur->team = 0;
			cur->player_class = 0;
			cur->has_aim = 0;
			if (ntok >= 4 && strcmp(tok[3], "aim"))
				return set_err(err, err_size, lineno, "vehicle takes only aim <x> <y> <z> after its model");
			if (ntok >= 4) {
				if (ntok < 7)
					return set_err(err, err_size, lineno, "vehicle aim needs x y z");
				cur->has_aim = 1;
				cur->aim[0] = (float)atof(tok[4]);
				cur->aim[1] = (float)atof(tok[5]);
				cur->aim[2] = (float)atof(tok[6]);
			}
			continue;
		}
		if (!strcmp(tok[0], "key")) {
			DodFilmKey *k;
			int pose;
			if (!cur)
				return set_err(err, err_size, lineno, "key before any actor");
			if (ntok < 7)
				return set_err(err, err_size, lineno, "key needs t x y z yaw pose [sequence [play]]");
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
			k->pose = (unsigned char)pose;
			k->seq = 0;
			k->play = DOD_FILM_PLAY_LOOP;
			k->bare = 0;
			if (ntok >= 8) {
				int slot = seq_slot(cast, tok[7]), play = DOD_FILM_PLAY_LOOP;
				if (!slot)
					return set_err(err, err_size, lineno, "too many named sequences");
				if (ntok >= 9 && (play = DodFilm_PlayFromName(tok[8])) < 0)
					return set_err(err, err_size, lineno, "unknown play (loop, once, upper, upperloop)");
				if (ntok >= 10) {
					if (strcmp(tok[9], "bare"))
						return set_err(err, err_size, lineno, "unknown hands (bare)");
					k->bare = 1;
				}
				k->seq = (unsigned char)slot;
				k->play = (unsigned char)play;
			}
			if (cur->key_count > 0 && k->t < cur->keys[cur->key_count - 1].t)
				return set_err(err, err_size, lineno, "keys out of time order");
			cur->key_count++;
			continue;
		}
		if (!strcmp(tok[0], "fx")) {
			DodFilmFx *f;
			int kind;
			if (ntok < 6)
				return set_err(err, err_size, lineno, "fx needs t kind x y z");
			if (cast->fx_count >= DOD_FILM_MAX_FX)
				return set_err(err, err_size, lineno, "too many fx");
			kind = DodFilm_FxFromName(tok[2]);
			if (kind < 0)
				return set_err(err, err_size, lineno, "unknown fx kind");
			if (kind == DOD_FX_KIND_TRACER && ntok < 9)
				return set_err(err, err_size, lineno, "tracer needs t tracer x y z x2 y2 z2");
			f = &cast->fx[cast->fx_count++];
			f->t = (float)atof(tok[1]);
			f->kind = (DodFilmFxKind)kind;
			f->origin[0] = (float)atof(tok[3]);
			f->origin[1] = (float)atof(tok[4]);
			f->origin[2] = (float)atof(tok[5]);
			f->scale = 1.0f;
			if (kind == DOD_FX_KIND_TRACER) {
				f->end[0] = (float)atof(tok[6]);
				f->end[1] = (float)atof(tok[7]);
				f->end[2] = (float)atof(tok[8]);
			} else {
				memcpy(f->end, f->origin, sizeof(f->end));
				if (ntok >= 7)
					f->scale = (float)atof(tok[6]);
				if (!(f->scale > 0.0f))
					f->scale = 1.0f;
			}
			f->team = 0;
			if (kind == DOD_FX_KIND_GIBS && ntok >= 8) {
				f->team = team_from_name(tok[7]);
				if (!f->team)
					return set_err(err, err_size, lineno, "gibs team must be allies or axis");
			}
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
	if (cast->fx_count > 1)
		qsort(cast->fx, (size_t)cast->fx_count, sizeof(cast->fx[0]), fx_cmp);
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

/* The facing an actor holds over segment i: -1 before the first key, i for the leg from key i to
 * key i + 1, key_count - 1 after the last key. */
static float segment_yaw(const DodFilmActor *a, int i)
{
	if (i < 0)
		return key_yaw(a, 0);
	if (i + 1 >= a->key_count)
		return key_yaw(a, a->key_count - 1);
	if (a->keys[i].yaw != DOD_FILM_YAW_TRAVEL)
		return a->keys[i].yaw;
	return travel_yaw(&a->keys[i], &a->keys[i + 1], key_yaw(a, i));
}

static float segment_span(const DodFilmActor *a, int i)
{
	if (i < 0 || i + 1 >= a->key_count)
		return 1e9f;
	return a->keys[i + 1].t - a->keys[i].t;
}

/* A man turns, he does not snap: across each key the facing eases from the leg that arrives to
 * the leg that leaves over DOD_FILM_TURN seconds (less when a leg is shorter), the short way
 * round, with a smoothstep. A function of t alone, so a skipped frame lands where it should. */
#define DOD_FILM_TURN 0.5f
static float turned_yaw(const DodFilmActor *a, float t)
{
	int seg = -1, k;
	float yaw;
	for (k = 0; k < a->key_count && t >= a->keys[k].t; ++k)
		seg = k;
	yaw = segment_yaw(a, seg);
	for (k = seg; k <= seg + 1; ++k) {
		float h, u, from, to, d;
		if (k < 0 || k >= a->key_count)
			continue;
		h = DOD_FILM_TURN * 0.5f;
		if (segment_span(a, k - 1) * 0.5f < h)
			h = segment_span(a, k - 1) * 0.5f;
		if (segment_span(a, k) * 0.5f < h)
			h = segment_span(a, k) * 0.5f;
		if (h <= 1e-4f || t <= a->keys[k].t - h || t >= a->keys[k].t + h)
			continue;
		from = segment_yaw(a, k - 1);
		to = segment_yaw(a, k);
		d = fmodf(to - from + 540.0f, 360.0f) - 180.0f;
		u = (t - (a->keys[k].t - h)) / (2.0f * h);
		u = u * u * (3.0f - 2.0f * u);
		yaw = from + d * u;
		break;
	}
	return yaw;
}

static void sample_keys(const DodFilmActor *actor, float t, DodFilmSample *out)
{
	int i;
	memset(out, 0, sizeof(*out));
	if (!actor || actor->key_count <= 0)
		return;
	if (t <= actor->keys[0].t || actor->key_count == 1) {
		const DodFilmKey *k = &actor->keys[0];
		memcpy(out->origin, k->origin, sizeof(out->origin));
		out->yaw = key_yaw(actor, 0);
		out->pose = (DodFilmPose)k->pose;
		out->seq = k->seq;
		out->play = (DodFilmPlay)k->play;
		out->bare = k->bare;
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
			out->pose = (DodFilmPose)a->pose;
			out->seq = a->seq;
			out->play = (DodFilmPlay)a->play;
			out->bare = a->bare;
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
		out->pose = (DodFilmPose)k->pose;
		out->seq = k->seq;
		out->play = (DodFilmPlay)k->play;
		out->bare = k->bare;
	}
}

void DodFilm_Sample(const DodFilmActor *actor, float t, DodFilmSample *out)
{
	sample_keys(actor, t, out);
	if (actor && actor->key_count > 1)
		out->yaw = turned_yaw(actor, t);
}
