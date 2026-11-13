#include "dod_hud.h"

#include <assert.h>
#include <string.h>

static DodHudState base_state(void)
{
	DodHudState s;
	memset(&s, 0, sizeof(s));
	s.phase = 1;
	s.seconds_left = 125;
	s.allies_score = 12;
	s.axis_score = 9;
	return s;
}

static int find_text(const DodHudOp *ops, int count, const char *needle)
{
	int i;
	for (i = 0; i < count; i++)
		if (ops[i].kind == DOD_HUD_TEXT && strstr(ops[i].text, needle))
			return i;
	return -1;
}

static int count_icons(const DodHudOp *ops, int count)
{
	int i, n = 0;
	for (i = 0; i < count; i++)
		n += ops[i].kind == DOD_HUD_ICON;
	return n;
}

static void test_owner_packing(void)
{
	int owners[DOD_HUD_MAX_POINTS] = { 1, 2, 0, 2, 1, 1, 2, 0 };
	unsigned short packed = Dod_HudPackOwners(owners, DOD_HUD_MAX_POINTS);
	int i;
	for (i = 0; i < DOD_HUD_MAX_POINTS; i++)
		assert(Dod_HudOwnerOf(packed, i) == owners[i]);
	assert(Dod_HudOwnerOf(packed, -1) == 0 && Dod_HudOwnerOf(packed, DOD_HUD_MAX_POINTS) == 0);
	/* junk owners become neutral; a count past the limit is clamped; 3 is not a team */
	{
		int junk[DOD_HUD_MAX_POINTS] = { 7, -1, 3, 1, 0, 0, 0, 0 };
		packed = Dod_HudPackOwners(junk, 20);
		assert(Dod_HudOwnerOf(packed, 0) == 0 && Dod_HudOwnerOf(packed, 1) == 0);
		assert(Dod_HudOwnerOf(packed, 2) == 0 && Dod_HudOwnerOf(packed, 3) == 1);
		assert(Dod_HudOwnerOf(0xffff, 0) == 0);
	}
	assert(Dod_HudPackOwners(owners, 0) == 0);
}

static void test_layout_content(void)
{
	DodHudOp ops[DOD_HUD_MAX_OPS];
	DodHudState s = base_state();
	int owners[DOD_HUD_MAX_POINTS] = { 1, 0, 2 };
	int n, i;

	s.num_points = 3;
	s.owners = Dod_HudPackOwners(owners, 3);
	s.my_class = 1;
	s.grenades = 2;
	n = Dod_HudLayout(&s, 640, 480, 8, 16, ops, DOD_HUD_MAX_OPS);
	assert(n > 0 && n <= DOD_HUD_MAX_OPS);
	assert(find_text(ops, n, "2:05") >= 0);
	assert(find_text(ops, n, "ALLIES 12") >= 0 && find_text(ops, n, "AXIS 9") >= 0);
	assert(find_text(ops, n, "Rifleman  GRENADES 2") >= 0);
	assert(find_text(ops, n, "ROUND") < 0);			/* no match tally */
	assert(count_icons(ops, n) == 3);
	/* the flags map owner to cell, left to right */
	{
		int cells[3], k = 0;
		for (i = 0; i < n; i++)
			if (ops[i].kind == DOD_HUD_ICON)
				cells[k++] = ops[i].cell;
		assert(cells[0] == DOD_HUD_CELL_ALLIES && cells[1] == DOD_HUD_CELL_NEUTRAL && cells[2] == DOD_HUD_CELL_AXIS);
	}
	/* Allies are on the left of the clock and Axis on the right */
	assert(ops[find_text(ops, n, "ALLIES")].x < ops[find_text(ops, n, "2:05")].x);
	assert(ops[find_text(ops, n, "AXIS")].x > ops[find_text(ops, n, "2:05")].x);
	/* the flags sit under the clock line and are centred */
	{
		int first = -1, last = -1;
		for (i = 0; i < n; i++)
			if (ops[i].kind == DOD_HUD_ICON) {
				if (first < 0) first = i;
				last = i;
				assert(ops[i].y > ops[find_text(ops, n, "2:05")].y);
			}
		assert(ops[first].x + (ops[last].x + DOD_HUD_ICON_SIZE - ops[first].x) / 2 == 320);
	}

	/* match tally and the between-rounds label */
	s.rounds_to_win = 2;
	s.round_number = 2;
	s.won_allies = 1;
	n = Dod_HudLayout(&s, 640, 480, 8, 16, ops, DOD_HUD_MAX_OPS);
	assert(find_text(ops, n, "ROUND 2  1-0") >= 0);
	s.phase = 2;
	n = Dod_HudLayout(&s, 640, 480, 8, 16, ops, DOD_HUD_MAX_OPS);
	assert(find_text(ops, n, "NEXT ROUND") >= 0 && find_text(ops, n, "2:05") < 0);

	/* no clock when there is no time limit; no class line without a class */
	s = base_state();
	s.seconds_left = 0;
	n = Dod_HudLayout(&s, 640, 480, 8, 16, ops, DOD_HUD_MAX_OPS);
	assert(find_text(ops, n, ":") < 0 && find_text(ops, n, "GRENADES") < 0);
	assert(count_icons(ops, n) == 0);

	/* negative scores and too many points are clamped */
	s = base_state();
	s.allies_score = -4;
	s.num_points = 40;
	n = Dod_HudLayout(&s, 640, 480, 8, 16, ops, DOD_HUD_MAX_OPS);
	assert(find_text(ops, n, "ALLIES 0") >= 0);
	assert(count_icons(ops, n) == DOD_HUD_MAX_POINTS);
}

static void test_layout_bounds_and_limits(void)
{
	DodHudOp ops[DOD_HUD_MAX_OPS];
	DodHudState s = base_state();
	int sizes[][2] = { { 640, 480 }, { 320, 240 }, { 400, 300 } };
	int si, n, i, cap;

	s.num_points = DOD_HUD_MAX_POINTS;
	s.rounds_to_win = 3;
	s.round_number = 3;
	s.my_class = 6;
	s.grenades = 9;
	s.allies_score = 999;
	s.axis_score = 999;
	for (si = 0; si < 3; si++) {
		const int w = sizes[si][0], h = sizes[si][1];
		n = Dod_HudLayout(&s, w, h, 8, 16, ops, DOD_HUD_MAX_OPS);
		assert(n > 0 && n <= DOD_HUD_MAX_OPS);
		for (i = 0; i < n; i++) {
			assert(ops[i].x >= 0 && ops[i].y >= 0 && ops[i].y < h);
			if (ops[i].kind == DOD_HUD_ICON)
				assert(ops[i].x + DOD_HUD_ICON_SIZE <= w && ops[i].y + DOD_HUD_ICON_SIZE <= h);
			else
				assert(ops[i].x + (int)strlen(ops[i].text) * 8 <= w);
		}
	}
	/* never writes past a small buffer */
	for (cap = 1; cap < 12; cap++) {
		DodHudOp small[12];
		memset(small, 0x5a, sizeof(small));
		n = Dod_HudLayout(&s, 640, 480, 8, 16, small, cap);
		assert(n >= 0 && n <= cap);
		for (i = cap; i < 12; i++)
			assert(((unsigned char *)&small[i])[0] == 0x5a);
	}
	/* bad input yields nothing */
	assert(Dod_HudLayout(0, 640, 480, 8, 16, ops, 8) == 0);
	assert(Dod_HudLayout(&s, 0, 480, 8, 16, ops, 8) == 0);
	assert(Dod_HudLayout(&s, 640, 480, 0, 16, ops, 8) == 0);
	assert(Dod_HudLayout(&s, 640, 480, 8, 16, 0, 8) == 0);
	assert(Dod_HudLayout(&s, 640, 480, 8, 16, ops, 0) == 0);
	assert(Dod_HudClassName(-1)[0] == 0 && Dod_HudClassName(99)[0] == 0 && Dod_HudClassName(5)[0] == 'M');
}

int main(void)
{
	test_owner_packing();
	test_layout_content();
	test_layout_bounds_and_limits();
	return 0;
}
