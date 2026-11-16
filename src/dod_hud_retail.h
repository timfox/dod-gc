/* Retail Day of Defeat 1.3 HUD layout, recovered from the retail client
 * (CHudDodIcons::Draw, CObjectiveIcons::Draw, CHudDoDCrossHair). Header-only
 * and engine-free so the arithmetic is host-tested; the client only binds the
 * sprite sheets and calls SPR_DrawHoles with what these functions return.
 *
 * Coordinates are screen pixels. Retail anchors raw 640-res sprites to the
 * screen edges without scaling, so the numbers here are the retail ones. */
#ifndef DODGC_DOD_HUD_RETAIL_H
#define DODGC_DOD_HUD_RETAIL_H

/* A sprite cell: sheet index (DodRetailSheet) and its hud.txt rectangle. */
typedef struct DodRetailCell {
	int sheet;
	int x, y, w, h;
} DodRetailCell;

typedef enum {
	DOD_SHEET_LAYOUT = 0,	/* sprites/hud_layout.spr */
	DOD_SHEET_CROSS,	/* sprites/dodcross.spr */
	DOD_SHEET_COUNT
} DodRetailSheet;

static const char *const dod_retail_sheet_files[DOD_SHEET_COUNT] = {
	"sprites/hud_layout.spr",
	"sprites/dodcross.spr",
};

/* hud.txt (640) records the retail HUD draws. */
static const DodRetailCell dod_cell_main = { DOD_SHEET_LAYOUT, 0, 172, 113, 84 };
static const DodRetailCell dod_cell_stamina = { DOD_SHEET_LAYOUT, 86, 93, 16, 72 };
static const DodRetailCell dod_cell_healthbar = { DOD_SHEET_LAYOUT, 51, 110, 5, 47 };
static const DodRetailCell dod_cell_health_overlay = { DOD_SHEET_LAYOUT, 0, 111, 44, 47 };
static const DodRetailCell dod_cell_reinforcements = { DOD_SHEET_LAYOUT, 81, 0, 129, 58 };
static const DodRetailCell dod_cell_timer = { DOD_SHEET_LAYOUT, 0, 0, 80, 30 };
static const DodRetailCell dod_cell_cross_horiz = { DOD_SHEET_CROSS, 36, 0, 10, 3 };
static const DodRetailCell dod_cell_cross_vert = { DOD_SHEET_CROSS, 24, 0, 3, 10 };
static const DodRetailCell dod_cell_cross_dot = { DOD_SHEET_CROSS, 24, 12, 3, 3 };

/* timer_num_0..9: 1-5 on the row at y 95, 6-9 and 0 on the row at y 114. */
static inline DodRetailCell Dod_RetailTimerDigit(int digit)
{
	DodRetailCell c = { DOD_SHEET_LAYOUT, 0, 0, 12, 18 };
	int col;
	if (digit < 0 || digit > 9)
		digit = 0;
	col = digit == 0 ? 4 : (digit - 1) % 5;
	c.x = 114 + col * 13;
	c.y = (digit >= 1 && digit <= 5) ? 95 : 114;
	return c;
}

/* One draw: cell (possibly cropped) at screen x, y. */
typedef struct DodRetailDraw {
	DodRetailCell cell;
	int x, y;
} DodRetailDraw;

#define DOD_RETAIL_MAX_DRAWS 16

typedef struct DodRetailInput {
	int screen_w, screen_h;
	int health;		/* 0..100 */
	float stamina;		/* 0..100 */
	int spectating;		/* g_iUser1 / g_iUser2 set */
	int dead;
	int show_reinforcements;
	int seconds_left;	/* < 0: no timer */
	int crosshair;		/* draw the dynamic crosshair */
	int crosshair_gap;	/* GetCrossHairWidth(); retail default 4 */
} DodRetailInput;

static inline int dod_retail_clamp(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static inline void dod_retail_add(DodRetailDraw *out, int *n, int max, DodRetailCell cell, int x, int y)
{
	if (*n >= max || cell.w <= 0 || cell.h <= 0)
		return;
	out[*n].cell = cell;
	out[*n].x = x;
	out[*n].y = y;
	(*n)++;
}

/* Build the retail draw list. Returns the number of draws (<= max). */
static inline int Dod_RetailHudLayout(const DodRetailInput *in, DodRetailDraw *out, int max)
{
	int n = 0, y0, health, rows;
	DodRetailCell c;

	if (!in || !out || max <= 0 || in->screen_w <= 0 || in->screen_h <= 0)
		return 0;
	health = dod_retail_clamp(in->health, 0, 100);

	/* Status panel (CHudDodIcons::Draw): nothing while dead or watching. */
	if (!in->dead && !in->spectating) {
		y0 = in->screen_h - dod_cell_main.h;
		dod_retail_add(out, &n, max, dod_cell_main, 0, y0);

		/* Stamina: the bottom n rows of the tube, bottom-aligned at x 8. */
		rows = (int)(dod_cell_stamina.h * (in->stamina < 0.0f ? 0.0f
			: in->stamina > 100.0f ? 100.0f : in->stamina) / 100.0f);
		if (rows > 0) {
			c = dod_cell_stamina;
			c.y += c.h - rows;
			c.h = rows;
			dod_retail_add(out, &n, max, c, 8, in->screen_h - rows);
		}

		/* Health bar: grows upward with health, x 99. */
		rows = (int)(dod_cell_healthbar.h * health / 100.0f);
		c = dod_cell_healthbar;
		c.h = dod_retail_clamp(rows + 1, 0, dod_cell_healthbar.h);
		dod_retail_add(out, &n, max, c, 99, y0 + 28 + (dod_cell_healthbar.h - c.h));

		/* Overlay: covers the monitor from the top down as health falls. */
		rows = (int)(dod_cell_health_overlay.h * (1.0f - health / 100.0f));
		c = dod_cell_health_overlay;
		c.h = dod_retail_clamp(rows + 1, 0, dod_cell_health_overlay.h);
		if (rows + 1 > 0)
			dod_retail_add(out, &n, max, c, 54, y0 + 27);

		if (in->show_reinforcements)
			dod_retail_add(out, &n, max, dod_cell_reinforcements, dod_cell_main.w,
				in->screen_h - dod_cell_reinforcements.h);
	}

	/* Objective timer (CObjectiveIcons::Draw): top-left plate, MM:SS. */
	if (in->seconds_left >= 0) {
		const int t = in->seconds_left;
		const int minutes = dod_retail_clamp(t / 60, 0, 99), seconds = t % 60;
		const int plate_y = in->spectating ? (int)(in->screen_h / 480.0f * 54.0f + 0.5f) : 2;
		const int digit_y = plate_y + 6;
		dod_retail_add(out, &n, max, dod_cell_timer, 0, plate_y);
		dod_retail_add(out, &n, max, Dod_RetailTimerDigit(minutes / 10), 10, digit_y);
		dod_retail_add(out, &n, max, Dod_RetailTimerDigit(minutes % 10), 24, digit_y);
		dod_retail_add(out, &n, max, Dod_RetailTimerDigit(seconds / 10), 44, digit_y);
		dod_retail_add(out, &n, max, Dod_RetailTimerDigit(seconds % 10), 58, digit_y);
	}

	/* Dynamic crosshair (cl_xhair_style 0): dot plus four bars w px out. */
	if (in->crosshair && !in->dead && !in->spectating) {
		const int cx = in->screen_w / 2, cy = in->screen_h / 2;
		const int w = in->crosshair_gap > 0 ? in->crosshair_gap : 4;
		const DodRetailCell hz = dod_cell_cross_horiz, vt = dod_cell_cross_vert;
		dod_retail_add(out, &n, max, dod_cell_cross_dot, cx - 1, cy - 1);
		dod_retail_add(out, &n, max, hz, cx - w - hz.w + 2, cy - hz.h / 2);
		dod_retail_add(out, &n, max, hz, cx + w, cy - hz.h / 2);
		dod_retail_add(out, &n, max, vt, cx - 1, cy - w - vt.h + 2);
		dod_retail_add(out, &n, max, vt, cx - 1, cy + w);
	}
	return n;
}

#endif /* DODGC_DOD_HUD_RETAIL_H */
