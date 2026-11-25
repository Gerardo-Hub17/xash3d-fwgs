/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*
*	This product contains software technology licensed from Id
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc.
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
//
// menu.cpp
//
// generic menu handler
//
#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "com_weapons.h"

#include <string.h>
#include <stdio.h>
#include "draw_util.h"
#include "strl.h"

//#include "vgui_TeamFortressViewport.h"

#define MAX_MENU_STRING	512

char g_szMenuString[MAX_MENU_STRING];
char g_szPrelocalisedMenuString[MAX_MENU_STRING];

int KB_ConvertString( char *in, char **ppout );

void Touch_CloseMenu()
{
	// no touch screen (Wii, a statically linked client): the engine never filled gMobileAPI in and
	// calling a null pointer ended in a red screen the first time the team menu showed up
	if( !g_iMobileAPIVersion )
		return;

	if( gMobileAPI.pfnTouchRemoveButton )
		gMobileAPI.pfnTouchRemoveButton( "_menu_*" );

	if( gMobileAPI.pfnTouchSetClientOnly )
		gMobileAPI.pfnTouchSetClientOnly( 0 );
}

int CHudMenu :: Init( void )
{
	gHUD.AddHudElem( this );

	HOOK_MESSAGE( gHUD.m_Menu, ShowMenu );
	HOOK_MESSAGE( gHUD.m_Menu, VGUIMenu );
	HOOK_MESSAGE( gHUD.m_Menu, BuyClose );
	HOOK_MESSAGE( gHUD.m_Menu, AllowSpec );
	HOOK_COMMAND( gHUD.m_Menu, "client_buy_open", OldStyleMenuOpen );
	HOOK_COMMAND( gHUD.m_Menu, "client_buy_close", OldStyleMenuClose );
	HOOK_COMMAND( gHUD.m_Menu, "showvguimenu", ShowVGUIMenu );
	HOOK_COMMAND( gHUD.m_Menu, "menu_prev", MenuPrev );
	HOOK_COMMAND( gHUD.m_Menu, "menu_next", MenuNext );
	HOOK_COMMAND( gHUD.m_Menu, "menu_ok", MenuOk );
	HOOK_COMMAND( gHUD.m_Menu, "menu_cancel", MenuCancel );

	_extended_menus = CVAR_CREATE("_extended_menus", "1", FCVAR_ARCHIVE);
	m_pMenuOpen = CVAR_CREATE( "_wii_menu_open", "0", 0 ); // the engine reads it to switch the controller buttons
	m_pTextMul = CVAR_CREATE( "_wii_textmul", "0", 0 ); // the engine draws the console strings this many times bigger (0 = normal)

	// the extended menus are touch scripts (nothing on the Wii): keep the numbered text menu
	m_bPanel = !HUD_HasNativeMenu();
	if( m_bPanel )
		gEngfuncs.Cvar_SetValue( "_extended_menus", 0.0f );

	InitHUDData();

	m_bAllowSpec = true; // by default, spectating is allowed

	return 1;
}

void CHudMenu :: InitHUDData( void )
{
	m_fMenuDisplayed = 0;
	m_bitsValidSlots = 0;
	m_nLines = 0;
	m_iTitleLine = -1;
	m_iSelLine = -1;
	m_bTeamMenu = false;
	m_iPanelKind = 0;
	m_szMapInfo[0] = 0;
	SetPanelOpen( false );
	Reset();
}

void CHudMenu :: Reset( void )
{
	g_szPrelocalisedMenuString[0] = 0;
	m_fWaitingForMore = FALSE;
}

int CHudMenu :: VidInit( void )
{
	// new map: whatever menu was open belongs to the old one
	if( m_bPanel )
	{
		m_fMenuDisplayed = 0;
		m_iFlags &= ~HUD_DRAW;
		SetPanelOpen( false );
	}

	return 1;
}

int CHudMenu :: Draw( float flTime )
{
	// check for if menu is set to disappear
	if ( m_flShutoffTime > 0 )
	{
		if ( m_flShutoffTime <= gHUD.m_flTime )
		{  // times up, shutoff
			UserCmd_OldStyleMenuClose();
			return 1;
		}
	}

	// the panel draws itself (DrawPanel) so it also shows with the HUD hidden
	if ( m_bPanel )
		return 1;

	// don't draw the menu if the scoreboard is being shown
	//if ( gViewPort && gViewPort->IsScoreBoardVisible() )
		//return 1;

	// draw the menu, along the left-hand side of the screen

	// count the number of newlines
	int nlc = 0;
	int i;
	for ( i = 0; i < MAX_MENU_STRING && g_szMenuString[i] != '\0'; i++ )
	{
		if ( g_szMenuString[i] == '\n' )
			nlc++;
	}

	// center it
	int y = (ScreenHeight/2) - ((nlc/2)*12) - 40; // make sure it is above the say text
	int x = 20;

	i = 0;
	while ( i < MAX_MENU_STRING && g_szMenuString[i] != '\0' )
	{
		DrawUtils::DrawHudString( x, y, 320, g_szMenuString + i, 255, 255, 255 );
		y += 24;

		while ( i < MAX_MENU_STRING && g_szMenuString[i] != '\0' && g_szMenuString[i] != '\n' )
			i++;
		if ( g_szMenuString[i] == '\n' )
			i++;
	}

	return 1;
}

// selects an item from the menu
void CHudMenu :: SelectMenuItem( int menu_item )
{
	// if menu_item is in a valid slot,  send a menuselect command to the server
	if ( (menu_item > 0) && (m_bitsValidSlots & (1 << (menu_item-1))) )
	{
		char szbuf[32];
		const char *cmd = "menuselect";

		// The server only listens to "menuselect" for the stock team / class menus when the
		// client asked for the old text menus (_vgui_menus 0). If it believes we have VGUI it
		// silently drops it, and nothing comes after choosing the team. "jointeam" and
		// "joinclass" are what the VGUI menus send and work in both cases.
		if( m_bPanel && menu_item != 10 )
		{
			if( m_iPanelKind == MENU_TEAM )
				cmd = "jointeam";
			else if( m_iPanelKind == MENU_CLASS_T || m_iPanelKind == MENU_CLASS_CT )
				cmd = "joinclass";
		}

		sprintf( szbuf, "%s %d\n", cmd, menu_item );
		if( m_bPanel )
			gEngfuncs.Con_Printf( "[MENU] envio '%s %d' (_vgui_menus=%g)\n", cmd, menu_item, gEngfuncs.pfnGetCvarFloat( "_vgui_menus" ));
		ClientCmd( szbuf );

		UserCmd_OldStyleMenuClose();
	}
}


// Message handler for ShowMenu message
// takes four values:
//		short: a bitfield of keys that are valid input
//		char : the duration, in seconds, the menu should stay up. -1 means is stays until something is chosen.
//		byte : a boolean, TRUE if there is more string yet to be received before displaying the menu, FALSE if it's the last string
//		string: menu string to display
// if this message is never received, then scores will simply be the combined totals of the players.
int CHudMenu :: MsgFunc_ShowMenu( const char *pszName, int iSize, void *pbuf )
{
	char *temp = NULL, *menustring;

	BufferReader reader( pszName, pbuf, iSize );

	m_bitsValidSlots = reader.ReadShort();
	int DisplayTime = reader.ReadChar();
	int NeedMore = reader.ReadByte();

	if ( DisplayTime > 0 )
		m_flShutoffTime = DisplayTime + gHUD.m_flTime;
	else
		m_flShutoffTime = -1;

	if ( !m_bitsValidSlots )
	{
		UserCmd_OldStyleMenuClose(); // no valid slots means that the menu should be turned off
		return 1;
	}

	menustring = reader.ReadString();

	// menu will be replaced by scripted touch config
	// so execute it and exit
	if( _extended_menus->value != 0.0f )
	{
		if( !strncmp(menustring, "#Radio", 6 ) )
		{
			if( menustring[6] == 'A' )
			{
				ShowVGUIMenu(MENU_RADIOA); return 1;
			}
			else if( menustring[6] == 'B' )
			{
				ShowVGUIMenu(MENU_RADIOB); return 1;
			}
			else if( menustring[6] == 'C' )
			{
				ShowVGUIMenu(MENU_RADIOC); return 1;
			}
			else ShowVGUIMenu( MENU_NUMERICAL_MENU ); // we just show touch screen numbers
		}
		else ShowVGUIMenu(MENU_NUMERICAL_MENU);
	}
	else ShowVGUIMenu(MENU_NUMERICAL_MENU);

	if ( !m_fWaitingForMore ) // this is the start of a new menu
	{
		strlcpy( g_szPrelocalisedMenuString, menustring, sizeof( g_szPrelocalisedMenuString ) );
	}
	else
	{  // append to the current menu string
		strlcat( g_szPrelocalisedMenuString, menustring, sizeof( g_szPrelocalisedMenuString ) );
	}

	if ( !NeedMore )
	{  // we have the whole string, so we can localise it now
		strlcpy( g_szMenuString, gHUD.m_TextMessage.BufferedLocaliseTextString( g_szPrelocalisedMenuString ), sizeof( g_szMenuString ) );
		// Swap in characters
		if ( KB_ConvertString( g_szMenuString, &temp ) )
		{
			strlcpy( g_szMenuString, temp, sizeof( g_szMenuString ) );
			free( temp );
		}
	}

	m_fMenuDisplayed = 1;
	m_iFlags |= HUD_DRAW;

	m_fWaitingForMore = NeedMore;

	if( m_bPanel && !NeedMore )
	{
		// the stock team/class menus are shown from our own table (titles.txt may not even be
		// there); any other numbered menu (buy, votes, AMX Mod X...) is shown from its text
		int known = PanelKnownMenu( g_szPrelocalisedMenuString );

		gEngfuncs.Con_Printf( "[MENU] ShowMenu teclas=0x%x tipo=%d '%.32s'\n", m_bitsValidSlots, known, g_szPrelocalisedMenuString );

		if( !known || !ShowPanelFor( known ))
			OpenPanel( g_szMenuString, false );
	}

	return 1;
}

int CHudMenu::MsgFunc_VGUIMenu( const char *pszName, int iSize, void *pbuf )
{
	BufferReader reader( pszName, pbuf, iSize );

	int menuType = reader.ReadByte();
	m_bitsValidSlots = reader.ReadShort(); // the VGUI menus ignore it; the panel needs it

	if( m_bPanel )
	{
		m_flShutoffTime = -1;
		gEngfuncs.Con_Printf( "[MENU] VGUIMenu tipo=%d teclas=0x%x (_vgui_menus=%g)\n", menuType, m_bitsValidSlots, gEngfuncs.pfnGetCvarFloat( "_vgui_menus" ));

		// config.cfg or a late exec can bring the archived "1" back after HUD_Init; if the
		// server is sending VGUI menus that is probably what happened. Set it again so the
		// menus that come next (class, buy) arrive as text menus.
		if( gEngfuncs.pfnGetCvarFloat( "_vgui_menus" ) != 0.0f )
		{
			gEngfuncs.Con_Printf( "[MENU] _vgui_menus estaba en 1: se vuelve a poner en 0\n" );
			gEngfuncs.Cvar_Set( "_vgui_menus", "0" );
		}
	}

	ShowVGUIMenu(menuType);
	return 1;
}

int CHudMenu::MsgFunc_BuyClose(const char *pszName, int iSize, void *pbuf)
{
	Touch_CloseMenu();

	if( m_bPanel )
		UserCmd_OldStyleMenuClose();

	if (g_pMenu)
		g_pMenu->HideVGUIMenu();

	return 1;
}

int CHudMenu::MsgFunc_AllowSpec(const char *pszName, int iSize, void *pbuf)
{
	BufferReader reader( pszName, pbuf, iSize );

	m_bAllowSpec = (bool)reader.ReadByte();

	return 1;
}

void CHudMenu::UserCmd_OldStyleMenuOpen()
{
	m_flShutoffTime = -1; // stay open until user will not close it
	strlcpy( g_szMenuString, gHUD.m_TextMessage.BufferedLocaliseTextString("Buy"), sizeof( g_szMenuString ) );
}

void CHudMenu::UserCmd_OldStyleMenuClose()
{
	m_fMenuDisplayed = 0; // no valid slots means that the menu should be turned off
	m_iFlags &= ~HUD_DRAW;

	SetPanelOpen( false );
	Touch_CloseMenu();
}

// lol, no real VGUI here
// it's really good only for touchscreen

void CHudMenu::ShowVGUIMenu( int menuType )
{
	int team = g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber;
	int haveCancel = team != TEAM_UNASSIGNED ? 1 : 0;

	if( m_bPanel && ShowPanelFor( menuType ))
		return;

	if( g_pMenu && !UseOldTouchMenusEnabled() )
	{
		switch( menuType )
		{
		case MENU_TEAM:
		{
			int param = 0;
			if( m_bAllowSpec )
			{
				if( g_iTeamNumber == TEAM_UNASSIGNED ||
					g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].dead ||
					!g_iFreezeTimeOver )
				{
					param |= 1 << 0;
				}
			}

			if( IsASMapType() && g_iTeamNumber == TEAM_CT )
				param |= 1 << 1;

			if( haveCancel )
				param |= 1 << 2;

			g_pMenu->ShowVGUIMenu( menuType, param, 0 );
			return;
		}
		case MENU_CLASS_T:
		case MENU_CLASS_CT:
			g_pMenu->ShowVGUIMenu( menuType, gHUD.GetGameType() == GAME_CZERO, haveCancel );
			return;
		case MENU_BUY:
		case MENU_BUY_PISTOL:
		case MENU_BUY_SHOTGUN:
		case MENU_BUY_RIFLE:
		case MENU_BUY_SUBMACHINEGUN:
		case MENU_BUY_MACHINEGUN:
		case MENU_BUY_ITEM:
			g_pMenu->ShowVGUIMenu( menuType, gHUD.GetGameType() == GAME_CZERO, team );
			return;
		}
	}

	const char *szCmd;

	switch(menuType)
	{
	case MENU_TEAM:
		szCmd = "exec touch/chooseteam.cfg";
		break;
	case MENU_CLASS_T:
		szCmd = "exec touch/chooseteam_tr.cfg";
		break;
	case MENU_CLASS_CT:
		szCmd = "exec touch/chooseteam_ct.cfg";
		break;
	case MENU_BUY:
		szCmd = "exec touch/buy.cfg";
		break;
	case MENU_BUY_PISTOL:
		if( g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber == TEAM_TERRORIST )
			szCmd = "exec touch/buy_pistol_t.cfg";
		else szCmd = "exec touch/buy_pistol_ct.cfg";
		break;
	case MENU_BUY_SHOTGUN:
		if( g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber == TEAM_TERRORIST )
			szCmd = "exec touch/buy_shotgun_t.cfg";
		else szCmd = "exec touch/buy_shotgun_ct.cfg";
		break;
	case MENU_BUY_RIFLE:
		if( g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber == TEAM_TERRORIST )
			szCmd = "exec touch/buy_rifle_t.cfg";
		else szCmd ="exec touch/buy_rifle_ct.cfg";
		break;
	case MENU_BUY_SUBMACHINEGUN:
		if( g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber == TEAM_TERRORIST )
			szCmd = "exec touch/buy_submachinegun_t.cfg";
		else szCmd = "exec touch/buy_submachinegun_ct.cfg";
		break;
	case MENU_BUY_MACHINEGUN:
		if( g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber == TEAM_TERRORIST )
			szCmd = "exec touch/buy_machinegun_t.cfg";
		else szCmd = "exec touch/buy_machinegun_ct.cfg";
		break;
	case MENU_BUY_ITEM:
		if( g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber == TEAM_TERRORIST )
			szCmd = "exec touch/buy_item_t.cfg";
		else szCmd = "exec touch/buy_item_ct.cfg";
		break;
	case MENU_RADIOA:
		szCmd = "exec touch/radioa.cfg";
		break;
	case MENU_RADIOB:
		szCmd = "exec touch/radiob.cfg";
		break;
	case MENU_RADIOC:
		szCmd = "exec touch/radioc.cfg";
		break;
	case MENU_RADIOSELECTOR:
		szCmd = "exec touch/radioselector.cfg";
		break;
	case MENU_BUY_CSDM:
		szCmd = "exec touch/custom/dm_menu.cfg";
		break;
	case MENU_NUMERICAL_MENU:
#ifdef __ANDROID__
		szCmd = "exec touch/numerical_menu.cfg";
		break;
#else
		return;
#endif
	default:
		UserCmd_OldStyleMenuClose();
		return;
	}

	m_fMenuDisplayed = 1;
	ClientCmd(szCmd);
}

void CHudMenu::UserCmd_ShowVGUIMenu()
{
	if( gEngfuncs.Cmd_Argc() < 2 )
	{
		ConsolePrint("usage: showvguimenu <menuType>\n");
		return;
	}

	int menuType = atoi(gEngfuncs.Cmd_Argv(1));
	ShowVGUIMenu(menuType);
}


// ---------------------------------------------------------------------------------------------
// Menu panel (Wii)
//
// The stock client draws the team / class / buy menus with VGUI, or hands them to the touch
// UI of the main menu. A statically linked client on the Wii has neither, so those menus never
// showed up. This draws the same menus with plain rectangles and text (no textures, so it costs
// no RAM) and lets the controller choose: the engine turns the D-pad, A and B into the
// menu_prev / menu_next / menu_ok / menu_cancel commands while _wii_menu_open is 1.
// ---------------------------------------------------------------------------------------------

static const char *const g_szPanelClassT[] = { "PHOENIX CONNEXION", "L337 KREW", "ARCTIC AVENGERS", "GUERILLA WARFARE" };
static const char *const g_szPanelClassCT[] = { "SEAL TEAM 6", "GSG-9", "SAS", "GIGN" };

static void PanelText( int x, int y, const char *str, int r, int g, int b )
{
	DrawUtils::SetConsoleTextColor( r / 255.0f, g / 255.0f, b / 255.0f );
	DrawUtils::DrawConsoleString( x, y, str );
}

// copy as much of the string as fits in maxw pixels; returns its width
static int PanelFit( const char *in, char *out, int outsize, int maxw )
{
	strlcpy( out, in, outsize );

	int len = (int)strlen( out );
	while( len > 1 && DrawUtils::ConsoleStringLen( out ) > maxw )
		out[--len] = 0;

	return DrawUtils::ConsoleStringLen( out );
}

static void PanelFrame( int x, int y, int w, int h, int r, int g, int b )
{
	FillRGBA( x, y, w, 1, r, g, b, 255 );
	FillRGBA( x, y + h - 1, w, 1, r, g, b, 255 );
	FillRGBA( x, y, 1, h, r, g, b, 255 );
	FillRGBA( x + w - 1, y, 1, h, r, g, b, 255 );
}

// greedy word wrap into a box maxH pixels tall; returns the height the text takes. With measure
// nothing is drawn and maxH does not cut: it is the height of all of it
static int PanelWrap( const char *text, int x, int y, int w, int lh, int maxH, int r, int g, int b, bool measure = false )
{
	char para[256], cur[256], test[256], word[96];
	const int y0 = y;

	// false when the next line would not fit
	auto put = [&]( const char *str ) -> bool
	{
		char fit[256];

		if( measure )
		{
			y += lh;
			return true;
		}

		if( y - y0 + lh > maxH )
			return false;

		// a word longer than the box (an address, say) is cut, not drawn over the frame
		PanelFit( str, fit, sizeof( fit ), w );
		PanelText( x, y, fit, r, g, b );
		y += lh;
		return true;
	};

	while( *text )
	{
		const char *eol = strchr( text, '\n' );
		int plen = eol ? (int)( eol - text ) : (int)strlen( text );
		if( plen > (int)sizeof( para ) - 1 )
			plen = sizeof( para ) - 1;

		memcpy( para, text, plen );
		para[plen] = 0;
		text = eol ? eol + 1 : text + strlen( text );

		if( !para[0] )
		{
			y += lh / 2;
			continue;
		}

		cur[0] = 0;
		for( char *tok = para; tok && *tok; )
		{
			char *sp = strchr( tok, ' ' );
			int wl = sp ? (int)( sp - tok ) : (int)strlen( tok );
			if( wl > (int)sizeof( word ) - 1 )
				wl = sizeof( word ) - 1;

			memcpy( word, tok, wl );
			word[wl] = 0;
			tok = sp ? sp + 1 : NULL;

			if( !word[0] )
				continue;

			if( cur[0] )
				snprintf( test, sizeof( test ), "%s %s", cur, word );
			else
				strlcpy( test, word, sizeof( test ));

			if( cur[0] && DrawUtils::ConsoleStringLen( test ) > w )
			{
				if( !put( cur ))
					return y - y0;

				strlcpy( cur, word, sizeof( cur ));
			}
			else strlcpy( cur, test, sizeof( cur ));
		}

		if( cur[0] && !put( cur ))
			return y - y0;
	}

	return y - y0;
}

// the stock text menus the server asks for by name (titles.txt key)
int CHudMenu::PanelKnownMenu( const char *key )
{
	if( !key )
		return 0;

	if( !strncmp( key, "#Team_Select", 12 ) || !strncmp( key, "#IG_Team_Select", 15 ) || !strncmp( key, "#IG_VIP_Team_Select", 19 ))
		return MENU_TEAM;

	if( !strncmp( key, "#Terrorist_Select", 17 ))
		return MENU_CLASS_T;

	if( !strncmp( key, "#CT_Select", 10 ))
		return MENU_CLASS_CT;

	return 0;
}

// 8 pixel letters are not legible on a TV: the panel asks the engine for double size glyphs
void CHudMenu::SetTextMul( int mul )
{
	if( m_pTextMul )
		m_pTextMul->value = mul > 1 ? (float)mul : 0.0f;
}

void CHudMenu::SetPanelOpen( bool open )
{
	if( !m_bPanel || !m_pMenuOpen )
		return;

	if(( m_pMenuOpen->value != 0.0f ) != open )
		gEngfuncs.Cvar_SetValue( "_wii_menu_open", open ? 1.0f : 0.0f );
}

// builds the team / class menus from their table, with the keys the server allows
bool CHudMenu::ShowPanelFor( int menuType )
{
	char text[MAX_MENU_STRING];
	char line[96];
	bool any = false;

	text[0] = 0;

	switch( menuType )
	{
	case MENU_TEAM:
		strlcpy( text, "SELECT TEAM\n\n", sizeof( text ));
		if( SlotValid( 1 )) { strlcat( text, "1. TERRORIST FORCES\n", sizeof( text )); any = true; }
		if( SlotValid( 2 )) { strlcat( text, "2. CT FORCES\n", sizeof( text )); any = true; }
		if( SlotValid( 3 )) { strlcat( text, "3. VIP\n", sizeof( text )); any = true; }
		if( SlotValid( 5 )) { strlcat( text, "5. AUTO ASSIGN\n", sizeof( text )); any = true; }
		if( SlotValid( 6 )) { strlcat( text, "6. SPECTATE\n", sizeof( text )); any = true; }
		if( SlotValid( 10 )) { strlcat( text, "0. CANCEL\n", sizeof( text )); any = true; }
		break;

	case MENU_CLASS_T:
	case MENU_CLASS_CT:
	{
		const bool t = menuType == MENU_CLASS_T;
		const char *const *names = t ? g_szPanelClassT : g_szPanelClassCT;
		const bool six = SlotValid( 6 ); // Condition Zero has a fifth class and auto select moves to 6

		strlcpy( text, "SELECT CLASS\n\n", sizeof( text ));

		for( int i = 1; i <= 4; i++ )
		{
			if( !SlotValid( i ))
				continue;

			snprintf( line, sizeof( line ), "%d. %s\n", i, names[i - 1] );
			strlcat( text, line, sizeof( text ));
			any = true;
		}

		if( SlotValid( 5 ))
		{
			snprintf( line, sizeof( line ), "5. %s\n", six ? ( t ? "MILITIA" : "SPETSNAZ" ) : "AUTO SELECT" );
			strlcat( text, line, sizeof( text ));
			any = true;
		}

		if( six ) { strlcat( text, "6. AUTO SELECT\n", sizeof( text )); any = true; }
		if( SlotValid( 10 )) { strlcat( text, "0. CANCEL\n", sizeof( text )); any = true; }
		break;
	}

	default:
		return false;
	}

	if( !any )
		return false;

	OpenPanel( text, menuType == MENU_TEAM );
	m_iPanelKind = menuType; // after OpenPanel, which clears it for the text menus
	return true;
}

void CHudMenu::OpenPanel( const char *text, bool teamMenu )
{
	BuildPanel( text );

	m_iPanelKind = 0;
	m_bTeamMenu = teamMenu;
	if( teamMenu )
		LoadMapInfo();
	else
		m_szMapInfo[0] = 0;

	m_fMenuDisplayed = 1;
	m_iFlags |= HUD_DRAW;

	// with nothing to choose the controller keeps playing
	SetPanelOpen( m_iSelLine >= 0 );
}

// text with one option per line ("1. Option", optionally preceded by "->") into panel lines
void CHudMenu::BuildPanel( const char *text )
{
	char buf[MAX_MENU_STRING];
	int o = 0;

	m_nLines = 0;
	m_iTitleLine = -1;
	m_iSelLine = -1;

	// AMX Mod X colour codes (\y \w \r \d \R) are not text
	for( int i = 0; text[i] && o < (int)sizeof( buf ) - 1; i++ )
	{
		if( text[i] == '\\' && text[i + 1] && strchr( "ywrdR", text[i + 1] ))
		{
			i++;
			continue;
		}

		buf[o++] = text[i];
	}
	buf[o] = 0;

	char *next;
	for( char *line = buf; line && m_nLines < MENU_PANEL_LINES; line = next )
	{
		next = strchr( line, '\n' );
		if( next )
			*next++ = 0;

		while( *line == ' ' || *line == '\t' || *line == '\r' )
			line++;

		if( line[0] == '-' && line[1] == '>' )
			line += 2;

		while( *line == ' ' )
			line++;

		int len = (int)strlen( line );
		while( len > 0 && ( line[len - 1] == ' ' || line[len - 1] == '\r' ))
			line[--len] = 0;

		panelline_t &pl = m_Lines[m_nLines];
		pl.slot = 0;
		pl.text[0] = 0;

		if( !*line )
		{
			// a blank line is a gap: never at the top, never two in a row
			if( m_nLines > 0 && ( m_Lines[m_nLines - 1].slot || m_Lines[m_nLines - 1].text[0] ))
				m_nLines++;
			continue;
		}

		const char *label = line;
		if( line[0] >= '0' && line[0] <= '9' && ( line[1] == '.' || line[1] == ')' ))
		{
			pl.slot = line[0] == '0' ? 10 : line[0] - '0';
			label = line + 2;
			while( *label == ' ' )
				label++;
		}

		strlcpy( pl.text, label, sizeof( pl.text ));
		m_nLines++;
	}

	while( m_nLines > 0 && !m_Lines[m_nLines - 1].slot && !m_Lines[m_nLines - 1].text[0] )
		m_nLines--;

	// the first plain line above the options is the title; start on the first choosable option
	for( int i = 0; i < m_nLines; i++ )
	{
		if( m_Lines[i].slot )
		{
			if( m_iSelLine < 0 && SlotValid( m_Lines[i].slot ))
				m_iSelLine = i;
		}
		else if( m_iTitleLine < 0 && m_iSelLine < 0 && m_Lines[i].text[0] )
		{
			bool optionsBelow = false;
			for( int j = i + 1; j < m_nLines; j++ )
				if( m_Lines[j].slot )
					optionsBelow = true;

			if( optionsBelow )
				m_iTitleLine = i;
		}
	}
}

// maps/<map>.txt is the description the stock team menu shows next to the buttons
void CHudMenu::LoadMapInfo( void )
{
	char path[96];
	const char *level = gEngfuncs.pfnGetLevelName();

	m_szMapInfo[0] = 0;

	if( !level || strncmp( level, "maps/", 5 ))
		return;

	strlcpy( path, level, sizeof( path ));

	char *dot = strrchr( path, '.' );
	if( !dot )
		return;

	strlcpy( dot, ".txt", sizeof( path ) - ( dot - path ));

	int len = 0;
	char *file = (char *)gEngfuncs.COM_LoadFile( path, 5, &len );
	if( !file )
		return;

	int o = 0;
	for( int i = 0; i < len && file[i] && o < (int)sizeof( m_szMapInfo ) - 1; i++ )
	{
		if( file[i] != '\r' )
			m_szMapInfo[o++] = file[i];
	}
	m_szMapInfo[o] = 0;

	gEngfuncs.COM_FreeFile( file );
}

void CHudMenu::PanelMove( int dir )
{
	if( !m_bPanel || !m_fMenuDisplayed || m_nLines <= 0 )
		return;

	int i = m_iSelLine;
	for( int n = 0; n < m_nLines; n++ )
	{
		i += dir;
		if( i >= m_nLines )
			i = 0;
		if( i < 0 )
			i = m_nLines - 1;

		if( m_Lines[i].slot && SlotValid( m_Lines[i].slot ))
		{
			m_iSelLine = i;
			return;
		}
	}
}

void CHudMenu::UserCmd_MenuPrev()
{
	PanelMove( -1 );
}

void CHudMenu::UserCmd_MenuNext()
{
	PanelMove( 1 );
}

void CHudMenu::UserCmd_MenuOk()
{
	if( !m_bPanel || !m_fMenuDisplayed || m_iSelLine < 0 )
		return;

	if( SlotValid( m_Lines[m_iSelLine].slot ))
		SelectMenuItem( m_Lines[m_iSelLine].slot );
}

void CHudMenu::UserCmd_MenuCancel()
{
	if( m_bPanel && m_fMenuDisplayed && SlotValid( 10 ))
		SelectMenuItem( 10 );
}

void CHudMenu::DrawPanel( float flTime )
{
	if( !m_bPanel || !m_fMenuDisplayed || m_nLines <= 0 )
		return;

	if( m_flShutoffTime > 0 && m_flShutoffTime <= gHUD.m_flTime )
	{
		UserCmd_OldStyleMenuClose();
		return;
	}

	const int sw = ScreenWidth, sh = ScreenHeight;
	const int mx = sw / 16; // keep clear of the edge of the TV
	int my = sh / 14;
	const bool wantInfo = m_bTeamMenu && m_szMapInfo[0];
	const char *title = m_iTitleLine >= 0 ? m_Lines[m_iTitleLine].text : "MENU";

	// layout: double size letters (legible on a TV) when everything fits. The buy menus have ten
	// lines, which did not fit with the roomy spacing and fell back to the 8 pixel letters that
	// nobody could read: the second try keeps the double size and tightens the spacing and the
	// margin of the screen, the normal size is the last resort
	int mul, lh, lhS, pad, rowH, numW, colW, pw, ph, gap, blank;
	bool info;

	for( int variant = 0;; variant++ )
	{
		int dummy;
		const bool compact = variant == 1;

		mul = variant < 2 ? 2 : 1;
		my = compact ? sh / 30 : sh / 14;

		SetTextMul( 1 );
		DrawUtils::ConsoleStringSize( "Wg", &dummy, &lhS );
		if( lhS < 8 )
			lhS = 12;

		SetTextMul( mul );
		DrawUtils::ConsoleStringSize( "Wg", &dummy, &lh );
		if( lh < lhS )
			lh = lhS;

		pad = compact ? lh / 4 + 2 : lh / 2 + 3;
		rowH = compact ? lh + 6 : lh + lh / 2 + 2;
		gap = compact ? 1 : 3;
		blank = compact ? lh / 4 : lh / 2;
		numW = DrawUtils::ConsoleStringLen( "00" );

		// width of the option column
		int labelW = 0;
		for( int i = 0; i < m_nLines; i++ )
		{
			if( m_Lines[i].slot )
				labelW = max( labelW, DrawUtils::ConsoleStringLen( m_Lines[i].text ));
		}

		colW = max( labelW + numW + pad, DrawUtils::ConsoleStringLen( title )) + pad;
		colW = max( colW, lh * 8 );

		pw = sw - mx * 2;
		info = wantInfo;
		if( info )
		{
			colW = min( colW, pw * 56 / 100 );
			if( pw - colW - pad * 3 < lhS * 10 )
				info = false;
		}

		if( !info )
		{
			pw = min( sw - mx * 2, max( colW + pad * 2, sw * 40 / 100 ));
			colW = pw - pad * 2;
		}

		// height: title, rows, hint
		int bodyH = rowH + 8;
		for( int i = 0; i < m_nLines; i++ )
		{
			if( i == m_iTitleLine )
				continue;

			if( m_Lines[i].slot )
				bodyH += rowH + gap;
			else if( m_Lines[i].text[0] )
				bodyH += lh + 2;
			else
				bodyH += blank;
		}

		const int phRows = pad * 2 + bodyH + lh + pad;

		ph = phRows;
		if( info ) // room for the description: ~16 lines of normal size text (the screen clamps it)
			ph = max( ph, ( pad + rowH + 8 ) + ( lhS * 16 + pad ) + pad + lh + 4 );

		// big text whenever the options fit; the description is the one that gives way
		if( variant >= 2 || phRows <= sh - my * 2 )
			break;
	}

	ph = min( ph, sh - my * 2 );

	const int px = ( sw - pw ) / 2;
	const int py = ( sh - ph ) / 2;

	FillRGBABlend( px, py, pw, ph, 0, 0, 0, 185 );
	PanelFrame( px, py, pw, ph, 120, 120, 120 );

	// title (the text size of the layout is still selected)
	PanelText( px + pad, py + pad, title, 255, 176, 0 );
	FillRGBA( px + pad, py + pad + rowH + 1, pw - pad * 2, 1, 150, 150, 150, 255 );

	const int top = py + pad + rowH + 8;
	int y = top;

	for( int i = 0; i < m_nLines; i++ )
	{
		const panelline_t &pl = m_Lines[i];
		char fit[96];

		if( i == m_iTitleLine )
			continue;

		if( pl.slot )
		{
			const bool valid = SlotValid( pl.slot );
			const bool sel = i == m_iSelLine;
			const int x0 = px + pad;
			char num[4];

			if( sel )
				FillRGBABlend( x0, y, colW, rowH, 255, 140, 0, 150 );
			else
				FillRGBABlend( x0, y, colW, rowH, 0, 0, 0, 120 );

			if( sel )
				PanelFrame( x0, y, colW, rowH, 255, 200, 60 );
			else if( valid )
				PanelFrame( x0, y, colW, rowH, 170, 105, 0 );
			else
				PanelFrame( x0, y, colW, rowH, 70, 70, 70 );

			const int r = sel ? 255 : valid ? 255 : 120;
			const int g = sel ? 255 : valid ? 190 : 120;
			const int b = sel ? 255 : valid ? 40 : 120;

			snprintf( num, sizeof( num ), "%d", pl.slot == 10 ? 0 : pl.slot );
			PanelFit( pl.text, fit, sizeof( fit ), colW - numW - pad * 2 );

			PanelText( x0 + pad / 2 + 2, y + ( rowH - lh ) / 2, num, r, g, b );
			PanelText( x0 + pad / 2 + numW + 4, y + ( rowH - lh ) / 2, fit, r, g, b );

			y += rowH + gap;
		}
		else if( pl.text[0] )
		{
			PanelFit( pl.text, fit, sizeof( fit ), pw - pad * 2 );
			PanelText( px + pad, y, fit, 225, 225, 225 );
			y += lh + 2;
		}
		else y += blank;
	}

	// what the buttons do: big if it fits
	const char *hint = SlotValid( 10 ) ? "Cruz: elegir   A: aceptar   B: cancelar" : "Cruz: elegir   A: aceptar";
	char fit[96];

	if( DrawUtils::ConsoleStringLen( hint ) > pw - pad * 2 )
	{
		SetTextMul( 1 );
		lh = lhS;
	}

	const int hw = PanelFit( hint, fit, sizeof( fit ), pw - pad * 2 );
	const int hintY = py + ph - pad - lh + 2;
	PanelText( px + ( pw - hw ) / 2, hintY, fit, 210, 210, 210 );

	// map description next to the buttons: normal size, lighter colour
	if( info )
	{
		const int ix = px + pad + colW + pad;
		const int iw = pw - ( ix - px ) - pad;
		const int ih = hintY - 4 - top;

		FillRGBABlend( ix, top, iw, ih, 0, 0, 0, 130 );
		PanelFrame( ix, top, iw, ih, 170, 105, 0 );

		// the description in double size letters too when all of it fits, else normal size
		const int tw = iw - pad - 4, avail = ih - pad;
		int dmul = 2, dlh;

		SetTextMul( 2 );
		DrawUtils::ConsoleStringSize( "Wg", &numW, &dlh );
		if( dlh < lhS )
			dlh = lhS;

		if( PanelWrap( m_szMapInfo, 0, 0, tw, dlh, avail, 0, 0, 0, true ) > avail )
		{
			dmul = 1;
			dlh = lhS;
		}

		SetTextMul( dmul );
		PanelWrap( m_szMapInfo, ix + pad / 2 + 2, top + pad / 2, tw, dlh, avail, 255, 222, 140 );
	}

	SetTextMul( 1 );
}
