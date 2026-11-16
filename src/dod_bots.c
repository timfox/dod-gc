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

float Dod_BotEase(float *angle, float *vel, float target, float max_speed, float accel, float dt)
{
	float delta, want, dv, step;
	if (!angle || !vel || dt <= 0.0f || max_speed <= 0.0f || accel <= 0.0f)
		return angle ? *angle : 0.0f;
	delta = Dod_BotAngleDelta(*angle, target);
	/* the fastest speed that can still brake to zero on the target with the acceleration available, in steps of dt
	 * (the exact discrete sum, not the continuous sqrt(2 a d), which leaves a hundred degrees/s of speed at arrival) */
	want = sqrtf(2.0f * accel * (delta < 0.0f ? -delta : delta) + 0.25f * accel * dt * accel * dt) - 0.5f * accel * dt;
	if (want < 0.0f)
		want = 0.0f;
	if (want > max_speed)
		want = max_speed;
	if (want * dt > (delta < 0.0f ? -delta : delta))
		want = (delta < 0.0f ? -delta : delta) / dt;	/* do not overshoot in this step */
	if (delta < 0.0f)
		want = -want;
	dv = want - *vel;
	if (dv > accel * dt)
		dv = accel * dt;
	else if (dv < -accel * dt)
		dv = -accel * dt;
	*vel += dv;
	step = *vel * dt;
	if ((delta >= 0.0f && step >= delta) || (delta < 0.0f && step <= delta)) {
		*angle = Dod_BotAngleDelta(0.0f, target);	/* arrived */
		*vel = 0.0f;
	} else {
		*angle = Dod_BotAngleDelta(0.0f, *angle + step);
	}
	return *angle;
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

/* ---------------------------------------------------------------- perception and aim */

DodBotSkill Dod_BotSkillParams(int skill)
{
	static const DodBotSkill table[4] = {
		/* fov  react       aim: base dist tspd own   settle turn  hear  mem  recoil */
		{ 45.0f, 0.40f, 0.70f, 4.5f, 4.0f, 3.0f, 2.5f, 0.35f, 160.0f, 0.70f, 4.0f, 7.0f, 900.0f },	/* easy */
		{ 50.0f, 0.22f, 0.40f, 2.0f, 2.2f, 2.0f, 1.5f, 0.60f, 270.0f, 1.00f, 6.0f, 5.0f, 1800.0f },	/* medium */
		{ 55.0f, 0.16f, 0.30f, 1.4f, 1.8f, 1.2f, 0.9f, 0.90f, 360.0f, 1.20f, 8.0f, 3.5f, 2600.0f },	/* hard */
		{ 60.0f, 0.15f, 0.25f, 0.8f, 1.1f, 0.7f, 0.5f, 1.30f, 450.0f, 1.40f, 10.0f, 2.0f, 3500.0f }	/* expert */
	};
	if (skill < 0)
		skill = 0;
	if (skill > 3)
		skill = 3;
	return table[skill];
}

int Dod_BotInFov(float view_yaw, float to_yaw, float half_angle)
{
	float d = Dod_BotAngleDelta(view_yaw, to_yaw);
	return (d < 0.0f ? -d : d) <= half_angle;
}

float Dod_BotReactionDelay(const DodBotSkill *sk, float roll)
{
	if (!sk)
		return 0.3f;
	if (roll < 0.0f)
		roll = 0.0f;
	if (roll > 1.0f)
		roll = 1.0f;
	return sk->react_min + (sk->react_max - sk->react_min) * roll;
}

float Dod_BotAimErrorEx(const DodBotSkill *sk, float distance, float target_speed, float own_speed, float track_time)
{
	float err, settle;
	if (!sk)
		return 10.0f;
	if (distance < 0.0f)
		distance = 0.0f;
	if (target_speed < 0.0f)
		target_speed = 0.0f;
	if (own_speed < 0.0f)
		own_speed = 0.0f;
	if (track_time < 0.0f)
		track_time = 0.0f;
	err = sk->aim_base + sk->aim_dist * (distance / 500.0f) + sk->aim_target_speed * (target_speed / 200.0f) +
		sk->aim_own_speed * (own_speed / 200.0f);
	settle = 1.0f / (1.0f + sk->settle * track_time);
	if (settle < DOD_BOT_SETTLE_FLOOR)
		settle = DOD_BOT_SETTLE_FLOOR;
	return err * settle;
}

float Dod_BotHearRadius(const DodBotSkill *sk, int gunfire, float source_speed, int has_los)
{
	float r = 0.0f;
	if (!sk)
		return 0.0f;
	if (gunfire)
		r = DOD_BOT_HEAR_GUNFIRE;
	else if (source_speed > 150.0f)	/* running; a walking or crouched player is quiet */
		r = DOD_BOT_HEAR_STEPS * (has_los ? 1.0f : 0.6f);
	return r * sk->hear_scale;
}

void Dod_BotHeardOffset(float distance, float roll_a, float roll_b, float *dx, float *dy)
{
	const float err = 48.0f + 0.12f * (distance < 0.0f ? 0.0f : distance);
	const float mag = err * (0.4f + 0.6f * roll_b);
	const float ang = roll_a * 6.2831853f;
	if (dx)
		*dx = mag * cosf(ang);
	if (dy)
		*dy = mag * sinf(ang);
}

float Dod_BotBurstLength(int automatic, float distance)
{
	if (!automatic)
		return 1.4f;
	if (distance < 300.0f)
		return 0.9f;
	if (distance < 700.0f)
		return 0.5f;
	return 0.28f;
}

float Dod_BotBurstPause(int automatic, float distance, float roll)
{
	float lo, hi;
	if (roll < 0.0f)
		roll = 0.0f;
	if (roll > 1.0f)
		roll = 1.0f;
	if (!automatic) {
		lo = 0.15f;
		hi = 0.35f;
	} else if (distance < 300.0f) {
		lo = 0.15f;
		hi = 0.35f;
	} else if (distance < 700.0f) {
		lo = 0.25f;
		hi = 0.55f;
	} else {
		lo = 0.4f;
		hi = 0.8f;
	}
	return lo + (hi - lo) * roll;
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

/* Attack target: keep the current one while it is still valid, else the nearest (crowd-adjusted) uncaptured. */
static int choose_attack(const DodBotObjectiveInfo *list, int count, DodTeam team, int current_id, int use_crowd)
{
	int i, best = -1;
	float best_dist = 0.0f;
	for (i = 0; i < count; ++i) {
		const DodBotObjectiveInfo *o = &list[i];
		float d;
		if (o->blocked || o->owner == team || !o->can_cap[team])
			continue;
		if (o->id == current_id)
			return o->id;
		d = o->distance;
		if (use_crowd) {
			/* a point that needs more than one player: a group forming there is joined (each teammate already
			 * heading there makes it nearer, until it has enough); otherwise teammates spread out */
			if (o->need > 1 && o->crowd > 0 && o->crowd < o->need)
				d -= DOD_BOT_CROWD_PENALTY * (float)o->crowd;
			else
				d += DOD_BOT_CROWD_PENALTY * (float)o->crowd;
		}
		if (best < 0 || d < best_dist) {
			best = o->id;
			best_dist = d;
		}
	}
	return best;
}

/* Defend target: a point being captured by the enemy first, then the current one, then the nearest held. */
static int choose_defend(const DodBotObjectiveInfo *list, int count, DodTeam team, int current_id)
{
	int i, under_attack = -1, nearest = -1, keep = -1;
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
	return under_attack >= 0 ? under_attack : (keep >= 0 ? keep : nearest);
}

int Dod_BotChooseObjective(const DodBotObjectiveInfo *list, int count, DodTeam team, int current_id, int *kind)
{
	return Dod_BotChooseObjectiveRole(list, count, team, current_id, DOD_ROLE_ATTACKER, kind);
}

int Dod_BotChooseObjectiveRole(const DodBotObjectiveInfo *list, int count, DodTeam team, int current_id, int role, int *kind)
{
	int id;
	if (kind)
		*kind = DOD_BOT_GOAL_NONE;
	if (!list || count <= 0 || (team != DOD_TEAM_ALLIES && team != DOD_TEAM_AXIS))
		return -1;
	if (role == DOD_ROLE_DEFENDER) {
		id = choose_defend(list, count, team, current_id);
		if (id >= 0) {
			if (kind)
				*kind = DOD_BOT_GOAL_DEFEND;
			return id;
		}
		id = choose_attack(list, count, team, current_id, 1);
		if (id >= 0 && kind)
			*kind = DOD_BOT_GOAL_ATTACK;
		return id;
	}
	id = choose_attack(list, count, team, current_id, 1);
	if (id >= 0) {
		if (kind)
			*kind = DOD_BOT_GOAL_ATTACK;
		return id;
	}
	id = choose_defend(list, count, team, current_id);
	if (id >= 0 && kind)
		*kind = DOD_BOT_GOAL_DEFEND;
	return id;
}

int Dod_BotRole(int rank, int team_bots, int held_points)
{
	int defenders;
	if (team_bots < 2 || held_points <= 0)
		return DOD_ROLE_ATTACKER;
	defenders = team_bots / 3;
	if (defenders < 1)
		defenders = 1;
	return rank >= team_bots - defenders ? DOD_ROLE_DEFENDER : DOD_ROLE_ATTACKER;
}

/* ---------------------------------------------------------------- GOAP */

typedef struct GoapAction {
	unsigned pre_mask, pre_value, set, clear;
	int cost;
} GoapAction;

static const GoapAction goap_actions[DOD_ACT_COUNT] = {
	/* NONE */ {0, 0, 0, 0, 0},
	/* RELOAD */ {0, 0, DOD_WS_HAS_AMMO, 0, 2},
	/* ATTACK */ {DOD_WS_HAS_AMMO | DOD_WS_ENEMY_VISIBLE, DOD_WS_HAS_AMMO | DOD_WS_ENEMY_VISIBLE, 0, DOD_WS_ENEMY_VISIBLE | DOD_WS_ENEMY_KNOWN, 2},
	/* SEEK */ {DOD_WS_ENEMY_KNOWN | DOD_WS_ENEMY_VISIBLE, DOD_WS_ENEMY_KNOWN, DOD_WS_ENEMY_VISIBLE, DOD_WS_AT_POINT | DOD_WS_IN_COVER, 5},
	/* TAKE_COVER */ {DOD_WS_IN_COVER, 0, DOD_WS_IN_COVER, DOD_WS_AT_POINT, 3},
	/* REGROUP */ {DOD_WS_NEAR_TEAM, 0, DOD_WS_NEAR_TEAM, DOD_WS_AT_POINT | DOD_WS_IN_COVER, 4},
	/* MOVE_POINT */ {DOD_WS_AT_POINT, 0, DOD_WS_AT_POINT, DOD_WS_IN_COVER, 6},
	/* CAPTURE */ {DOD_WS_AT_POINT | DOD_WS_POINT_HELD, DOD_WS_AT_POINT, DOD_WS_POINT_HELD, DOD_WS_POINT_CONTESTED, 3},
	/* GUARD */ {DOD_WS_AT_POINT, DOD_WS_AT_POINT, 0, DOD_WS_POINT_CONTESTED, 3}
};

int Dod_GoapStepApplicable(int action, unsigned state)
{
	if (action <= DOD_ACT_NONE || action >= DOD_ACT_COUNT)
		return 0;
	return (state & goap_actions[action].pre_mask) == goap_actions[action].pre_value;
}

unsigned Dod_GoapStepApply(int action, unsigned state)
{
	if (action <= DOD_ACT_NONE || action >= DOD_ACT_COUNT)
		return state;
	return (state & ~goap_actions[action].clear) | goap_actions[action].set;
}

void Dod_GoapGoalWant(int goal, unsigned *mask, unsigned *value)
{
	unsigned m = 0, v = 0;
	switch (goal) {
	case DOD_GOAL_CAPTURE: m = DOD_WS_POINT_HELD; v = DOD_WS_POINT_HELD; break;
	case DOD_GOAL_DEFEND: m = DOD_WS_AT_POINT | DOD_WS_POINT_CONTESTED; v = DOD_WS_AT_POINT; break;
	case DOD_GOAL_HUNT: m = DOD_WS_ENEMY_VISIBLE | DOD_WS_ENEMY_KNOWN; v = 0; break;
	case DOD_GOAL_COVER: m = DOD_WS_IN_COVER; v = DOD_WS_IN_COVER; break;
	case DOD_GOAL_RELOAD: m = DOD_WS_HAS_AMMO; v = DOD_WS_HAS_AMMO; break;
	case DOD_GOAL_REGROUP: m = DOD_WS_NEAR_TEAM; v = DOD_WS_NEAR_TEAM; break;
	default: break;
	}
	if (mask)
		*mask = m;
	if (value)
		*value = v;
}

int Dod_GoapInsistence(int goal, unsigned state, int role, int team_bots)
{
	const int enemy = (state & (DOD_WS_ENEMY_VISIBLE | DOD_WS_ENEMY_KNOWN)) != 0;
	const int visible = (state & DOD_WS_ENEMY_VISIBLE) != 0;
	const int low = (state & DOD_WS_LOW_HEALTH) != 0;
	const int cover = (state & DOD_WS_IN_COVER) != 0;
	const int ammo = (state & DOD_WS_HAS_AMMO) != 0;
	switch (goal) {
	case DOD_GOAL_RELOAD:
		return ammo ? 0 : (visible ? 88 : 92);
	case DOD_GOAL_COVER:
		if (cover)
			return 0;
		if (low && enemy)
			return 90;
		return (!ammo && visible) ? 91 : 0;
	case DOD_GOAL_HUNT:
		if (visible)
			return ammo ? (low ? 60 : 80) : 0;
		if (state & DOD_WS_ENEMY_KNOWN)
			return role == DOD_ROLE_DEFENDER ? 25 : 50;
		return 0;
	case DOD_GOAL_REGROUP:
		return (team_bots > 1 && !(state & DOD_WS_NEAR_TEAM) && low) ? 62 : 0;
	case DOD_GOAL_CAPTURE:
		if (role != DOD_ROLE_ATTACKER || (state & DOD_WS_POINT_HELD))
			return 0;
		return (state & DOD_WS_POINT_CONTESTED) ? 66 : 55;
	case DOD_GOAL_DEFEND:
		if (role != DOD_ROLE_DEFENDER)
			return 0;
		return (state & DOD_WS_POINT_CONTESTED) ? 72 : 45;
	default:
		return 0;
	}
}

int Dod_GoapPickGoal(unsigned state, int role, int team_bots, int current)
{
	int g, best = -1, best_score = 0;
	for (g = 0; g < DOD_GOAL_COUNT; ++g) {
		unsigned mask, value;
		int score = Dod_GoapInsistence(g, state, role, team_bots);
		if (score <= 0)
			continue;
		Dod_GoapGoalWant(g, &mask, &value);
		if ((state & mask) == value)
			continue;	/* already true */
		if (g == current)
			score += 10;
		if (score > best_score) {
			best_score = score;
			best = g;
		}
	}
	return best;
}

#define GOAP_NODES 96

DodGoapPlan Dod_GoapPlan(unsigned state, int goal)
{
	unsigned st[GOAP_NODES];
	short parent[GOAP_NODES];
	unsigned char act[GOAP_NODES], depth[GOAP_NODES], done[GOAP_NODES];
	int cost[GOAP_NODES];
	int n = 1, i, a, cur;
	unsigned mask, value;
	DodGoapPlan plan;
	memset(&plan, 0, sizeof(plan));
	plan.goal = goal;
	Dod_GoapGoalWant(goal, &mask, &value);
	if (goal < 0 || goal >= DOD_GOAL_COUNT || (state & mask) == value)
		return plan;
	st[0] = state;
	parent[0] = -1;
	act[0] = 0;
	depth[0] = 0;
	done[0] = 0;
	cost[0] = 0;
	for (;;) {
		cur = -1;
		for (i = 0; i < n; ++i)
			if (!done[i] && (cur < 0 || cost[i] < cost[cur]))
				cur = i;
		if (cur < 0)
			return plan;	/* unreachable */
		if ((st[cur] & mask) == value) {
			int len = depth[cur], k = cur;
			plan.count = len;
			plan.cost = cost[cur];
			while (k > 0 && len > 0) {
				plan.steps[--len] = act[k];
				k = parent[k];
			}
			return plan;
		}
		done[cur] = 1;
		if (depth[cur] >= DOD_GOAP_MAX_PLAN)
			continue;
		for (a = 1; a < DOD_ACT_COUNT; ++a) {
			unsigned ns;
			int seen = 0, nc;
			if (!Dod_GoapStepApplicable(a, st[cur]))
				continue;
			ns = Dod_GoapStepApply(a, st[cur]);
			if (ns == st[cur])
				continue;
			nc = cost[cur] + goap_actions[a].cost;
			for (i = 0; i < n; ++i) {
				if (st[i] == ns && cost[i] <= nc) {
					seen = 1;
					break;
				}
			}
			if (seen || n >= GOAP_NODES)
				continue;
			st[n] = ns;
			parent[n] = (short)cur;
			act[n] = (unsigned char)a;
			depth[n] = (unsigned char)(depth[cur] + 1);
			done[n] = 0;
			cost[n] = nc;
			++n;
		}
	}
}

DodGoapPlan Dod_GoapThink(unsigned state, int role, int team_bots, int current_goal)
{
	const int goal = Dod_GoapPickGoal(state, role, team_bots, current_goal);
	DodGoapPlan none;
	if (goal < 0) {
		memset(&none, 0, sizeof(none));
		none.goal = -1;
		return none;
	}
	return Dod_GoapPlan(state, goal);
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
	nav->f = (float *)malloc(sizeof(float) * (size_t)nav->node_count);
	nav->parent = (short *)malloc(sizeof(short) * (size_t)nav->node_count);
	nav->state = (unsigned char *)malloc((size_t)nav->node_count);
	nav->heap = (unsigned short *)malloc(sizeof(unsigned short) * (size_t)nav->node_count);
	nav->hpos = (short *)malloc(sizeof(short) * (size_t)nav->node_count);
	if (!nav->first || !nav->g || !nav->f || !nav->parent || !nav->state || !nav->heap || !nav->hpos) {
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
	free(nav->f);
	free(nav->parent);
	free(nav->state);
	free(nav->heap);
	free(nav->hpos);
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

static void nav_heap_up(DodNav *nav, int i)
{
	const unsigned short n = nav->heap[i];
	while (i > 0) {
		const int p = (i - 1) / 2;
		if (nav->f[nav->heap[p]] <= nav->f[n])
			break;
		nav->heap[i] = nav->heap[p];
		nav->hpos[nav->heap[i]] = (short)i;
		i = p;
	}
	nav->heap[i] = n;
	nav->hpos[n] = (short)i;
}

static void nav_heap_down(DodNav *nav, int i, int count)
{
	const unsigned short n = nav->heap[i];
	for (;;) {
		int c = 2 * i + 1;
		if (c >= count)
			break;
		if (c + 1 < count && nav->f[nav->heap[c + 1]] < nav->f[nav->heap[c]])
			++c;
		if (nav->f[n] <= nav->f[nav->heap[c]])
			break;
		nav->heap[i] = nav->heap[c];
		nav->hpos[nav->heap[i]] = (short)i;
		i = c;
	}
	nav->heap[i] = n;
	nav->hpos[n] = (short)i;
}

static float nav_dist(const DodNav *nav, int a, int b)
{
	float ax, ay, az, bx, by, bz;
	Dod_NavNodePos(nav, a, &ax, &ay, &az);
	Dod_NavNodePos(nav, b, &bx, &by, &bz);
	return sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay) + (bz - az) * (bz - az));
}

int Dod_NavInside(const DodNav *nav, const float mins[3], const float maxs[3], float margin, int *out, int max)
{
	float best[8], cx, cy, cz;
	int count = 0, i, j;
	if (!nav || !mins || !maxs || !out || max < 1)
		return 0;
	if (max > 8)
		max = 8;
	cx = 0.5f * (mins[0] + maxs[0]);
	cy = 0.5f * (mins[1] + maxs[1]);
	cz = 0.5f * (mins[2] + maxs[2]);
	for (i = 0; i < nav->node_count; ++i) {
		float x, y, z, d;
		Dod_NavNodePos(nav, i, &x, &y, &z);
		if (x < mins[0] - margin || x > maxs[0] + margin || y < mins[1] - margin || y > maxs[1] + margin ||
			z < mins[2] - 72.0f || z > maxs[2] + 72.0f)
			continue;
		d = (x - cx) * (x - cx) + (y - cy) * (y - cy) + 9.0f * (z - cz) * (z - cz);
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

void Dod_NavAddHazard(DodNav *nav, const float mins[3], const float maxs[3], int team_mask)
{
	int i;
	if (!nav || !mins || !maxs || nav->zone_count >= DOD_NAV_MAX_ZONES || !team_mask)
		return;
	for (i = 0; i < 3; ++i) {
		nav->zone_min[nav->zone_count][i] = mins[i];
		nav->zone_max[nav->zone_count][i] = maxs[i];
	}
	nav->zone_teams[nav->zone_count] = (unsigned char)team_mask;
	nav->zone_count++;
}

void Dod_NavSetAvoidTeam(DodNav *nav, DodTeam team)
{
	if (nav)
		nav->avoid_team = team == DOD_TEAM_ALLIES || team == DOD_TEAM_AXIS ? (int)team : 0;
}

/* A player standing here (origin 36 above the floor) is in the zone: the box grown 16 sideways, from just
 * below its floor to a head above its top. */
static int nav_in_hazard_margin(const DodNav *nav, int team, float x, float y, float z, float margin)
{
	int i;
	for (i = 0; i < nav->zone_count; ++i) {
		if (!(nav->zone_teams[i] & (1 << team)))
			continue;
		if (x >= nav->zone_min[i][0] - margin && x <= nav->zone_max[i][0] + margin && y >= nav->zone_min[i][1] - margin &&
			y <= nav->zone_max[i][1] + margin && z >= nav->zone_min[i][2] - 40.0f && z <= nav->zone_max[i][2] + 36.0f)
			return 1;
	}
	return 0;
}

static int nav_in_hazard(const DodNav *nav, int team, float x, float y, float z)
{
	return nav_in_hazard_margin(nav, team, x, y, z, 16.0f);	/* the player's half-width: touching the box hurts */
}

int Dod_NavInHazard(const DodNav *nav, DodTeam team, float x, float y, float z)
{
	return Dod_NavInHazardMargin(nav, team, x, y, z, 16.0f);
}

int Dod_NavInHazardMargin(const DodNav *nav, DodTeam team, float x, float y, float z, float margin)
{
	if (!nav || !nav->zone_count || (team != DOD_TEAM_ALLIES && team != DOD_TEAM_AXIS))
		return 0;
	return nav_in_hazard_margin(nav, (int)team, x, y, z, margin);
}

/* Does the straight edge a -> b enter a zone hurting the team? Sampled every ~24 units. */
static int nav_edge_hazard(const DodNav *nav, int team, int a, int b)
{
	float ax, ay, az, bx, by, bz, len;
	int steps, i;
	Dod_NavNodePos(nav, a, &ax, &ay, &az);
	Dod_NavNodePos(nav, b, &bx, &by, &bz);
	len = sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
	steps = (int)(len / 24.0f) + 1;
	for (i = 0; i <= steps; ++i) {
		const float t = (float)i / (float)steps;
		if (nav_in_hazard(nav, team, ax + (bx - ax) * t, ay + (by - ay) * t, az + (bz - az) * t))
			return 1;
	}
	return 0;
}

int Dod_NavPath(DodNav *nav, int from, int to, int *path, int max_path)
{
	int i, e, count, n, open = 0;
	if (!nav || !path || from < 0 || to < 0 || from >= nav->node_count || to >= nav->node_count || max_path < 1)
		return 0;
	for (i = 0; i < nav->node_count; ++i) {
		nav->state[i] = 0;
		nav->parent[i] = -1;
	}
	nav->g[from] = 0.0f;
	nav->f[from] = nav_dist(nav, from, to);
	nav->state[from] = 1;
	nav->heap[0] = (unsigned short)from;
	nav->hpos[from] = 0;
	open = 1;
	while (open > 0) {
		const int cur = nav->heap[0];
		if (cur == to)
			break;
		/* pop the best open node */
		--open;
		if (open > 0) {
			nav->heap[0] = nav->heap[open];
			nav->hpos[nav->heap[0]] = 0;
			nav_heap_down(nav, 0, open);
		}
		nav->state[cur] = 2;
		for (e = nav->first[cur]; e < nav->first[cur + 1]; ++e) {
			const unsigned v = nav_be16(nav->edges + 2 * e);
			const int next = (int)(v & 0x3fff), kind = (int)(v >> 14);
			float cost, g;
			if (nav->state[next] == 2)
				continue;
			cost = nav_dist(nav, cur, next) + 1.0f;
			if (kind == DOD_NAV_JUMP)
				cost += 64.0f;
			else if (kind == DOD_NAV_DROP)
				cost += 16.0f;
			else if (kind == DOD_NAV_LADDER)
				cost = cost * 1.5f + 64.0f;
			if (Dod_NavPenalized(nav, cur, next))
				cost += 1500.0f;
			if (nav->avoid_team && nav->zone_count && nav_edge_hazard(nav, nav->avoid_team, cur, next))
				cost += DOD_NAV_HAZARD_COST;
			g = nav->g[cur] + cost;
			if (nav->state[next] == 1 && g >= nav->g[next])
				continue;
			nav->g[next] = g;
			nav->f[next] = g + nav_dist(nav, next, to);
			nav->parent[next] = (short)cur;
			if (nav->state[next] == 1) {
				nav_heap_up(nav, nav->hpos[next]);
			} else {
				nav->state[next] = 1;
				nav->heap[open] = (unsigned short)next;
				nav->hpos[next] = (short)open;
				nav_heap_up(nav, open);
				++open;
			}
		}
	}
	if (open <= 0)
		return 0;
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
