#ifndef DODGC_HUD_DOD_H
#define DODGC_HUD_DOD_H

/* Client side of the Day of Defeat HUD. Included from hud.h after CHudBase.
 * Layout and state live in dod_hud.h (host-tested); this class only receives
 * the server's DodHud message and draws the layout. It is off unless the
 * cl_dodhud cvar is set, and loads its one icon sprite the first time it draws. */

#include "dod_hud.h"

class CHudDod : public CHudBase
{
public:
	/* Register the cvar and the message; safe to call twice. The lean (quality 0)
	 * HUD never runs Init, so CHud::Init calls this first. */
	void InitState( void );
	int Init( void );
	int VidInit( void );
	int Draw( float flTime );
	int MsgFunc_DodHud( const char *pszName, int iSize, void *pbuf );

private:
	DodHudState m_state;
	float m_flLastUpdate;
	int m_fHaveState;
	HSPRITE m_hIcons;
	int m_fIconsTried;
	cvar_t *m_pCvar;
};

#endif
