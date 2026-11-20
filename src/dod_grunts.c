/* Grunt rules; see dod_grunts.h. */
#include "dod_grunts.h"

#include <math.h>
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
	return (int)(sizeof(DodGruntPool) - sizeof(p->cap) - sizeof(p->rng) - sizeof(p->look_cursor) - sizeof(p->stats)) / DOD_GRUNT_MAX;
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

void DodGrunt_Move(DodGruntPool *p, int i, float now, float dt)
{
	float speed = 0.0f, want_yaw = p->yaw[i], d;
	const int remembers = now - p->target_seen[i] < DOD_GRUNT_MEMORY;
	if (!alive(p, i))
		return;
	switch (p->state[i]) {
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
	p->respawn_at[i] = now + DOD_GRUNT_RESPAWN;
	p->stats.deaths++;
	return 1;
}

int DodGrunt_RespawnDue(const DodGruntPool *p, int i, float now)
{
	return p->state[i] == DOD_GRUNT_DEAD && p->respawn_at[i] <= now;
}
