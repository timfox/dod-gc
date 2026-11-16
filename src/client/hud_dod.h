#ifndef DODGC_HUD_DOD_H
#define DODGC_HUD_DOD_H

/* Client side of the Day of Defeat HUD. Included from hud.h after CHudBase.
 * Draws the retail DoD 1.3 HUD (dod_hud_retail.h): the status panel with
 * stamina and health, the objective timer and the dynamic crosshair, from the
 * retail hud.txt sheets. The match state (clock, flags, scores) arrives in
 * the server's DodHud message (dod_hud.h). cl_dodhud 0 turns it off. */

#include "dod_hud.h"
#include "dod_hud_retail.h"

class CHudDod : public CHudBase
{
public:
	/* Register the cvar and the message; safe to call twice. The lean (quality 0)
	 * HUD never runs Init, so CHud::Init calls this first. */
	void InitState( void );
	int Init( void );
	int VidInit( void );
	int Draw( float flTime );
	/* The retail HUD, for the lean tier's Redraw: returns 1 when it drew and
	 * the caller should skip its own HUD. */
	int DrawRetail( float flTime );
	int MsgFunc_DodHud( const char *pszName, int iSize, void *pbuf );

private:
	DodHudState m_state;
	float m_flLastUpdate;
	float m_flTimerBase;	/* client time the clock was last set */
	int m_fHaveState;
	HSPRITE m_hSheets[DOD_SHEET_COUNT];
	int m_fSheetsTried;
	int m_fDrawLogged;
	cvar_t *m_pCvar;
};

#endif
