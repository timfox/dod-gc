#include "dod_hud_geom.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* covers: CHudDod::DrawKills  CHudDod::DrawVoice  CHudDod::DrawBoard  CHudDod::DrawRetail */
#define MAX_EL 40

typedef struct {
	const char *name;
	DodRect r;
	unsigned groups;	/* bit set of the screen states it appears in */
} El;

enum {
	ST_PLAY = 1,	/* alive, kill feed and chat running */
	ST_DEAD = 2,	/* dead, waiting for the next wave */
	ST_MENU = 4,	/* team or class menu up */
	ST_VOICE = 8,	/* voice menu up */
	ST_BOARD = 16,	/* scoreboard up */
	ST_CHAT = 32	/* chatting through the on-screen keyboard */
};

static El els[MAX_EL];
static int nels;

static void add(const char *name, DodRect r, unsigned groups)
{
	assert(nels < MAX_EL);
	els[nels].name = name;
	els[nels].r = r;
	els[nels].groups = groups;
	nels++;
}

static DodRect union_of(const DodRetailDraw *d, int n, int *ok)
{
	int i, x0 = 9999, y0 = 9999, x1 = -9999, y1 = -9999;

	for (i = 0; i < n; i++) {
		if (d[i].x < x0) x0 = d[i].x;
		if (d[i].y < y0) y0 = d[i].y;
		if (d[i].x + d[i].cell.w > x1) x1 = d[i].x + d[i].cell.w;
		if (d[i].y + d[i].cell.h > y1) y1 = d[i].y + d[i].cell.h;
	}
	*ok = n > 0;
	return dod_rect(x0, y0, x1 - x0, y1 - y0);
}

/* Draw rects of the retail layout with the client's margins, for the worst case of every optional piece. */
static void retail_elements(void)
{
	DodRetailInput in;
	DodRetailDraw d[DOD_RETAIL_MAX_DRAWS], one[DOD_RETAIL_MAX_DRAWS];
	DodRetailText text;
	int n, i, ok;
	int status_n = 0, ammo_n = 0, timer_n = 0, flag_n = 0;
	DodRetailDraw status[DOD_RETAIL_MAX_DRAWS], ammo[DOD_RETAIL_MAX_DRAWS], timer[DOD_RETAIL_MAX_DRAWS],
		flags[DOD_RETAIL_MAX_DRAWS];

	memset(&in, 0, sizeof(in));
	in.screen_w = 640;
	in.screen_h = 480;
	in.health = 100;
	in.stamina = 100.0f;
	in.show_reinforcements = 1;
	in.seconds_left = 5999;	/* 99:59, widest timer */
	in.num_points = DOD_HUD_MAX_FLAGS;
	in.crosshair = 0;
	in.weapon = Dod_RetailClipForKit("weapon_dod_garand", 0);
	assert(in.weapon);
	in.clip = 8;
	in.reserve = 8 * 99;
	in.safe_left = in.safe_right = DOD_SAFE_L;
	in.safe_top = in.safe_bottom = DOD_SAFE_T;
	in.text_w = 4 * DOD_HUD_CHAR_W;	/* "x 99" */
	in.text_h = DOD_HUD_LINE_H;
	n = Dod_RetailHudLayoutText(&in, d, DOD_RETAIL_MAX_DRAWS, &text);
	assert(n > 0 && text.x >= 0);
	(void)one;
	/* classify draws by the part they belong to */
	for (i = 0; i < n; i++) {
		const int s = d[i].cell.sheet;
		if (s == DOD_SHEET_OBJ_NEUTRAL || s == DOD_SHEET_OBJ_ALLIES || s == DOD_SHEET_OBJ_AXIS)
			flags[flag_n++] = d[i];
		else if (s == DOD_SHEET_BULLETS || s == DOD_SHEET_CLIPS || s == DOD_SHEET_EXTRAS)
			ammo[ammo_n++] = d[i];
		else if (d[i].y < 100 && d[i].x < 120)
			timer[timer_n++] = d[i];
		else
			status[status_n++] = d[i];
	}
	assert(status_n && ammo_n && timer_n && flag_n == DOD_HUD_MAX_FLAGS);
	{
		DodRect r = union_of(status, status_n, &ok);
		add("status panel + stamina + reinforcements", r, ST_PLAY | ST_MENU | ST_BOARD | ST_VOICE);
		r = union_of(ammo, ammo_n, &ok);
		/* the "x NN" text */
		{
			const int tx1 = text.x + in.text_w, ty1 = text.y + DOD_HUD_LINE_H;
			int x1 = r.x + r.w > tx1 ? r.x + r.w : tx1, y1 = r.y + r.h > ty1 ? r.y + r.h : ty1;
			r.w = x1 - r.x;
			r.h = y1 - r.y;
		}
		add("ammo clip + spare text", r, ST_PLAY | ST_MENU | ST_BOARD | ST_VOICE);
		r = union_of(timer, timer_n, &ok);
		add("timer", r, ST_PLAY | ST_DEAD | ST_MENU | ST_BOARD | ST_VOICE | ST_CHAT);
		r = union_of(flags, flag_n, &ok);
		add("flag row (12 flags)", r, ST_PLAY | ST_DEAD | ST_MENU | ST_BOARD | ST_VOICE | ST_CHAT);
	}
}

static int rows_for_menu(int wanted)
{
	const int cap = Dod_HudModalRows(DOD_HUD_LINE_H);
	return wanted < cap ? wanted : cap;
}

static void build(void)
{
	const int lh = DOD_HUD_LINE_H;
	DodRect r;

	retail_elements();
	/* dead: no status panel, no ammo; the respawn text takes the corner */
	/* (the status/ammo elements above are not in ST_DEAD) */
	add("respawn text", Dod_HudRespawnText(lh), ST_DEAD);

	/* kill feed: worst case lines, widest */
	add("kill feed", Dod_HudKillFeed(DOD_HUD_KILL_LINES, lh), ST_PLAY | ST_DEAD);
	/* chat lines at the bottom of the play area; hidden while a menu, the voice menu, the board or chat input is up */
	add("chat lines", Dod_HudChatLines(lh), ST_PLAY | ST_DEAD);

	/* team menu: title, blank, 3 items, blank, hint = 7 lines; class menu: title + 6 classes = 7 lines (blank dropped) */
	r = Dod_HudMenuPanel(rows_for_menu(7), 28 * DOD_HUD_CHAR_W, lh);
	add("team/class menu panel", r, ST_MENU);
	/* voice menu: title + up to 10 entries, capped to the band; 20 chars wide */
	r = Dod_HudCentredPanel(11, 20 * DOD_HUD_CHAR_W, lh);
	add("voice panel", r, ST_VOICE);
	/* scoreboard: widest, as many rows as the band holds */
	r = Dod_HudCentredPanel(40, 520, lh);
	add("scoreboard panel", r, ST_BOARD);
	/* chatting: keyboard + prompt above it; status/ammo are under the opaque keyboard panel */
	add("on-screen keyboard", Dod_HudKeyboard(), ST_CHAT);
	add("chat prompt", Dod_HudChatInput(lh), ST_CHAT);
}

int main(void)
{
	static const struct { unsigned st; const char *name; } states[] = {
		{ ST_PLAY, "play" }, { ST_DEAD, "dead" }, { ST_MENU, "menu" }, { ST_VOICE, "voice" },
		{ ST_BOARD, "scoreboard" }, { ST_CHAT, "chat" },
	};
	int i, j, s, bad = 0;

	build();
	/* every element inside the safe area */
	for (i = 0; i < nels; i++) {
		if (!dod_rect_in_safe(els[i].r)) {
			printf("OUTSIDE SAFE AREA: %s (%d,%d %dx%d)\n", els[i].name, els[i].r.x, els[i].r.y, els[i].r.w, els[i].r.h);
			bad++;
		}
	}
	/* no two elements of one screen state intersect */
	for (s = 0; s < (int)(sizeof(states) / sizeof(states[0])); s++)
		for (i = 0; i < nels; i++)
			for (j = i + 1; j < nels; j++)
				if ((els[i].groups & states[s].st) && (els[j].groups & states[s].st) && dod_rect_hit(els[i].r, els[j].r)) {
					printf("OVERLAP in %s: %s (%d,%d %dx%d) x %s (%d,%d %dx%d)\n", states[s].name, els[i].name, els[i].r.x,
						els[i].r.y, els[i].r.w, els[i].r.h, els[j].name, els[j].r.x, els[j].r.y, els[j].r.w, els[j].r.h);
					bad++;
				}
	fflush(stdout);
	assert(bad == 0);
	/* with the keyboard up the layout leaves nothing at the bottom for it to cut into */
	{
		DodRetailInput in;
		DodRetailDraw d[DOD_RETAIL_MAX_DRAWS];
		const DodRect kb = Dod_HudKeyboard();
		int n, k;

		memset(&in, 0, sizeof(in));
		in.screen_w = 640;
		in.screen_h = 480;
		in.health = 100;
		in.stamina = 100.0f;
		in.show_reinforcements = 1;
		in.seconds_left = 90;
		in.weapon = Dod_RetailClipForKit("weapon_dod_garand", 0);
		in.clip = 8;
		in.reserve = 80;
		in.hide_bottom = 1;
		n = Dod_RetailHudLayout(&in, d, DOD_RETAIL_MAX_DRAWS);
		for (k = 0; k < n; k++)
			assert(!dod_rect_hit(dod_rect(d[k].x, d[k].y, d[k].cell.w, d[k].cell.h), kb));
		assert(n > 0);	/* the timer row stays */
	}
	puts("hud geometry ok");
	return 0;
}
