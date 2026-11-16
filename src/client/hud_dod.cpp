/* Day of Defeat HUD (client); see hud_dod.h, dod_hud.h and dod_hud_retail.h. */
#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "ammohistory.h"

int __MsgFunc_DodHud( const char *pszName, int iSize, void *pbuf )
{
	return gHUD.m_Dod.MsgFunc_DodHud( pszName, iSize, pbuf );
}

int __MsgFunc_DodVoice( const char *pszName, int iSize, void *pbuf )
{
	return gHUD.m_Dod.MsgFunc_DodVoice( pszName, iSize, pbuf );
}

int __MsgFunc_DodBoard( const char *pszName, int iSize, void *pbuf )
{
	return gHUD.m_Dod.MsgFunc_DodBoard( pszName, iSize, pbuf );
}

void CHudDod::InitState( void )
{
	int i;

	if( m_pCvar )
		return;	/* Init after InitState: already registered */
	memset( &m_state, 0, sizeof( m_state ) );
	m_flLastUpdate = 0.0f;
	m_flTimerBase = 0.0f;
	m_fHaveState = 0;
	for( i = 0; i < DOD_SHEET_COUNT; i++ )
		m_hSheets[i] = 0;
	m_fSheetsTried = 0;
	m_fSheetTried = 0;
	m_fDrawLogged = 0;
	m_szLoggedWeapon[0] = 0;
	m_pCvar = gEngfuncs.pfnRegisterVariable( "cl_dodhud", "1", FCVAR_ARCHIVE );
	HOOK_MESSAGE( DodHud );
	HOOK_MESSAGE( DodBoard );
	HOOK_MESSAGE( DodVoice );
	m_szVoice[0] = 0;
	m_iVoiceSel = 0;
	m_flVoiceTime = 0.0f;
	m_fVoiceShown = 0;
	m_szBoard[0] = 0;
	m_flBoardTime = 0.0f;
	m_fBoardShown = 0;
}

int CHudDod::Init( void )
{
	InitState();
	m_iFlags = HUD_ACTIVE;
	gHUD.AddHudElem( this );
	return 1;
}

int CHudDod::VidInit( void )
{
	int i;

	/* Sprite handles do not survive a video restart: load again on demand. */
	for( i = 0; i < DOD_SHEET_COUNT; i++ )
		m_hSheets[i] = 0;
	m_fSheetsTried = 0;
	m_fSheetTried = 0;
	return 1;
}

int CHudDod::MsgFunc_DodHud( const char *pszName, int iSize, void *pbuf )
{
	int owners_lo;

	BEGIN_READ( pbuf, iSize );
	m_state.allies_score = (short)READ_SHORT();
	m_state.axis_score = (short)READ_SHORT();
	m_state.round_number = (unsigned char)READ_BYTE();
	m_state.rounds_to_win = (unsigned char)READ_BYTE();
	m_state.won_allies = (unsigned char)READ_BYTE();
	m_state.won_axis = (unsigned char)READ_BYTE();
	m_state.seconds_left = (short)READ_SHORT();
	m_state.phase = (unsigned char)READ_BYTE();
	m_state.num_points = (unsigned char)READ_BYTE();
	owners_lo = READ_SHORT();
	m_state.owners = (unsigned short)owners_lo;
	m_state.my_team = (unsigned char)READ_BYTE();
	m_state.my_class = (unsigned char)READ_BYTE();
	m_state.grenades = (unsigned char)READ_BYTE();
	m_state.kit = (unsigned char)READ_BYTE();
	m_state.stamina = (unsigned char)READ_BYTE();
	m_flLastUpdate = m_flTimerBase = gEngfuncs.GetClientTime();
	m_fHaveState = 1;
	return 1;
}

/* The server sends the scoreboard text (Dod_FormatScoreboard) once a second while the player has it open, and an
 * empty one to close it. */
int CHudDod::MsgFunc_DodBoard( const char *pszName, int iSize, void *pbuf )
{
	int show;
	const char *text;

	BEGIN_READ( pbuf, iSize );
	show = READ_BYTE();
	text = show ? READ_STRING() : "";
	strncpy( m_szBoard, show && text ? text : "", sizeof( m_szBoard ) - 1 );
	m_szBoard[sizeof( m_szBoard ) - 1] = 0;
	m_flBoardTime = gEngfuncs.GetClientTime();
	m_fBoardShown = show && m_szBoard[0];
	return 1;
}

/* The voice menu (Y on the pad): the server sends the lines and the highlighted one whenever it changes. */
int CHudDod::MsgFunc_DodVoice( const char *pszName, int iSize, void *pbuf )
{
	int show;

	BEGIN_READ( pbuf, iSize );
	show = READ_BYTE();
	m_iVoiceSel = show ? READ_BYTE() : 0;
	strncpy( m_szVoice, show ? READ_STRING() : "", sizeof( m_szVoice ) - 1 );
	m_szVoice[sizeof( m_szVoice ) - 1] = 0;
	m_flVoiceTime = gEngfuncs.GetClientTime();
	m_fVoiceShown = show && m_szVoice[0];
	return 1;
}

void CHudDod::DrawVoice( void )
{
	char line[48];
	const char *p = m_szVoice;
	const int line_h = gHUD.m_scrinfo.iCharHeight > 0 ? gHUD.m_scrinfo.iCharHeight : 12;
	const int w = ScreenWidth > 0 ? ScreenWidth : 320, h = ScreenHeight > 0 ? ScreenHeight : 240;
	const int pw = 150, pad = 8;
	int rows = 1, x0, y0, ph, y, row = 0;

	if( !m_fVoiceShown || gEngfuncs.GetClientTime() - m_flVoiceTime > 9.0f )
		return;
	for( const char *q = p; *q; q++ )
		if( *q == '\n' ) rows++;
	ph = ( rows + 1 ) * line_h + 2 * pad;
	x0 = ( w - pw ) / 2;
	y0 = ( h - ph ) / 2;
	if( y0 < 8 ) y0 = 8;
	gEngfuncs.pfnFillRGBABlend( x0, y0, pw, ph, 0, 0, 0, 170 );
	gEngfuncs.pfnFillRGBABlend( x0, y0, pw, 1, 200, 200, 200, 160 );
	gEngfuncs.pfnFillRGBABlend( x0, y0 + ph - 1, pw, 1, 200, 200, 200, 160 );
	gEngfuncs.pfnFillRGBABlend( x0, y0, 1, ph, 200, 200, 200, 160 );
	gEngfuncs.pfnFillRGBABlend( x0 + pw - 1, y0, 1, ph, 200, 200, 200, 160 );
	y = y0 + pad;
	gHUD.DrawHudString( x0 + pad, y, x0 + pw - pad, "Voice", 255, 255, 255 );
	y += line_h;
	while( *p )
	{
		int n = 0;
		const int sel = row == m_iVoiceSel;

		while( p[n] && p[n] != '\n' && n < (int)sizeof( line ) - 1 )
		{
			line[n] = p[n];
			n++;
		}
		line[n] = 0;
		p += n;
		if( *p == '\n' ) p++;
		/* the chosen line sits on a bar (the HUD font has no marker glyph that reads well) */
		if( sel )
			gEngfuncs.pfnFillRGBABlend( x0 + 2, y - 1, pw - 4, line_h, 255, 230, 90, 70 );
		gHUD.DrawHudString( x0 + pad, y, x0 + pw - pad, line, sel ? 255 : 220, sel ? 230 : 220, sel ? 90 : 220 );
		y += line_h;
		row++;
	}
}

void CHudDod::DrawBoard( void )
{
	/* A centred panel, as the retail scoreboard: the title, then a team header with its score and the team's players
	 * ranked, name and class on the left and Score/Kills/Deaths against the right edge. */
	char line[96], left[96];
	const char *p = m_szBoard;
	const int line_h = gHUD.m_scrinfo.iCharHeight > 0 ? gHUD.m_scrinfo.iCharHeight : 12;
	const int w = ScreenWidth > 0 ? ScreenWidth : 320, h = ScreenHeight > 0 ? ScreenHeight : 240;
	const int pw = w - 24 < 288 ? w - 24 : 288, pad = 8;
	int rows = 0, gaps = 0, ph, x0, y0, y;

	if( !m_fBoardShown || gEngfuncs.GetClientTime() - m_flBoardTime > 3.0f )
		return;
	for( const char *q = p; *q; q++ )
	{
		if( *q == '\n' && q[1] == '\n' ) gaps++;
		else if( *q == '\n' && q[1] ) rows++;
	}
	rows++;	/* the first line has no newline before it */
	ph = ( rows + 1 ) * line_h + gaps * ( line_h / 2 ) + 2 * pad;	/* + the title */
	x0 = ( w - pw ) / 2;
	y0 = ( h - ph ) / 2;
	if( y0 < 8 ) y0 = 8;
	gEngfuncs.pfnFillRGBABlend( x0, y0, pw, ph, 0, 0, 0, 170 );
	/* a 1 px border */
	gEngfuncs.pfnFillRGBABlend( x0, y0, pw, 1, 200, 200, 200, 160 );
	gEngfuncs.pfnFillRGBABlend( x0, y0 + ph - 1, pw, 1, 200, 200, 200, 160 );
	gEngfuncs.pfnFillRGBABlend( x0, y0, 1, ph, 200, 200, 200, 160 );
	gEngfuncs.pfnFillRGBABlend( x0 + pw - 1, y0, 1, ph, 200, 200, 200, 160 );
	y = y0 + pad;
	{
		const char *title = "Scoreboard";
		gHUD.DrawHudString( x0 + ( pw - gHUD.DrawHudStringLen( title )) / 2, y, x0 + pw, title, 255, 255, 255 );
		y += line_h;
	}
	while( *p )
	{
		int n = 0, r = 255, g = 255, b = 255, split = -1, i;

		while( p[n] && p[n] != '\n' && n < (int)sizeof( line ) - 1 )
		{
			line[n] = p[n];
			n++;
		}
		line[n] = 0;
		p += n;
		if( *p == '\n' ) p++;
		if( !n ) { y += line_h / 2; continue; }
		if( !strncmp( line, "Allies", 6 )) { r = 140; g = 230; b = 140; }
		else if( !strncmp( line, "Axis", 4 )) { r = 235; g = 130; b = 130; }
		else if( line[0] == '>' )
		{
			r = 255; g = 230; b = 90;
			memmove( line, line + 1, n );	/* the viewer's row: a bar instead of the marker glyph */
			n--;
			gEngfuncs.pfnFillRGBABlend( x0 + 2, y - 1, pw - 4, line_h, 255, 230, 90, 60 );
		}
		/* where the right-hand part starts: the score after a team name, S.. K.. D.. after a player */
		if( !strncmp( line, "Allies", 6 ) || !strncmp( line, "Axis", 4 ))
		{
			for( i = 0; line[i]; i++ )
				if( line[i] == ' ' && line[i + 1] == ' ' ) { split = i; break; }
		}
		else
		{
			for( i = n - 1; i > 1; i-- )
				if( line[i] == 'S' && line[i - 1] == ' ' && line[i - 2] == ' ' ) { split = i - 2; break; }
		}
		if( split > 0 )
		{
			const char *right = line + split;
			while( *right == ' ' ) right++;
			strncpy( left, line, split );
			left[split] = 0;
			gHUD.DrawHudString( x0 + pad, y, x0 + pw - pad, left, r, g, b );
			gHUD.DrawHudStringReverse( x0 + pw - pad, y, x0 + pad, right, r, g, b );
		}
		else
			gHUD.DrawHudString( x0 + pad, y, x0 + pw - pad, line, r, g, b );
		y += line_h;
	}
}

int CHudDod::DrawRetail( float flTime )
{
	DodRetailInput in;
	DodRetailDraw draws[DOD_RETAIL_MAX_DRAWS];
	DodRetailText spare;
	const WEAPON *pw;
	int count, i, drawn = 0;

	if( !m_pCvar || m_pCvar->value == 0.0f )
		return 0;

	/* The sheets load the first time the HUD draws (SPR_Load on demand). */
	if( !m_fSheetsTried )
	{
		m_fSheetsTried = 1;
		/* The always-on pieces load now; the clip and rocket sheets (65 KB
		 * each while loading) only when a gun that uses them is held. */
		for( i = 0; i < DOD_SHEET_COUNT; i++ )
		{
			m_hSheets[i] = 0;
			if( Dod_RetailSheetAlwaysOn( i ))
			{
				m_fSheetTried |= 1u << i;
				m_hSheets[i] = SPR_Load( dod_retail_sheet_files[i] );
			}
		}
		gEngfuncs.Con_Printf( "Xash3D GameCube: DoD retail HUD sheets layout=%d cross=%d\n",
			m_hSheets[DOD_SHEET_LAYOUT] ? 1 : 0, m_hSheets[DOD_SHEET_CROSS] ? 1 : 0 );
		for( i = 0; i < DOD_SHEET_COUNT; i++ )
			gEngfuncs.Con_Printf( "Xash3D GameCube: DoD retail HUD sheet %s h=%d %dx%d\n",
				dod_retail_sheet_files[i], m_hSheets[i],
				m_hSheets[i] ? SPR_Width( m_hSheets[i], 0 ) : 0,
				m_hSheets[i] ? SPR_Height( m_hSheets[i], 0 ) : 0 );
	}
	if( !m_hSheets[DOD_SHEET_LAYOUT] )
		return 0;	/* no retail sheets on this disc: let the caller draw */

	/* Retail lays the 640-res art out 1:1 on a 640x480 screen. The GameCube
	 * HUD space is 320x240 (shown at 2x), so lay out at 640x480 and scale
	 * every draw by the HUD space's share of that. */
	const int hud_w = ScreenWidth > 0 ? ScreenWidth : 640;
	const int hud_h = ScreenHeight > 0 ? ScreenHeight : 480;
	const float sx = hud_w / 640.0f, sy = hud_h / 480.0f;

	memset( &in, 0, sizeof( in ) );
	in.screen_w = 640;
	in.screen_h = 480;
	in.health = gHUD.m_Health.m_iHealth;
	in.stamina = m_fHaveState ? (float)m_state.stamina : 100.0f;
	in.spectating = g_iUser1 != 0;
	in.dead = in.health <= 0;
	in.show_reinforcements = 0;
	in.crosshair = 1;
	in.crosshair_gap = 4;
	/* The server sends the clock a couple of times a second; count down
	 * between messages, and hide it when the messages stop. */
	in.seconds_left = -1;
	if( m_fHaveState && m_state.seconds_left > 0
		&& gEngfuncs.GetClientTime() - m_flLastUpdate <= 4.0f )
	{
		const int left = m_state.seconds_left - (int)( gEngfuncs.GetClientTime() - m_flTimerBase );
		in.seconds_left = left > 0 ? left : 0;
	}

	/* Capture points from the DodHud message, left to right. */
	if( m_fHaveState )
	{
		in.num_points = m_state.num_points < DOD_HUD_MAX_POINTS ? m_state.num_points : DOD_HUD_MAX_POINTS;
		for( i = 0; i < in.num_points; i++ )
			in.owner[i] = Dod_HudOwnerOf( m_state.owners, i );
	}

	/* Clip ammo: the held weapon's magazine graphic and spare clips. */
	pw = gHUD.m_Ammo.GetCurrentWeapon();
	if( pw && pw->iClip >= 0 )
	{
		in.weapon = Dod_RetailClipForKit( pw->szName, m_fHaveState ? m_state.kit : 0 );
		in.clip = pw->iClip;
		in.reserve = pw->iAmmoType > 0 ? gWR.CountAmmo( pw->iAmmoType ) : 0;
	}

	count = Dod_RetailHudLayoutText( &in, draws, DOD_RETAIL_MAX_DRAWS, &spare );
	{
		/* Log each weapon change once (which clip graphic, if any, is drawn). */
		const char *name = pw ? pw->szName : "-";
		if( strncmp( m_szLoggedWeapon, name, sizeof( m_szLoggedWeapon ) - 1 ))
		{
			strncpy( m_szLoggedWeapon, name, sizeof( m_szLoggedWeapon ) - 1 );
			m_szLoggedWeapon[sizeof( m_szLoggedWeapon ) - 1] = 0;
			gEngfuncs.Con_Printf( "Xash3D GameCube: DoD retail HUD weapon %s clip=%d reserve=%d graphic=%s draws=%d\n",
				name, in.clip, in.reserve, in.weapon ? in.weapon->key : "-", count );
		}
	}
	for( i = 0; i < count; i++ )
	{
		const int sheet = draws[i].cell.sheet;
		wrect_t rc;

		if( !m_hSheets[sheet] && !( m_fSheetTried & ( 1u << sheet )))
		{
			m_fSheetTried |= 1u << sheet;
			m_hSheets[sheet] = SPR_Load( dod_retail_sheet_files[sheet] );
			gEngfuncs.Con_Printf( "Xash3D GameCube: DoD retail HUD sheet %s loaded on use h=%d\n",
				dod_retail_sheet_files[sheet], m_hSheets[sheet] );
		}
		const HSPRITE spr = m_hSheets[sheet];

		if( !spr )
			continue;
		rc.left = draws[i].cell.x;
		rc.right = draws[i].cell.x + draws[i].cell.w;
		rc.top = draws[i].cell.y;
		rc.bottom = draws[i].cell.y + draws[i].cell.h;
		SPR_Set( spr, 255, 255, 255 );
		if( sx == 1.0f && sy == 1.0f )
			SPR_DrawHoles( 0, draws[i].x, draws[i].y, &rc );
		else
		{
			/* Snap both edges so neighbouring pieces still meet. */
			const int x0 = (int)( draws[i].x * sx + 0.5f );
			const int y0 = (int)( draws[i].y * sy + 0.5f );
			int w = (int)(( draws[i].x + draws[i].cell.w ) * sx + 0.5f ) - x0;
			int h = (int)(( draws[i].y + draws[i].cell.h ) * sy + 0.5f ) - y0;

			if( w < 1 ) w = 1;
			if( h < 1 ) h = 1;
			gEngfuncs.pfnSPR_DrawGeneric( 0, x0, y0, &rc, 0, 0, w, h );
		}
		drawn++;
	}
	if( spare.count > 0 && spare.x >= 0 )
	{
		/* retail: DrawHudString( x, base, x + 64, "x %d", white ) */
		char buf[16];
		const int tx = (int)( spare.x * sx + 0.5f );

		sprintf( buf, "x %d", spare.count );
		gHUD.DrawHudString( tx, (int)( spare.y * sy + 0.5f ), tx + (int)( 64 * sx + 0.5f ), buf, 255, 255, 255 );
	}
	DrawBoard();
	DrawVoice();
	if( !m_fDrawLogged )
	{
		m_fDrawLogged = 1;
		gEngfuncs.Con_Printf( "Xash3D GameCube: DoD retail HUD drew %d/%d health=%d timer=%d weapon=%s\n",
			drawn, count, in.health, in.seconds_left, in.weapon ? in.weapon->key : "-" );
	}
	return 1;
}

int CHudDod::Draw( float flTime )
{
	DrawRetail( flTime );
	return 1;
}
