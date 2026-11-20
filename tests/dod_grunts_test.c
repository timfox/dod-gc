#include "dod_grunts.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void test_count(void)
{
	assert(Dod_GruntTargetCount(0, 400) == 0);
	assert(Dod_GruntTargetCount(10, 400) == 10);
	assert(Dod_GruntTargetCount(500, 400) == DOD_GRUNT_MAX);
	/* the edict reserve: with 40 free and 24 held back only 16 fit */
	assert(Dod_GruntTargetCount(30, 40) == 16);
	assert(Dod_GruntTargetCount(30, 10) == 0);
	assert(Dod_GruntTargetCount(-3, 400) == 0);
}

static void test_row_is_small(void)
{
	/* the whole point: a row is tens of bytes, not a client slot */
	assert(DodGrunt_RowBytes() < 100);
	assert(sizeof(DodGruntPool) < 6000);
}

static void test_look_budget_and_stagger(void)
{
	DodGruntPool p;
	int out[DOD_GRUNT_SENSE_BUDGET], i, n, total = 0, seen[DOD_GRUNT_MAX];
	float now = 0.0f;
	DodGrunt_Init(&p, 7);
	for (i = 0; i < 32; i++)
		DodGrunt_Spawn(&p, i, DOD_TEAM_AXIS, (float)i * 10.0f, 0, 0, 0, 0.0f, 1);
	memset(seen, 0, sizeof(seen));
	/* run 2 seconds at 60 FPS: no frame exceeds the budget, every grunt looks, and looks are not bunched */
	for (int f = 0; f < 120; f++, now += 1.0f / 60.0f) {
		n = DodGrunt_DueLooks(&p, now, DOD_GRUNT_SENSE_BUDGET, out);
		assert(n <= DOD_GRUNT_SENSE_BUDGET);
		for (i = 0; i < n; i++) {
			seen[out[i]]++;
			DodGrunt_Looked(&p, out[i], now, 0, 0, 0, 0, 0, 1);
		}
		total += n;
	}
	for (i = 0; i < 32; i++)
		assert(seen[i] >= 4);	/* about 3 per second */
	assert(total >= 32 * 4 && total <= 32 * 9);
	assert(p.stats.looks == (unsigned)total);
}

static void test_dead_never_look(void)
{
	DodGruntPool p;
	int out[4];
	DodGrunt_Init(&p, 3);
	DodGrunt_Spawn(&p, 0, DOD_TEAM_ALLIES, 0, 0, 0, 0, 0.0f, 0);
	assert(DodGrunt_Hurt(&p, 0, 1000, 1.0f) == 1);
	assert(DodGrunt_DueLooks(&p, 5.0f, 4, out) == 0);
	assert(!DodGrunt_RespawnDue(&p, 0, 5.0f));
	assert(DodGrunt_RespawnDue(&p, 0, 1.0f + DOD_GRUNT_RESPAWN));
	assert(DodGrunt_Hurt(&p, 0, 5, 5.0f) == 0);	/* the dead take no more */
}

static void test_states(void)
{
	DodGruntPool p;
	DodGrunt_Init(&p, 11);
	DodGrunt_Spawn(&p, 0, DOD_TEAM_AXIS, 0, 0, 0, 0, 0.0f, 0);
	assert(p.state[0] == DOD_GRUNT_WANDER);
	DodGrunt_Looked(&p, 0, 1.0f, 1, 1, 400, 0, 400, 0);
	assert(p.state[0] == DOD_GRUNT_SHOOT && p.target[0] == 1);
	DodGrunt_Looked(&p, 0, 1.4f, 1, 1, 2000, 0, 2000, 1);
	assert(p.state[0] == DOD_GRUNT_ADVANCE);
	DodGrunt_Looked(&p, 0, 1.8f, 0, 0, 0, 0, 0, 0);
	assert(p.state[0] == DOD_GRUNT_ADVANCE || p.state[0] == DOD_GRUNT_WANDER);
	assert(p.target[0] == 0);
}

static void test_rusher_runs_to_target_and_stops_close(void)
{
	DodGruntPool p;
	float now = 0.0f;
	DodGrunt_Init(&p, 5);
	DodGrunt_Spawn(&p, 0, DOD_TEAM_AXIS, 0, 0, 0, 90.0f, 0.0f, 1);
	DodGrunt_Looked(&p, 0, 0.0f, 1, 1, 1000.0f, 0.0f, 1000.0f, 1);
	for (int f = 0; f < 600; f++, now += 1.0f / 60.0f) {
		DodGrunt_Looked(&p, 0, now, 1, 1, 1000.0f, 0.0f, fabsf(1000.0f - p.x[0]), 1);
		DodGrunt_Move(&p, 0, now, 1.0f / 60.0f);
	}
	/* it ran at the target along +x and stopped about DOD_GRUNT_CLOSE short of it */
	assert(p.x[0] > 1000.0f - DOD_GRUNT_CLOSE * 3.0f - 1.0f);
	assert(p.x[0] < 1000.0f - DOD_GRUNT_CLOSE + DOD_GRUNT_RUN / 60.0f + 1.0f);
	assert(fabsf(p.y[0]) < 1.0f);
}

static void test_rusher_without_target_runs_its_heading(void)
{
	DodGruntPool p;
	DodGrunt_Init(&p, 5);
	DodGrunt_Spawn(&p, 0, DOD_TEAM_AXIS, 0, 0, 0, 0.0f, 0.0f, 1);
	for (int f = 0; f < 60; f++)
		DodGrunt_Move(&p, 0, 0.0f, 1.0f / 60.0f);
	assert(fabsf(p.x[0] - DOD_GRUNT_RUN) < 2.0f);
}

static void test_blocked_stops_and_turns(void)
{
	DodGruntPool p;
	float h;
	DodGrunt_Init(&p, 9);
	DodGrunt_Spawn(&p, 0, DOD_TEAM_AXIS, 0, 0, 0, 0.0f, 0.0f, 0);
	h = p.heading[0];
	DodGrunt_MoveChecked(&p, 0, 0);
	assert(p.blocked[0] == 1);
	assert(fabsf(p.heading[0] - h) >= 59.0f);
	{
		float x = p.x[0];
		DodGrunt_Move(&p, 0, 0.0f, 0.1f);
		assert(p.x[0] == x);	/* held still until the next check clears it */
	}
	DodGrunt_MoveChecked(&p, 0, 1);
	assert(p.blocked[0] == 0);
}

static void test_move_check_stagger(void)
{
	DodGruntPool p;
	int checks = 0;
	float now = 0.0f;
	DodGrunt_Init(&p, 2);
	for (int i = 0; i < 32; i++)
		DodGrunt_Spawn(&p, i, DOD_TEAM_AXIS, 0, 0, 0, 0, 0.0f, 1);
	for (int f = 0; f < 60; f++, now += 1.0f / 60.0f)
		for (int i = 0; i < 32; i++)
			checks += DodGrunt_CheckDue(&p, i, now);
	/* about 6.7 a second each: a few hundred hull traces a second for 32 grunts, 5 or so per frame */
	assert(checks >= 32 * 6 && checks <= 32 * 8);
}

static void test_shooting(void)
{
	DodGruntPool p;
	int shots = 0;
	float now = 0.0f;
	DodGrunt_Init(&p, 4);
	DodGrunt_Spawn(&p, 0, DOD_TEAM_AXIS, 0, 0, 0, 0, 0.0f, 0);
	assert(!DodGrunt_ShotDue(&p, 0, 10.0f, 1));	/* not shooting */
	DodGrunt_Looked(&p, 0, 0.0f, 1, 1, 300, 0, 300, 0);
	for (int f = 0; f < 600; f++, now += 1.0f / 60.0f)
		shots += DodGrunt_ShotDue(&p, 0, now, 0);
	assert(shots >= 4 && shots <= 11);	/* one every 1 to 2 s over 10 s */
	assert(DodGrunt_HitChance(100, 0) > DodGrunt_HitChance(1500, 0));
	assert(DodGrunt_HitChance(500, 3) > DodGrunt_HitChance(500, 0));
	assert(DodGrunt_HitChance(100000, 0) >= 0.05f && DodGrunt_HitChance(0, 3) <= 0.95f);
	assert(DodGrunt_ShotHits(100, 0, 0.1f) && !DodGrunt_ShotHits(1700, 0, 0.5f));
}

static void test_rand_range(void)
{
	DodGruntPool p;
	DodGrunt_Init(&p, 1);
	for (int i = 0; i < 10000; i++) {
		float r = DodGrunt_Rand(&p);
		assert(r >= 0.0f && r < 1.0f);
	}
}

int main(void)
{
	test_count();
	test_row_is_small();
	test_look_budget_and_stagger();
	test_dead_never_look();
	test_states();
	test_rusher_runs_to_target_and_stops_close();
	test_rusher_without_target_runs_its_heading();
	test_blocked_stops_and_turns();
	test_move_check_stagger();
	test_shooting();
	test_rand_range();
	printf("dod_grunts_test: ok (row %d bytes, pool %d bytes)\n", DodGrunt_RowBytes(), (int)sizeof(DodGruntPool));
	return 0;
}
