/* Day of Defeat bot planning and decisions; see dod_bots.h. */
#include "dod_bots.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

int Dod_BotTargetCount(int requested, int max_clients, int humans, int hard_cap)
{
	int free_slots, count;
	if (requested <= 0 || max_clients <= 0 || hard_cap <= 0)
		return 0;
	if (humans < 0)
		humans = 0;
	free_slots = max_clients - humans;
	if (free_slots <= 0)
		return 0;
	count = requested;
	if (count > free_slots)
		count = free_slots;
	if (count > hard_cap)
		count = hard_cap;
	return count;
}

int Dod_BotRequestedCount(int requested, int network_up, int allowed_online)
{
	if (requested <= 0)
		return 0;
	return network_up && !allowed_online ? 0 : requested;
}

DodTeam Dod_BotPickTeam(int allies, int axis)
{
	return allies < axis ? DOD_TEAM_ALLIES : DOD_TEAM_AXIS;
}

DodTeam Dod_BotPickTeamBalanced(int allies, int axis, int allies_bots, int axis_bots)
{
	/* Fewer players first. On a tie put the bot on the side with fewer bots, so
	 * the bots end up opposing each other instead of stacking on one side. */
	if (allies != axis)
		return allies < axis ? DOD_TEAM_ALLIES : DOD_TEAM_AXIS;
	if (allies_bots != axis_bots)
		return allies_bots < axis_bots ? DOD_TEAM_ALLIES : DOD_TEAM_AXIS;
	return DOD_TEAM_AXIS;
}

DodClass Dod_BotPickClass(int roll, const int special_counts[DOD_CLASS_COUNT])
{
	/* Weighted draw: rifleman 4, assault 3, support 2, sniper 1, mg 1, rocket 1. */
	static const struct { DodClass cls; int weight; } table[] = {
		{ DOD_CLASS_RIFLEMAN, 4 }, { DOD_CLASS_ASSAULT, 3 }, { DOD_CLASS_SUPPORT, 2 },
		{ DOD_CLASS_SNIPER, 1 }, { DOD_CLASS_MG, 1 }, { DOD_CLASS_ROCKET, 1 }
	};
	int total = 0, i, pick;
	for (i = 0; i < 6; i++) {
		const int special = table[i].cls == DOD_CLASS_SNIPER || table[i].cls == DOD_CLASS_MG ||
			table[i].cls == DOD_CLASS_ROCKET;
		if (special && special_counts && special_counts[table[i].cls] > 0)
			continue;
		total += table[i].weight;
	}
	if (roll < 0)
		roll = -roll;
	pick = roll % total;
	for (i = 0; i < 6; i++) {
		const int special = table[i].cls == DOD_CLASS_SNIPER || table[i].cls == DOD_CLASS_MG ||
			table[i].cls == DOD_CLASS_ROCKET;
		if (special && special_counts && special_counts[table[i].cls] > 0)
			continue;
		if (pick < table[i].weight)
			return table[i].cls;
		pick -= table[i].weight;
	}
	return DOD_CLASS_RIFLEMAN;
}

float Dod_BotAngleDelta(float a, float b)
{
	float d = fmodf(b - a, 360.0f);
	if (d >= 180.0f)
		d -= 360.0f;
	else if (d < -180.0f)
		d += 360.0f;
	return d;
}

float Dod_BotTurn(float current, float target, float max_step)
{
	float d = Dod_BotAngleDelta(current, target);
	float out;
	if (max_step < 0.0f)
		max_step = 0.0f;
	if (d > max_step)
		d = max_step;
	else if (d < -max_step)
		d = -max_step;
	out = current + d;
	return Dod_BotAngleDelta(0.0f, out);
}

float Dod_BotAimError(float distance, int skill)
{
	static const float base[4] = { 7.0f, 4.5f, 2.5f, 1.2f };
	static const float per_unit[4] = { 0.0050f, 0.0032f, 0.0018f, 0.0008f };
	if (skill < 0)
		skill = 0;
	if (skill > 3)
		skill = 3;
	if (distance < 0.0f)
		distance = 0.0f;
	return base[skill] + per_unit[skill] * distance;
}

DodBotMove Dod_BotDecide(const DodBotInput *in)
{
	DodBotMove move = { 0.0f, 0.0f, 0, 0, 0, 0 };
	if (!in)
		return move;
	/* Standing off against an enemy beside a wall must still shoot: only a
	 * ledge pre-empts the fight; a low obstacle just gets a hop. */
	if (in->has_target && in->blocked && !in->ledge) {
		move.jump = 1;
	} else if (in->blocked || in->ledge) {
		move.turn_away = 1;
		move.jump = in->blocked && !in->ledge;	/* hop low obstacles, never off a ledge */
		move.forward = in->ledge ? 0.0f : 0.5f;
		return move;
	}
	if (in->clip_empty)
		move.reload = 1;
	if (!in->has_target) {
		move.forward = 1.0f;
		return move;
	}
	/* Engage: keep a fighting distance and strafe on a slow rhythm. */
	if (in->target_distance > DOD_BOT_FAR)
		move.forward = 1.0f;
	else if (in->target_distance < DOD_BOT_CLOSE)
		move.forward = -0.6f;
	else
		move.forward = 0.2f;
	move.side = ((int)(in->time * 0.6f) & 1) ? 0.7f : -0.7f;
	if (fabsf(in->aim_error) <= DOD_BOT_FIRE_CONE && !in->clip_empty) {
		/* A semi-auto trigger must be released between shots. */
		move.fire = in->semi_auto ? (in->fire_phase & 1) : 1;
	}
	return move;
}

DodBotEscape Dod_BotStuckUpdate(DodBotStuck *s, float x, float y, float time, int wants_move, float roll)
{
	DodBotEscape esc = { 0, 0.0f, 0.0f, 0 };
	float dx, dy;
	if (!s)
		return esc;
	if (!s->primed || time < s->next_check - 10.0f * DOD_BOT_STUCK_WINDOW) {
		/* first sample, or the level time restarted */
		s->primed = 1;
		s->x = x;
		s->y = y;
		s->next_check = time + DOD_BOT_STUCK_WINDOW;
		s->failures = 0;
		return esc;
	}
	if (time < s->next_check)
		return esc;
	dx = x - s->x;
	dy = y - s->y;
	s->x = x;
	s->y = y;
	s->next_check = time + DOD_BOT_STUCK_WINDOW;
	if (!wants_move || dx * dx + dy * dy >= DOD_BOT_STUCK_MIN * DOD_BOT_STUCK_MIN) {
		s->failures = 0;
		return esc;
	}
	if (s->failures < 6)
		s->failures++;
	esc.stuck = 1;
	/* 90, 135, 180 degrees and on: first try a side step, then turn around. */
	esc.turn = (60.0f + 30.0f * (float)s->failures) * (roll < 0.5f ? 1.0f : -1.0f);
	if (esc.turn > 180.0f) esc.turn = 180.0f;
	if (esc.turn < -180.0f) esc.turn = -180.0f;
	esc.hold = 0.8f + 0.4f * (float)s->failures;
	esc.jump = (s->failures & 1);
	return esc;
}

int Dod_BotChooseObjective(const DodBotObjectiveInfo *list, int count, DodTeam team, int current_id, int *kind)
{
	int i, best = -1, best_kind = DOD_BOT_GOAL_NONE;
	float best_dist = 0.0f;
	if (kind)
		*kind = DOD_BOT_GOAL_NONE;
	if (!list || count <= 0 || (team != DOD_TEAM_ALLIES && team != DOD_TEAM_AXIS))
		return -1;
	/* Attack: keep the current target while it is still one, else the nearest. */
	for (i = 0; i < count; ++i) {
		const DodBotObjectiveInfo *o = &list[i];
		if (o->blocked || o->owner == team || !o->can_cap[team])
			continue;
		if (o->id == current_id) {
			if (kind)
				*kind = DOD_BOT_GOAL_ATTACK;
			return o->id;
		}
		if (best < 0 || o->distance < best_dist) {
			best = o->id;
			best_dist = o->distance;
			best_kind = DOD_BOT_GOAL_ATTACK;
		}
	}
	if (best >= 0) {
		if (kind)
			*kind = best_kind;
		return best;
	}
	/* Defend: a point being captured by the enemy first, then the current one, then the nearest. */
	{
		int under_attack = -1, nearest = -1, keep = -1;
		float under_dist = 0.0f, near_dist = 0.0f;
		for (i = 0; i < count; ++i) {
			const DodBotObjectiveInfo *o = &list[i];
			if (o->blocked || o->owner != team)
				continue;
			if (o->capturing_team != DOD_TEAM_UNASSIGNED && o->capturing_team != team &&
				(under_attack < 0 || o->distance < under_dist)) {
				under_attack = o->id;
				under_dist = o->distance;
			}
			if (o->id == current_id)
				keep = o->id;
			if (nearest < 0 || o->distance < near_dist) {
				nearest = o->id;
				near_dist = o->distance;
			}
		}
		best = under_attack >= 0 ? under_attack : (keep >= 0 ? keep : nearest);
	}
	if (best >= 0 && kind)
		*kind = DOD_BOT_GOAL_DEFEND;
	return best;
}

void Dod_BotBlockAdd(int *ids, float *untils, int slots, int id, float until, float now)
{
	int i, victim = 0;
	if (!ids || !untils || slots <= 0)
		return;
	for (i = 0; i < slots; ++i) {
		if (ids[i] == id) {
			untils[i] = until;
			return;
		}
	}
	/* an expired or empty slot first, else the one that expires soonest */
	for (i = 0; i < slots; ++i) {
		if (untils[i] <= now) {
			victim = i;
			break;
		}
		if (untils[i] < untils[victim])
			victim = i;
	}
	ids[victim] = id;
	untils[victim] = until;
}

int Dod_BotBlockHas(const int *ids, const float *untils, int slots, int id, float now)
{
	int i;
	if (!ids || !untils)
		return 0;
	for (i = 0; i < slots; ++i)
		if (ids[i] == id && untils[i] > now)
			return 1;
	return 0;
}

/* ---------------------------------------------------------------- navigation graph */

static unsigned nav_be16(const unsigned char *p)
{
	return ((unsigned)p[0] << 8) | p[1];
}

static int nav_be16s(const unsigned char *p)
{
	int v = (int)nav_be16(p);
	return v >= 32768 ? v - 65536 : v;
}

int Dod_NavLoad(DodNav *nav, const unsigned char *data, int len)
{
	int i, total = 0;
	if (!nav)
		return 0;
	memset(nav, 0, sizeof(*nav));
	if (!data || len < 20 || memcmp(data, "DWPT", 4) != 0 || nav_be16(data + 4) != 2)
		return 0;
	nav->bsp_size = ((unsigned)data[8] << 24) | ((unsigned)data[9] << 16) | ((unsigned)data[10] << 8) | data[11];
	nav->node_count = (int)nav_be16(data + 12);
	nav->edge_count = (int)nav_be16(data + 14);
	if (nav->node_count < 1 || len < 20 + 7 * nav->node_count + 2 * nav->edge_count)
		return 0;
	nav->nodes = data + 20;
	nav->edges = data + 20 + 7 * nav->node_count;
	nav->first = (unsigned short *)malloc(sizeof(unsigned short) * (size_t)(nav->node_count + 1));
	nav->g = (float *)malloc(sizeof(float) * (size_t)nav->node_count);
	nav->parent = (short *)malloc(sizeof(short) * (size_t)nav->node_count);
	nav->state = (unsigned char *)malloc((size_t)nav->node_count);
	if (!nav->first || !nav->g || !nav->parent || !nav->state) {
		Dod_NavFree(nav);
		return 0;
	}
	for (i = 0; i < nav->node_count; ++i) {
		nav->first[i] = (unsigned short)total;
		total += nav->nodes[7 * i + 6];
	}
	nav->first[nav->node_count] = (unsigned short)total;
	if (total != nav->edge_count) {
		Dod_NavFree(nav);
		return 0;
	}
	for (i = 0; i < nav->edge_count; ++i) {
		if ((int)(nav_be16(nav->edges + 2 * i) & 0x3fff) >= nav->node_count) {
			Dod_NavFree(nav);
			return 0;
		}
	}
	return 1;
}

void Dod_NavFree(DodNav *nav)
{
	if (!nav)
		return;
	free(nav->first);
	free(nav->g);
	free(nav->parent);
	free(nav->state);
	memset(nav, 0, sizeof(*nav));
}

void Dod_NavNodePos(const DodNav *nav, int node, float *x, float *y, float *z)
{
	const unsigned char *n;
	if (!nav || node < 0 || node >= nav->node_count)
		return;
	n = nav->nodes + 7 * node;
	if (x) *x = (float)nav_be16s(n);
	if (y) *y = (float)nav_be16s(n + 2);
	if (z) *z = (float)nav_be16s(n + 4);
}

int Dod_NavEdgeKind(const DodNav *nav, int from, int to)
{
	int e;
	if (!nav || from < 0 || from >= nav->node_count)
		return -1;
	for (e = nav->first[from]; e < nav->first[from + 1]; ++e)
		if ((int)(nav_be16(nav->edges + 2 * e) & 0x3fff) == to)
			return (int)(nav_be16(nav->edges + 2 * e) >> 14);
	return -1;
}

int Dod_NavNearest(const DodNav *nav, float x, float y, float z, int *out, int max)
{
	float best[8];
	int count = 0, i, j;
	if (!nav || !out || max < 1)
		return 0;
	if (max > 8)
		max = 8;
	for (i = 0; i < nav->node_count; ++i) {
		float nx, ny, nz, d;
		Dod_NavNodePos(nav, i, &nx, &ny, &nz);
		d = (nx - x) * (nx - x) + (ny - y) * (ny - y) + 9.0f * (nz - z) * (nz - z);	/* a floor above or below is far */
		if (count == max && d >= best[max - 1])
			continue;
		j = count < max ? count++ : max - 1;
		while (j > 0 && best[j - 1] > d) {
			best[j] = best[j - 1];
			out[j] = out[j - 1];
			--j;
		}
		best[j] = d;
		out[j] = i;
	}
	return count;
}

void Dod_NavSetTime(DodNav *nav, float now)
{
	if (nav)
		nav->now = now;
}

void Dod_NavPenalize(DodNav *nav, int from, int to, float until)
{
	int i;
	if (!nav)
		return;
	for (i = 0; i < 8; ++i)
		if (nav->pen_from[i] == from && nav->pen_to[i] == to && nav->pen_until[i] > 0.0f) {
			nav->pen_until[i] = until;
			return;
		}
	i = nav->pen_next;
	nav->pen_from[i] = from;
	nav->pen_to[i] = to;
	nav->pen_until[i] = until;
	nav->pen_next = (i + 1) & 7;
}

int Dod_NavPenalized(const DodNav *nav, int from, int to)
{
	int i;
	if (!nav)
		return 0;
	for (i = 0; i < 8; ++i)
		if (nav->pen_until[i] > nav->now && nav->pen_from[i] == from && nav->pen_to[i] == to)
			return 1;
	return 0;
}

int Dod_NavPath(DodNav *nav, int from, int to, int *path, int max_path)
{
	int i, e, count, n;
	float tx, ty, tz;
	if (!nav || !path || from < 0 || to < 0 || from >= nav->node_count || to >= nav->node_count || max_path < 1)
		return 0;
	for (i = 0; i < nav->node_count; ++i) {
		nav->state[i] = 0;
		nav->parent[i] = -1;
	}
	Dod_NavNodePos(nav, to, &tx, &ty, &tz);
	nav->g[from] = 0.0f;
	nav->state[from] = 1;
	for (;;) {
		int cur = -1;
		float cur_f = 0.0f;
		for (i = 0; i < nav->node_count; ++i) {
			float x, y, z, h, f;
			if (nav->state[i] != 1)
				continue;
			Dod_NavNodePos(nav, i, &x, &y, &z);
			h = sqrtf((x - tx) * (x - tx) + (y - ty) * (y - ty) + (z - tz) * (z - tz));
			f = nav->g[i] + h;
			if (cur < 0 || f < cur_f) {
				cur = i;
				cur_f = f;
			}
		}
		if (cur < 0)
			return 0;
		if (cur == to)
			break;
		nav->state[cur] = 2;
		for (e = nav->first[cur]; e < nav->first[cur + 1]; ++e) {
			const unsigned v = nav_be16(nav->edges + 2 * e);
			const int next = (int)(v & 0x3fff), kind = (int)(v >> 14);
			float cost, g, cx, cy, cz, nx, ny, nz;
			Dod_NavNodePos(nav, cur, &cx, &cy, &cz);
			Dod_NavNodePos(nav, next, &nx, &ny, &nz);
			cost = sqrtf((nx - cx) * (nx - cx) + (ny - cy) * (ny - cy) + (nz - cz) * (nz - cz)) + 1.0f;
			if (kind == DOD_NAV_JUMP)
				cost += 64.0f;
			else if (kind == DOD_NAV_DROP)
				cost += 16.0f;
			else if (kind == DOD_NAV_LADDER)
				cost = cost * 1.5f + 64.0f;
			if (Dod_NavPenalized(nav, cur, next))
				cost += 1500.0f;
			g = nav->g[cur] + cost;
			if (nav->state[next] == 2 || (nav->state[next] == 1 && g >= nav->g[next]))
				continue;
			nav->g[next] = g;
			nav->parent[next] = (short)cur;
			nav->state[next] = 1;
		}
	}
	count = 0;
	for (n = to; n >= 0; n = nav->parent[n]) {
		if (++count > max_path)
			return 0;
		if (n == from)
			break;
	}
	i = count;
	for (n = to; i > 0; n = nav->parent[n])
		path[--i] = n;
	return count;
}

int Dod_SpecSanitizeMode(int mode)
{
	switch (mode) {
	case DOD_SPEC_CHASE_LOCKED:
	case DOD_SPEC_CHASE_FREE:
	case DOD_SPEC_ROAMING:
	case DOD_SPEC_IN_EYE:
		return mode;
	case DOD_SPEC_MAP_FREE:
	case DOD_SPEC_MAP_CHASE:
		return DOD_SPEC_CHASE_FREE;
	default:
		return DOD_SPEC_IN_EYE;
	}
}

float Dod_SpecInterest(const DodSpecCandidate *c)
{
	float d;
	if (!c || !c->alive)
		return 0.0f;
	if (c->enemy_distance < 0.0f)
		return 1.0f;
	d = c->enemy_distance > 1500.0f ? 1500.0f : c->enemy_distance;
	return 2.0f + (1500.0f - d) / 100.0f;	/* 2 .. 17 */
}

int Dod_SpecCycle(const DodSpecCandidate *list, int count, int current, int reverse)
{
	int start = -1, i, step;
	if (!list || count <= 0)
		return 0;
	for (i = 0; i < count; i++) {
		if (list[i].index == current) {
			start = i;
			break;
		}
	}
	step = reverse ? -1 : 1;
	if (start < 0)
		start = reverse ? 0 : count - 1;	/* so the first step lands on an end */
	for (i = 1; i <= count; i++) {
		const int at = ((start + step * i) % count + count) % count;
		if (list[at].alive && list[at].index != current)
			return list[at].index;
	}
	return 0;
}

int Dod_SpecDirectorPick(const DodSpecCandidate *list, int count, int current, float dwell)
{
	int i, best = 0;
	float best_interest = 0.0f, current_interest = 0.0f;
	if (!list || count <= 0)
		return 0;
	for (i = 0; i < count; i++) {
		const float interest = Dod_SpecInterest(&list[i]);
		if (list[i].index == current)
			current_interest = interest;
		if (interest > best_interest) {
			best_interest = interest;
			best = list[i].index;
		}
	}
	if (best == 0)
		return 0;
	if (current_interest <= 0.0f)
		return best;	/* current is dead or gone */
	if (best == current || dwell < DOD_SPEC_MIN_DWELL)
		return current;
	if (dwell >= DOD_SPEC_MAX_DWELL || best_interest > current_interest * 1.25f)
		return best;
	return current;
}
