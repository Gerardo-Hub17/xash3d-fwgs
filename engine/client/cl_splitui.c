/*
cl_splitui.c - split screen: HUD of the extra players, per player menu and lobby
Copyright (C) 2026

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "client.h"
#include "server.h"
#include "platform/platform.h"

/*
Everything here is drawn by the engine with the console font and flat
rectangles. Player 1 keeps the game's own HUD (it lives in the view of player 1);
the other players have no client dll behind them, so they get a small HUD of
their own. The menu and the lobby read the controllers themselves through
OGC_PortUI, because the normal key path only knows about player 1's controller.
*/

#define UI_EDGE_DELAY	0.4	// seconds before a held direction repeats
#define UI_EDGE_RATE	0.1

/*
=============
drawing helpers
=============
*/
void CL_SplitFill( int x, int y, int w, int h, int r, int g, int b, int a )
{
	if( w <= 0 || h <= 0 )
		return;

	ref.dllFuncs.FillRGBA( a >= 255 ? kRenderNormal : kRenderTransTexture, x, y, w, h, r, g, b, a );
}

void CL_SplitDrawText( int x, int y, const char *text, int r, int g, int b, int a )
{
	rgba_t color = { r, g, b, a };

	Con_DrawString( x, y, text, color );
}

int CL_SplitTextWidth( const char *text )
{
	int w = 0, h = 0;

	Con_DrawStringLen( text, &w, &h );

	return w;
}

int CL_SplitTextHeight( void )
{
	int w = 0, h = 0;

	Con_DrawStringLen( "A", &w, &h );

	return h > 0 ? h : 16;
}

static void UI_TextRight( int x, int y, const char *text, int r, int g, int b )
{
	CL_SplitDrawText( x - CL_SplitTextWidth( text ), y, text, r, g, b, 255 );
}

static void UI_TextCenter( int x, int y, const char *text, int r, int g, int b )
{
	CL_SplitDrawText( x - CL_SplitTextWidth( text ) / 2, y, text, r, g, b, 255 );
}

// a bar with a dark back and a 1 pixel frame, value 0..1
static void UI_Bar( int x, int y, int w, int h, float value, int r, int g, int b )
{
	const int fill = (int)( w * bound( 0.0f, value, 1.0f ));

	CL_SplitFill( x - 1, y - 1, w + 2, h + 2, 0, 0, 0, 200 );
	CL_SplitFill( x, y, w, h, 40, 40, 40, 255 );
	CL_SplitFill( x, y, fill, h, r, g, b, 255 );
}

/*
=============
tuning of every player (the menu)
=============
*/
enum
{
	ROW_IR_DZ,	// pointer dead zone
	ROW_STICK_DZ,	// stick dead zone
	ROW_STICK_SENS,
	ROW_IR_SENS,
	ROW_AUTOAIM,
	ROW_CLOSE,
	ROW_COUNT
};

static const struct
{
	const char	*label;
	int		min, max, step;
} ui_rows[ROW_COUNT] =
{
	{ "Zona muerta puntero",	0,  60,  5 },
	{ "Zona muerta palanca",	0,  40,  2 },
	{ "Sensibilidad palanca",	50, 200, 10 },
	{ "Sensibilidad puntero",	50, 200, 10 },
	{ "Autoapuntado",		0,  100, 10 },
	{ "Cerrar",			0,  0,   0 },
};

// players 2-4 keep their settings in a saved cvar: "ir_dz stick_dz stick_sens ir_sens autoaim" in percent
static CVAR_DEFINE_AUTO( wii_split_cfg1, "35 16 100 100 0", FCVAR_ARCHIVE, "split screen player 2 settings (internal)" );
static CVAR_DEFINE_AUTO( wii_split_cfg2, "35 16 100 100 0", FCVAR_ARCHIVE, "split screen player 3 settings (internal)" );
static CVAR_DEFINE_AUTO( wii_split_cfg3, "35 16 100 100 0", FCVAR_ARCHIVE, "split screen player 4 settings (internal)" );

static convar_t *const ui_cfgvars[] = { NULL, &wii_split_cfg1, &wii_split_cfg2, &wii_split_cfg3 };

static int ui_cfg[SPLIT_MAX_PLAYERS][ROW_COUNT]; // percent, for players 2-4

static void UI_LoadCfg( int player )
{
	int v[5] = { 35, 16, 100, 100, 0 };
	int i;

	if( player > 0 && player < SPLIT_MAX_PLAYERS )
		sscanf( ui_cfgvars[player]->string, "%d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4] );

	for( i = 0; i < 5; i++ )
		ui_cfg[player][i] = bound( ui_rows[i].min, v[i], ui_rows[i].max );
}

static void UI_SaveCfg( int player )
{
	char s[64];

	if( player < 1 || player >= SPLIT_MAX_PLAYERS )
		return;

	Q_snprintf( s, sizeof( s ), "%d %d %d %d %d", ui_cfg[player][0], ui_cfg[player][1],
		ui_cfg[player][2], ui_cfg[player][3], ui_cfg[player][4] );
	Cvar_DirectSet( ui_cfgvars[player], s );
}

// player 1 edits the normal cvars
static int UI_GetRow( int player, int row )
{
	if( player == 0 )
	{
		switch( row )
		{
		case ROW_IR_DZ:		return (int)( Cvar_VariableValue( "wii_ir_deadzone" ) * 100.0f + 0.5f );
		case ROW_STICK_DZ:	return (int)( Cvar_VariableValue( "wii_stick_deadzone" ) * 100.0f + 0.5f );
		case ROW_STICK_SENS:	return (int)( Cvar_VariableValue( "wii_stick_sens" ) * 100.0f + 0.5f );
		case ROW_IR_SENS:	return (int)( Cvar_VariableValue( "wii_ir_sens" ) * 100.0f + 0.5f );
		case ROW_AUTOAIM:	return (int)( Cvar_VariableValue( "wii_aimassist_strength" ) * 100.0f + 0.5f );
		}

		return 0;
	}

	return ui_cfg[player][row];
}

static void UI_SetRow( int player, int row, int value )
{
	value = bound( ui_rows[row].min, value, ui_rows[row].max );

	if( player == 0 )
	{
		const float f = value / 100.0f;

		switch( row )
		{
		case ROW_IR_DZ:		Cvar_SetValue( "wii_ir_deadzone", f ); break;
		case ROW_STICK_DZ:	Cvar_SetValue( "wii_stick_deadzone", f ); break;
		case ROW_STICK_SENS:	Cvar_SetValue( "wii_stick_sens", f ); break;
		case ROW_IR_SENS:	Cvar_SetValue( "wii_ir_sens", f ); break;
		case ROW_AUTOAIM:
			Cvar_SetValue( "wii_aimassist_strength", f );
			SV_SplitSetAutoaim( 0, (float)value );
			break;
		}

		return;
	}

	ui_cfg[player][row] = value;
	UI_SaveCfg( player );

	if( row == ROW_AUTOAIM )
		SV_SplitSetAutoaim( player, (float)value );
}

void CL_SplitUIConfig( int player, float *ir_dz, float *stick_dz, float *stick_sens, float *ir_sens, float *autoaim )
{
	if( player < 0 || player >= SPLIT_MAX_PLAYERS )
		player = 1;

	if( ir_dz ) *ir_dz = UI_GetRow( player, ROW_IR_DZ ) / 100.0f;
	if( stick_dz ) *stick_dz = UI_GetRow( player, ROW_STICK_DZ ) / 100.0f;
	if( stick_sens ) *stick_sens = UI_GetRow( player, ROW_STICK_SENS ) / 100.0f;
	if( ir_sens ) *ir_sens = UI_GetRow( player, ROW_IR_SENS ) / 100.0f;
	if( autoaim ) *autoaim = (float)UI_GetRow( player, ROW_AUTOAIM );
}

void CL_SplitUIPlayerStarted( int player )
{
	UI_LoadCfg( player );
	SV_SplitSetAutoaim( player, (float)ui_cfg[player][ROW_AUTOAIM] );
}

typedef struct
{
	qboolean	open;
	int		sel;
	unsigned	prev;		// buttons held last frame
	double		repeat;		// when a held direction fires again
} ui_menu_t;

static ui_menu_t ui_menu[SPLIT_MAX_PLAYERS];

qboolean CL_SplitMenuOpen( int player )
{
	return player >= 0 && player < SPLIT_MAX_PLAYERS && ui_menu[player].open;
}

void CL_SplitMenuToggle( int player )
{
	if( player < 0 || player >= SPLIT_MAX_PLAYERS || !CL_SplitActive( ))
		return;

	ui_menu[player].open = !ui_menu[player].open;
	ui_menu[player].sel = 0;
}

static void UI_MenuAdjust( int player, int dir )
{
	const int row = ui_menu[player].sel;

	if( row == ROW_CLOSE )
		return;

	UI_SetRow( player, row, UI_GetRow( player, row ) + dir * ui_rows[row].step );
}

/*
=============
CL_SplitUIInput

the menu buttons of a player; true while its menu is (or has just been) open, so
the game ignores that player's controller meanwhile
=============
*/
qboolean CL_SplitUIInput( int player, unsigned ui )
{
	ui_menu_t *m;
	unsigned pressed, repeat = 0;

	if( player < 0 || player >= SPLIT_MAX_PLAYERS )
		return false;

	m = &ui_menu[player];
	pressed = ui & ~m->prev;
	m->prev = ui;

	if( !CL_SplitActive( ))
	{
		m->open = false;
		return false;
	}

	// players 2-4 open and close it with + / Start; player 1 uses the chat button
	if( player > 0 && ( pressed & OGC_UI_START ))
	{
		m->open = !m->open;
		m->sel = 0;
		return true;
	}

	if( !m->open )
		return false;

	// held left / right keep adjusting
	if( ui & ( OGC_UI_LEFT | OGC_UI_RIGHT ))
	{
		if( pressed & ( OGC_UI_LEFT | OGC_UI_RIGHT ))
			m->repeat = host.realtime + UI_EDGE_DELAY;
		else if( host.realtime >= m->repeat )
		{
			repeat = ui & ( OGC_UI_LEFT | OGC_UI_RIGHT );
			m->repeat = host.realtime + UI_EDGE_RATE;
		}
	}

	if(( pressed | repeat ) & OGC_UI_UP )
		m->sel = ( m->sel + ROW_COUNT - 1 ) % ROW_COUNT;
	if( pressed & OGC_UI_DOWN )
		m->sel = ( m->sel + 1 ) % ROW_COUNT;
	if(( pressed | repeat ) & OGC_UI_LEFT )
		UI_MenuAdjust( player, -1 );
	if(( pressed | repeat ) & OGC_UI_RIGHT )
		UI_MenuAdjust( player, 1 );

	if(( pressed & OGC_UI_ACCEPT ) && m->sel == ROW_CLOSE )
		m->open = false;
	if( pressed & OGC_UI_BACK )
		m->open = false;

	return true;
}

static void UI_DrawMenu( int player, int x, int y, int w, int h )
{
	const float k = CL_SplitPixelAspectValue();
	const int th = CL_SplitTextHeight();
	const int rowh = th + 6;
	int pw = Q_min( w - 8, (int)( 300.0f / k ));
	int ph = rowh * ( ROW_COUNT + 2 );
	int px, py, i;
	char text[64];

	if( ph > h - 4 )
		ph = h - 4;

	px = x + ( w - pw ) / 2;
	py = y + ( h - ph ) / 2;

	CL_SplitFill( px, py, pw, ph, 0, 0, 0, 215 );
	CL_SplitFill( px, py, pw, 2, 255, 160, 0, 255 );

	Q_snprintf( text, sizeof( text ), "AJUSTES  JUGADOR %d", player + 1 );
	UI_TextCenter( px + pw / 2, py + 6, text, 255, 200, 40 );

	for( i = 0; i < ROW_COUNT; i++ )
	{
		const int ry = py + rowh * ( i + 1 ) + 8;
		const qboolean sel = ui_menu[player].sel == i;
		const int r = sel ? 255 : 200, g = sel ? 220 : 200, b = sel ? 60 : 200;

		if( ry + th > py + ph )
			break;

		if( sel )
			CL_SplitFill( px + 3, ry - 2, pw - 6, th + 4, 255, 160, 0, 70 );

		CL_SplitDrawText( px + 8, ry, ui_rows[i].label, r, g, b, 255 );

		if( i != ROW_CLOSE )
		{
			Q_snprintf( text, sizeof( text ), sel ? "< %d%% >" : "%d%%", UI_GetRow( player, i ));
			UI_TextRight( px + pw - 8, ry, text, r, g, b );
		}
	}
}

/*
=============
HUD of the extra players
=============
*/
static const char *UI_WeaponName( int id )
{
	switch( id )
	{
	case 1:  return "Palanca";
	case 2:  return "9mm";
	case 3:  return ".357";
	case 4:  return "MP5";
	case 5:  return "Chaingun";
	case 6:  return "Ballesta";
	case 7:  return "Escopeta";
	case 8:  return "RPG";
	case 9:  return "Gauss";
	case 10: return "Egon";
	case 11: return "Hornet";
	case 12: return "Granada";
	case 13: return "Mina";
	case 14: return "Satchel";
	case 15: return "Snark";
	}

	return "";
}

static void UI_DrawHUD( int player, int x, int y, int w, int h )
{
	const float k = CL_SplitPixelAspectValue();
	const int th = CL_SplitTextHeight();
	const int mx = Q_max( 6, (int)( 10.0f / k )), my = 6;
	float health, armor, frags;
	int weapon, clip, res1, res2;
	char text[64];
	int bw, bh, by;

	if( !SV_SplitGetView( player, NULL, NULL, NULL, &health, &armor, &frags ))
		return;

	// the view of a player with a small pane gets smaller bars
	bw = Q_min( (int)( 90.0f / k ), w / 4 );
	bh = Q_max( 5, th * 2 / 3 );

	// crosshair: outlined so it shows on any background; the arms are scaled so
	// they look the same on a stretched 16:9 screen
	if( health > 0.0f )
	{
		const int cx = x + w / 2, cy = y + h / 2;
		const int len = Q_max( 4, h / 40 ), gap = 3;
		const int lx = Q_max( 1, (int)( len / k ));
		const int gx = Q_max( 1, (int)( gap / k ));

		CL_SplitFill( cx - gx - lx - 1, cy - 2, lx + 2, 4, 0, 0, 0, 160 );
		CL_SplitFill( cx + gx - 1, cy - 2, lx + 2, 4, 0, 0, 0, 160 );
		CL_SplitFill( cx - 2, cy - gap - len - 1, 4, len + 2, 0, 0, 0, 160 );
		CL_SplitFill( cx - 2, cy + gap - 1, 4, len + 2, 0, 0, 0, 160 );

		CL_SplitFill( cx - gx - lx, cy - 1, lx, 2, 255, 255, 255, 255 );
		CL_SplitFill( cx + gx, cy - 1, lx, 2, 255, 255, 255, 255 );
		CL_SplitFill( cx - 1, cy - gap - len, 2, len, 255, 255, 255, 255 );
		CL_SplitFill( cx - 1, cy + gap, 2, len, 255, 255, 255, 255 );
	}
	else
	{
		UI_TextCenter( x + w / 2, y + h / 2 - th, "ELIMINADO", 255, 60, 60 );
		UI_TextCenter( x + w / 2, y + h / 2 + 2, "Dispara para reaparecer", 220, 220, 220 );
	}

	// health and armour, bottom left
	by = y + h - my - th;
	CL_SplitFill( x + mx - 4, by - th - 8, mx + (int)( 54.0f / k ) + bw + 8, th * 2 + 14, 0, 0, 0, 120 );

	Q_snprintf( text, sizeof( text ), "AR %d", (int)armor );
	CL_SplitDrawText( x + mx, by, text, 90, 190, 255, 255 );
	UI_Bar( x + mx + (int)( 54.0f / k ), by + ( th - bh ) / 2, bw, bh, armor / 100.0f, 70, 150, 255 );

	Q_snprintf( text, sizeof( text ), "HP %d", (int)Q_max( health, 0.0f ));
	by -= th + 4;
	{
		const float f = health / 100.0f;
		const int r = f > 0.5f ? 90 : 255, g = f > 0.25f ? 220 : 60;

		CL_SplitDrawText( x + mx, by, text, r, g, 70, 255 );
		UI_Bar( x + mx + (int)( 54.0f / k ), by + ( th - bh ) / 2, bw, bh, f, r, g, 70 );
	}

	// frags, top right
	Q_snprintf( text, sizeof( text ), "FR %d", (int)frags );
	UI_TextRight( x + w - mx, y + my, text, 255, 220, 100 );

	// ammo, bottom right
	if( SV_SplitGetAmmo( player, &weapon, &clip, &res1, &res2 ) && health > 0.0f )
	{
		const int ay = y + h - my - th;

		if( clip >= 0 && res1 >= 0 )
			Q_snprintf( text, sizeof( text ), "%d / %d", clip, res1 );
		else if( clip >= 0 )
			Q_snprintf( text, sizeof( text ), "%d", clip );
		else if( res1 >= 0 )
			Q_snprintf( text, sizeof( text ), "%d", res1 );
		else text[0] = 0;

		if( res2 > 0 )
		{
			char extra[16];

			Q_snprintf( extra, sizeof( extra ), "  +%d", res2 );
			Q_strncat( text, extra, sizeof( text ));
		}

		if( text[0] )
		{
			CL_SplitFill( x + w - mx - CL_SplitTextWidth( text ) - 4, ay - th - 6, CL_SplitTextWidth( text ) + 8, th * 2 + 10, 0, 0, 0, 120 );
			UI_TextRight( x + w - mx, ay, text, 255, 190, 60 );
			UI_TextRight( x + w - mx, ay - th - 2, UI_WeaponName( weapon ), 200, 200, 200 );
		}
		else UI_TextRight( x + w - mx, ay, UI_WeaponName( weapon ), 200, 200, 200 );
	}
}

void CL_SplitUIDrawHUD( int player, int x, int y, int w, int h )
{
	if( player > 0 )
		UI_DrawHUD( player, x, y, w, h );

	if( ui_menu[player].open )
		UI_DrawMenu( player, x, y, w, h );
}

qboolean CL_SplitGrab( int player )
{
	return CL_SplitLobbyActive() || ( CL_SplitActive() && CL_SplitMenuOpen( player ));
}

/*
=============
Lobby

Before the game starts the players take their places: each one presses + / Start
on its controller (the LEDs of the remotes show the same order), picks a
character and a map, and confirms with A. When everyone is ready a 5 second
countdown starts that anyone can cancel with B.
=============
*/
#define LOBBY_MAX_LIST	64
#define LOBBY_COUNTDOWN	5.0

typedef struct
{
	qboolean	joined;
	qboolean	ready;
	int		kind;
	int		model;
	int		map;
	unsigned	prev;
	double		repeat;
	int		deny;		// frames left of the "can't use this controller" message
} lobby_player_t;

static struct
{
	qboolean	active;
	lobby_player_t	p[SPLIT_MAX_PLAYERS];
	int		layout;		// 1 = side by side, 2 = top and bottom (two players)
	qboolean	counting;
	double		end;
	char		models[LOBBY_MAX_LIST][32];
	int		nmodels;
	char		maps[LOBBY_MAX_LIST][48];
	int		nmaps;
} lobby;

qboolean CL_SplitLobbyActive( void )
{
	return lobby.active;
}

static int Lobby_Cmp( const void *a, const void *b )
{
	return Q_stricmp((const char *)a, (const char *)b );
}

// models/player/<name>/<name>.mdl
static void Lobby_ListModels( void )
{
	search_t *t = FS_Search( "models/player/*/*.mdl", true, false );
	int i;

	lobby.nmodels = 0;

	if( t )
	{
		for( i = 0; i < t->numfilenames && lobby.nmodels < LOBBY_MAX_LIST; i++ )
		{
			char dir[32], file[32], *slash;
			const char *name = t->filenames[i] + Q_strlen( "models/player/" );

			Q_strncpy( dir, name, sizeof( dir ));
			slash = Q_strchr( dir, '/' );

			if( !slash )
				continue;

			*slash = 0;
			COM_FileBase( slash + 1, file, sizeof( file ));

			if( Q_stricmp( dir, file ))
				continue; // "gordon/gordon.mdl" only, not the extra files next to it

			Q_strncpy( lobby.models[lobby.nmodels++], dir, sizeof( lobby.models[0] ));
		}

		Mem_Free( t );
	}

	if( !lobby.nmodels )
		Q_strncpy( lobby.models[lobby.nmodels++], "gordon", sizeof( lobby.models[0] ));

	qsort( lobby.models, lobby.nmodels, sizeof( lobby.models[0] ), Lobby_Cmp );
}

static qboolean Lobby_IsCampaignMap( const char *name )
{
	// c1a0, c2a1... t0a0 and the hl demo maps are single player
	if(( name[0] == 'c' || name[0] == 't' ) && name[1] >= '0' && name[1] <= '9' && name[2] == 'a' )
		return true;

	return !Q_strnicmp( name, "hldemo", 6 ) || !Q_strnicmp( name, "ba_", 3 ) || !Q_stricmp( name, "background" );
}

static void Lobby_ListMaps( void )
{
	search_t *t = FS_Search( "maps/*.bsp", true, false );
	int i, j;

	lobby.nmaps = 0;

	if( t )
	{
		for( i = 0; i < t->numfilenames && lobby.nmaps < LOBBY_MAX_LIST; i++ )
		{
			char name[48];

			COM_FileBase( t->filenames[i], name, sizeof( name ));

			if( Lobby_IsCampaignMap( name ))
				continue;

			for( j = 0; j < lobby.nmaps; j++ )
				if( !Q_stricmp( lobby.maps[j], name ))
					break;

			if( j == lobby.nmaps )
				Q_strncpy( lobby.maps[lobby.nmaps++], name, sizeof( lobby.maps[0] ));
		}

		Mem_Free( t );
	}

	qsort( lobby.maps, lobby.nmaps, sizeof( lobby.maps[0] ), Lobby_Cmp );
}

static void Lobby_Stop( qboolean to_menu )
{
	lobby.active = false;
	lobby.counting = false;

	if( to_menu )
		UI_SetActiveMenu( true );
}

static void Lobby_Start_f( void )
{
#if XASH_OGC
	int i, def;

	if( lobby.active )
		return;

	memset( &lobby, 0, sizeof( lobby ));
	lobby.layout = 1;

	Lobby_ListModels();
	Lobby_ListMaps();

	if( !lobby.nmaps )
	{
		Con_Printf( "wii_splitlobby: no deathmatch maps found\n" );
		return;
	}

	// start on the map picked last time
	def = 0;
	for( i = 0; i < lobby.nmaps; i++ )
	{
		if( !Q_stricmp( lobby.maps[i], Cvar_VariableString( "wii_split_lastmap" )))
			def = i;
	}

	for( i = 0; i < SPLIT_MAX_PLAYERS; i++ )
		lobby.p[i].map = def;

	for( i = 0; i < lobby.nmodels; i++ )
	{
		if( !Q_stricmp( lobby.models[i], Cvar_VariableString( "model" )))
			lobby.p[0].model = i;
	}

	// the other players start with different characters
	for( i = 1; i < SPLIT_MAX_PLAYERS; i++ )
		lobby.p[i].model = ( lobby.p[0].model + i ) % lobby.nmodels;

	lobby.active = true;

	// the menu steps aside; the lobby draws on top of whatever is behind it
	UI_SetActiveMenu( false );
	Key_SetKeyDest( key_game );
#else
	Con_Printf( "wii_splitlobby: only on the Wii\n" );
#endif
}

// controller port of every player of the game, set by the lobby
static int ui_port[SPLIT_MAX_PLAYERS] = { 0, 1, 2, 3 };

int CL_SplitPortOf( int player )
{
	return ( player >= 0 && player < SPLIT_MAX_PLAYERS ) ? ui_port[player] : player;
}

#if XASH_OGC
static int Lobby_Joined( void )
{
	int i, n = 0;

	for( i = 0; i < SPLIT_MAX_PLAYERS; i++ )
		if( lobby.p[i].joined )
			n++;

	return n;
}

static void Lobby_Launch( void )
{
	int i, n = Lobby_Joined(), nplayers = 0;
	int picks[SPLIT_MAX_PLAYERS], npicks = 0;
	const char *map;
	char cmd[256];

	for( i = 0; i < SPLIT_MAX_PLAYERS; i++ )
	{
		if( !lobby.p[i].joined )
			continue;

		nplayers++;
		picks[npicks++] = lobby.p[i].map;
	}

	// everyone picked the same map: that one; otherwise the game draws one of the picks
	map = lobby.maps[picks[npicks > 1 ? COM_RandomLong( 0, npicks - 1 ) : 0]];

	// the models and controllers: slots go to the players in the order they joined
	{
		int slot = 0;

		for( i = 0; i < SPLIT_MAX_PLAYERS; i++ )
		{
			if( !lobby.p[i].joined )
				continue;

			ui_port[slot] = i;

			if( slot == 0 )
				Cvar_Set( "model", lobby.models[lobby.p[i].model] );
			else Cvar_Set( va( "wii_split_model%d", slot ), lobby.models[lobby.p[i].model] );

			slot++;
		}
	}

	Cvar_Set( "wii_split_lastmap", map );

	// two players choose how the screen is cut; with more it is fixed
	Cvar_SetValue( "wii_split", nplayers == 2 ? (float)lobby.layout : 1.0f );
	Cvar_SetValue( "wii_split_players", (float)nplayers );

	Lobby_Stop( false );

	Q_snprintf( cmd, sizeof( cmd ), "disconnect\nmaxplayers %d\ncoop 0\nmap %s\n", Q_max( nplayers, 2 ), map );
	Cbuf_AddText( cmd );

	(void)n;
}

static void Lobby_Frame( void )
{
	double now = host.realtime;
	int i, joined, ready = 0;

	// the menu or the console took over (Home): leave the lobby
	if( cls.key_dest == key_menu || cls.key_dest == key_console )
	{
		Lobby_Stop( false );
		return;
	}

	for( i = 0; i < SPLIT_MAX_PLAYERS; i++ )
	{
		lobby_player_t *p = &lobby.p[i];
		int kind = 0;
		unsigned ui = OGC_PortUI( i, &kind ), pressed = ui & ~p->prev, repeat = 0;

		p->prev = ui;
		p->kind = kind;

		if( p->deny > 0 )
			p->deny--;

		if( ui & ( OGC_UI_LEFT | OGC_UI_RIGHT | OGC_UI_UP | OGC_UI_DOWN ))
		{
			if( pressed )
				p->repeat = now + UI_EDGE_DELAY;
			else if( now >= p->repeat )
			{
				repeat = ui;
				p->repeat = now + UI_EDGE_RATE;
			}
		}

		if( !p->joined )
		{
			// first confirmation: + / Start takes the place
			if( pressed & OGC_UI_START )
			{
				// a remote with no Nunchuk or Classic can't play from a split screen, except player 1
				// that aims with the pointer
				if( kind == OGC_KIND_NONE || ( kind == OGC_KIND_WIIMOTE && i > 0 ))
					p->deny = 150;
				else p->joined = true;
			}

			continue;
		}

		if( p->ready )
		{
			// B takes the confirmation back (and stops a countdown)
			if( pressed & OGC_UI_BACK )
			{
				p->ready = false;
				lobby.counting = false;
			}

			continue;
		}

		if(( pressed | repeat ) & OGC_UI_LEFT )
			p->model = ( p->model + lobby.nmodels - 1 ) % lobby.nmodels;
		if(( pressed | repeat ) & OGC_UI_RIGHT )
			p->model = ( p->model + 1 ) % lobby.nmodels;
		if(( pressed | repeat ) & OGC_UI_UP )
			p->map = ( p->map + lobby.nmaps - 1 ) % lobby.nmaps;
		if(( pressed | repeat ) & OGC_UI_DOWN )
			p->map = ( p->map + 1 ) % lobby.nmaps;

		// player 1 cuts the screen for two players
		if( i == 0 && ( pressed & OGC_UI_START ))
			lobby.layout = lobby.layout == 1 ? 2 : 1;

		// second confirmation: A says "ready"
		if( pressed & OGC_UI_ACCEPT )
			p->ready = true;

		if( pressed & OGC_UI_BACK )
		{
			if( i == 0 )
			{
				Lobby_Stop( true ); // player 1 backs out of the lobby
				return;
			}

			p->joined = false;
			p->ready = false;
		}
	}

	joined = Lobby_Joined();

	for( i = 0; i < SPLIT_MAX_PLAYERS; i++ )
		if( lobby.p[i].joined && lobby.p[i].ready )
			ready++;

	if( joined < 2 || ready != joined || !lobby.p[0].joined )
	{
		lobby.counting = false;
		return;
	}

	if( !lobby.counting )
	{
		lobby.counting = true;
		lobby.end = now + LOBBY_COUNTDOWN;
	}
	else if( now >= lobby.end )
		Lobby_Launch();
}
#endif

void CL_SplitUIFrame( void )
{
#if XASH_OGC
	if( lobby.active )
		Lobby_Frame();
#endif
}

void CL_SplitLobbyDraw( void )
{
	const int W = refState.width, H = refState.height;
	const float k = CL_SplitPixelAspectValue();
	const int th = CL_SplitTextHeight();
	const int cw = ( W - 20 ) / SPLIT_MAX_PLAYERS;
	char text[96];
	int i, joined = 0, ready = 0;

	if( !lobby.active )
		return;

	CL_SplitFill( 0, 0, W, H, 8, 10, 22, 255 );
	CL_SplitFill( 0, 0, W, 4, 255, 160, 0, 255 );
	UI_TextCenter( W / 2, 14, "PANTALLA DIVIDIDA", 255, 200, 40 );

	for( i = 0; i < SPLIT_MAX_PLAYERS; i++ )
	{
		const lobby_player_t *p = &lobby.p[i];
		const int x = 10 + cw * i + 4, w = cw - 8;
		const int y = 14 + th * 2 + 8;
		const int h = H - y - th * 5 - 12;
		int ty = y + 8, lw = 10, lh;

		if( p->joined ) joined++;
		if( p->joined && p->ready ) ready++;

		CL_SplitFill( x - 2, y - 2, w + 4, h + 4, p->ready ? 60 : 255, p->ready ? 220 : 160, p->ready ? 90 : 0, p->joined ? 255 : 90 );
		CL_SplitFill( x, y, w, h, 20, 24, 44, 255 );

		Q_snprintf( text, sizeof( text ), "JUGADOR %d", i + 1 );
		UI_TextCenter( x + w / 2, ty, text, 255, 255, 255 );
		ty += th + 8;

		// the LED the remote shows
		lh = Q_max( 6, th / 2 );
		{
			int l, total = 4 * ( lw + 4 ) - 4;
			const int lx0 = x + ( w - (int)( total / k )) / 2;

			for( l = 0; l < 4; l++ )
				CL_SplitFill( lx0 + (int)( l * ( lw + 4 ) / k ), ty, Q_max( 3, (int)( lw / k )), lh, l == i ? 255 : 50, l == i ? 220 : 50, l == i ? 60 : 60, 255 );
		}
		ty += lh + 10;

		if( !p->joined )
		{
			UI_TextCenter( x + w / 2, ty, "Pulsa  +  /  Start", 200, 200, 200 );
			UI_TextCenter( x + w / 2, ty + th + 2, "para unirte", 200, 200, 200 );

			if( p->deny > 0 )
			{
				UI_TextCenter( x + w / 2, ty + th * 3, "Conecta un Nunchuk", 255, 90, 90 );
				UI_TextCenter( x + w / 2, ty + th * 4 + 2, "o un Classic / GC", 255, 90, 90 );
			}

			continue;
		}

		{
			const char *kind = p->kind == OGC_KIND_GC ? "GameCube" : p->kind == OGC_KIND_CLASSIC ? "Classic" :
				p->kind == OGC_KIND_NUNCHUK ? "Wiimote + Nunchuk" : "Wiimote";

			UI_TextCenter( x + w / 2, ty, kind, 140, 200, 255 );
			ty += th + 10;
		}

		UI_TextCenter( x + w / 2, ty, "Personaje", 160, 160, 160 );
		ty += th + 2;
		Q_snprintf( text, sizeof( text ), p->ready ? "%s" : "< %s >", lobby.models[p->model] );
		UI_TextCenter( x + w / 2, ty, text, 255, 255, 120 );
		ty += th + 10;

		UI_TextCenter( x + w / 2, ty, "Mapa", 160, 160, 160 );
		ty += th + 2;
		Q_snprintf( text, sizeof( text ), p->ready ? "%s" : "^ %s v", lobby.maps[p->map] );
		UI_TextCenter( x + w / 2, ty, text, 255, 255, 120 );
		ty += th + 12;

		if( p->ready )
			UI_TextCenter( x + w / 2, y + h - th - 8, "LISTO", 90, 255, 120 );
		else UI_TextCenter( x + w / 2, y + h - th - 8, "A: listo", 200, 200, 200 );
	}

	// bottom: instructions and the countdown
	{
		const int by = H - th * 4 - 6;

		if( lobby.counting )
		{
			Q_snprintf( text, sizeof( text ), "Empieza en %d   (B: cancelar)", (int)ceil( lobby.end - host.realtime ));
			UI_TextCenter( W / 2, by, text, 120, 255, 140 );
		}
		else if( joined >= 2 && ready == joined )
			UI_TextCenter( W / 2, by, "Todos listos", 120, 255, 140 );
		else if( joined < 2 )
			UI_TextCenter( W / 2, by, "Hacen falta al menos dos jugadores", 255, 200, 90 );
		else UI_TextCenter( W / 2, by, "Todos deben confirmar con A", 255, 200, 90 );

		if( joined == 2 )
		{
			Q_snprintf( text, sizeof( text ), "Jugador 1: +  cambia la division (%s)", lobby.layout == 1 ? "vertical" : "horizontal" );
			UI_TextCenter( W / 2, by + th + 4, text, 200, 200, 200 );
		}

		UI_TextCenter( W / 2, by + th * 2 + 8, "Izq/Der personaje   Arriba/Abajo mapa   B atras   Home salir", 150, 150, 150 );
	}
}

void CL_SplitUIInit( void )
{
	Cvar_RegisterVariable( &wii_split_cfg1 );
	Cvar_RegisterVariable( &wii_split_cfg2 );
	Cvar_RegisterVariable( &wii_split_cfg3 );
	Cvar_Get( "wii_split_lastmap", "", FCVAR_ARCHIVE, "map picked last time in the split screen lobby" );

	Cmd_AddCommand( "wii_splitlobby", Lobby_Start_f, "open the split screen lobby (controllers, characters, map)" );

	UI_LoadCfg( 0 );
	UI_LoadCfg( 1 );
	UI_LoadCfg( 2 );
	UI_LoadCfg( 3 );
}
