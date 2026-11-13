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

static void test_team_pick(void)
{
	assert(Dod_BotPickTeam(0, 1) == DOD_TEAM_ALLIES);	/* human on Axis: bot joins Allies */
	assert(Dod_BotPickTeam(1, 0) == DOD_TEAM_AXIS);
	assert(Dod_BotPickTeam(2, 2) == DOD_TEAM_AXIS);		/* tie */
	assert(Dod_BotPickTeam(0, 0) == DOD_TEAM_AXIS);
	assert(Dod_BotPickTeam(3, 5) == DOD_TEAM_ALLIES);
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

int main(void)
{
	test_target_count();
	test_team_pick();
	test_class_pick();
	test_angles();
	test_aim_error();
	test_decide();
	return 0;
}
