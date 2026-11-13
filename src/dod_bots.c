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
	if (in->blocked || in->ledge) {
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
