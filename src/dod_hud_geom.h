/*
 * dod_hud_geom.h - where every HUD element sits at the real 640x480 (4:3) output, and the title-safe area they
 * must stay inside. Pure C so the host test can prove that elements which can be on screen together do not
 * intersect (tests/dod_hud_geom_test.c). The client (hud_dod.cpp) and the integrated menu/chat code use the same
 * numbers.
 *
 * Layout units are 640x480 pixels. DodRect is x, y, w, h.
 */
#ifndef DODGC_DOD_HUD_GEOM_H
#define DODGC_DOD_HUD_GEOM_H

#include "dod_hud_retail.h"

#define DOD_SAFE_L 32	/* 5% of 640 */
#define DOD_SAFE_R (640 - 32)
#define DOD_SAFE_T 24	/* 5% of 480 */
#define DOD_SAFE_B (480 - 24)

/* The HUD font at 640x480: the team/class menus measured about 37 px per line; widest glyph average about 18 px. */
#define DOD_HUD_LINE_H 38
#define DOD_HUD_CHAR_W 18

#define DOD_HUD_PANEL_PAD 6	/* menu / voice / board panels: text inset */
#define DOD_HUD_GAP 4		/* clearance kept between neighbouring elements */

#define DOD_HUD_KILL_LINES 5	/* retail shows five; the chat block gives up lines instead */
#define DOD_HUD_CHAT_LINES 3	/* saytext MAX_LINES: 5 feed + 3 chat lines of 38 px fill the 306 px band */
#define DOD_HUD_MAX_FLAGS 12

typedef struct DodRect {
	int x, y, w, h;
} DodRect;

static inline DodRect dod_rect(int x, int y, int w, int h)
{
	DodRect r;
	r.x = x;
	r.y = y;
	r.w = w;
	r.h = h;
	return r;
}

static inline int dod_rect_hit(DodRect a, DodRect b)
{
	return a.w > 0 && a.h > 0 && b.w > 0 && b.h > 0 && a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h
		&& b.y < a.y + a.h;
}

static inline int dod_rect_in_safe(DodRect r)
{
	return r.x >= DOD_SAFE_L && r.y >= DOD_SAFE_T && r.x + r.w <= DOD_SAFE_R && r.y + r.h <= DOD_SAFE_B;
}

/* The area under the top-left objective row (timer plate and flags): kill feed, menus and chat start below it. */
static inline int Dod_HudTopRowBottom(void)
{
	/* flags are 32 px tall and sit at y 2 + safe top; the timer plate is 30 px tall at the same y */
	return 2 + DOD_SAFE_T + 32;
}

/* The top of the bottom-left status panel (retail art 84 px tall, bottom aligned inside the safe area). */
static inline int Dod_HudStatusTop(void)
{
	return DOD_SAFE_B - dod_cell_main.h;
}

/* Modal text panel (team/class menu, voice menu, scoreboard) starts here, under the objective row. */
static inline int Dod_HudModalTop(void)
{
	return Dod_HudTopRowBottom() + DOD_HUD_GAP;
}

/* ...and may not run lower than this, which keeps it off the status panel. */
static inline int Dod_HudModalBottom(void)
{
	return Dod_HudStatusTop() - DOD_HUD_GAP;
}

/* How many text lines a modal panel can hold. */
static inline int Dod_HudModalRows(int line_h)
{
	return (Dod_HudModalBottom() - Dod_HudModalTop() - 2 * DOD_HUD_PANEL_PAD) / (line_h > 0 ? line_h : 1);
}

/* A left-aligned modal panel: lines of text (already limited to Dod_HudModalRows), widest line in pixels. */
static inline DodRect Dod_HudMenuPanel(int lines, int text_w, int line_h)
{
	int w = text_w + 2 * DOD_HUD_PANEL_PAD;
	const int max_w = DOD_SAFE_R - DOD_SAFE_L;

	if (w > max_w)
		w = max_w;
	return dod_rect(DOD_SAFE_L, Dod_HudModalTop(), w, lines * line_h + 2 * DOD_HUD_PANEL_PAD);
}

/* A centred modal panel (voice menu, scoreboard), kept inside the modal band. */
static inline DodRect Dod_HudCentredPanel(int lines, int text_w, int line_h)
{
	const int max_w = DOD_SAFE_R - DOD_SAFE_L;
	int w = text_w + 2 * DOD_HUD_PANEL_PAD, h = lines * line_h + 2 * DOD_HUD_PANEL_PAD, y;
	const int band = Dod_HudModalBottom() - Dod_HudModalTop();

	if (w > max_w)
		w = max_w;
	if (h > band)
		h = band;
	y = Dod_HudModalTop() + (band - h) / 2;
	return dod_rect((640 - w) / 2, y, w, h);
}

/* The kill feed: from just under the objective row, left to right across the safe width. */
static inline DodRect Dod_HudKillFeed(int lines, int line_h)
{
	return dod_rect(DOD_SAFE_L, Dod_HudModalTop(), DOD_SAFE_R - DOD_SAFE_L, lines * line_h);
}

/* The chat lines: a block of DOD_HUD_CHAT_LINES whose bottom edge sits just above the status panel. */
static inline int Dod_HudChatTop(int line_h)
{
	return Dod_HudModalBottom() - DOD_HUD_CHAT_LINES * line_h;
}

static inline DodRect Dod_HudChatLines(int line_h)
{
	return dod_rect(DOD_SAFE_L, Dod_HudChatTop(line_h), DOD_SAFE_R - DOD_SAFE_L, DOD_HUD_CHAT_LINES * line_h);
}

/* On-screen keyboard (engine in_osk.c: OSK_CELL 36, OSK_GAP 4, OSK_PAD 14, OSK_HEADER 26, OSK_FOOTER 38, 10 columns,
 * 4 character rows + 1 action row), its bottom edge on the safe-area bottom (the panel border adds 2 px each side). */
#define DOD_OSK_W (10 * (36 + 4) - 4 + 2 * 14)
#define DOD_OSK_H (26 + 5 * (36 + 4) - 4 + 38 + 2 * 14)

static inline DodRect Dod_HudKeyboard(void)
{
	/* in_osk.c: top = 480 - height - 26; the panel border adds 2 px on every side */
	return dod_rect((640 - DOD_OSK_W) / 2 - 2, 480 - 26 - DOD_OSK_H - 2, DOD_OSK_W + 4, DOD_OSK_H + 4);
}

/* The chat prompt sits directly above the keyboard while chatting, inside the safe area. */
static inline DodRect Dod_HudChatInput(int line_h)
{
	const DodRect kb = Dod_HudKeyboard();

	return dod_rect(DOD_SAFE_L, kb.y - DOD_HUD_GAP - line_h, DOD_SAFE_R - DOD_SAFE_L, line_h);
}

/* "Respawn in N": shown only while dead, when the status panel and ammo are hidden, so it takes the status panel's
 * corner instead of the middle of the screen (where chat, menus and the scoreboard live). */
static inline DodRect Dod_HudRespawnText(int line_h)
{
	const int w = 16 * DOD_HUD_CHAR_W;

	return dod_rect(DOD_SAFE_L, DOD_SAFE_B - line_h, w, line_h);
}

#endif /* DODGC_DOD_HUD_GEOM_H */
