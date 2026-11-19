/* Retail Day of Defeat 1.3 HUD layout, recovered from the retail client
 * (CHudDodIcons::Draw, CObjectiveIcons::Draw, CHudDoDCrossHair). Header-only
 * and engine-free so the arithmetic is host-tested; the client only binds the
 * sprite sheets and calls SPR_DrawHoles with what these functions return.
 *
 * Coordinates are screen pixels. Retail anchors raw 640-res sprites to the
 * screen edges without scaling, so the numbers here are the retail ones. */
#ifndef DODGC_DOD_HUD_RETAIL_H
#define DODGC_DOD_HUD_RETAIL_H

#include <string.h>

/* A sprite cell: sheet index (DodRetailSheet) and its hud.txt rectangle. */
typedef struct DodRetailCell {
	int sheet;
	int x, y, w, h;
} DodRetailCell;

typedef enum {
	DOD_SHEET_LAYOUT = 0,	/* sprites/hud_layout.spr */
	DOD_SHEET_CROSS,	/* sprites/dodcross.spr */
	DOD_SHEET_BULLETS,	/* sprites/clip_bullets.spr */
	DOD_SHEET_CLIPS,	/* sprites/clip_clips.spr */
	DOD_SHEET_EXTRAS,	/* sprites/clip_extras.spr */
	DOD_SHEET_ROCKETS,	/* sprites/rockets.spr */
	DOD_SHEET_ROCKETS2,	/* sprites/rockets2.spr */
	DOD_SHEET_OBJ_NEUTRAL,	/* sprites/obj_icons/icon_obj_neutral.spr (32x32) */
	DOD_SHEET_OBJ_ALLIES,	/* sprites/obj_icons/icon_obj_allies.spr */
	DOD_SHEET_OBJ_AXIS,	/* sprites/obj_icons/icon_obj_axis.spr */
	DOD_SHEET_COUNT
} DodRetailSheet;

static const char *const dod_retail_sheet_files[DOD_SHEET_COUNT] = {
	"sprites/hud_layout.spr",
	"sprites/dodcross.spr",
	"sprites/clip_bullets.spr",
	"sprites/clip_clips.spr",
	"sprites/clip_extras.spr",
	"sprites/rockets.spr",
	"sprites/rockets2.spr",
	"sprites/obj_icons/icon_obj_neutral.spr",
	"sprites/obj_icons/icon_obj_allies.spr",
	"sprites/obj_icons/icon_obj_axis.spr",
};

/* CHudAmmo's weapon table (retail CHudAmmo::VidInit): the clip graphics of
 * each weapon from hud.txt, and the share of the bullet column a chambered
 * round (x0) and each round in the clip (per) uncover. per 0 = no column
 * (rockets): the full graphic shows while loaded. clip is the magazine the
 * reserve is counted in (retail divides by the WeaponList max clip). The
 * key is the weapon's classname after "weapon_dod_". */
typedef struct DodRetailClip {
	const char *key;
	DodRetailCell full, empty, extra;
	float x0, per;
	int clip;
} DodRetailClip;

#define DOD_CELL(sheet, x, y, w, h) { DOD_SHEET_##sheet, x, y, w, h }
static const DodRetailClip dod_retail_clips[] = {
	{ "garand", DOD_CELL(BULLETS, 0, 183, 57, 48), DOD_CELL(CLIPS, 0, 183, 57, 48), DOD_CELL(EXTRAS, 82, 125, 40, 35), 0.12500f, 0.10417f, 8 },
	{ "spring", DOD_CELL(BULLETS, 91, 132, 45, 43), DOD_CELL(CLIPS, 91, 132, 45, 43), DOD_CELL(EXTRAS, 150, 0, 28, 29), 0.06977f, 0.18605f, 5 },
	{ "thompson", DOD_CELL(BULLETS, 107, 0, 37, 130), DOD_CELL(CLIPS, 107, 0, 37, 130), DOD_CELL(EXTRAS, 139, 183, 28, 73), 0.03800f, 0.03077f, 30 },
	{ "bar", DOD_CELL(BULLETS, 0, 0, 70, 115), DOD_CELL(CLIPS, 0, 0, 70, 115), DOD_CELL(EXTRAS, 0, 198, 39, 58), 0.24348f, 0.03478f, 20 },
	{ "30cal", DOD_CELL(BULLETS, 161, 123, 35, 133), DOD_CELL(EXTRAS, 181, 40, 64, 128), DOD_CELL(EXTRAS, 0, 68, 81, 66), 0.02300f, 0.03100f, 150 },
	{ "bazooka", DOD_CELL(ROCKETS2, 0, 128, 128, 64), DOD_CELL(ROCKETS2, 128, 128, 128, 64), DOD_CELL(ROCKETS, 128, 0, 32, 128), 0.0f, 0.0f, 1 },
	{ "colt", DOD_CELL(BULLETS, 69, 0, 38, 102), DOD_CELL(CLIPS, 69, 0, 38, 102), DOD_CELL(EXTRAS, 150, 28, 24, 56), 0.09804f, 0.10784f, 7 },
	{ "k98", DOD_CELL(BULLETS, 91, 132, 45, 43), DOD_CELL(CLIPS, 91, 132, 45, 43), DOD_CELL(EXTRAS, 150, 0, 28, 29), 0.06977f, 0.18605f, 5 },
	{ "k98s", DOD_CELL(BULLETS, 91, 132, 45, 43), DOD_CELL(CLIPS, 91, 132, 45, 43), DOD_CELL(EXTRAS, 150, 0, 28, 29), 0.06977f, 0.18605f, 5 },
	{ "mp40", DOD_CELL(BULLETS, 161, 123, 35, 133), DOD_CELL(CLIPS, 161, 123, 35, 133), DOD_CELL(EXTRAS, 125, 40, 23, 71), 0.05200f, 0.03008f, 32 },
	{ "stg44", DOD_CELL(BULLETS, 195, 123, 61, 133), DOD_CELL(CLIPS, 195, 123, 61, 133), DOD_CELL(EXTRAS, 120, 113, 35, 73), 0.06710f, 0.03008f, 30 },
	{ "mg42", DOD_CELL(BULLETS, 161, 123, 35, 133), DOD_CELL(EXTRAS, 181, 40, 64, 128), DOD_CELL(EXTRAS, 0, 132, 81, 65), 0.02300f, 0.03100f, 250 },
	{ "pschreck", DOD_CELL(ROCKETS2, 0, 64, 128, 64), DOD_CELL(ROCKETS2, 128, 64, 128, 64), DOD_CELL(ROCKETS, 64, 0, 32, 128), 0.0f, 0.0f, 1 },
	{ "luger", DOD_CELL(BULLETS, 143, 0, 44, 119), DOD_CELL(CLIPS, 143, 0, 44, 119), DOD_CELL(EXTRAS, 111, 192, 29, 64), 0.18908f, 0.07980f, 8 },
	/* The British and paratrooper guns. The cells are hud.txt's clip_* entries; the bullet column share (x0, per) of the
	 * ones the retail client table was not recovered for follows the stock gun with the same graphic: x0 as there and
	 * per such that the clip's rounds fill the column the way the stock guns do (x0 + per * clip is about 0.97). */
	{ "m1carbine", DOD_CELL(BULLETS, 0, 115, 54, 66), DOD_CELL(CLIPS, 0, 115, 54, 66), DOD_CELL(EXTRAS, 40, 196, 42, 57), 0.10000f, 0.05800f, 15 },
	{ "grease", DOD_CELL(BULLETS, 58, 115, 36, 133), DOD_CELL(CLIPS, 58, 115, 36, 133), DOD_CELL(EXTRAS, 88, 190, 25, 66), 0.05000f, 0.02875f, 32 },
	{ "fg42", DOD_CELL(BULLETS, 0, 0, 70, 115), DOD_CELL(CLIPS, 0, 0, 70, 115), DOD_CELL(EXTRAS, 82, 63, 40, 63), 0.24348f, 0.03478f, 20 },
	{ "enfield", DOD_CELL(BULLETS, 91, 132, 45, 43), DOD_CELL(CLIPS, 91, 132, 45, 43), DOD_CELL(EXTRAS, 150, 0, 28, 29), 0.06977f, 0.09000f, 10 },
	{ "sten", DOD_CELL(BULLETS, 107, 0, 37, 130), DOD_CELL(CLIPS, 107, 0, 37, 130), DOD_CELL(EXTRAS, 139, 183, 28, 73), 0.03800f, 0.02910f, 32 },
	{ "bren", DOD_CELL(ROCKETS, 128, 160, 64, 96), DOD_CELL(ROCKETS, 192, 160, 64, 96), DOD_CELL(ROCKETS, 64, 138, 32, 54), 0.0f, 0.0f, 30 },
	{ "webley", DOD_CELL(ROCKETS, 0, 73, 64, 64), DOD_CELL(ROCKETS, 0, 73, 64, 64), DOD_CELL(ROCKETS, 0, 138, 32, 46), 0.0f, 0.0f, 6 },
	{ "piat", DOD_CELL(ROCKETS2, 0, 0, 128, 64), DOD_CELL(ROCKETS2, 128, 0, 128, 64), DOD_CELL(ROCKETS, 96, 0, 32, 128), 0.0f, 0.0f, 1 },
};
#undef DOD_CELL

#define DOD_RETAIL_FONT_H 25	/* hud.txt number_0 height */

/* The clip entry for a classname ("weapon_dod_garand"), or 0. */
static inline const DodRetailClip *Dod_RetailClipFor(const char *classname)
{
	const char *key = classname;
	unsigned i;
	if (!key)
		return 0;
	if (!strncmp(key, "weapon_dod_", 11))
		key += 11;
	else if (!strncmp(key, "weapon_", 7))
		key += 7;
	for (i = 0; i < sizeof(dod_retail_clips) / sizeof(dod_retail_clips[0]); i++)
		if (!strcmp(key, dod_retail_clips[i].key))
			return &dod_retail_clips[i];
	return 0;
}

/* The clip entry of a class name for a map's weapon variants (DodHudState.kit): the British and paratrooper guns carry the
 * stock class names, so the HUD swaps to their clip graphics: British Allies garand/spring -> enfield, thompson -> sten,
 * bar -> bren, bazooka -> piat, colt -> webley; paratrooper Allies garand -> m1carbine, thompson -> grease; paratrooper
 * Axis k98 -> fg42. */
static inline const DodRetailClip *Dod_RetailClipForKit(const char *classname, int kit)
{
	const char *key = classname;
	static const struct { const char *stock, *variant; } british[] = {
		{ "garand", "enfield" }, { "spring", "enfield" }, { "thompson", "sten" }, { "bar", "bren" },
		{ "bazooka", "piat" }, { "colt", "webley" }
	};
	unsigned i;
	if (!key)
		return 0;
	if (!strncmp(key, "weapon_dod_", 11))
		key += 11;
	else if (!strncmp(key, "weapon_", 7))
		key += 7;
	if (kit & 1) {
		for (i = 0; i < sizeof(british) / sizeof(british[0]); i++)
			if (!strcmp(key, british[i].stock)) {
				key = british[i].variant;
				break;
			}
	} else if (kit & 2) {
		if (!strcmp(key, "garand"))
			key = "m1carbine";
		else if (!strcmp(key, "thompson"))
			key = "grease";
	}
	if ((kit & 4) && !strcmp(key, "k98"))
		key = "fg42";
	return Dod_RetailClipFor(key);
}

/* hud.txt (640) records the retail HUD draws. */
static const DodRetailCell dod_cell_main = { DOD_SHEET_LAYOUT, 0, 172, 113, 84 };
static const DodRetailCell dod_cell_stamina = { DOD_SHEET_LAYOUT, 86, 93, 16, 72 };
static const DodRetailCell dod_cell_healthbar = { DOD_SHEET_LAYOUT, 51, 110, 5, 47 };
static const DodRetailCell dod_cell_health_overlay = { DOD_SHEET_LAYOUT, 0, 111, 44, 47 };
static const DodRetailCell dod_cell_reinforcements = { DOD_SHEET_LAYOUT, 81, 0, 129, 58 };
static const DodRetailCell dod_cell_timer = { DOD_SHEET_LAYOUT, 0, 0, 80, 30 };
/* cross_style_2_horiz 36,0,10,3 / _vert 24,0,3,10 / _dot 24,12,3,3 hold a
 * 1-px line on their middle row/column and nothing else. Drawing the whole
 * 3-px cell at half scale samples between rows and drops the line, so the
 * cells are cropped to the opaque pixels and placed one pixel in. */
static const DodRetailCell dod_cell_cross_horiz = { DOD_SHEET_CROSS, 36, 1, 10, 1 };
static const DodRetailCell dod_cell_cross_vert = { DOD_SHEET_CROSS, 25, 0, 1, 10 };
static const DodRetailCell dod_cell_cross_dot = { DOD_SHEET_CROSS, 25, 13, 1, 1 };

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
/* Sheets every frame may draw (panel, timer, crosshair, flags); the clip
 * and rocket sheets are only needed while a gun that uses them is held. */
static inline int Dod_RetailSheetAlwaysOn(int sheet)
{
	return sheet == DOD_SHEET_LAYOUT || sheet == DOD_SHEET_CROSS
		|| sheet == DOD_SHEET_OBJ_NEUTRAL || sheet == DOD_SHEET_OBJ_ALLIES
		|| sheet == DOD_SHEET_OBJ_AXIS;
}

typedef struct DodRetailDraw {
	DodRetailCell cell;
	int x, y;
} DodRetailDraw;

#define DOD_RETAIL_MAX_POINTS 12	/* CObjectiveIcons draws up to 12 areas */
#define DOD_RETAIL_MAX_DRAWS (20 + DOD_RETAIL_MAX_POINTS)

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
	const DodRetailClip *weapon;	/* held weapon's clip graphics, or 0 */
	int clip;		/* rounds in the clip, < 0: none */
	int reserve;		/* rounds carried outside the clip */
	int max_clip;		/* retail: the WeaponList max clip; <= 0 (HL WeaponList has none): the table's */
	int safe_right, safe_bottom;	/* title-safe margins in layout pixels the ammo block keeps clear (0: retail) */
	int text_w;		/* width of the widest "x NN" spare count in the client font, layout pixels (0: retail's 45) */
	int num_points;		/* capture points on the objective bar */
	int owner[DOD_RETAIL_MAX_POINTS];	/* 0 neutral, 1 Allies, 2 Axis */
} DodRetailInput;

/* The "x N" spare-clip count retail prints beside the extra-clip graphic. */
typedef struct DodRetailText {
	int x, y;		/* < 0: nothing to print */
	int count;
} DodRetailText;

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
static inline int Dod_RetailHudLayoutText(const DodRetailInput *in, DodRetailDraw *out, int max,
	DodRetailText *text)
{
	int n = 0, y0, health, rows;
	DodRetailCell c;

	if (text) {
		text->x = text->y = -1;
		text->count = 0;
	}
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

	/* Clip ammo (CHudAmmo::Draw): bottom right, the empty clip with the
	 * remaining rounds uncovered from the top, spare clips beside it. */
	if (in->weapon && in->clip >= 0 && !in->dead && !in->spectating) {
		const DodRetailClip *w = in->weapon;
		const int fh = DOD_RETAIL_FONT_H;
		/* retail reserves 45 px for the "x N" text; a wider client font (and the TV's overscan) needs more */
		const int text_room = in->text_w > 45 ? in->text_w : 45;
		const int tx = in->screen_w - in->safe_right - w->full.w - w->extra.w - text_room;
		const int base = in->screen_h - in->safe_bottom - fh - fh / 2;
		const int per_clip = in->max_clip > 0 ? in->max_clip : w->clip;
		const int spare = per_clip > 0 ? in->reserve / per_clip : 0;

		dod_retail_add(out, &n, max, w->empty, tx, base - w->full.h + fh);
		if (w->per > 0.0f) {
			float frac = (in->clip > 0 ? w->x0 : 0.0f) + in->clip * w->per;
			int top;
			if (frac > 1.0f)
				frac = 1.0f;
			top = w->full.y + (int)(w->full.h * (1.0f - frac));
			c = w->full;
			c.h = w->full.y + w->full.h - top;
			c.y = top;
			if (c.h > 0)
				dod_retail_add(out, &n, max, c, tx, base + top - w->empty.y - w->full.h + fh);
		} else if (in->clip > 0)
			dod_retail_add(out, &n, max, w->full, tx, base - w->full.h + fh);
		if (spare > 0) {
			dod_retail_add(out, &n, max, w->extra, tx + w->full.w, base - w->extra.h + fh);
			if (text) {
				text->x = tx + w->full.w + w->extra.w;
				text->y = base;
				text->count = spare;
			}
		}
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

	/* Objective icons (CObjectiveIcons::Draw): one 32x32 flag per area,
	 * 34 apart, right of the timer plate (x 82) or from x 10 without it. */
	if (in->num_points > 0) {
		const int count = in->num_points < DOD_RETAIL_MAX_POINTS ? in->num_points : DOD_RETAIL_MAX_POINTS;
		const int icon_y = in->spectating ? (int)(2.0f * (54.0f * in->screen_h / 480.0f) + 0.5f)
			: (int)(2.0f * in->screen_h / 480.0f + 0.5f);
		int i, x = in->seconds_left >= 0 ? 82 : 10;
		for (i = 0; i < count; i++, x += 34) {
			DodRetailCell icon = { DOD_SHEET_OBJ_NEUTRAL, 0, 0, 32, 32 };
			if (in->owner[i] == 1)
				icon.sheet = DOD_SHEET_OBJ_ALLIES;
			else if (in->owner[i] == 2)
				icon.sheet = DOD_SHEET_OBJ_AXIS;
			dod_retail_add(out, &n, max, icon, x, icon_y);
		}
	}

	/* Dynamic crosshair (cl_xhair_style 0): dot plus four bars w px out. */
	if (in->crosshair && !in->dead && !in->spectating) {
		const int cx = in->screen_w / 2, cy = in->screen_h / 2;
		const int w = in->crosshair_gap > 0 ? in->crosshair_gap : 4;
		const DodRetailCell hz = dod_cell_cross_horiz, vt = dod_cell_cross_vert;
		/* retail: dot at (cx-1, cy-1), bars at cy - 3/2 and cx - 1, all
		 * 3-px cells whose line is one pixel in: hence cx / cy here. */
		dod_retail_add(out, &n, max, dod_cell_cross_dot, cx, cy);
		dod_retail_add(out, &n, max, hz, cx - w - hz.w + 2, cy);
		dod_retail_add(out, &n, max, hz, cx + w, cy);
		dod_retail_add(out, &n, max, vt, cx, cy - w - vt.h + 2);
		dod_retail_add(out, &n, max, vt, cx, cy + w);
	}
	return n;
}

static inline int Dod_RetailHudLayout(const DodRetailInput *in, DodRetailDraw *out, int max)
{
	return Dod_RetailHudLayoutText(in, out, max, 0);
}

#endif /* DODGC_DOD_HUD_RETAIL_H */
