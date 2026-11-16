#include "dod_bots.h"

#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
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

/* The toy graph tests/test_gen_waypoints.py writes with tools/gen_waypoints.py (the same literal is asserted there):
 * nodes 0..4 at (0,0,0) (100,0,0) (200,0,0) (100,0,200) (200,0,200); a ladder between 1 and 3 (both ways),
 * a costly jump 2 -> 4. */
static const char *TOY_WPT_HEX = "44575054000200000001e2400005000900200120000000000000010064000000000300c800000000020064000000c80200c8000000c801000100000002c00300014004c00100040003";

static int hex_to_bytes(const char *hex, unsigned char *out, int max)
{
	int n = 0;
	while (hex[0] && hex[1] && n < max) {
		unsigned v = 0;
		sscanf(hex, "%2x", &v);
		out[n++] = (unsigned char)v;
		hex += 2;
	}
	return n;
}

static void test_nav(void)
{
	unsigned char buf[128];
	DodNav nav;
	int len = hex_to_bytes(TOY_WPT_HEX, buf, sizeof(buf)), path[DOD_NAV_MAX_PATH], near[4], count;
	float x, y, z;

	assert(len == 73);
	assert(Dod_NavLoad(&nav, buf, len));
	assert(nav.node_count == 5 && nav.edge_count == 9 && nav.bsp_size == 123456u);
	Dod_NavNodePos(&nav, 3, &x, &y, &z);
	assert(x == 100.0f && y == 0.0f && z == 200.0f);
	assert(Dod_NavEdgeKind(&nav, 1, 3) == DOD_NAV_LADDER && Dod_NavEdgeKind(&nav, 3, 1) == DOD_NAV_LADDER);
	assert(Dod_NavEdgeKind(&nav, 2, 4) == DOD_NAV_JUMP && Dod_NavEdgeKind(&nav, 1, 2) == DOD_NAV_WALK);
	assert(Dod_NavEdgeKind(&nav, 0, 4) == -1 && Dod_NavEdgeKind(&nav, 9, 0) == -1);

	/* walking and jumping is cheaper to the far corner; the ladder is the way up to node 3 */
	count = Dod_NavPath(&nav, 0, 4, path, DOD_NAV_MAX_PATH);
	assert(count == 4 && path[0] == 0 && path[1] == 1 && path[2] == 2 && path[3] == 4);
	count = Dod_NavPath(&nav, 0, 3, path, DOD_NAV_MAX_PATH);
	assert(count == 3 && path[0] == 0 && path[1] == 1 && path[2] == 3);
	count = Dod_NavPath(&nav, 4, 0, path, DOD_NAV_MAX_PATH);
	assert(count == 4 && path[0] == 4 && path[1] == 3 && path[2] == 1 && path[3] == 0);
	assert(Dod_NavPath(&nav, 2, 2, path, DOD_NAV_MAX_PATH) == 1 && path[0] == 2);
	assert(Dod_NavPath(&nav, 0, 4, path, 3) == 0);	/* longer than max_path */
	assert(Dod_NavPath(&nav, 0, 7, path, DOD_NAV_MAX_PATH) == 0);

	/* a penalized edge is avoided while the penalty lasts: 0 -> 4 has to take the ladder when the jump is stuck */
	Dod_NavSetTime(&nav, 10.0f);
	Dod_NavPenalize(&nav, 2, 4, 40.0f);
	assert(Dod_NavPenalized(&nav, 2, 4) && !Dod_NavPenalized(&nav, 4, 2) && !Dod_NavPenalized(&nav, 1, 2));
	count = Dod_NavPath(&nav, 0, 4, path, DOD_NAV_MAX_PATH);
	assert(count == 4 && path[2] == 3);
	Dod_NavSetTime(&nav, 50.0f);	/* expired */
	assert(!Dod_NavPenalized(&nav, 2, 4));
	count = Dod_NavPath(&nav, 0, 4, path, DOD_NAV_MAX_PATH);
	assert(count == 4 && path[2] == 2);

	/* nearest nodes, nearest first */
	count = Dod_NavNearest(&nav, 190.0f, 0.0f, 190.0f, near, 4);
	assert(count == 4 && near[0] == 4 && near[1] == 3 && near[2] == 2);
	assert(Dod_NavNearest(&nav, 0.0f, 0.0f, 0.0f, near, 1) == 1 && near[0] == 0);

	Dod_NavFree(&nav);
	/* a bad file is rejected, not trusted (and leaves nothing allocated) */
	assert(!Dod_NavLoad(&nav, buf, 40));
	buf[0] = 'X';
	assert(!Dod_NavLoad(&nav, buf, len));
	buf[0] = 'D';
	buf[15] = 8;	/* edge count no longer matches the per-node counts */
	assert(!Dod_NavLoad(&nav, buf, len));
	Dod_NavFree(&nav);
}


/* The heap A* against a brute-force Dijkstra on the real charlie graph (skipped when the file is not found):
 * same cost for every sampled pair, and every step of the path is a real edge. */
static float real_edge_cost(const DodNav *nav, int a, int b)
{
	float ax, ay, az, bx, by, bz, c;
	const int kind = Dod_NavEdgeKind(nav, a, b);
	Dod_NavNodePos(nav, a, &ax, &ay, &az);
	Dod_NavNodePos(nav, b, &bx, &by, &bz);
	c = sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay) + (bz - az) * (bz - az)) + 1.0f;
	if (kind == DOD_NAV_JUMP)
		c += 64.0f;
	else if (kind == DOD_NAV_DROP)
		c += 16.0f;
	else if (kind == DOD_NAV_LADDER)
		c = c * 1.5f + 64.0f;
	return c;
}

static void test_nav_real(void)
{
	static const char *const paths[] = { "data/waypoints/dod_charlie.wpt", "../data/waypoints/dod_charlie.wpt", NULL };
	unsigned char *buf = NULL;
	long len = 0;
	DodNav nav;
	int pi, from, to, sampled = 0, n, e, k;
	FILE *fh = NULL;
	for (pi = 0; paths[pi] && !fh; ++pi)
		fh = fopen(paths[pi], "rb");
	if (!fh) {
		printf("test_nav_real: charlie graph not found, skipped\n");
		return;
	}
	fseek(fh, 0, SEEK_END);
	len = ftell(fh);
	fseek(fh, 0, SEEK_SET);
	buf = (unsigned char *)malloc((size_t)len);
	assert(buf && fread(buf, 1, (size_t)len, fh) == (size_t)len);
	fclose(fh);
	assert(Dod_NavLoad(&nav, buf, (int)len));
	n = nav.node_count;
	{
		float *dist = (float *)malloc(sizeof(float) * (size_t)n);
		char *done = (char *)malloc((size_t)n);
		int path[DOD_NAV_MAX_PATH];
		assert(dist && done);
		for (from = 0; from < n; from += n / 7 + 1) {
			int i, cur;
			for (i = 0; i < n; ++i) {
				dist[i] = 1e30f;
				done[i] = 0;
			}
			dist[from] = 0.0f;
			for (;;) {	/* brute-force Dijkstra */
				cur = -1;
				for (i = 0; i < n; ++i)
					if (!done[i] && dist[i] < 1e29f && (cur < 0 || dist[i] < dist[cur]))
						cur = i;
				if (cur < 0)
					break;
				done[cur] = 1;
				for (e = nav.first[cur]; e < nav.first[cur + 1]; ++e) {
					const int nx = (int)(((unsigned)nav.edges[2 * e] << 8 | nav.edges[2 * e + 1]) & 0x3fff);
					const float nd = dist[cur] + real_edge_cost(&nav, cur, nx);
					if (nd < dist[nx])
						dist[nx] = nd;
				}
			}
			for (to = 0; to < n; to += n / 11 + 3) {
				const int len_path = Dod_NavPath(&nav, from, to, path, DOD_NAV_MAX_PATH);
				float cost = 0.0f;
				if (dist[to] > 1e29f) {
					assert(len_path == 0);
					continue;
				}
				if (len_path == 0)
					continue;	/* longer than the path buffer: the game falls back to heading for the goal */
				assert(path[0] == from && path[len_path - 1] == to);
				for (k = 0; k + 1 < len_path; ++k) {
					assert(Dod_NavEdgeKind(&nav, path[k], path[k + 1]) >= 0);
					cost += real_edge_cost(&nav, path[k], path[k + 1]);
				}
				assert(fabsf(cost - dist[to]) < 0.5f);	/* optimal */
				++sampled;
			}
		}
		free(dist);
		free(done);
	}
	assert(sampled > 20);
	Dod_NavFree(&nav);
	free(buf);
	printf("test_nav_real: %d optimal paths checked on %d waypoints\n", sampled, n);
}

static void test_goap(void)
{
	const unsigned ammo = DOD_WS_HAS_AMMO;
	DodGoapPlan p;
	DodBotObjectiveInfo o[3];
	int kind, id;

	/* an enemy in sight and ammo: shoot it */
	p = Dod_GoapThink(ammo | DOD_WS_ENEMY_VISIBLE | DOD_WS_ENEMY_KNOWN, DOD_ROLE_ATTACKER, 2, -1);
	assert(p.goal == DOD_GOAL_HUNT && p.count == 1 && p.steps[0] == DOD_ACT_ATTACK);
	/* known but out of sight: go there, then shoot */
	p = Dod_GoapThink(ammo | DOD_WS_ENEMY_KNOWN | DOD_WS_POINT_HELD, DOD_ROLE_ATTACKER, 2, -1);
	assert(p.goal == DOD_GOAL_HUNT && p.count == 2 && p.steps[0] == DOD_ACT_SEEK && p.steps[1] == DOD_ACT_ATTACK);
	/* a defender hunts an unseen enemy less than it guards its point */
	p = Dod_GoapThink(ammo | DOD_WS_ENEMY_KNOWN | DOD_WS_POINT_HELD | DOD_WS_POINT_CONTESTED, DOD_ROLE_DEFENDER, 2, -1);
	assert(p.goal == DOD_GOAL_DEFEND && p.count == 2 && p.steps[0] == DOD_ACT_MOVE_POINT && p.steps[1] == DOD_ACT_GUARD);
	/* empty clip, nobody around: reload */
	p = Dod_GoapThink(0, DOD_ROLE_ATTACKER, 2, -1);
	assert(p.goal == DOD_GOAL_RELOAD && p.count == 1 && p.steps[0] == DOD_ACT_RELOAD);
	/* empty clip with an enemy in sight: cover first, then (out of sight) reload */
	p = Dod_GoapThink(DOD_WS_ENEMY_VISIBLE | DOD_WS_ENEMY_KNOWN, DOD_ROLE_ATTACKER, 2, -1);
	assert(p.goal == DOD_GOAL_COVER && p.count == 1 && p.steps[0] == DOD_ACT_TAKE_COVER);
	p = Dod_GoapThink(DOD_WS_ENEMY_KNOWN | DOD_WS_IN_COVER, DOD_ROLE_ATTACKER, 2, -1);
	assert(p.goal == DOD_GOAL_RELOAD);
	/* hurt and hunted: take cover rather than fight on */
	p = Dod_GoapThink(ammo | DOD_WS_LOW_HEALTH | DOD_WS_ENEMY_VISIBLE | DOD_WS_ENEMY_KNOWN, DOD_ROLE_ATTACKER, 2, -1);
	assert(p.goal == DOD_GOAL_COVER);
	/* hurt, in cover, with a friend elsewhere: regroup */
	p = Dod_GoapThink(ammo | DOD_WS_LOW_HEALTH | DOD_WS_IN_COVER, DOD_ROLE_ATTACKER, 2, -1);
	assert(p.goal == DOD_GOAL_REGROUP && p.steps[0] == DOD_ACT_REGROUP);
	p = Dod_GoapThink(ammo | DOD_WS_LOW_HEALTH | DOD_WS_IN_COVER, DOD_ROLE_ATTACKER, 1, -1);
	assert(p.goal == -1 || p.goal == DOD_GOAL_CAPTURE);	/* alone: nobody to regroup with */
	/* nothing special: an attacker goes and captures, a defender guards */
	p = Dod_GoapThink(ammo, DOD_ROLE_ATTACKER, 2, -1);
	assert(p.goal == DOD_GOAL_CAPTURE && p.count == 2 && p.steps[0] == DOD_ACT_MOVE_POINT && p.steps[1] == DOD_ACT_CAPTURE);
	p = Dod_GoapThink(ammo | DOD_WS_AT_POINT, DOD_ROLE_ATTACKER, 2, -1);
	assert(p.goal == DOD_GOAL_CAPTURE && p.count == 1 && p.steps[0] == DOD_ACT_CAPTURE);
	p = Dod_GoapThink(ammo | DOD_WS_AT_POINT | DOD_WS_POINT_HELD, DOD_ROLE_ATTACKER, 2, -1);
	assert(p.goal == -1 && p.count == 0);	/* the point is ours: nothing to do */
	p = Dod_GoapThink(ammo | DOD_WS_POINT_HELD, DOD_ROLE_DEFENDER, 2, -1);
	assert(p.goal == DOD_GOAL_DEFEND && p.steps[0] == DOD_ACT_MOVE_POINT);
	p = Dod_GoapThink(ammo | DOD_WS_AT_POINT | DOD_WS_POINT_HELD, DOD_ROLE_DEFENDER, 2, -1);
	assert(p.goal == -1 && p.count == 0);	/* guarding already */

	/* a held goal's bonus stops flapping between near-equal goals */
	assert(Dod_GoapPickGoal(ammo | DOD_WS_ENEMY_KNOWN, DOD_ROLE_ATTACKER, 2, DOD_GOAL_CAPTURE) == DOD_GOAL_CAPTURE);
	assert(Dod_GoapPickGoal(ammo | DOD_WS_ENEMY_KNOWN, DOD_ROLE_ATTACKER, 2, -1) == DOD_GOAL_CAPTURE);	/* 55 vs 50 */
	assert(Dod_GoapPickGoal(ammo | DOD_WS_ENEMY_KNOWN | DOD_WS_ENEMY_VISIBLE, DOD_ROLE_ATTACKER, 2, DOD_GOAL_CAPTURE) == DOD_GOAL_HUNT);

	/* transitions */
	assert(Dod_GoapStepApplicable(DOD_ACT_ATTACK, ammo | DOD_WS_ENEMY_VISIBLE));
	assert(!Dod_GoapStepApplicable(DOD_ACT_ATTACK, DOD_WS_ENEMY_VISIBLE));
	assert(!Dod_GoapStepApplicable(DOD_ACT_CAPTURE, ammo));
	assert(Dod_GoapStepApply(DOD_ACT_ATTACK, ammo | DOD_WS_ENEMY_VISIBLE | DOD_WS_ENEMY_KNOWN) == ammo);
	assert(!Dod_GoapStepApplicable(DOD_ACT_NONE, 0) && !Dod_GoapStepApplicable(99, 0));

	/* roles: a lone bot attacks; with a held point a third (at least one) of the team defends */
	assert(Dod_BotRole(0, 1, 2) == DOD_ROLE_ATTACKER);
	assert(Dod_BotRole(0, 3, 0) == DOD_ROLE_ATTACKER && Dod_BotRole(2, 3, 0) == DOD_ROLE_ATTACKER);
	assert(Dod_BotRole(0, 2, 1) == DOD_ROLE_ATTACKER && Dod_BotRole(1, 2, 1) == DOD_ROLE_DEFENDER);
	assert(Dod_BotRole(0, 6, 1) == DOD_ROLE_ATTACKER && Dod_BotRole(3, 6, 1) == DOD_ROLE_ATTACKER);
	assert(Dod_BotRole(4, 6, 1) == DOD_ROLE_DEFENDER && Dod_BotRole(5, 6, 1) == DOD_ROLE_DEFENDER);

	/* role-aware objectives: attackers spread over the uncaptured points, defenders guard held ones */
	memset(o, 0, sizeof(o));
	o[0].id = 0; o[0].owner = DOD_TEAM_ALLIES; o[0].distance = 100.0f;
	o[1].id = 1; o[1].owner = DOD_TEAM_AXIS; o[1].distance = 500.0f;
	o[2].id = 2; o[2].owner = DOD_TEAM_AXIS; o[2].distance = 700.0f;
	o[0].can_cap[DOD_TEAM_AXIS] = o[1].can_cap[DOD_TEAM_ALLIES] = o[2].can_cap[DOD_TEAM_ALLIES] = 1;
	o[0].can_cap[DOD_TEAM_ALLIES] = 1;
	id = Dod_BotChooseObjectiveRole(o, 3, DOD_TEAM_ALLIES, -1, DOD_ROLE_ATTACKER, &kind);
	assert(id == 1 && kind == DOD_BOT_GOAL_ATTACK);
	o[1].crowd = 1;	/* a teammate is already going to point 1: the next attacker takes point 2 */
	id = Dod_BotChooseObjectiveRole(o, 3, DOD_TEAM_ALLIES, -1, DOD_ROLE_ATTACKER, &kind);
	assert(id == 2 && kind == DOD_BOT_GOAL_ATTACK);
	id = Dod_BotChooseObjectiveRole(o, 3, DOD_TEAM_ALLIES, -1, DOD_ROLE_DEFENDER, &kind);
	assert(id == 0 && kind == DOD_BOT_GOAL_DEFEND);
	o[0].capturing_team = DOD_TEAM_AXIS;	/* under attack: defenders go there */
	id = Dod_BotChooseObjectiveRole(o, 3, DOD_TEAM_ALLIES, -1, DOD_ROLE_DEFENDER, &kind);
	assert(id == 0 && kind == DOD_BOT_GOAL_DEFEND);
	/* a defender with nothing held takes something instead */
	o[0].owner = DOD_TEAM_AXIS;
	id = Dod_BotChooseObjectiveRole(o, 3, DOD_TEAM_ALLIES, -1, DOD_ROLE_DEFENDER, &kind);
	assert(id >= 0 && kind == DOD_BOT_GOAL_ATTACK);
}

static void test_perception(void)
{
	int skill;
	float dx, dy;
	const DodBotSkill med = Dod_BotSkillParams(1);

	/* the presets get stricter with skill and stay inside the "human" reaction window */
	for (skill = 0; skill < 3; skill++) {
		const DodBotSkill a = Dod_BotSkillParams(skill), b = Dod_BotSkillParams(skill + 1);
		assert(a.fov_half <= b.fov_half && a.react_min >= b.react_min && a.aim_base > b.aim_base && a.turn_rate < b.turn_rate);
		assert(Dod_BotAimErrorEx(&a, 600.0f, 100.0f, 100.0f, 0.0f) > Dod_BotAimErrorEx(&b, 600.0f, 100.0f, 100.0f, 0.0f));
	}
	assert(Dod_BotSkillParams(-3).aim_base == Dod_BotSkillParams(0).aim_base && Dod_BotSkillParams(9).aim_base == Dod_BotSkillParams(3).aim_base);
	assert(med.fov_half * 2.0f >= 90.0f && med.fov_half * 2.0f <= 110.0f);	/* medium sees a ~100 degree cone */
	for (skill = 0; skill < 4; skill++) {
		const DodBotSkill sk = Dod_BotSkillParams(skill);
		assert(Dod_BotReactionDelay(&sk, 0.0f) >= 0.15f - 0.001f && Dod_BotReactionDelay(&sk, 1.0f) <= 0.70f + 0.001f);
	}
	assert(Dod_BotReactionDelay(&med, 0.0f) == med.react_min && Dod_BotReactionDelay(&med, 1.0f) == med.react_max);
	assert(Dod_BotReactionDelay(&med, 0.5f) > Dod_BotReactionDelay(&med, 0.1f) && Dod_BotReactionDelay(NULL, 0.5f) > 0.0f);

	/* the view cone: in front yes, behind and to the side no, across the +-180 seam */
	assert(Dod_BotInFov(0.0f, 30.0f, 50.0f) && Dod_BotInFov(0.0f, -49.0f, 50.0f));
	assert(!Dod_BotInFov(0.0f, 80.0f, 50.0f) && !Dod_BotInFov(0.0f, 180.0f, 50.0f));
	assert(Dod_BotInFov(170.0f, -170.0f, 50.0f) && !Dod_BotInFov(170.0f, -100.0f, 50.0f));

	/* aim error: wider with distance, target speed and own movement; tighter the longer it tracks; floor kept */
	assert(Dod_BotAimErrorEx(&med, 1000.0f, 0, 0, 0) > Dod_BotAimErrorEx(&med, 200.0f, 0, 0, 0));
	assert(Dod_BotAimErrorEx(&med, 400.0f, 250.0f, 0, 0) > Dod_BotAimErrorEx(&med, 400.0f, 0, 0, 0));
	assert(Dod_BotAimErrorEx(&med, 400.0f, 0, 250.0f, 0) > Dod_BotAimErrorEx(&med, 400.0f, 0, 0, 0));
	assert(Dod_BotAimErrorEx(&med, 400.0f, 0, 0, 2.0f) < Dod_BotAimErrorEx(&med, 400.0f, 0, 0, 0.0f));
	assert(Dod_BotAimErrorEx(&med, 400.0f, 0, 0, 1000.0f) >= Dod_BotAimErrorEx(&med, 400.0f, 0, 0, 0.0f) * DOD_BOT_SETTLE_FLOOR - 0.001f);
	/* a long shot at a running target misses a standing man's width (~1 degree at 1000 units) most of the time,
	 * a close still target is near the limit of what is hittable */
	assert(Dod_BotAimErrorEx(&med, 1200.0f, 150.0f, 0, 0.0f) > 6.0f);
	assert(Dod_BotAimErrorEx(&med, 100.0f, 0, 0, 3.0f) < 3.0f);

	/* hearing: gunfire carries, footsteps do not, walking is silent, walls muffle steps, skill scales it */
	assert(Dod_BotHearRadius(&med, 1, 0.0f, 0) > Dod_BotHearRadius(&med, 0, 250.0f, 1));
	assert(Dod_BotHearRadius(&med, 0, 100.0f, 1) == 0.0f && Dod_BotHearRadius(&med, 0, 0.0f, 1) == 0.0f);
	assert(Dod_BotHearRadius(&med, 0, 250.0f, 0) < Dod_BotHearRadius(&med, 0, 250.0f, 1));
	{
		const DodBotSkill easy = Dod_BotSkillParams(0), expert = Dod_BotSkillParams(3);
		assert(Dod_BotHearRadius(&easy, 1, 0, 1) < Dod_BotHearRadius(&expert, 1, 0, 1) && Dod_BotHearRadius(NULL, 1, 0, 1) == 0.0f);
	}
	/* a heard position is only approximate, and worse the farther away */
	Dod_BotHeardOffset(1000.0f, 0.0f, 1.0f, &dx, &dy);
	assert(dx > 150.0f && fabsf(dy) < 1.0f);
	Dod_BotHeardOffset(0.0f, 0.5f, 0.0f, &dx, &dy);
	assert(dx * dx + dy * dy > 15.0f * 15.0f && dx * dx + dy * dy < 60.0f * 60.0f);

	/* burst discipline */
	assert(Dod_BotBurstLength(1, 100.0f) > Dod_BotBurstLength(1, 500.0f) && Dod_BotBurstLength(1, 500.0f) > Dod_BotBurstLength(1, 1500.0f));
	assert(Dod_BotBurstPause(1, 1500.0f, 0.0f) > Dod_BotBurstPause(1, 100.0f, 1.0f) - 0.001f);
	assert(Dod_BotBurstPause(1, 100.0f, 0.0f) < Dod_BotBurstPause(1, 100.0f, 1.0f) && Dod_BotBurstPause(0, 100.0f, 2.0f) <= 0.35f);
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
	test_nav();
	test_nav_real();
	test_goap();
	test_perception();
	return 0;
}
