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
	ROW_XH_SIZE,	// crosshair size, percent
	ROW_XH_COLOR,	// crosshair colour, index in ui_xh_colors
	ROW_CLOSE,
	ROW_COUNT
};

#define ROW_CFG_COUNT	ROW_CLOSE	// the rows that are saved

// the colours of the crosshair (0 keeps the one of the weapon)
static const struct
{
	const char	*name;
	byte		r, g, b;
} ui_xh_colors[] =
{
	{ "Normal",	255, 255, 255 },
	{ "Rojo",	255,  40,  40 },
	{ "Verde",	 60, 255,  60 },
	{ "Azul",	 80, 140, 255 },
	{ "Amarillo",	255, 230,  40 },
	{ "Cian",	 40, 240, 255 },
	{ "Magenta",	255,  70, 255 },
	{ "Naranja",	255, 150,   0 },
};

#define UI_XH_COLORS	( sizeof( ui_xh_colors ) / sizeof( ui_xh_colors[0] ))

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
	{ "Tamano de la mira",		50, 300, 10 },
	{ "Color de la mira",		0,  7,   1 },
	{ "Cerrar",			0,  0,   0 },
};

// player 1 keeps the crosshair in cvars
static CVAR_DEFINE_AUTO( wii_xhair_size, "100", FCVAR_ARCHIVE, "crosshair size in percent (split screen menu)" );
static CVAR_DEFINE_AUTO( wii_xhair_color, "0", FCVAR_ARCHIVE, "crosshair colour, 0 = the weapon's own (split screen menu)" );

// players 2-4 keep their settings in a saved cvar: "ir_dz stick_dz stick_sens ir_sens autoaim xh_size xh_color" in percent
static CVAR_DEFINE_AUTO( wii_split_set1, "35 16 100 100 35 100 0", FCVAR_ARCHIVE, "split screen player 2 settings (internal)" );
static CVAR_DEFINE_AUTO( wii_split_set2, "35 16 100 100 35 100 0", FCVAR_ARCHIVE, "split screen player 3 settings (internal)" );
static CVAR_DEFINE_AUTO( wii_split_set3, "35 16 100 100 35 100 0", FCVAR_ARCHIVE, "split screen player 4 settings (internal)" );

static convar_t *const ui_cfgvars[] = { NULL, &wii_split_set1, &wii_split_set2, &wii_split_set3 };

static int ui_cfg[SPLIT_MAX_PLAYERS][ROW_COUNT]; // percent, for players 2-4

static void UI_LoadCfg( int player )
{
	int v[ROW_CFG_COUNT] = { 35, 16, 100, 100, 35, 100, 0 };
	int i;

	if( player > 0 && player < SPLIT_MAX_PLAYERS )
		sscanf( ui_cfgvars[player]->string, "%d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6] );

	for( i = 0; i < ROW_CFG_COUNT; i++ )
		ui_cfg[player][i] = bound( ui_rows[i].min, v[i], ui_rows[i].max );
}

static void UI_SaveCfg( int player )
{
	char s[64];

	if( player < 1 || player >= SPLIT_MAX_PLAYERS )
		return;

	Q_snprintf( s, sizeof( s ), "%d %d %d %d %d %d %d", ui_cfg[player][0], ui_cfg[player][1],
		ui_cfg[player][2], ui_cfg[player][3], ui_cfg[player][4], ui_cfg[player][5], ui_cfg[player][6] );
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
		case ROW_XH_SIZE:	return bound( ui_rows[ROW_XH_SIZE].min, (int)wii_xhair_size.value, ui_rows[ROW_XH_SIZE].max );
		case ROW_XH_COLOR:	return bound( 0, (int)wii_xhair_color.value, (int)UI_XH_COLORS - 1 );
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
		case ROW_XH_SIZE:	Cvar_SetValue( "wii_xhair_size", (float)value ); break;
		case ROW_XH_COLOR:	Cvar_SetValue( "wii_xhair_color", (float)value ); break;
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

/*
=============
CL_SplitCrosshairStyle

size (1.0 = the normal one) and colour of the crosshair of a player; false when
both are the defaults
=============
*/
qboolean CL_SplitCrosshairStyle( int player, float *scale, int *r, int *g, int *b )
{
	int size, color;

	if( player < 0 || player >= SPLIT_MAX_PLAYERS )
		player = 0;

	size = UI_GetRow( player, ROW_XH_SIZE );
	color = bound( 0, UI_GetRow( player, ROW_XH_COLOR ), (int)UI_XH_COLORS - 1 );

	if( scale ) *scale = size * 0.01f;
	if( r ) *r = ui_xh_colors[color].r;
	if( g ) *g = ui_xh_colors[color].g;
	if( b ) *b = ui_xh_colors[color].b;

	return size != 100 || color != 0;
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
	const int rowh = Q_min( th + 6, Q_max( th + 1, ( h - 4 ) / ( ROW_COUNT + 2 )));
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
			if( i == ROW_XH_COLOR )
			{
				const int c = bound( 0, UI_GetRow( player, i ), (int)UI_XH_COLORS - 1 );

				Q_snprintf( text, sizeof( text ), sel ? "< %s >" : "%s", ui_xh_colors[c].name );
				CL_SplitFill( px + pw - 8 - CL_SplitTextWidth( text ) - th - 4, ry + 1, th - 2, th - 2,
					ui_xh_colors[c].r, ui_xh_colors[c].g, ui_xh_colors[c].b, 255 );
			}
			else Q_snprintf( text, sizeof( text ), sel ? "< %d%% >" : "%d%%", UI_GetRow( player, i ));
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

/*
=============
the table of players, for a dead player (the client DLL shows it to player 1)
=============
*/
typedef struct
{
	int	client;
	int	frags, deaths, ping;
	qboolean	known;
} ui_score_t;

static int UI_ScoreCompare( const void *a, const void *b )
{
	const ui_score_t *x = (const ui_score_t *)a, *y = (const ui_score_t *)b;

	if( x->frags != y->frags )
		return y->frags - x->frags;

	if( x->deaths != y->deaths )
		return x->deaths - y->deaths;

	return x->client - y->client;
}

static void UI_DrawScoreboard( int player, int x, int y, int w, int h )
{
	const float k = CL_SplitPixelAspectValue();
	const int th = CL_SplitTextHeight();
	const int rowh = th + 3;
	const int colw = CL_SplitTextWidth( "00000" );
	ui_score_t list[MAX_CLIENTS];
	int count = 0, i, self = -1, shown, pw, ph, px, py, top, maxrows;
	int frags_r, deaths_r, ping_r, name_w;
	char text[64], name[MAX_SCOREBOARDNAME];
	float f;

	if( !cl.maxclients )
		return;

	if( SV_SplitGetView( player, NULL, NULL, &i, NULL, NULL, &f ))
		self = i - 1;

	for( i = 0; i < cl.maxclients && i < MAX_CLIENTS; i++ )
	{
		ui_score_t *e;

		if( !cl.players[i].name[0] )
			continue;

		e = &list[count++];
		e->client = i;
		e->ping = cl.players[i].ping;
		e->known = SV_SplitGetScore( i, &e->frags, &e->deaths );

		if( !e->known )
		{
			e->frags = 0;
			e->deaths = 0;
		}
	}

	if( count <= 0 )
		return;

	qsort( list, count, sizeof( list[0] ), UI_ScoreCompare );

	// the table sits in the upper part, the "dispara para reaparecer" line stays below it
	top = y + Q_max( 4, h / 16 );
	maxrows = ( y + h * 3 / 4 - top ) / rowh - 2;
	maxrows = bound( 1, maxrows, count );
	shown = maxrows;

	pw = Q_min( w - 8, (int)( 340.0f / k ));
	ph = rowh * ( shown + 2 ) + 8;
	px = x + ( w - pw ) / 2;
	py = top;

	CL_SplitFill( px, py, pw, ph, 0, 0, 0, 190 );
	CL_SplitFill( px, py, pw, 2, 255, 160, 0, 255 );

	ping_r = px + pw - 8;
	deaths_r = ping_r - colw;
	frags_r = deaths_r - colw;
	name_w = frags_r - colw - ( px + 8 );

	CL_SplitDrawText( px + 8, py + 6, "Jugador", 255, 200, 40, 255 );
	UI_TextRight( frags_r, py + 6, "Frags", 255, 200, 40 );
	UI_TextRight( deaths_r, py + 6, "Muer.", 255, 200, 40 );
	UI_TextRight( ping_r, py + 6, "Ping", 255, 200, 40 );

	for( i = 0; i < shown; i++ )
	{
		const ui_score_t *e = &list[i];
		const int ry = py + 6 + rowh * ( i + 1 ) + 2;
		const qboolean mine = e->client == self;
		const int r = mine ? 255 : 210, g = mine ? 220 : 210, b = mine ? 60 : 210;
		int len;

		if( mine )
			CL_SplitFill( px + 3, ry - 1, pw - 6, th + 2, 255, 160, 0, 60 );

		Q_strncpy( name, cl.players[e->client].name, sizeof( name ));
		len = Q_strlen( name );

		while( len > 1 && CL_SplitTextWidth( name ) > name_w )
			name[--len] = 0;

		CL_SplitDrawText( px + 8, ry, name, r, g, b, 255 );

		Q_snprintf( text, sizeof( text ), "%d", e->frags );
		UI_TextRight( frags_r, ry, text, r, g, b );
		Q_snprintf( text, sizeof( text ), "%d", e->deaths );
		UI_TextRight( deaths_r, ry, text, r, g, b );
		Q_snprintf( text, sizeof( text ), "%d", e->ping );
		UI_TextRight( ping_r, ry, text, r, g, b );
	}
}

static void UI_DrawDead( int player, int x, int y, int w, int h )
{
	const int th = CL_SplitTextHeight();

	UI_DrawScoreboard( player, x, y, w, h );
	UI_TextCenter( x + w / 2, y + h - th * 2 - 8, "ELIMINADO", 255, 60, 60 );
	UI_TextCenter( x + w / 2, y + h - th - 4, "Dispara para reaparecer", 220, 220, 220 );
}

static void HUD_PlainCrosshair( int cx, int cy, int h, float k, float scale, int cr, int cg, int cb );

static void UI_DrawHUDText( int player, int x, int y, int w, int h )
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

	// the safe zone of the screen
	{
		const int ix = (int)( w * bound( 0.0f, hud_margin_x.value, 20.0f ) * 0.01f );
		const int iy = (int)( h * bound( 0.0f, hud_margin_y.value, 20.0f ) * 0.01f );

		x += ix; w -= 2 * ix;
		y += iy; h -= 2 * iy;
	}

	// the view of a player with a small pane gets smaller bars
	bw = Q_min( (int)( 90.0f / k ), w / 4 );
	bh = Q_max( 5, th * 2 / 3 );

	// crosshair: outlined so it shows on any background; the arms are scaled so
	// they look the same on a stretched 16:9 screen
	if( health > 0.0f )
	{
		float xs = 1.0f;
		int xr = 255, xg = 255, xb = 255;

		CL_SplitCrosshairStyle( player, &xs, &xr, &xg, &xb );
		HUD_PlainCrosshair( x + w / 2, y + h / 2, h, k, xs, xr, xg, xb );
	}
	else
	{
		UI_DrawDead( player, x, y, w, h );
		return;
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

/*
=============
HUD of the extra players, drawn like the game's own (health, armour, ammo,
flashlight, crosshair) with the sprites of sprites/hud.txt and the weapon lists
=============
*/
typedef struct
{
	HSPRITE		spr;
	wrect_t		rc;
	qboolean	ok;
} hudspr_t;

static struct
{
	int		res;		// 320 or 640: the sprites are laid out for that screen width
	int		servercount;	// sprite handles are only good for the map they were loaded in
	qboolean	ok;
	hudspr_t	number[10], cross, suit_empty, suit_full, flash_empty, flash_full, flash_beam;
} hud;

#define HUD_WEAPONS	32

typedef struct
{
	char		name[32];
	int		res;
	int		servercount;
	hudspr_t	ammo, ammo2, crosshair;
} hudweapon_t;

static hudweapon_t hud_weapon[HUD_WEAPONS];

static void HUD_Find( const client_sprite_t *list, int count, const char *name, int res, hudspr_t *out )
{
	int i, pass;

	memset( out, 0, sizeof( *out ));

	// the entry for this width, else the one for the other
	for( pass = 0; pass < 2 && !out->ok; pass++ )
	{
		const int want = pass ? ( res == 640 ? 320 : 640 ) : res;

		for( i = 0; i < count; i++ )
		{
			char path[MAX_QPATH];

			if( list[i].iRes != want || Q_stricmp( list[i].szName, name ))
				continue;

			Q_snprintf( path, sizeof( path ), "sprites/%s.spr", list[i].szSprite );
			out->spr = CL_SplitLoadSprite( path );
			out->rc = list[i].rc;
			out->ok = out->spr != 0;
			break;
		}
	}
}

static void HUD_Load( int res )
{
	const client_sprite_t *list;
	int count = 0, i;

	if( hud.res == res && hud.servercount == cl.servercount )
		return;

	memset( &hud, 0, sizeof( hud ));
	hud.res = res;
	hud.servercount = cl.servercount;

	list = CL_SplitSpriteList( "sprites/hud.txt", &count );

	if( !list || count <= 0 )
		return;

	for( i = 0; i < 10; i++ )
		HUD_Find( list, count, va( "number_%d", i ), res, &hud.number[i] );

	HUD_Find( list, count, "cross", res, &hud.cross );
	HUD_Find( list, count, "suit_empty", res, &hud.suit_empty );
	HUD_Find( list, count, "suit_full", res, &hud.suit_full );
	HUD_Find( list, count, "flash_empty", res, &hud.flash_empty );
	HUD_Find( list, count, "flash_full", res, &hud.flash_full );
	HUD_Find( list, count, "flash_beam", res, &hud.flash_beam );

	hud.ok = hud.number[0].ok && hud.cross.ok;
}

// the sprites of a weapon (its txt in sprites/): ammo icons and crosshair
static const hudweapon_t *HUD_WeaponSprites( const char *name, int res )
{
	int i, free = -1;

	for( i = 0; i < HUD_WEAPONS; i++ )
	{
		if( !hud_weapon[i].name[0] )
		{
			if( free < 0 )
				free = i;
			continue;
		}

		if( !Q_stricmp( hud_weapon[i].name, name ))
		{
			if( hud_weapon[i].res == res && hud_weapon[i].servercount == cl.servercount )
				return &hud_weapon[i];

			free = i;
			break;
		}
	}

	if( free < 0 )
		free = 0;

	{
		const client_sprite_t *list;
		int count = 0;
		char path[MAX_QPATH];

		memset( &hud_weapon[free], 0, sizeof( hud_weapon[free] ));
		Q_strncpy( hud_weapon[free].name, name, sizeof( hud_weapon[free].name ));
		hud_weapon[free].res = res;
		hud_weapon[free].servercount = cl.servercount;

		Q_snprintf( path, sizeof( path ), "sprites/%s.txt", name );
		list = CL_SplitSpriteList( path, &count );

		if( list && count > 0 )
		{
			HUD_Find( list, count, "ammo", res, &hud_weapon[free].ammo );
			HUD_Find( list, count, "ammo2", res, &hud_weapon[free].ammo2 );
			HUD_Find( list, count, "crosshair", res, &hud_weapon[free].crosshair );
		}
	}

	return &hud_weapon[free];
}

// framebuffer position and scale of a pane's HUD: the HUD is laid out in "square" pixels
typedef struct
{
	float	x, y;		// top left of the pane
	float	sx, sy;		// framebuffer pixels per HUD pixel
	int	w, h;		// size of the pane in HUD pixels
} hudview_t;

static void HUD_Sprite( const hudview_t *v, const hudspr_t *s, float x, float y, const wrect_t *rc, int r, int g, int b )
{
	if( !s->ok )
		return;

	CL_SplitDrawSprite( s->spr, rc ? rc : &s->rc, v->x + x * v->sx, v->y + y * v->sy, v->sx, v->sy, r, g, b, 255, kRenderTransAdd );
}

// CHud::DrawHudNumber
#define DHN_DRAWZERO	1
#define DHN_2DIGITS	2
#define DHN_3DIGITS	4

static int HUD_Number( const hudview_t *v, int x, int y, int flags, int number, int r, int g, int b )
{
	const int width = hud.number[0].rc.right - hud.number[0].rc.left;
	int k;

	if( number > 0 )
	{
		number = Q_min( number, 999 );

		if( number >= 100 )
		{
			k = number / 100;
			HUD_Sprite( v, &hud.number[k], x, y, NULL, r, g, b );
			x += width;
		}
		else if( flags & DHN_3DIGITS )
			x += width;

		if( number >= 10 )
		{
			k = ( number % 100 ) / 10;
			HUD_Sprite( v, &hud.number[k], x, y, NULL, r, g, b );
			x += width;
		}
		else if( flags & ( DHN_3DIGITS | DHN_2DIGITS ))
			x += width;

		k = number % 10;
		HUD_Sprite( v, &hud.number[k], x, y, NULL, r, g, b );
		x += width;
	}
	else if( flags & DHN_DRAWZERO )
	{
		if( flags & DHN_3DIGITS )
			x += width;

		if( flags & ( DHN_3DIGITS | DHN_2DIGITS ))
			x += width;

		HUD_Sprite( v, &hud.number[0], x, y, NULL, r, g, b );
		x += width;
	}

	return x;
}

static void HUD_Scale( int *r, int *g, int *b, int a )
{
	*r = *r * a / 255;
	*g = *g * a / 255;
	*b = *b * a / 255;
}

// outlined plus, for when the weapon has no crosshair sprite of its own
static void HUD_PlainCrosshair( int cx, int cy, int h, float k, float scale, int cr, int cg, int cb )
{
	const int len = Q_max( 4, (int)( Q_max( 4, h / 40 ) * scale )), gap = 3;
	const int lx = Q_max( 1, (int)( len / k ));
	const int gx = Q_max( 1, (int)( gap / k ));

	CL_SplitFill( cx - gx - lx - 1, cy - 2, lx + 2, 4, 0, 0, 0, 160 );
	CL_SplitFill( cx + gx - 1, cy - 2, lx + 2, 4, 0, 0, 0, 160 );
	CL_SplitFill( cx - 2, cy - gap - len - 1, 4, len + 2, 0, 0, 0, 160 );
	CL_SplitFill( cx - 2, cy + gap - 1, 4, len + 2, 0, 0, 0, 160 );

	CL_SplitFill( cx - gx - lx, cy - 1, lx, 2, cr, cg, cb, 255 );
	CL_SplitFill( cx + gx, cy - 1, lx, 2, cr, cg, cb, 255 );
	CL_SplitFill( cx - 1, cy - gap - len, 2, len, cr, cg, cb, 255 );
	CL_SplitFill( cx - 1, cy + gap, 2, len, cr, cg, cb, 255 );
}

static void UI_DrawHUD( int player, int x, int y, int w, int h )
{
	const float k = CL_SplitPixelAspectValue();
	const int th = CL_SplitTextHeight();
	float health, armor, frags, px = 0.0f, py = 0.0f;
	int weapon = 0, clip = -1, res1 = -1, res2 = -1, charge = 100;
	qboolean ammo1 = false, ammo2 = false, flashon = false, haveweapon;
	char wname[32];
	hudview_t v;
	int r, g, b, a, fh, y0, bx, cx, cy;
	const hudweapon_t *ws = NULL;

	if( !SV_SplitGetView( player, NULL, NULL, NULL, &health, &armor, &frags ))
		return;

	// the safe zone of the screen (hud_margin_x / hud_margin_y), applied to the view like it is to
	// player 1's HUD: the HUD shrinks towards the middle of the view
	{
		const float mx = bound( 0.0f, hud_margin_x.value, 20.0f ) * 0.01f;
		const float my = bound( 0.0f, hud_margin_y.value, 20.0f ) * 0.01f;

		v.x = x + (int)( w * mx );
		v.y = y + (int)( h * my );
		v.sx = ( 1.0f / k ) * ( 1.0f - 2.0f * mx );
		v.sy = 1.0f - 2.0f * my;
	}

	v.w = (int)( w * k );
	v.h = h;

	HUD_Load( v.w >= 640 ? 640 : 320 );

	if( !hud.ok )
	{
		UI_DrawHUDText( player, x, y, w, h );
		return;
	}

	if( health <= 0.0f )
	{
		UI_DrawDead( player, x, y, w, h );
		return;
	}

	wname[0] = 0;
	haveweapon = SV_SplitGetWeaponInfo( player, wname, sizeof( wname ), &ammo1, &ammo2 ) && wname[0];
	SV_SplitGetAmmo( player, &weapon, &clip, &res1, &res2 );
	SV_SplitGetFlashlight( player, &flashon, &charge );

	if( haveweapon )
		ws = HUD_WeaponSprites( wname, hud.res );

	// crosshair
	CL_SplitPointer( player, &px, &py );
	cx = x + (int)( w * 0.5f * ( 1.0f + px ));
	cy = y + (int)( h * 0.5f * ( 1.0f + py ));

	{
		float xs = 1.0f;
		int xr = 255, xg = 255, xb = 255;

		CL_SplitCrosshairStyle( player, &xs, &xr, &xg, &xb );

		if( ws && ws->crosshair.ok )
		{
			const wrect_t *rc = &ws->crosshair.rc;
			const float cw = ( rc->right - rc->left ) * v.sx * xs, ch = ( rc->bottom - rc->top ) * v.sy * xs;

			CL_SplitDrawSprite( ws->crosshair.spr, rc, cx - cw * 0.5f, cy - ch * 0.5f, v.sx * xs, v.sy * xs, xr, xg, xb, 255, kRenderTransColor );
		}
		else if( !haveweapon )
			HUD_PlainCrosshair( cx, cy, h, k, xs, xr, xg, xb );
	}

	fh = hud.number[0].rc.bottom - hud.number[0].rc.top;
	y0 = v.h - fh - fh / 2;

	// health: a cross and three digits, red when it is low
	{
		const int cw = hud.cross.rc.right - hud.cross.rc.left;

		if( health > 25.0f ) { r = 255; g = 160; b = 0; } else { r = 255; g = 16; b = 16; }
		a = health <= 15.0f ? 255 : 100;
		HUD_Scale( &r, &g, &b, a );

		HUD_Sprite( &v, &hud.cross, cw / 2, y0, NULL, r, g, b );
		HUD_Number( &v, cw + fh / 2, y0, DHN_3DIGITS | DHN_DRAWZERO, (int)health, r, g, b );
	}

	// armour: the suit fills up from the bottom
	if( hud.suit_empty.ok && hud.suit_full.ok )
	{
		const wrect_t *e = &hud.suit_empty.rc, *f = &hud.suit_full.rc;
		const int height = f->bottom - e->top;
		const int offset = ( e->bottom - e->top ) / 6;
		const int bat = bound( 0, (int)armor, 100 );
		wrect_t rc = *f;

		r = 255; g = 160; b = 0;
		HUD_Scale( &r, &g, &b, 100 );
		rc.top += (int)( height * (float)( 100 - bat ) * 0.01f );

		bx = v.w / 5;
		HUD_Sprite( &v, &hud.suit_empty, bx, y0 - offset, NULL, r, g, b );

		if( rc.bottom > rc.top )
			HUD_Sprite( &v, &hud.suit_full, bx, y0 - offset + ( rc.top - f->top ), &rc, r, g, b );

		bx += e->right - e->left;
		HUD_Number( &v, bx, y0, DHN_3DIGITS | DHN_DRAWZERO, bat, r, g, b );
	}

	// ammo: clip | reserve and the icon of the ammunition
	if( ws && ( ammo1 || ammo2 ))
	{
		const int aw = hud.number[0].rc.right - hud.number[0].rc.left;
		int ax, ay = y0;

		r = 255; g = 160; b = 0;
		HUD_Scale( &r, &g, &b, 100 );

		if( ammo1 )
		{
			const int iw = ws->ammo.rc.right - ws->ammo.rc.left;

			if( clip >= 0 )
			{
				const int bw = Q_max( 1, aw / 10 );
				int rr = 255, gg = 160, bb = 0;

				ax = v.w - 8 * aw - iw;
				ax = HUD_Number( &v, ax, ay, DHN_DRAWZERO | DHN_3DIGITS, clip, r, g, b );
				ax += aw / 2;

				// the bar between both numbers
				CL_SplitFill( (int)( v.x + ax * v.sx ), (int)( v.y + ay * v.sy ), Q_max( 1, (int)( bw * v.sx )), Q_max( 1, (int)( fh * v.sy )), rr, gg, bb, 100 );
				ax += bw + aw / 2;
				ax = HUD_Number( &v, ax, ay, DHN_DRAWZERO | DHN_3DIGITS, Q_max( res1, 0 ), r, g, b );
			}
			else
			{
				ax = v.w - 4 * aw - iw;
				ax = HUD_Number( &v, ax, ay, DHN_DRAWZERO | DHN_3DIGITS, Q_max( res1, 0 ), r, g, b );
			}

			if( ws->ammo.ok )
				HUD_Sprite( &v, &ws->ammo, ax, ay - ( ws->ammo.rc.bottom - ws->ammo.rc.top ) / 8, NULL, r, g, b );
		}

		if( ammo2 && res2 > 0 && ws->ammo2.ok )
		{
			const int iw = ws->ammo2.rc.right - ws->ammo2.rc.left;

			ay -= fh + fh / 4;
			ax = v.w - 4 * aw - iw;
			ax = HUD_Number( &v, ax, ay, DHN_DRAWZERO | DHN_3DIGITS, res2, r, g, b );
			HUD_Sprite( &v, &ws->ammo2, ax, ay - ( ws->ammo2.rc.bottom - ws->ammo2.rc.top ) / 8, NULL, r, g, b );
		}
	}

	// flashlight, top right
	if( hud.flash_empty.ok && hud.flash_full.ok )
	{
		const int fw = hud.flash_full.rc.right - hud.flash_full.rc.left;
		const float bat = bound( 0.0f, charge * 0.01f, 1.0f );
		const int offset = (int)( fw * ( 1.0f - bat ));
		int fx, fy;

		if( bat < 0.20f ) { r = 255; g = 16; b = 16; } else { r = 255; g = 160; b = 0; }
		HUD_Scale( &r, &g, &b, flashon ? 225 : 100 );

		fy = ( hud.flash_empty.rc.bottom - hud.flash_full.rc.top ) / 2;
		fx = v.w - fw - fw / 2;

		HUD_Sprite( &v, &hud.flash_empty, fx, fy, NULL, r, g, b );

		if( flashon && hud.flash_beam.ok )
			HUD_Sprite( &v, &hud.flash_beam, v.w - fw / 2, fy, NULL, r, g, b );

		if( offset < fw )
		{
			wrect_t rc = hud.flash_full.rc;

			rc.left += offset;
			HUD_Sprite( &v, &hud.flash_full, fx + offset, fy, &rc, r, g, b );
		}
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
	int		thumb[LOBBY_MAX_LIST];	// picture of each character: 0 = not looked for, -1 = none
	int		nmodels;
	int		winner;		// map the votes chose (set when the countdown starts)
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

// every character in the files: models/player/<name>/<name>.mdl, in the game folder, in the
// base folder and inside the paks (the same way the player setup menu finds them)
static void Lobby_ListModels( void )
{
	search_t *t = FS_Search( "models/player/*", true, false );
	int i, j;

	lobby.nmodels = 0;

	if( t )
	{
		for( i = 0; i < t->numfilenames && lobby.nmodels < LOBBY_MAX_LIST; i++ )
		{
			char name[32], path[128];

			COM_FileBase( t->filenames[i], name, sizeof( name ));

			if( !name[0] )
				continue;

			for( j = 0; j < lobby.nmodels; j++ )
				if( !Q_stricmp( lobby.models[j], name ))
					break;

			if( j < lobby.nmodels )
				continue;

			// only the folders that hold a model of their own name
			Q_snprintf( path, sizeof( path ), "models/player/%s/%s.mdl", name, name );

			if( !FS_FileExists( path, false ))
				continue;

			Q_strncpy( lobby.models[lobby.nmodels++], name, sizeof( lobby.models[0] ));
		}

		Mem_Free( t );
	}

	if( !lobby.nmodels )
		Q_strncpy( lobby.models[lobby.nmodels++], "gordon", sizeof( lobby.models[0] ));

	qsort( lobby.models, lobby.nmodels, sizeof( lobby.models[0] ), Lobby_Cmp );
}

// the picture of a character: models/player/<name>/<name>.bmp (or .png, .tga); 0 when it has none
static int Lobby_Thumb( int model )
{
	static const char *ext[] = { "bmp", "png", "tga" };
	int i;

	if( model < 0 || model >= lobby.nmodels )
		return 0;

	if( lobby.thumb[model] )
		return lobby.thumb[model] > 0 ? lobby.thumb[model] : 0;

	lobby.thumb[model] = -1;

	for( i = 0; i < (int)ARRAYSIZE( ext ); i++ )
	{
		char path[128];
		int tex;

		Q_snprintf( path, sizeof( path ), "models/player/%s/%s.%s", lobby.models[model], lobby.models[model], ext[i] );

		if( !FS_FileExists( path, false ))
			continue;

		tex = ref.dllFuncs.GL_LoadTexture( path, NULL, 0, TF_IMAGE );

		if( tex > 0 )
		{
			lobby.thumb[model] = tex;
			return tex;
		}
	}

	return 0;
}

// draws the picture in a box of w x h (fitted, in the aspect of the picture)
static void Lobby_DrawThumb( int model, int x, int y, int w, int h, float k )
{
	const int tex = Lobby_Thumb( model );
	int pw = 0, ph = 0, dw, dh;

	CL_SplitFill( x - 1, y - 1, w + 2, h + 2, 90, 90, 110, 255 );
	CL_SplitFill( x, y, w, h, 0, 0, 0, 255 );

	if( tex <= 0 )
	{
		UI_TextCenter( x + w / 2, y + ( h - CL_SplitTextHeight( )) / 2, "sin foto", 120, 120, 120 );
		return;
	}

	R_GetTextureParms( &pw, &ph, tex );

	if( pw <= 0 || ph <= 0 )
	{
		pw = 4;
		ph = 3;
	}

	// the framebuffer is stretched on a wide screen: the width is divided by k to keep the shape
	dh = h;
	dw = (int)( dh * (float)pw / (float)ph / k );

	if( dw > w )
	{
		dw = w;
		dh = (int)( dw * k * (float)ph / (float)pw );
	}

	ref.dllFuncs.GL_SetRenderMode( kRenderNormal );
	ref.dllFuncs.Color4ub( 255, 255, 255, 255 );
	ref.dllFuncs.R_DrawStretchPic( x + ( w - dw ) / 2, y + ( h - dh ) / 2, dw, dh, 0, 0, 1, 1, tex );
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

/*
============
The lobby background

The same picture the main menu has behind its buttons: the tiles of the Steam
layout (resource/BackgroundLayout.txt, or the HD one when the game asks for it),
or the single splash of the old WON menu. ui_prefer_won_background picks between
them the way the menu does. Without either the lobby keeps its plain colour.
============
*/
#define LOBBY_BG_MAX	32

static struct
{
	int	tex, x, y, w, h;
} lobby_bg[LOBBY_BG_MAX];
static int lobby_bg_n, lobby_bg_w, lobby_bg_h;

#if XASH_OGC
static qboolean Lobby_LoadSteamBackground( void )
{
	static const char *files[2] = { "resource/HD_BackgroundLayout.txt", "resource/BackgroundLayout.txt" };
	int f;

	for( f = GI->hd_background ? 0 : 1; f < 2; f++ )
	{
		byte *buf = FS_LoadFile( files[f], NULL, false );
		char *pfile = (char *)buf, token[256];
		int n = 0, rw = 0, rh = 0;
		qboolean ok = false;

		if( !buf )
			continue;

		// "resolution W H" comes first, then "<tga> <scaled> <x> <y>" for every tile
		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		if( !pfile || Q_strcmp( token, "resolution" ))
			goto done;

		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		if( !pfile ) goto done;
		rw = Q_atoi( token );

		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		if( !pfile ) goto done;
		rh = Q_atoi( token );

		while( n < LOBBY_BG_MAX && ( pfile = COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
		{
			int tex;

			if( !FS_FileExists( token, false ))
				goto done;

			tex = ref.dllFuncs.GL_LoadTexture( token, NULL, 0, TF_IMAGE | TF_NOFLIP_TGA );
			if( tex <= 0 )
				goto done;

			lobby_bg[n].tex = tex;
			R_GetTextureParms( &lobby_bg[n].w, &lobby_bg[n].h, tex );

			pfile = COM_ParseFile( pfile, token, sizeof( token )); // "scaled", ignored like the menu does
			if( !pfile ) goto done;

			pfile = COM_ParseFile( pfile, token, sizeof( token ));
			if( !pfile ) goto done;
			lobby_bg[n].x = Q_atoi( token );

			pfile = COM_ParseFile( pfile, token, sizeof( token ));
			if( !pfile ) goto done;
			lobby_bg[n].y = Q_atoi( token );

			n++;
		}

		ok = n > 0;
done:
		Mem_Free( buf );

		if( ok )
		{
			lobby_bg_n = n;
			lobby_bg_w = rw;
			lobby_bg_h = rh;
			return true;
		}
	}

	return false;
}

static qboolean Lobby_LoadWONBackground( void )
{
	int tex;

	if( !FS_FileExists( "gfx/shell/splash.bmp", false ))
		return false;

	tex = ref.dllFuncs.GL_LoadTexture( "gfx/shell/splash.bmp", NULL, 0, TF_IMAGE );
	if( tex <= 0 )
		return false;

	lobby_bg[0].tex = tex;
	lobby_bg[0].x = lobby_bg[0].y = 0;
	R_GetTextureParms( &lobby_bg[0].w, &lobby_bg[0].h, tex );
	lobby_bg_n = 1;
	lobby_bg_w = lobby_bg[0].w;
	lobby_bg_h = lobby_bg[0].h;

	return lobby_bg_w > 0 && lobby_bg_h > 0;
}

static void Lobby_LoadBackground( void )
{
	lobby_bg_n = 0;

	if( Cvar_VariableValue( "ui_prefer_won_background" ) != 0.0f )
	{
		if( !Lobby_LoadWONBackground( ))
			Lobby_LoadSteamBackground( );
	}
	else
	{
		if( !Lobby_LoadSteamBackground( ))
			Lobby_LoadWONBackground( );
	}

	if( lobby_bg_w <= 0 || lobby_bg_h <= 0 )
		lobby_bg_n = 0;
}
#endif // XASH_OGC

// covers the screen with the menu picture, the way the menu fits it (no stretching, centred)
static qboolean Lobby_DrawBackground( int W, int H )
{
	float scale;
	int i, xoff, yoff;

	if( lobby_bg_n <= 0 )
		return false;

	if( (float)W * lobby_bg_h > (float)H * lobby_bg_w )
		scale = (float)W / lobby_bg_w;
	else
		scale = (float)H / lobby_bg_h;

	xoff = (int)( ( W - lobby_bg_w * scale ) / 2 );
	yoff = (int)( ( H - lobby_bg_h * scale ) / 2 );

	ref.dllFuncs.GL_SetRenderMode( kRenderNormal );
	ref.dllFuncs.Color4ub( 255, 255, 255, 255 );

	for( i = 0; i < lobby_bg_n; i++ )
	{
		const int dx = (int)ceilf( lobby_bg[i].x * scale );
		const int dy = (int)ceilf( lobby_bg[i].y * scale );
		const int dw = (int)ceilf( lobby_bg[i].w * scale );
		const int dh = (int)ceilf( lobby_bg[i].h * scale );

		ref.dllFuncs.R_DrawStretchPic( xoff + dx, yoff + dy, dw, dh, 0, 0, 1, 1, lobby_bg[i].tex );
	}

	return true;
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
	Lobby_LoadBackground();

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

// the map with the most votes; a tie (everyone chose a different one, two against two, ...)
// is drawn between the maps that tie
static int Lobby_Tally( void )
{
	int votes[SPLIT_MAX_PLAYERS], maps[SPLIT_MAX_PLAYERS], n = 0, i, j, best = 0;
	int tied[SPLIT_MAX_PLAYERS], ntied = 0;

	for( i = 0; i < SPLIT_MAX_PLAYERS; i++ )
	{
		if( !lobby.p[i].joined )
			continue;

		for( j = 0; j < n; j++ )
			if( maps[j] == lobby.p[i].map )
				break;

		if( j == n )
		{
			maps[n] = lobby.p[i].map;
			votes[n++] = 0;
		}

		votes[j]++;
	}

	for( j = 0; j < n; j++ )
		best = Q_max( best, votes[j] );

	for( j = 0; j < n; j++ )
		if( votes[j] == best )
			tied[ntied++] = maps[j];

	return ntied ? tied[COM_RandomLong( 0, ntied - 1 )] : 0;
}

static void Lobby_Launch( void )
{
	int i, n = Lobby_Joined(), nplayers = 0;
	const char *map;
	char cmd[256];

	for( i = 0; i < SPLIT_MAX_PLAYERS; i++ )
		if( lobby.p[i].joined )
			nplayers++;

	// the map the votes chose when the countdown started
	map = lobby.maps[bound( 0, lobby.winner, Q_max( lobby.nmaps - 1, 0 ))];

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
		lobby.winner = Lobby_Tally();
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

	if( Lobby_DrawBackground( W, H ))
		CL_SplitFill( 0, 0, W, H, 0, 0, 0, 140 ); // dim it so the panels read
	else
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
		ty += th + 6;

		// the picture of the character, in what is left of the card
		{
			const int room = ( y + h - th - 16 ) - ty - ( th * 2 + 14 );
			const int ph = Q_min( 96, room );

			if( ph >= 36 )
			{
				Lobby_DrawThumb( p->model, x + 6, ty, w - 12, ph, k );
				ty += ph + 8;
			}
		}

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
			Q_snprintf( text, sizeof( text ), "Mapa: %s   Empieza en %d   (B: cancelar)",
				lobby.maps[bound( 0, lobby.winner, lobby.nmaps - 1 )], (int)ceil( lobby.end - host.realtime ));
			UI_TextCenter( W / 2, by, text, 120, 255, 140 );
		}
		else if( joined >= 2 && ready == joined )
			UI_TextCenter( W / 2, by, "Todos listos", 120, 255, 140 );
		else if( joined < 2 )
			UI_TextCenter( W / 2, by, "Hacen falta al menos dos jugadores", 255, 200, 90 );
		else UI_TextCenter( W / 2, by, "Todos confirman con A - gana el mapa mas votado", 255, 200, 90 );

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
	Cvar_RegisterVariable( &wii_xhair_size );
	Cvar_RegisterVariable( &wii_xhair_color );
	Cvar_RegisterVariable( &wii_split_set1 );
	Cvar_RegisterVariable( &wii_split_set2 );
	Cvar_RegisterVariable( &wii_split_set3 );
	Cvar_Get( "wii_split_lastmap", "", FCVAR_ARCHIVE, "map picked last time in the split screen lobby" );

	Cmd_AddCommand( "wii_splitlobby", Lobby_Start_f, "open the split screen lobby (controllers, characters, map)" );

	UI_LoadCfg( 0 );
	UI_LoadCfg( 1 );
	UI_LoadCfg( 2 );
	UI_LoadCfg( 3 );
}

/*
=============
Menus por numero del servidor (ShowMenu)

Los servidores (AMX Mod X, el menu de equipos, votaciones...) piden "pulsa 1-9"
con el mensaje ShowMenu y el cliente solo escucha slot1..slot10, o sea el
teclado. Con un mando no habia forma de elegir. Aqui se lee el mismo mensaje
(el HUD sigue pintando el texto) y se deja elegir la opcion con el mando: el
dispositivo cambia su tabla de botones (in_ogc.c) mientras haya un menu y la
opcion se manda como slotN, igual que si se pulsara la tecla.
=============
*/
static struct
{
	int	valid;		// mascara: bit 0 = opcion 1 ... bit 9 = opcion 10
	int	sel;		// opcion elegida (1..10)
	double	shutoff;	// 0 = sin limite
	qboolean	hidden;	// el jugador lo cerro con "cancelar"
	char	hint[96];
} menupick;

static qboolean MenuPick_Valid( int n )
{
	return n >= 1 && n <= 10 && ( menupick.valid & ( 1 << ( n - 1 )));
}

void CL_MenuPickClear( void )
{
	menupick.valid = 0;
	menupick.sel = 0;
	menupick.shutoff = 0.0;
	menupick.hidden = false;
}

void CL_MenuPickMessage( const byte *buf, int size )
{
	int valid, i;
	int time;

	// short validslots, char displaytime, byte needmore, string
	if( size < 4 )
		return;

	valid = buf[0] | ( buf[1] << 8 );
	time = (signed char)buf[2];

	if( !( valid & 0x3ff ))
	{
		CL_MenuPickClear();
		return;
	}

	menupick.valid = valid & 0x3ff;
	menupick.shutoff = time > 0 ? host.realtime + time : 0.0;
	menupick.hidden = false;

	// primera opcion valida; un menu nuevo siempre empieza desde ahi
	menupick.sel = 0;
	for( i = 1; i <= 10; i++ )
	{
		if( MenuPick_Valid( i ))
		{
			menupick.sel = i;
			break;
		}
	}
}

qboolean CL_MenuPickActive( void )
{
	if( !menupick.valid || menupick.hidden )
		return false;

	if( cls.state != ca_active || ( menupick.shutoff > 0.0 && host.realtime > menupick.shutoff ))
	{
		CL_MenuPickClear();
		return false;
	}

	return true;
}

void CL_MenuPickSetHint( const char *hint )
{
	Q_strncpy( menupick.hint, hint ? hint : "", sizeof( menupick.hint ));
}

// 0 anterior, 1 siguiente, 2 aceptar, 3 cancelar
void CL_MenuPickAction( int act )
{
	int i, n;

	if( !CL_MenuPickActive( ))
		return;

	switch( act )
	{
	case 0:
	case 1:
		n = menupick.sel;
		for( i = 0; i < 10; i++ )
		{
			n += act ? 1 : -1;
			if( n > 10 ) n = 1;
			if( n < 1 ) n = 10;

			if( MenuPick_Valid( n ))
			{
				menupick.sel = n;
				break;
			}
		}
		break;

	case 2:
		n = menupick.sel;
		if( !MenuPick_Valid( n ))
			break;

		// slotN pasa por el cliente del juego, que cierra su menu y manda menuselect
		if( n <= 9 )
			Cbuf_AddText( va( "slot%d\n", n ));
		else
			Cbuf_AddText( "slot10\n" );

		CL_MenuPickClear();
		break;

	case 3:
		if( MenuPick_Valid( 10 ))
		{
			Cbuf_AddText( "slot10\n" );
			CL_MenuPickClear();
		}
		else menupick.hidden = true;	// vuelve con el siguiente menu
		break;
	}
}

void CL_MenuPickDraw( void )
{
	const int W = refState.width, H = refState.height;
	char line[128], tmp[16];
	int th, tw, x, y, i, bw;

	if( !CL_MenuPickActive( ) || W <= 0 || H <= 0 )
		return;

	line[0] = 0;
	for( i = 1; i <= 10; i++ )
	{
		if( !MenuPick_Valid( i ))
			continue;

		Q_snprintf( tmp, sizeof( tmp ), i == menupick.sel ? "[%d] " : " %d  ", i == 10 ? 0 : i );
		Q_strncat( line, tmp, sizeof( line ));
	}

	th = CL_SplitTextHeight();
	tw = Q_max( CL_SplitTextWidth( line ), CL_SplitTextWidth( menupick.hint ));
	bw = tw + 24;
	x = ( W - bw ) / 2;
	y = H - th * 2 - 28;

	CL_SplitFill( x, y, bw, th * 2 + 14, 0, 0, 0, 190 );
	CL_SplitDrawText( ( W - CL_SplitTextWidth( line )) / 2, y + 4, line, 255, 220, 80, 255 );
	CL_SplitDrawText( ( W - CL_SplitTextWidth( menupick.hint )) / 2, y + 8 + th, menupick.hint, 190, 190, 190, 255 );
}
