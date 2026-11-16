#include "dod_bots.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static void test_target_count(void)
{
	/* the GameCube case: two client slots, one human, a cap of one */
	assert(Dod_BotTargetCount(1, 2, 1, 1) == 1);
	assert(Dod_BotTargetCount(8, 2, 1, 1) == 1);		/* cap */
	assert(Dod_BotTargetCount(8, 2, 2, 1) == 0);		/* two humans: no room */
	assert(Dod_BotTargetCount(8, 2, 0, 1) == 1);		/* empty server still capped */
	assert(Dod_BotTargetCount(0, 2, 1, 1) == 0);		/* cvar off */
	assert(Dod_BotTargetCount(-3, 2, 1, 1) == 0);
	assert(Dod_BotTargetCount(1, 0, 0, 1) == 0);
	assert(Dod_BotTargetCount(1, 2, 1, 0) == 0);		/* cap off */
	/* a bigger server fills up to the free slots, then the cap */
	assert(Dod_BotTargetCount(6, 16, 2, 8) == 6);
	assert(Dod_BotTargetCount(20, 16, 2, 32) == 14);
	assert(Dod_BotTargetCount(20, 16, 2, 8) == 8);
	assert(Dod_BotTargetCount(3, 4, 5, 8) == 0);		/* over-full: never negative */
	assert(Dod_BotTargetCount(3, 4, -2, 8) == 3);		/* bad human count is clamped */
}

static void test_requested_count(void)
{
	assert(Dod_BotRequestedCount(3, 0, 0) == 3);	/* offline: as asked */
	assert(Dod_BotRequestedCount(3, 1, 0) == 0);	/* networked: the slot is for the peer */
	assert(Dod_BotRequestedCount(3, 1, 1) == 3);	/* unless bots are allowed online */
	assert(Dod_BotRequestedCount(0, 0, 0) == 0);
	assert(Dod_BotRequestedCount(-1, 0, 1) == 0);
	assert(Dod_BotRequestedCount(2, 0, 1) == 2);
}

static void test_team_pick(void)
{
	assert(Dod_BotPickTeam(0, 1) == DOD_TEAM_ALLIES);	/* human on Axis: bot joins Allies */
	assert(Dod_BotPickTeam(1, 0) == DOD_TEAM_AXIS);
	assert(Dod_BotPickTeam(2, 2) == DOD_TEAM_AXIS);		/* tie */
	assert(Dod_BotPickTeam(0, 0) == DOD_TEAM_AXIS);
	/* human on Allies, one bot already on Axis: the second bot joins Allies so the bots oppose */
	assert(Dod_BotPickTeamBalanced(1, 0, 0, 0) == DOD_TEAM_AXIS);
	assert(Dod_BotPickTeamBalanced(1, 1, 0, 1) == DOD_TEAM_ALLIES);
	assert(Dod_BotPickTeamBalanced(2, 1, 1, 0) == DOD_TEAM_AXIS);
	assert(Dod_BotPickTeamBalanced(0, 0, 0, 0) == DOD_TEAM_AXIS);
	assert(Dod_BotPickTeamBalanced(2, 2, 1, 1) == DOD_TEAM_AXIS);
	assert(Dod_BotPickTeam(3, 5) == DOD_TEAM_ALLIES);
	{	/* one human on Allies plus three bots fills out to two a side */
		int allies = 1, axis = 0, i;
		for (i = 0; i < 3; i++) {
			if (Dod_BotPickTeam(allies, axis) == DOD_TEAM_ALLIES)
				allies++;
			else
				axis++;
		}
		assert(allies == 2 && axis == 2);
	}
}

static void test_class_pick(void)
{
	int none[DOD_CLASS_COUNT] = {0};
	int taken[DOD_CLASS_COUNT] = {0};
	int seen[DOD_CLASS_COUNT] = {0};
	int roll, c;

	for (roll = 0; roll < 600; roll++) {
		c = Dod_BotPickClass(roll, none);
		assert(c >= DOD_CLASS_RIFLEMAN && c < DOD_CLASS_COUNT);
		seen[c]++;
	}
	for (c = DOD_CLASS_RIFLEMAN; c < DOD_CLASS_COUNT; c++)
		assert(seen[c] > 0);				/* every class is reachable */
	assert(seen[DOD_CLASS_RIFLEMAN] > seen[DOD_CLASS_SNIPER]);	/* cheap classes dominate */

	/* a special class already on the team is never picked again */
	taken[DOD_CLASS_SNIPER] = taken[DOD_CLASS_MG] = taken[DOD_CLASS_ROCKET] = 1;
	for (roll = 0; roll < 600; roll++) {
		c = Dod_BotPickClass(roll, taken);
		assert(c == DOD_CLASS_RIFLEMAN || c == DOD_CLASS_ASSAULT || c == DOD_CLASS_SUPPORT);
	}
	/* negative rolls and a missing table are safe */
	assert(Dod_BotPickClass(-7, none) >= DOD_CLASS_RIFLEMAN);
	assert(Dod_BotPickClass(5, 0) >= DOD_CLASS_RIFLEMAN);
}

static void test_angles(void)
{
	assert(fabsf(Dod_BotAngleDelta(10.0f, 30.0f) - 20.0f) < 0.001f);
	assert(fabsf(Dod_BotAngleDelta(350.0f, 10.0f) - 20.0f) < 0.001f);	/* across zero */
	assert(fabsf(Dod_BotAngleDelta(10.0f, 350.0f) + 20.0f) < 0.001f);
	assert(fabsf(Dod_BotAngleDelta(0.0f, 180.0f) + 180.0f) < 0.001f);	/* [-180, 180) */
	assert(fabsf(Dod_BotAngleDelta(90.0f, 90.0f)) < 0.001f);

	assert(fabsf(Dod_BotTurn(0.0f, 90.0f, 30.0f) - 30.0f) < 0.001f);	/* limited step */
	assert(fabsf(Dod_BotTurn(0.0f, 20.0f, 30.0f) - 20.0f) < 0.001f);	/* arrives */
	assert(fabsf(Dod_BotTurn(350.0f, 20.0f, 15.0f) - 5.0f) < 0.001f);	/* short way, wraps */
	assert(fabsf(Dod_BotTurn(10.0f, 340.0f, 15.0f) + 5.0f) < 0.001f);
	assert(fabsf(Dod_BotTurn(40.0f, 100.0f, -5.0f) - 40.0f) < 0.001f);	/* negative step is no turn */
}

static void test_aim_error(void)
{
	int skill;
	for (skill = 0; skill < 3; skill++)
		assert(Dod_BotAimError(500.0f, skill) > Dod_BotAimError(500.0f, skill + 1));	/* skill tightens */
	assert(Dod_BotAimError(2000.0f, 1) > Dod_BotAimError(100.0f, 1));	/* distance widens */
	assert(Dod_BotAimError(100.0f, -5) == Dod_BotAimError(100.0f, 0));	/* clamped */
	assert(Dod_BotAimError(100.0f, 99) == Dod_BotAimError(100.0f, 3));
	assert(Dod_BotAimError(-50.0f, 2) == Dod_BotAimError(0.0f, 2));
	assert(Dod_BotAimError(0.0f, 3) > 0.0f);				/* nobody is perfect */
}

static void test_decide(void)
{
	DodBotInput in;
	DodBotMove m;

	memset(&in, 0, sizeof(in));
	assert(Dod_BotDecide(0).forward == 0.0f);

	/* nothing to shoot at: head for the objective */
	m = Dod_BotDecide(&in);
	assert(m.forward == 1.0f && !m.fire && !m.turn_away);

	/* a wall: hop and pick a new heading; a ledge: stop and turn, never jump */
	in.blocked = 1;
	m = Dod_BotDecide(&in);
	assert(m.turn_away && m.jump);
	in.ledge = 1;
	m = Dod_BotDecide(&in);
	assert(m.turn_away && !m.jump && m.forward == 0.0f);
	in.blocked = in.ledge = 0;

	/* engagement distances */
	in.has_target = 1;
	in.aim_error = 2.0f;
	in.target_distance = DOD_BOT_FAR + 100.0f;
	assert(Dod_BotDecide(&in).forward > 0.5f);
	in.target_distance = DOD_BOT_CLOSE - 50.0f;
	assert(Dod_BotDecide(&in).forward < 0.0f);
	in.target_distance = 400.0f;
	m = Dod_BotDecide(&in);
	assert(m.forward > 0.0f && m.forward < 0.5f);

	/* fire only inside the cone and with ammo */
	assert(m.fire);
	in.aim_error = DOD_BOT_FIRE_CONE + 1.0f;
	assert(!Dod_BotDecide(&in).fire);
	in.aim_error = -(DOD_BOT_FIRE_CONE + 1.0f);
	assert(!Dod_BotDecide(&in).fire);
	in.aim_error = 1.0f;
	in.clip_empty = 1;
	m = Dod_BotDecide(&in);
	assert(!m.fire && m.reload);
	in.clip_empty = 0;

	/* semi-auto releases the trigger every other call */
	in.semi_auto = 1;
	in.fire_phase = 0;
	assert(!Dod_BotDecide(&in).fire);
	in.fire_phase = 1;
	assert(Dod_BotDecide(&in).fire);

	/* strafing alternates over time and stays within range */
	in.time = 0.0f;
	m = Dod_BotDecide(&in);
	in.time = 2.0f;
	assert(Dod_BotDecide(&in).side == -m.side);
	assert(fabsf(m.side) <= 1.0f);
}

static void test_engage_at_wall(void)
{
	DodBotInput in;
	DodBotMove m;
	memset(&in, 0, sizeof(in));
	in.has_target = 1;
	in.target_distance = 400.0f;
	in.aim_error = 1.0f;
	in.blocked = 1;
	m = Dod_BotDecide(&in);
	assert(!m.turn_away && m.jump && m.fire);	/* fights on, hops the obstacle */
	in.ledge = 1;
	m = Dod_BotDecide(&in);
	assert(m.turn_away && !m.jump && !m.fire);	/* a ledge still wins */
}

static void test_stuck(void)
{
	DodBotStuck s;
	DodBotEscape e;
	float t;
	memset(&s, 0, sizeof(s));
	e = Dod_BotStuckUpdate(&s, 0, 0, 10.0f, 1, 0.1f);	/* primes */
	assert(!e.stuck);
	e = Dod_BotStuckUpdate(&s, 1, 1, 10.5f, 1, 0.1f);	/* too early */
	assert(!e.stuck);
	e = Dod_BotStuckUpdate(&s, 2, 0, 11.1f, 1, 0.1f);	/* barely moved */
	assert(e.stuck && e.turn > 0.0f && e.hold > 0.0f);
	t = e.turn;
	e = Dod_BotStuckUpdate(&s, 2, 0, 12.2f, 1, 0.9f);	/* still stuck, other side, wider */
	assert(e.stuck && e.turn < 0.0f && fabsf(e.turn) > t);
	e = Dod_BotStuckUpdate(&s, 200, 0, 13.3f, 1, 0.1f);	/* moved: reset */
	assert(!e.stuck);
	e = Dod_BotStuckUpdate(&s, 200, 0, 14.4f, 0, 0.1f);	/* standing to fight is not stuck */
	assert(!e.stuck);
	e = Dod_BotStuckUpdate(&s, 200, 0, 15.5f, 1, 0.1f);
	assert(e.stuck && fabsf(e.turn) < t + 1.0f);	/* failure count restarted */
	for (int i = 0; i < 12; i++)
		e = Dod_BotStuckUpdate(&s, 200, 0, 16.6f + 1.1f * i, 1, 0.1f);
	assert(e.turn <= 180.0f && e.hold < 4.0f);	/* bounded */
	e = Dod_BotStuckUpdate(&s, 200, 0, 1.0f, 1, 0.1f);	/* level restarted: re-prime */
	assert(!e.stuck);
	assert(!Dod_BotStuckUpdate(NULL, 0, 0, 0, 1, 0).stuck);
}

static void test_spectator(void)
{
	DodSpecCandidate c[3] = { { 2, 1, 900.0f }, { 3, 1, 200.0f }, { 4, 0, 50.0f } };
	assert(Dod_SpecSanitizeMode(DOD_SPEC_MAP_FREE) == DOD_SPEC_CHASE_FREE);
	assert(Dod_SpecSanitizeMode(DOD_SPEC_MAP_CHASE) == DOD_SPEC_CHASE_FREE);
	assert(Dod_SpecSanitizeMode(DOD_SPEC_IN_EYE) == DOD_SPEC_IN_EYE);
	assert(Dod_SpecSanitizeMode(DOD_SPEC_ROAMING) == DOD_SPEC_ROAMING);
	assert(Dod_SpecSanitizeMode(0) == DOD_SPEC_IN_EYE);
	assert(Dod_SpecSanitizeMode(99) == DOD_SPEC_IN_EYE);

	assert(Dod_SpecInterest(&c[2]) == 0.0f);			/* dead */
	assert(Dod_SpecInterest(&c[1]) > Dod_SpecInterest(&c[0]));	/* nearer a fight */
	{
		DodSpecCandidate lone = { 5, 1, -1.0f };
		assert(Dod_SpecInterest(&lone) > 0.0f);
		assert(Dod_SpecInterest(&lone) < Dod_SpecInterest(&c[0]));
	}
	assert(Dod_SpecInterest(NULL) == 0.0f);

	/* cycling skips the dead and wraps */
	assert(Dod_SpecCycle(c, 3, 2, 0) == 3);
	assert(Dod_SpecCycle(c, 3, 3, 0) == 2);		/* skips dead 4, wraps */
	assert(Dod_SpecCycle(c, 3, 2, 1) == 3);		/* reverse wraps past dead 4 */
	assert(Dod_SpecCycle(c, 3, 0, 0) == 2);		/* no current: first living */
	assert(Dod_SpecCycle(c, 3, 0, 1) == 3);		/* no current, reverse: last living */
	assert(Dod_SpecCycle(c, 0, 2, 0) == 0);
	assert(Dod_SpecCycle(NULL, 3, 2, 0) == 0);
	{
		DodSpecCandidate only[1] = { { 2, 1, 100.0f } };
		assert(Dod_SpecCycle(only, 1, 2, 0) == 0);	/* nothing else to cycle to */
		assert(Dod_SpecCycle(only, 1, 0, 0) == 2);
	}

	/* director: stays put early, switches to the better fight later */
	assert(Dod_SpecDirectorPick(c, 3, 2, 1.0f) == 2);		/* too soon */
	assert(Dod_SpecDirectorPick(c, 3, 2, 5.0f) == 3);		/* 3 is in a much closer fight */
	assert(Dod_SpecDirectorPick(c, 3, 3, 5.0f) == 3);		/* already the best */
	assert(Dod_SpecDirectorPick(c, 3, 4, 0.0f) == 3);		/* current dead: switch at once */
	assert(Dod_SpecDirectorPick(c, 3, 0, 0.0f) == 3);		/* none yet */
	{
		DodSpecCandidate near[2] = { { 2, 1, 500.0f }, { 3, 1, 480.0f } };
		assert(Dod_SpecDirectorPick(near, 2, 2, 6.0f) == 2);	/* not clearly duller */
		assert(Dod_SpecDirectorPick(near, 2, 2, 15.0f) == 3);	/* max dwell */
	}
	{
		DodSpecCandidate dead[1] = { { 2, 0, 10.0f } };
		assert(Dod_SpecDirectorPick(dead, 1, 2, 30.0f) == 0);
	}
}

static DodBotObjectiveInfo obj(int id, DodTeam owner, DodTeam capturing, int allies_can, int axis_can, float dist, int blocked)
{
	DodBotObjectiveInfo o;
	memset(&o, 0, sizeof(o));
	o.id = id;
	o.owner = owner;
	o.capturing_team = capturing;
	o.can_cap[DOD_TEAM_ALLIES] = allies_can;
	o.can_cap[DOD_TEAM_AXIS] = axis_can;
	o.distance = dist;
	o.blocked = blocked;
	return o;
}

static void test_objectives(void)
{
	DodBotObjectiveInfo list[4];
	int kind = -1, ids[3] = { -1, -1, -1 };
	float untils[3] = { 0.0f, 0.0f, 0.0f };

	/* charlie: Axis owns every point and only Allies may capture them */
	list[0] = obj(1, DOD_TEAM_AXIS, DOD_TEAM_UNASSIGNED, 1, 0, 1500.0f, 0);
	list[1] = obj(2, DOD_TEAM_AXIS, DOD_TEAM_UNASSIGNED, 1, 0, 900.0f, 0);
	list[2] = obj(3, DOD_TEAM_AXIS, DOD_TEAM_UNASSIGNED, 1, 0, 1200.0f, 0);
	list[3] = obj(4, DOD_TEAM_AXIS, DOD_TEAM_UNASSIGNED, 1, 0, 2000.0f, 0);
	assert(Dod_BotChooseObjective(list, 4, DOD_TEAM_ALLIES, -1, &kind) == 2 && kind == DOD_BOT_GOAL_ATTACK);	/* nearest */
	/* keeps the current target even when another is nearer (stay in the area until it flips) */
	assert(Dod_BotChooseObjective(list, 4, DOD_TEAM_ALLIES, 3, &kind) == 3 && kind == DOD_BOT_GOAL_ATTACK);
	/* a captured current target is dropped */
	list[2].owner = DOD_TEAM_ALLIES;
	assert(Dod_BotChooseObjective(list, 4, DOD_TEAM_ALLIES, 3, &kind) == 2 && kind == DOD_BOT_GOAL_ATTACK);
	/* Axis cannot capture any of them, so it defends: nothing under attack, nearest owned point */
	list[2].owner = DOD_TEAM_AXIS;
	assert(Dod_BotChooseObjective(list, 4, DOD_TEAM_AXIS, -1, &kind) == 2 && kind == DOD_BOT_GOAL_DEFEND);
	assert(Dod_BotChooseObjective(list, 4, DOD_TEAM_AXIS, 1, &kind) == 1 && kind == DOD_BOT_GOAL_DEFEND);	/* keeps its post */
	/* a point being captured by the enemy pulls the defender over */
	list[3].capturing_team = DOD_TEAM_ALLIES;
	assert(Dod_BotChooseObjective(list, 4, DOD_TEAM_AXIS, 1, &kind) == 4 && kind == DOD_BOT_GOAL_DEFEND);
	/* the attacker's own team capturing it does not count as an attack on it */
	list[3].capturing_team = DOD_TEAM_UNASSIGNED;
	/* a point the bot could not reach is skipped */
	list[1].blocked = 1;
	assert(Dod_BotChooseObjective(list, 4, DOD_TEAM_ALLIES, -1, &kind) == 3);
	/* Allies hold everything: they defend; Axis (can capture nothing here) has nothing to retake */
	{
		DodBotObjectiveInfo mine[2];
		mine[0] = obj(1, DOD_TEAM_ALLIES, DOD_TEAM_UNASSIGNED, 1, 1, 300.0f, 0);
		mine[1] = obj(2, DOD_TEAM_ALLIES, DOD_TEAM_UNASSIGNED, 1, 1, 100.0f, 0);
		assert(Dod_BotChooseObjective(mine, 2, DOD_TEAM_ALLIES, -1, &kind) == 2 && kind == DOD_BOT_GOAL_DEFEND);
		assert(Dod_BotChooseObjective(mine, 2, DOD_TEAM_AXIS, -1, &kind) == 2 && kind == DOD_BOT_GOAL_ATTACK);	/* both may capture */
	}
	/* nothing to do */
	assert(Dod_BotChooseObjective(list, 0, DOD_TEAM_ALLIES, -1, &kind) == -1 && kind == DOD_BOT_GOAL_NONE);
	assert(Dod_BotChooseObjective(list, 4, DOD_TEAM_SPECTATOR, -1, &kind) == -1);
	assert(Dod_BotChooseObjective(NULL, 4, DOD_TEAM_ALLIES, -1, &kind) == -1);

	/* blacklist: add, expire, replace the soonest-expiring */
	Dod_BotBlockAdd(ids, untils, 3, 7, 50.0f, 10.0f);
	assert(Dod_BotBlockHas(ids, untils, 3, 7, 20.0f));
	assert(!Dod_BotBlockHas(ids, untils, 3, 7, 51.0f));
	assert(!Dod_BotBlockHas(ids, untils, 3, 8, 20.0f));
	Dod_BotBlockAdd(ids, untils, 3, 8, 60.0f, 10.0f);
	Dod_BotBlockAdd(ids, untils, 3, 9, 70.0f, 10.0f);
	Dod_BotBlockAdd(ids, untils, 3, 10, 80.0f, 10.0f);	/* full: drops id 7, the soonest */
	assert(!Dod_BotBlockHas(ids, untils, 3, 7, 20.0f));
	assert(Dod_BotBlockHas(ids, untils, 3, 8, 20.0f) && Dod_BotBlockHas(ids, untils, 3, 9, 20.0f)
		&& Dod_BotBlockHas(ids, untils, 3, 10, 20.0f));
	Dod_BotBlockAdd(ids, untils, 3, 9, 99.0f, 10.0f);	/* re-adding extends */
	assert(Dod_BotBlockHas(ids, untils, 3, 9, 90.0f));
}

int main(void)
{
	test_spectator();
	test_engage_at_wall();
	test_stuck();
	test_target_count();
	test_requested_count();
	test_team_pick();
	test_class_pick();
	test_angles();
	test_aim_error();
	test_decide();
	test_objectives();
	return 0;
}
