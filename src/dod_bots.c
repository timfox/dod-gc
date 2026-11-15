/* Day of Defeat bot planning and decisions; see dod_bots.h. */
#include "dod_bots.h"

#include <math.h>

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
