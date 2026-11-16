/* Day of Defeat HUD (client); see hud_dod.h, dod_hud.h and dod_hud_retail.h. */
#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "ammohistory.h"

int __MsgFunc_DodHud( const char *pszName, int iSize, void *pbuf )
{
	return gHUD.m_Dod.MsgFunc_DodHud( pszName, iSize, pbuf );
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
	m_fDrawLogged = 0;
	m_szLoggedWeapon[0] = 0;
	m_pCvar = gEngfuncs.pfnRegisterVariable( "cl_dodhud", "1", FCVAR_ARCHIVE );
	HOOK_MESSAGE( DodHud );
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
	m_flLastUpdate = m_flTimerBase = gEngfuncs.GetClientTime();
	m_fHaveState = 1;
	return 1;
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
		for( i = 0; i < DOD_SHEET_COUNT; i++ )
			m_hSheets[i] = SPR_Load( dod_retail_sheet_files[i] );
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
	in.stamina = 100.0f;	/* the server has no stamina yet: full tube */
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
		in.weapon = Dod_RetailClipFor( pw->szName );
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
		const HSPRITE spr = m_hSheets[draws[i].cell.sheet];
		wrect_t rc;

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
		char buf[16];

		sprintf( buf, "x%d", spare.count );
		gEngfuncs.pfnDrawSetTextColor( 1.0f, 1.0f, 1.0f );
		gEngfuncs.pfnDrawConsoleString( (int)( spare.x * sx + 0.5f ),
			(int)( spare.y * sy + 0.5f ), buf );
	}
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
