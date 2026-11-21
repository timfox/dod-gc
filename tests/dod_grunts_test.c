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

static void test_script_actor(void)
{
	DodGruntPool p;
	DodGrunt_Init(&p, 3);
	const int i = DodGrunt_FreeScriptSlot(&p);
	assert(i == DOD_GRUNT_MAX - 1);
	DodGrunt_SpawnScripted(&p, i, DOD_TEAM_AXIS, 0, 0, 0, 0, 0.0f);
	assert(p.state[i] == DOD_GRUNT_HOLD && p.script[i] == DOD_GRUNT_SCRIPTED);
	assert(DodGrunt_FreeScriptSlot(&p) == DOD_GRUNT_MAX - 2);
	/* holding: a far enemy turns it but does not make it wander; a close one makes it shoot */
	DodGrunt_Looked(&p, i, 0.1f, 1, 2, 0, 1000, 1000, 0);
	assert(p.state[i] == DOD_GRUNT_HOLD && fabsf(p.heading[i] - 90.0f) < 0.01f);
	DodGrunt_Looked(&p, i, 0.2f, 1, 2, 100, 0, 100, 0);
	assert(p.state[i] == DOD_GRUNT_SHOOT);
	DodGrunt_Looked(&p, i, 0.3f, 0, 0, 0, 0, 0, 1);
	assert(p.state[i] == DOD_GRUNT_HOLD);	/* the cvar's rush does not apply to script actors */
	/* ai off: blind and still */
	DodGrunt_SetAI(&p, i, 0);
	DodGrunt_Looked(&p, i, 0.4f, 1, 2, 100, 0, 100, 0);
	assert(p.state[i] == DOD_GRUNT_HOLD && p.target[i] == 0);
	assert(!DodGrunt_ShotDue(&p, i, 10.0f, 1));
	float x0 = p.x[i];
	DodGrunt_Move(&p, i, 1.0f, 0.1f);
	assert(p.x[i] == x0);
	/* runto: arrives, holds, reports once */
	DodGrunt_OrderMove(&p, i, 400, 0, 0, 1);
	float now = 1.0f;
	int f;
	for (f = 0; f < 200 && !p.order_done[i]; f++, now += 0.05f) {
		DodGrunt_Looked(&p, i, now, 1, 2, 100, 0, 100, 0);	/* an order wins over what it sees */
		assert(p.state[i] == DOD_GRUNT_ADVANCE);
		DodGrunt_Move(&p, i, now, 0.05f);
	}
	assert(f < 60);			/* 400 units at 190 u/s: a bit over 2 s */
	assert(fabsf(p.x[i] - 400.0f) <= DOD_GRUNT_ARRIVE + 0.01f);
	assert(p.state[i] == DOD_GRUNT_HOLD);
	assert(DodGrunt_TakeOrderDone(&p, i) == 1 && DodGrunt_TakeOrderDone(&p, i) == 0);
	/* turnto */
	DodGrunt_Face(&p, i, p.x[i], p.y[i] - 100.0f);
	for (f = 0; f < 20; f++)
		DodGrunt_Move(&p, i, now, 0.05f);
	assert(fabsf(p.yaw[i] + 90.0f) < 0.5f);
	/* attackplayer: AI back on, runs at a far enemy */
	DodGrunt_SetAttack(&p, i, 1);
	DodGrunt_Looked(&p, i, now, 1, 2, p.x[i] + 1500.0f, p.y[i], 1500, 0);
	assert(p.state[i] == DOD_GRUNT_ADVANCE);
	/* a dead script actor never comes back */
	assert(DodGrunt_Hurt(&p, i, 1000, now));
	assert(!DodGrunt_RespawnDue(&p, i, now + 1000.0f));
	/* an ordinary grunt still does */
	DodGrunt_Spawn(&p, 0, DOD_TEAM_AXIS, 0, 0, 0, 0, 0.0f, 0);
	assert(p.script[0] == 0);
	assert(DodGrunt_Hurt(&p, 0, 1000, 0.0f) && DodGrunt_RespawnDue(&p, 0, DOD_GRUNT_RESPAWN));
}

static int linked(const DodPaths *g, int a, int b)
{
	int k;
	for (k = 0; k < DOD_PATH_LINKS; k++)
		if (g->link[a][k] == b)
			return 1;
	return 0;
}

/* a wall along x = 0 from y = -50 to 150: only the far end (y > 150) gets round it */
static int test_walkable(void *ctx, const float a[3], const float b[3])
{
	(void)ctx;
	if ((a[0] < 0) == (b[0] < 0))
		return 1;
	{
		const float t = (0 - a[0]) / (b[0] - a[0]), y = a[1] + t * (b[1] - a[1]);
		return y > 150.0f || y < -50.0f;
	}
}

static void test_paths(void)
{
	/* a U round the wall: 0 west, 1 north-west, 2 north-east, 3 east, plus one far away on its own */
	static const char text[] =
		"# mohaa path nodes: x y z grav spawnflags targetname\n"
		"-100 0 0 0 0 start\n"
		"-100 250 0 0 0 -\n"
		"100 250 0 1 4 $Corner\n"
		"100 0 0 0 0 end\n"
		"5000 5000 0 0 0 alone";	/* no newline and no NUL past the length */
	DodPaths g;
	assert(DodPaths_Load(&g, text, sizeof(text) - 1) == 5);
	assert(DodPaths_Find(&g, "corner") == 2 && DodPaths_Find(&g, "$START") == 0 && DodPaths_Find(&g, "-") == -1);
	assert(g.pos[4][0] == 5000.0f && DodPaths_Find(&g, "alone") == 4);
	const int traces = DodPaths_Link(&g, test_walkable, NULL, DOD_PATH_REACH);
	assert(traces > 0);
	assert(!linked(&g, 0, 3) && linked(&g, 0, 1) && linked(&g, 1, 2) && linked(&g, 2, 3) && linked(&g, 3, 2));
	assert(DodPaths_NextHop(&g, 0, 3) == 1);
	assert(DodPaths_NextHop(&g, 1, 3) == 2);
	assert(DodPaths_NextHop(&g, 2, 3) == 3);
	assert(DodPaths_NextHop(&g, 0, 4) == -1 && DodPaths_NextHop(&g, 3, 3) == 3);
	{
		const float p[3] = { -90, 10, 0 };
		assert(DodPaths_Nearest(&g, p) == 0);
	}
	/* a scripted grunt west of the wall told to run east goes round it node by node */
	DodGruntPool p;
	DodGrunt_Init(&p, 5);
	p.paths = &g;
	const int i = DodGrunt_FreeScriptSlot(&p);
	DodGrunt_SpawnScripted(&p, i, DOD_TEAM_AXIS, -120, 0, 36, 0, 0.0f);
	DodGrunt_OrderMove(&p, i, 150, 0, 0, 1);
	assert(p.path_node[i] == 0 && p.goal_node[i] == 3);
	int f, worst_x_at_wall = 0;
	float now = 0.0f;
	for (f = 0; f < 400 && !p.order_done[i]; f++, now += 0.05f) {
		DodGrunt_Move(&p, i, now, 0.05f);
		if (fabsf(p.x[i]) < 5.0f && p.y[i] < 150.0f)
			worst_x_at_wall = 1;	/* crossed x = 0 inside the wall */
	}
	assert(p.order_done[i] && !worst_x_at_wall);
	assert(fabsf(p.x[i] - 150.0f) <= DOD_GRUNT_ARRIVE + 0.01f && p.path_node[i] == DOD_PATH_NONE);
	/* a goal nearer than the first node goes straight */
	DodGrunt_OrderMove(&p, i, 160, 10, 0, 0);
	assert(p.path_node[i] == DOD_PATH_NONE && p.order_x[i] == 160.0f);
	DodPaths_Free(&g);
	assert(g.count == 0 && g.block == NULL);
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
	test_script_actor();
	test_paths();
	printf("dod_grunts_test: ok (row %d bytes, pool %d bytes)\n", DodGrunt_RowBytes(), (int)sizeof(DodGruntPool));
	return 0;
}
