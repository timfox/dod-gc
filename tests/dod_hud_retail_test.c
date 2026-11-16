#include "dod_hud_retail.h"

#include <assert.h>
#include <string.h>

static DodRetailInput base(void)
{
	DodRetailInput in;
	memset(&in, 0, sizeof(in));
	in.screen_w = 640;
	in.screen_h = 480;
	in.health = 100;
	in.stamina = 100.0f;
	in.seconds_left = -1;
	return in;
}

static const DodRetailDraw *find(const DodRetailDraw *d, int n, int sheet, int cx, int cy)
{
	int i;
	for (i = 0; i < n; i++)
		if (d[i].cell.sheet == sheet && d[i].cell.x == cx && d[i].cell.y == cy)
			return &d[i];
	return 0;
}

static void test_panel_full_health(void)
{
	DodRetailInput in = base();
	DodRetailDraw d[DOD_RETAIL_MAX_DRAWS];
	int n = Dod_RetailHudLayout(&in, d, DOD_RETAIL_MAX_DRAWS);
	const DodRetailDraw *p;

	p = find(d, n, DOD_SHEET_LAYOUT, 0, 172);	/* hud_main */
	assert(p && p->x == 0 && p->y == 480 - 84 && p->cell.w == 113);
	p = find(d, n, DOD_SHEET_LAYOUT, 86, 93);	/* full stamina tube */
	assert(p && p->cell.h == 72 && p->x == 8 && p->y == 480 - 72);
	p = find(d, n, DOD_SHEET_LAYOUT, 51, 110);	/* full health bar */
	assert(p && p->cell.h == 47 && p->x == 99 && p->y == 396 + 28);
	p = find(d, n, DOD_SHEET_LAYOUT, 0, 111);	/* overlay: one row at full health */
	assert(p && p->cell.h == 1 && p->x == 54 && p->y == 396 + 27);
}

static void test_half_health_and_stamina(void)
{
	DodRetailInput in = base();
	DodRetailDraw d[DOD_RETAIL_MAX_DRAWS];
	int n;
	const DodRetailDraw *p;

	in.health = 50;
	in.stamina = 50.0f;
	n = Dod_RetailHudLayout(&in, d, DOD_RETAIL_MAX_DRAWS);
	/* stamina: bottom 36 rows of the cell, bottom-aligned */
	p = find(d, n, DOD_SHEET_LAYOUT, 86, 93 + 36);
	assert(p && p->cell.h == 36 && p->y == 480 - 36);
	/* health bar: 23 rows + 1 from the top of the cell, pushed down */
	p = find(d, n, DOD_SHEET_LAYOUT, 51, 110);
	assert(p && p->cell.h == 24 && p->y == 396 + 28 + (47 - 24));
	p = find(d, n, DOD_SHEET_LAYOUT, 0, 111);
	assert(p && p->cell.h == 24);
}

static void test_dead_or_spectating_hides_panel(void)
{
	DodRetailInput in = base();
	DodRetailDraw d[DOD_RETAIL_MAX_DRAWS];
	in.dead = 1;
	assert(!find(d, Dod_RetailHudLayout(&in, d, DOD_RETAIL_MAX_DRAWS), DOD_SHEET_LAYOUT, 0, 172));
	in = base();
	in.spectating = 1;
	assert(!find(d, Dod_RetailHudLayout(&in, d, DOD_RETAIL_MAX_DRAWS), DOD_SHEET_LAYOUT, 0, 172));
}

static void test_timer_digits(void)
{
	DodRetailInput in = base();
	DodRetailDraw d[DOD_RETAIL_MAX_DRAWS];
	int n, i, digits = 0;
	DodRetailCell c;

	c = Dod_RetailTimerDigit(1); assert(c.x == 114 && c.y == 95);
	c = Dod_RetailTimerDigit(5); assert(c.x == 166 && c.y == 95);
	c = Dod_RetailTimerDigit(6); assert(c.x == 114 && c.y == 114);
	c = Dod_RetailTimerDigit(0); assert(c.x == 166 && c.y == 114);

	in.seconds_left = 754;	/* 12:34 */
	n = Dod_RetailHudLayout(&in, d, DOD_RETAIL_MAX_DRAWS);
	assert(find(d, n, DOD_SHEET_LAYOUT, 0, 0)->y == 2);
	for (i = 0; i < n; i++) {
		if (d[i].cell.w != 12 || d[i].cell.h != 18)
			continue;
		assert(d[i].y == 8);
		if (d[i].x == 10) assert(d[i].cell.x == Dod_RetailTimerDigit(1).x && d[i].cell.y == 95);
		if (d[i].x == 24) assert(d[i].cell.x == Dod_RetailTimerDigit(2).x && d[i].cell.y == 95);
		if (d[i].x == 44) assert(d[i].cell.x == Dod_RetailTimerDigit(3).x);
		if (d[i].x == 58) assert(d[i].cell.x == Dod_RetailTimerDigit(4).x);
		digits++;
	}
	assert(digits == 4);
}

static void test_crosshair(void)
{
	DodRetailInput in = base();
	DodRetailDraw d[DOD_RETAIL_MAX_DRAWS];
	int n, i, bars = 0;

	in.crosshair = 1;
	in.crosshair_gap = 4;
	n = Dod_RetailHudLayout(&in, d, DOD_RETAIL_MAX_DRAWS);
	for (i = 0; i < n; i++) {
		if (d[i].cell.sheet != DOD_SHEET_CROSS)
			continue;
		bars++;
		if (d[i].cell.w == 10 && d[i].x < 320) assert(d[i].x == 320 - 4 - 10 + 2 && d[i].y == 240 - 1);
		if (d[i].cell.w == 10 && d[i].x > 320) assert(d[i].x == 324);
		if (d[i].cell.h == 10 && d[i].y < 240) assert(d[i].y == 240 - 4 - 10 + 2 && d[i].x == 319);
		if (d[i].cell.w == 3 && d[i].cell.h == 3) assert(d[i].x == 319 && d[i].y == 239);
	}
	assert(bars == 5);
}

int main(void)
{
	test_panel_full_health();
	test_half_health_and_stamina();
	test_dead_or_spectating_hides_panel();
	test_timer_digits();
	test_crosshair();
	return 0;
}
