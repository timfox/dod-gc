/* Grunt rules; see dod_grunts.h. */
#include "dod_grunts.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float wrap180(float a)
{
	while (a >= 180.0f)
		a -= 360.0f;
	while (a < -180.0f)
		a += 360.0f;
	return a;
}

int DodGrunt_RowBytes(void)
{
	const DodGruntPool *p = 0;
	return (int)(sizeof(DodGruntPool) - sizeof(p->cap) - sizeof(p->rng) - sizeof(p->look_cursor) - sizeof(p->stats) -
		sizeof(p->paths)) / DOD_GRUNT_MAX;
}

int Dod_GruntTargetCount(int requested, int edicts_free)
{
	int room = edicts_free - DOD_GRUNT_EDICT_RESERVE;
	int n = requested;
	if (n > DOD_GRUNT_MAX)
		n = DOD_GRUNT_MAX;
	if (n > room)
		n = room;
	return n < 0 ? 0 : n;
}

void DodGrunt_Init(DodGruntPool *p, unsigned seed)
{
	memset(p, 0, sizeof(*p));
	p->rng = seed ? seed : 0x9E3779B9u;
}

float DodGrunt_Rand(DodGruntPool *p)
{
	unsigned x = p->rng;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	p->rng = x;
	return (float)(x >> 8) / 16777216.0f;
}

float DodGrunt_YawTo(float x, float y, float tx, float ty)
{
	return atan2f(ty - y, tx - x) * 57.29578f;
}

void DodGrunt_Spawn(DodGruntPool *p, int i, int team, float x, float y, float z, float yaw, float now, int rush)
{
	p->x[i] = x;
	p->y[i] = y;
	p->z[i] = z;
	p->yaw[i] = yaw;
	p->heading[i] = yaw;
	p->phase[i] = DodGrunt_Rand(p) * 256.0f;
	/* A fresh look after a random delay, so a wave of spawns does not look on the same frame. */
	p->next_look[i] = now + DodGrunt_Rand(p) * DOD_GRUNT_SENSE_INTERVAL;
	p->next_check[i] = now + DodGrunt_Rand(p) * DOD_GRUNT_MOVE_CHECK;
	p->next_fire[i] = now + 0.5f + DodGrunt_Rand(p);
	p->turn_at[i] = now + 2.0f + DodGrunt_Rand(p) * 3.0f;
	p->respawn_at[i] = 0.0f;
	p->target_seen[i] = -1000.0f;
	p->hp[i] = DOD_GRUNT_HEALTH;
	p->target[i] = 0;
	p->team[i] = (unsigned char)team;
	p->blocked[i] = 0;
	p->script[i] = 0;
	p->order[i] = DOD_GRUNT_ORDER_NONE;
	p->order_done[i] = 0;
	p->state[i] = rush ? DOD_GRUNT_ADVANCE : DOD_GRUNT_WANDER;
	if (i >= p->cap)
		p->cap = i + 1;
}

static int alive(const DodGruntPool *p, int i)
{
	return p->state[i] >= DOD_GRUNT_WANDER;
}

int DodGrunt_DueLooks(DodGruntPool *p, float now, int budget, int *out)
{
	int n = 0, k;
	for (k = 0; k < p->cap; k++) {
		const int i = (p->look_cursor + k) % p->cap;
		if (!alive(p, i) || p->next_look[i] > now)
			continue;
		if (n >= budget) {
			p->stats.deferred++;
			continue;
		}
		out[n++] = i;
	}
	if (n > 0)
		p->look_cursor = (out[n - 1] + 1) % p->cap;
	return n;
}

void DodGrunt_Looked(DodGruntPool *p, int i, float now, int visible, int target, float tx, float ty, float dist, int rush)
{
	p->stats.looks++;
	/* +-25% jitter keeps the looks spread out over time. */
	p->next_look[i] = now + DOD_GRUNT_SENSE_INTERVAL * (0.75f + 0.5f * DodGrunt_Rand(p));
	if (p->script[i]) {
		/* A script actor: off means blind, a move order wins over fighting, and it holds instead of wandering. */
		const int attack = (p->script[i] & DOD_GRUNT_ATTACK) != 0;
		if ((p->script[i] & DOD_GRUNT_AI_OFF) || p->order[i] != DOD_GRUNT_ORDER_NONE) {
			p->target[i] = 0;
			return;
		}
		if (visible) {
			p->target[i] = (signed char)target;
			p->target_x[i] = tx;
			p->target_y[i] = ty;
			p->target_seen[i] = now;
			p->state[i] = (dist <= DOD_GRUNT_CLOSE * 3.0f) ? DOD_GRUNT_SHOOT : (attack ? DOD_GRUNT_ADVANCE : DOD_GRUNT_HOLD);
			if (p->state[i] == DOD_GRUNT_HOLD)
				p->heading[i] = DodGrunt_YawTo(p->x[i], p->y[i], tx, ty);
			return;
		}
		p->target[i] = 0;
		if (p->state[i] == DOD_GRUNT_SHOOT)
			p->state[i] = attack ? DOD_GRUNT_ADVANCE : DOD_GRUNT_HOLD;
		return;
	}
	if (visible) {
		p->target[i] = (signed char)target;
		p->target_x[i] = tx;
		p->target_y[i] = ty;
		p->target_seen[i] = now;
		p->state[i] = (dist <= DOD_GRUNT_CLOSE * 3.0f) ? DOD_GRUNT_SHOOT : (rush ? DOD_GRUNT_ADVANCE : DOD_GRUNT_WANDER);
		return;
	}
	p->target[i] = 0;
	if (p->state[i] == DOD_GRUNT_SHOOT)
		p->state[i] = rush ? DOD_GRUNT_ADVANCE : DOD_GRUNT_WANDER;
}

/* A path node reached: head for the next one, or for the goal from the last. */
static void next_leg(DodGruntPool *p, int i)
{
	const int n = p->path_node[i];
	int h = n == p->goal_node[i] || !p->paths ? -1 : DodPaths_NextHop(p->paths, n, p->goal_node[i]);
	if (h < 0) {
		p->path_node[i] = DOD_PATH_NONE;
		p->order_x[i] = p->goal_x[i];
		p->order_y[i] = p->goal_y[i];
	} else {
		p->path_node[i] = (unsigned short)h;
		p->order_x[i] = p->paths->pos[h][0];
		p->order_y[i] = p->paths->pos[h][1];
	}
	p->blocked[i] = 0;
	p->heading[i] = DodGrunt_YawTo(p->x[i], p->y[i], p->order_x[i], p->order_y[i]);
}

void DodGrunt_Move(DodGruntPool *p, int i, float now, float dt)
{
	float speed = 0.0f, want_yaw = p->yaw[i], d;
	const int remembers = now - p->target_seen[i] < DOD_GRUNT_MEMORY;
	if (!alive(p, i))
		return;
	if (p->order[i] != DOD_GRUNT_ORDER_NONE) {
		const float dx = p->order_x[i] - p->x[i], dy = p->order_y[i] - p->y[i];
		if (dx * dx + dy * dy <= DOD_GRUNT_ARRIVE * DOD_GRUNT_ARRIVE && p->path_node[i] != DOD_PATH_NONE) {
			next_leg(p, i);
			want_yaw = p->heading[i];
			goto turn_and_step;
		}
		if (dx * dx + dy * dy <= DOD_GRUNT_ARRIVE * DOD_GRUNT_ARRIVE) {
			p->order[i] = DOD_GRUNT_ORDER_NONE;
			p->order_done[i] = 1;
			p->state[i] = DOD_GRUNT_HOLD;
		} else {
			/* a blocked check turned the heading away; the next unblocked frame aims at the point again */
			if (!p->blocked[i])
				p->heading[i] = DodGrunt_YawTo(p->x[i], p->y[i], p->order_x[i], p->order_y[i]);
			speed = p->order[i] == DOD_GRUNT_ORDER_RUN ? DOD_GRUNT_RUN : DOD_GRUNT_WALK;
			p->state[i] = p->order[i] == DOD_GRUNT_ORDER_RUN ? DOD_GRUNT_ADVANCE : DOD_GRUNT_WANDER;
			/* never step past the point in one frame */
			if (speed * dt > sqrtf(dx * dx + dy * dy))
				speed = sqrtf(dx * dx + dy * dy) / (dt > 0.0f ? dt : 1.0f);
		}
		want_yaw = p->heading[i];
		goto turn_and_step;
	}
	switch (p->state[i]) {
	case DOD_GRUNT_HOLD:
		want_yaw = p->heading[i];
		break;
	case DOD_GRUNT_SHOOT:
		if (remembers)
			want_yaw = DodGrunt_YawTo(p->x[i], p->y[i], p->target_x[i], p->target_y[i]);
		break;
	case DOD_GRUNT_ADVANCE:
		if (remembers) {
			const float dx = p->target_x[i] - p->x[i], dy = p->target_y[i] - p->y[i];
			if (dx * dx + dy * dy > DOD_GRUNT_CLOSE * DOD_GRUNT_CLOSE) {
				p->heading[i] = DodGrunt_YawTo(p->x[i], p->y[i], p->target_x[i], p->target_y[i]);
				speed = DOD_GRUNT_RUN;
			}
		} else
			speed = DOD_GRUNT_RUN;
		want_yaw = p->heading[i];
		break;
	default:
		if (now >= p->turn_at[i]) {
			p->heading[i] = wrap180(p->heading[i] + (DodGrunt_Rand(p) - 0.5f) * 140.0f);
			p->turn_at[i] = now + 2.0f + DodGrunt_Rand(p) * 4.0f;
		}
		/* Stand still part of the time: a grunt that never stops looks like a conveyor belt. */
		speed = DOD_GRUNT_WALK;
		want_yaw = p->heading[i];
		break;
	}
turn_and_step:
	if (p->blocked[i])
		speed = 0.0f;
	/* The body turns at up to 360 degrees per second. */
	d = wrap180(want_yaw - p->yaw[i]);
	{
		const float step = 360.0f * dt;
		p->yaw[i] = wrap180(p->yaw[i] + (d > step ? step : (d < -step ? -step : d)));
	}
	if (speed > 0.0f) {
		const float r = p->heading[i] * 0.017453293f;
		p->x[i] += cosf(r) * speed * dt;
		p->y[i] += sinf(r) * speed * dt;
		/* Walk cycle: about one stride per 40 units run, 28 walk, as a fraction of 256 frames. */
		p->phase[i] += speed * dt * (speed > 100.0f ? 3.2f : 4.6f);
		while (p->phase[i] >= 256.0f)
			p->phase[i] -= 256.0f;
	} else {
		p->phase[i] += dt * 14.0f;
		while (p->phase[i] >= 256.0f)
			p->phase[i] -= 256.0f;
	}
}

int DodGrunt_CheckDue(DodGruntPool *p, int i, float now)
{
	if (!alive(p, i) || p->next_check[i] > now)
		return 0;
	p->next_check[i] = now + DOD_GRUNT_MOVE_CHECK;
	/* Only a grunt that is trying to walk needs the probe. */
	return p->state[i] == DOD_GRUNT_WANDER || p->state[i] == DOD_GRUNT_ADVANCE;
}

float DodGrunt_ProbeDistance(const DodGruntPool *p, int i)
{
	return (p->state[i] == DOD_GRUNT_ADVANCE ? DOD_GRUNT_RUN : DOD_GRUNT_WALK) * DOD_GRUNT_MOVE_CHECK * 2.0f + 24.0f;
}

void DodGrunt_MoveChecked(DodGruntPool *p, int i, int clear)
{
	p->stats.move_traces++;
	if (clear) {
		p->blocked[i] = 0;
		return;
	}
	/* Stopped: turn away so the next check finds a way. Reads as the grunt backing off a wall. */
	{
		const float turn = 60.0f + DodGrunt_Rand(p) * 120.0f;
		p->heading[i] = wrap180(p->heading[i] + (DodGrunt_Rand(p) < 0.5f ? -turn : turn));
	}
	p->blocked[i] = 1;
	/* Clear the stop at the next check, which probes the new heading. */
}

int DodGrunt_ShotDue(DodGruntPool *p, int i, float now, int skill)
{
	float gap;
	if (p->state[i] != DOD_GRUNT_SHOOT || p->target[i] == 0 || p->next_fire[i] > now)
		return 0;
	if (skill < 0)
		skill = 0;
	if (skill > 3)
		skill = 3;
	gap = (1.0f + DodGrunt_Rand(p)) * (1.0f - 0.12f * (float)skill);
	p->next_fire[i] = now + gap;
	p->stats.shots++;
	return 1;
}

float DodGrunt_HitChance(float dist, int skill)
{
	float c;
	if (skill < 0)
		skill = 0;
	if (skill > 3)
		skill = 3;
	c = 0.70f - dist / 1800.0f * 0.6f + 0.08f * (float)skill;
	return c < 0.05f ? 0.05f : (c > 0.95f ? 0.95f : c);
}

int DodGrunt_ShotHits(float dist, int skill, float roll)
{
	return roll < DodGrunt_HitChance(dist, skill);
}

int DodGrunt_Hurt(DodGruntPool *p, int i, int damage, float now)
{
	if (!alive(p, i))
		return 0;
	p->hp[i] = (short)(p->hp[i] - damage);
	if (p->hp[i] > 0)
		return 0;
	p->hp[i] = 0;
	p->state[i] = DOD_GRUNT_DEAD;
	p->target[i] = 0;
	p->order[i] = DOD_GRUNT_ORDER_NONE;
	/* a script actor stays dead: the mission counts its kills */
	p->respawn_at[i] = p->script[i] ? 1e30f : now + DOD_GRUNT_RESPAWN;
	p->stats.deaths++;
	return 1;
}

int DodGrunt_RespawnDue(const DodGruntPool *p, int i, float now)
{
	return p->state[i] == DOD_GRUNT_DEAD && p->respawn_at[i] <= now;
}

/* ---- script control ---- */

int DodGrunt_FreeScriptSlot(const DodGruntPool *p)
{
	int i;
	for (i = DOD_GRUNT_MAX - 1; i >= 0; i--)
		if (p->state[i] == DOD_GRUNT_FREE)
			return i;
	return -1;
}

void DodGrunt_SpawnScripted(DodGruntPool *p, int i, int team, float x, float y, float z, float yaw, float now)
{
	DodGrunt_Spawn(p, i, team, x, y, z, yaw, now, 0);
	p->script[i] = DOD_GRUNT_SCRIPTED;
	p->state[i] = DOD_GRUNT_HOLD;
}

void DodGrunt_SetAI(DodGruntPool *p, int i, int on)
{
	if (on) {
		p->script[i] &= (unsigned char)~DOD_GRUNT_AI_OFF;
		return;
	}
	p->script[i] |= DOD_GRUNT_AI_OFF;
	p->script[i] &= (unsigned char)~DOD_GRUNT_ATTACK;
	p->target[i] = 0;
	if (alive(p, i) && p->order[i] == DOD_GRUNT_ORDER_NONE)
		p->state[i] = DOD_GRUNT_HOLD;
}

void DodGrunt_SetAttack(DodGruntPool *p, int i, int on)
{
	if (on) {
		p->script[i] |= DOD_GRUNT_ATTACK;
		p->script[i] &= (unsigned char)~DOD_GRUNT_AI_OFF;
	} else
		p->script[i] &= (unsigned char)~DOD_GRUNT_ATTACK;
}

void DodGrunt_OrderMove(DodGruntPool *p, int i, float x, float y, float z, int run)
{
	if (!alive(p, i))
		return;
	p->order[i] = (unsigned char)(run ? DOD_GRUNT_ORDER_RUN : DOD_GRUNT_ORDER_WALK);
	p->order_x[i] = p->goal_x[i] = x;
	p->order_y[i] = p->goal_y[i] = y;
	p->path_node[i] = p->goal_node[i] = DOD_PATH_NONE;
	if (p->paths && p->paths->count > 1) {
		const float here[3] = { p->x[i], p->y[i], p->z[i] - 36.0f }, there[3] = { x, y, z };
		const int s = DodPaths_Nearest(p->paths, here), g = DodPaths_Nearest(p->paths, there);
		const float *sp = p->paths->pos[s];
		const float to_goal = (x - here[0]) * (x - here[0]) + (y - here[1]) * (y - here[1]);
		const float to_node = (sp[0] - here[0]) * (sp[0] - here[0]) + (sp[1] - here[1]) * (sp[1] - here[1]);
		/* nodes only when the goal is past the first node and the two ends are joined */
		if (s != g && to_goal > to_node && DodPaths_NextHop(p->paths, s, g) >= 0) {
			p->path_node[i] = (unsigned short)s;
			p->goal_node[i] = (unsigned short)g;
			p->order_x[i] = sp[0];
			p->order_y[i] = sp[1];
		}
	}
	p->order_done[i] = 0;
	p->target[i] = 0;
	p->blocked[i] = 0;
	p->state[i] = run ? DOD_GRUNT_ADVANCE : DOD_GRUNT_WANDER;
	p->heading[i] = DodGrunt_YawTo(p->x[i], p->y[i], p->order_x[i], p->order_y[i]);
}

void DodGrunt_OrderStop(DodGruntPool *p, int i)
{
	p->order[i] = DOD_GRUNT_ORDER_NONE;
	if (alive(p, i))
		p->state[i] = DOD_GRUNT_HOLD;
}

void DodGrunt_Face(DodGruntPool *p, int i, float tx, float ty)
{
	if (p->order[i] == DOD_GRUNT_ORDER_NONE)
		p->heading[i] = DodGrunt_YawTo(p->x[i], p->y[i], tx, ty);
}

int DodGrunt_TakeOrderDone(DodGruntPool *p, int i)
{
	if (!p->order_done[i])
		return 0;
	p->order_done[i] = 0;
	return 1;
}

/* ---- path nodes ---- */

uint32_t DodPaths_Hash(const char *name)
{
	uint32_t h = 2166136261u;
	for (; *name; name++) {
		unsigned char c = (unsigned char)*name;
		if (c >= 'A' && c <= 'Z')
			c = (unsigned char)(c - 'A' + 'a');
		h = (h ^ c) * 16777619u;
	}
	return h ? h : 1;
}

void DodPaths_Free(DodPaths *g)
{
	free(g->block);
	memset(g, 0, sizeof(*g));
}

/* the next line of text into buf (cut to fit), the cursor moved past it; 0 at the end */
static int next_line(const char **at, const char *end, char *buf, size_t size)
{
	const char *s = *at;
	size_t n = 0;
	if (s >= end)
		return 0;
	while (s < end && *s != '\n' && *s != '\0') {
		if (n + 1 < size)
			buf[n++] = *s;
		s++;
	}
	buf[n] = '\0';
	*at = s < end ? s + 1 : end;
	if (s < end && *s == '\0')
		*at = end;
	return 1;
}

int DodPaths_Load(DodPaths *g, const char *text, size_t len)
{
	const char *end = text + len, *at = text;
	char line[160], name[96];
	float v[3];
	int n = 0, k = 0;
	memset(g, 0, sizeof(*g));
	while (next_line(&at, end, line, sizeof(line)))
		if (line[0] != '#' && sscanf(line, "%f %f %f", &v[0], &v[1], &v[2]) == 3)
			n++;
	if (n > DOD_PATH_MAX)
		n = DOD_PATH_MAX;
	if (n <= 0)
		return 0;
	{
		const size_t pos = sizeof(float) * 3 * (size_t)n, nm = sizeof(uint32_t) * (size_t)n;
		const size_t lk = sizeof(unsigned short) * DOD_PATH_LINKS * (size_t)n, cost = sizeof(float) * (size_t)n;
		char *b = (char *)malloc(pos + nm + cost + lk + sizeof(unsigned short) * 3 * (size_t)n);
		if (!b)
			return 0;
		g->block = b;
		g->pos = (float (*)[3])b;
		g->name = (uint32_t *)(b + pos);
		g->cost = (float *)(b + pos + nm);
		g->link = (unsigned short (*)[DOD_PATH_LINKS])(b + pos + nm + cost);
		g->from = (unsigned short *)(b + pos + nm + cost + lk);
		g->heap = g->from + n;
		g->slot = g->heap + n;
		memset(g->link, 0xFF, lk);
	}
	at = text;
	while (k < n && next_line(&at, end, line, sizeof(line))) {
		int grav, flags;
		if (line[0] == '#' || sscanf(line, "%f %f %f", &v[0], &v[1], &v[2]) != 3)
			continue;
		memcpy(g->pos[k], v, sizeof(v));
		g->name[k] = 0;
		if (sscanf(line, "%*f %*f %*f %d %d %95s", &grav, &flags, name) == 3 && strcmp(name, "-"))
			g->name[k] = DodPaths_Hash(name[0] == '$' ? name + 1 : name);
		k++;
	}
	g->count = k;
	return k;
}

static float dist2(const float a[3], const float b[3])
{
	const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
	return dx * dx + dy * dy + dz * dz;
}

static int links(const DodPaths *g, int i)
{
	int n = 0;
	while (n < DOD_PATH_LINKS && g->link[i][n] != DOD_PATH_NONE)
		n++;
	return n;
}

static int linked(const DodPaths *g, int i, int j)
{
	int k;
	for (k = 0; k < DOD_PATH_LINKS && g->link[i][k] != DOD_PATH_NONE; k++)
		if (g->link[i][k] == j)
			return 1;
	return 0;
}

int DodPaths_Link(DodPaths *g, DodPathWalkable walkable, void *ctx, float reach)
{
	int traces = 0, i, j, k;
	for (i = 0; i < g->count; i++) {
		int cand[DOD_PATH_TRIES], nc = 0;
		float cd[DOD_PATH_TRIES];
		/* the nearest few inside reach, nearest first */
		for (j = 0; j < g->count; j++) {
			const float d = dist2(g->pos[i], g->pos[j]);
			if (j == i || d > reach * reach)
				continue;
			if (nc == DOD_PATH_TRIES && d >= cd[nc - 1])
				continue;
			k = nc < DOD_PATH_TRIES ? nc++ : nc - 1;
			while (k > 0 && cd[k - 1] > d) {
				cd[k] = cd[k - 1];
				cand[k] = cand[k - 1];
				k--;
			}
			cd[k] = d;
			cand[k] = j;
		}
		for (k = 0; k < nc && links(g, i) < DOD_PATH_LINKS; k++) {
			j = cand[k];
			if (linked(g, i, j) || links(g, j) >= DOD_PATH_LINKS)
				continue;
			traces++;
			if (!walkable(ctx, g->pos[i], g->pos[j]))
				continue;
			g->link[i][links(g, i)] = (unsigned short)j;
			g->link[j][links(g, j)] = (unsigned short)i;
		}
	}
	return traces;
}

int DodPaths_Find(const DodPaths *g, const char *name)
{
	const uint32_t h = DodPaths_Hash(name[0] == '$' ? name + 1 : name);
	int i;
	for (i = 0; i < g->count; i++)
		if (g->name[i] == h)
			return i;
	return -1;
}

int DodPaths_Nearest(const DodPaths *g, const float p[3])
{
	int best = -1, i;
	float bd = 0.0f;
	for (i = 0; i < g->count; i++) {
		const float dx = g->pos[i][0] - p[0], dy = g->pos[i][1] - p[1], dz = (g->pos[i][2] - p[2]) * 2.0f;
		const float d = dx * dx + dy * dy + dz * dz;
		if (best < 0 || d < bd) {
			best = i;
			bd = d;
		}
	}
	return best;
}

/* the search's open list: a binary heap on cost + straight-line distance to the target, with each node's heap slot */
static float heap_key(const DodPaths *g, int n, int target)
{
	return g->cost[n] + sqrtf(dist2(g->pos[n], g->pos[target]));
}

static void heap_put(DodPaths *g, int at, int n)
{
	g->heap[at] = (unsigned short)n;
	g->slot[n] = (unsigned short)at;
}

static void heap_up(DodPaths *g, int at, int target)
{
	const int n = g->heap[at];
	const float key = heap_key(g, n, target);
	while (at > 0 && heap_key(g, g->heap[(at - 1) / 2], target) > key) {
		heap_put(g, at, g->heap[(at - 1) / 2]);
		at = (at - 1) / 2;
	}
	heap_put(g, at, n);
}

static void heap_down(DodPaths *g, int size, int target)
{
	int at = 0;
	const int n = g->heap[0];
	const float key = heap_key(g, n, target);
	for (;;) {
		int c = at * 2 + 1;
		if (c >= size)
			break;
		if (c + 1 < size && heap_key(g, g->heap[c + 1], target) < heap_key(g, g->heap[c], target))
			c++;
		if (heap_key(g, g->heap[c], target) >= key)
			break;
		heap_put(g, at, g->heap[c]);
		at = c;
	}
	heap_put(g, at, n);
}

#define PATH_CLOSED 0xFFFE

int DodPaths_NextHop(DodPaths *g, int from, int to)
{
	int size = 0, i;
	if (from < 0 || to < 0 || from >= g->count || to >= g->count)
		return -1;
	if (from == to)
		return to;
	/* searched from the goal back to the walker: each node's from[] is then its next step toward the goal */
	for (i = 0; i < g->count; i++)
		g->slot[i] = DOD_PATH_NONE;	/* unseen */
	g->cost[to] = 0.0f;
	g->from[to] = DOD_PATH_NONE;
	heap_put(g, size++, to);
	while (size > 0) {
		const int n = g->heap[0];
		g->slot[n] = PATH_CLOSED;
		if (n == from)
			return g->from[from];
		if (--size > 0) {
			heap_put(g, 0, g->heap[size]);
			heap_down(g, size, from);
		}
		for (i = 0; i < DOD_PATH_LINKS && g->link[n][i] != DOD_PATH_NONE; i++) {
			const int m = g->link[n][i];
			const float c = g->cost[n] + sqrtf(dist2(g->pos[n], g->pos[m]));
			if (g->slot[m] == PATH_CLOSED)
				continue;
			if (g->slot[m] == DOD_PATH_NONE) {
				g->cost[m] = c;
				g->from[m] = (unsigned short)n;
				heap_put(g, size, m);
				heap_up(g, size++, from);
			} else if (c < g->cost[m]) {
				g->cost[m] = c;
				g->from[m] = (unsigned short)n;
				heap_up(g, g->slot[m], from);
			}
		}
	}
	return -1;
}
