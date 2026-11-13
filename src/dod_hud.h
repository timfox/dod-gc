/* Day of Defeat HUD state and layout, shared by the game DLL and the client.
 *
 * Header-only and engine-free (plain C99, usable from C++) so the layout can be
 * host-tested. The server sends a DodHudState a couple of times a second; the
 * client turns it into a short list of draw operations with Dod_HudLayout and
 * draws them with the engine's resident console font plus one tiny icon sheet
 * (sprites/icon_obj.spr, 96x32: neutral, Allies, Axis). Nothing else is loaded. */
#ifndef DODGC_DOD_HUD_H
#define DODGC_DOD_HUD_H

#include <stdio.h>
#include <string.h>

#define DOD_HUD_MAX_POINTS 8
#define DOD_HUD_MAX_OPS 24
#define DOD_HUD_ICON_SIZE 32
#define DOD_HUD_ICON_STEP 36

/* Owner cells in icon_obj.spr, left to right. */
#define DOD_HUD_CELL_NEUTRAL 0
#define DOD_HUD_CELL_ALLIES 1
#define DOD_HUD_CELL_AXIS 2

typedef struct DodHudState {
	short allies_score;
	short axis_score;
	unsigned char round_number;	/* 1-based */
	unsigned char rounds_to_win;	/* 0 = no match tally */
	unsigned char won_allies;
	unsigned char won_axis;
	short seconds_left;		/* 0 = no clock */
	unsigned char phase;		/* 0 waiting, 1 active, 2 intermission */
	unsigned char num_points;
	unsigned short owners;		/* 2 bits per point: 0 neutral, 1 Allies, 2 Axis */
	unsigned char my_team;		/* 0 none, 1 Allies, 2 Axis */
	unsigned char my_class;		/* DodClass, 0 none */
	unsigned char grenades;
} DodHudState;

typedef enum {
	DOD_HUD_TEXT = 0,
	DOD_HUD_ICON
} DodHudOpKind;

typedef struct DodHudOp {
	int kind;
	int x, y;		/* top left, in screen pixels */
	int cell;		/* icon: DOD_HUD_CELL_* */
	int r, g, b;		/* text colour */
	char text[40];
} DodHudOp;

static inline unsigned short Dod_HudPackOwners(const int owners[DOD_HUD_MAX_POINTS], int count)
{
	unsigned short packed = 0;
	int i;
	for (i = 0; i < count && i < DOD_HUD_MAX_POINTS; i++) {
		const int owner = owners[i] == 1 || owners[i] == 2 ? owners[i] : 0;
		packed |= (unsigned short)(owner << (i * 2));
	}
	return packed;
}

static inline int Dod_HudOwnerOf(unsigned short packed, int index)
{
	int owner;
	if (index < 0 || index >= DOD_HUD_MAX_POINTS)
		return 0;
	owner = (packed >> (index * 2)) & 3;
	return owner == 1 || owner == 2 ? owner : 0;	/* 3 is not a team */
}

static inline const char *Dod_HudClassName(int player_class)
{
	static const char *const names[] = {
		"", "Rifleman", "Assault", "Support", "Sniper", "Machine Gunner", "Rocket"
	};
	if (player_class < 0 || player_class > 6)
		return "";
	return names[player_class];
}

static inline int dod_hud_clamp(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static inline DodHudOp *dod_hud_text(DodHudOp *ops, int *count, int max, int x, int y, int r, int g, int b, const char *text)
{
	DodHudOp *op;
	if (*count >= max)
		return 0;
	op = &ops[(*count)++];
	memset(op, 0, sizeof(*op));
	op->kind = DOD_HUD_TEXT;
	op->x = x;
	op->y = y;
	op->r = r;
	op->g = g;
	op->b = b;
	snprintf(op->text, sizeof(op->text), "%s", text);
	return op;
}

/* Build the draw list for a screen of screen_w x screen_h pixels and a font
 * char_w x char_h wide. Returns the number of ops written (never more than max).
 * Everything lands inside the screen; text is positioned with the estimate
 * char_w * strlen, which the client can refine when it draws. */
static inline int Dod_HudLayout(const DodHudState *s, int screen_w, int screen_h, int char_w, int char_h,
	DodHudOp *ops, int max)
{
	char text[40];
	int count = 0, cx, y, i, n, width, minutes, seconds;
	int allies, axis;

	if (!s || !ops || max <= 0 || screen_w <= 0 || screen_h <= 0 || char_w <= 0 || char_h <= 0)
		return 0;
	cx = screen_w / 2;
	y = char_h / 2 + 4;
	allies = s->allies_score < 0 ? 0 : s->allies_score;
	axis = s->axis_score < 0 ? 0 : s->axis_score;

	/* Clock in the middle; between rounds it says what happens next. */
	if (s->phase == 2)
		snprintf(text, sizeof(text), "NEXT ROUND");
	else if (s->seconds_left > 0) {
		minutes = s->seconds_left / 60;
		seconds = s->seconds_left % 60;
		snprintf(text, sizeof(text), "%d:%02d", minutes, seconds);
	} else
		text[0] = '\0';
	if (text[0]) {
		width = (int)strlen(text) * char_w;
		dod_hud_text(ops, &count, max, cx - width / 2, y, 255, 220, 120, text);
	}

	/* Team scores either side of the clock. */
	snprintf(text, sizeof(text), "ALLIES %d", allies);
	width = (int)strlen(text) * char_w;
	dod_hud_text(ops, &count, max, dod_hud_clamp(cx - 56 - width, 0, screen_w), y, 120, 255, 140, text);
	snprintf(text, sizeof(text), "AXIS %d", axis);
	dod_hud_text(ops, &count, max, dod_hud_clamp(cx + 56, 0, screen_w), y, 255, 120, 120, text);

	/* One flag per control point, centred under the clock. */
	n = dod_hud_clamp(s->num_points, 0, DOD_HUD_MAX_POINTS);
	if (n > 0) {
		const int total = n * DOD_HUD_ICON_STEP - (DOD_HUD_ICON_STEP - DOD_HUD_ICON_SIZE);
		const int icon_y = y + char_h + 6;
		int x = cx - total / 2;
		if (x < 0)
			x = 0;
		for (i = 0; i < n && count < max; i++) {
			DodHudOp *op = &ops[count++];
			memset(op, 0, sizeof(*op));
			op->kind = DOD_HUD_ICON;
			op->x = x + i * DOD_HUD_ICON_STEP;
			op->y = icon_y;
			op->cell = Dod_HudOwnerOf(s->owners, i);
		}
		y = icon_y + DOD_HUD_ICON_SIZE;
	} else
		y += char_h;

	/* Match tally under the flags. */
	if (s->rounds_to_win > 0) {
		snprintf(text, sizeof(text), "ROUND %d  %d-%d", s->round_number, s->won_allies, s->won_axis);
		width = (int)strlen(text) * char_w;
		dod_hud_text(ops, &count, max, dod_hud_clamp(cx - width / 2, 0, screen_w), y + 2, 220, 220, 220, text);
	}

	/* Class and grenades, bottom left. */
	if (s->my_class > 0 && Dod_HudClassName(s->my_class)[0]) {
		snprintf(text, sizeof(text), "%s  GRENADES %d", Dod_HudClassName(s->my_class), s->grenades);
		dod_hud_text(ops, &count, max, 16, dod_hud_clamp(screen_h - 2 * char_h - 8, 0, screen_h), 220, 220, 220, text);
	}
	return count;
}

#endif /* DODGC_DOD_HUD_H */
