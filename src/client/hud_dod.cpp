/* Day of Defeat HUD (client); see hud_dod.h and dod_hud.h. */
#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"

int __MsgFunc_DodHud( const char *pszName, int iSize, void *pbuf )
{
	return gHUD.m_Dod.MsgFunc_DodHud( pszName, iSize, pbuf );
}

void CHudDod::InitState( void )
{
	memset( &m_state, 0, sizeof( m_state ) );
	m_flLastUpdate = 0.0f;
	m_fHaveState = 0;
	m_hIcons = 0;
	m_fIconsTried = 0;
	/* Off by default: the server's text strip stays until this is turned on. */
	m_pCvar = gEngfuncs.pfnRegisterVariable( "cl_dodhud", "0", FCVAR_ARCHIVE );
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
	/* Sprite handles do not survive a video restart: load again on demand. */
	m_hIcons = 0;
	m_fIconsTried = 0;
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
	m_flLastUpdate = gEngfuncs.GetClientTime();
	m_fHaveState = 1;
	return 1;
}

int CHudDod::Draw( float flTime )
{
	DodHudOp ops[DOD_HUD_MAX_OPS];
	int count, i, char_w = 8, char_h = 16;

	if( !m_pCvar || m_pCvar->value == 0.0f || !m_fHaveState )
		return 1;
	/* The server sends a couple of times a second; stop drawing stale numbers. */
	if( gEngfuncs.GetClientTime() - m_flLastUpdate > 4.0f )
		return 1;

	gEngfuncs.pfnDrawConsoleStringLen( "W", &char_w, &char_h );
	if( char_w <= 0 ) char_w = 8;
	if( char_h <= 0 ) char_h = 16;
	count = Dod_HudLayout( &m_state, gHUD.m_scrinfo.iWidth > 0 ? gHUD.m_scrinfo.iWidth : 640,
		gHUD.m_scrinfo.iHeight > 0 ? gHUD.m_scrinfo.iHeight : 480, char_w, char_h, ops, DOD_HUD_MAX_OPS );

	for( i = 0; i < count; i++ )
	{
		if( ops[i].kind == DOD_HUD_TEXT )
		{
			gEngfuncs.pfnDrawSetTextColor( ops[i].r / 255.0f, ops[i].g / 255.0f, ops[i].b / 255.0f );
			gEngfuncs.pfnDrawConsoleString( ops[i].x, ops[i].y, ops[i].text );
		}
		else
		{
			wrect_t cell;
			/* One tiny sheet, loaded the first time a flag is drawn. */
			if( !m_hIcons && !m_fIconsTried )
			{
				m_fIconsTried = 1;
				m_hIcons = SPR_Load( "sprites/icon_obj.spr" );
			}
			if( !m_hIcons )
				continue;	/* no sprite on this disc: the text still shows */
			cell.left = ops[i].cell * DOD_HUD_ICON_SIZE;
			cell.right = cell.left + DOD_HUD_ICON_SIZE;
			cell.top = 0;
			cell.bottom = DOD_HUD_ICON_SIZE;
			SPR_Set( m_hIcons, 255, 255, 255 );
			SPR_DrawHoles( 0, ops[i].x, ops[i].y, &cell );
		}
	}
	return 1;
}
