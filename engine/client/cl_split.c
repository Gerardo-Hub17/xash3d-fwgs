/*
cl_split.c - split screen (two local players)
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
Player 1 is the normal client. Player 2 is a fake client of the listen server
(see sv_split.c) whose camera is rendered here, in the second half of the
screen, from the server's own entity data.
*/

CVAR_DEFINE_AUTO( wii_split, "0", 0, "split screen with a second local player (0 = off, 1 = side by side, 2 = top and bottom)" );
static CVAR_DEFINE_AUTO( wii_split_name, "Jugador 2", FCVAR_ARCHIVE, "name of the split screen player 2" );

static struct
{
	qboolean	active;
	int		mode;
	vec3_t		angles;		// where player 2 looks
	double		nexttry;	// do not retry a failed start every frame
	int		drawn_mode;	// layout the HUD was last initialised for
} split;

void CL_SplitInit( void )
{
	Cvar_RegisterVariable( &wii_split );
	Cvar_RegisterVariable( &wii_split_name );
}

qboolean CL_SplitActive( void )
{
	return split.active;
}

// framebuffer pixels per "square" pixel horizontally: a 16:9 TV stretches the framebuffer
static float CL_SplitPixelAspect( void )
{
#if XASH_OGC
	if( refState.width > 0 && refState.height > 0 )
		return OGC_GetAspect() * refState.height / refState.width;
#endif
	return 1.0f;
}

/*
=============
CL_SplitRect

rectangle of a player in framebuffer pixels, top left origin
=============
*/
void CL_SplitRect( int player, int *x, int *y, int *w, int *h )
{
	const int W = refState.width, H = refState.height;

	if( !split.active )
	{
		*x = *y = 0;
		*w = W;
		*h = H;
		return;
	}

	if( split.mode == 2 ) // top and bottom
	{
		*x = 0;
		*w = W;
		*h = H / 2;
		*y = player ? *h : 0;
		if( player ) *h = H - *y;
	}
	else // side by side
	{
		*y = 0;
		*h = H;
		*w = W / 2;
		*x = player ? *w : 0;
		if( player ) *w = W - *x;
	}
}

// view rectangle in the renderer's convention (origin at the bottom left)
void CL_SplitViewport( int player, int *vp )
{
	int x, y, w, h;

	CL_SplitRect( player, &x, &y, &w, &h );

	vp[0] = x;
	vp[1] = refState.height - ( y + h );
	vp[2] = w;
	vp[3] = h;
}

/*
=============
CL_SplitHudSize

size of the screen a player's HUD is laid out on, in square pixels
=============
*/
void CL_SplitHudSize( int *w, int *h )
{
	int x, y, rw, rh;

	CL_SplitRect( 0, &x, &y, &rw, &rh );

	*w = (int)( rw * CL_SplitPixelAspect( ) + 0.5f );
	*h = rh;
}

/*
=============
CL_SplitFov

Each view keeps as much of the normal field of view as its shape allows: the
vertical FOV of the 4:3 picture, but never more horizontal FOV than the full
screen has. base is the horizontal FOV of a 4:3 picture (cl_fov).
=============
*/
void CL_SplitFov( int player, float base, float *fov_x, float *fov_y )
{
	int x, y, w, h;
	float display, a, half, th, tv, tx;

	CL_SplitRect( player, &x, &y, &w, &h );

	display = (float)refState.width / (float)refState.height * CL_SplitPixelAspect( );
	a = (float)w / (float)h * CL_SplitPixelAspect( ); // shape of this view

	half = tan( DEG2RAD( bound( 10.0f, base, 150.0f )) * 0.5f );
	th = half * ( display / ( 4.0f / 3.0f )); // what the full screen would show
	tv = half * 0.75f;

	tx = Q_min( th, tv * a );

	*fov_x = RAD2DEG( atan( tx )) * 2.0f;
	*fov_y = RAD2DEG( atan( tx / a )) * 2.0f;
}

/*
=============
CL_SplitFrame

once per rendered frame: starts or stops player 2 and reads its controller
=============
*/
void CL_SplitFrame( void )
{
	const int mode = (int)wii_split.value;
	const qboolean wanted = ( mode == 1 || mode == 2 ) && cls.state == ca_active && SV_Active() &&
		cl.maxclients > 1 && !cl.background && !cls.demoplayback;

	if( !wanted )
	{
		if( SV_SplitActive( ))
			SV_SplitStop();
		split.active = false;
	}
	else
	{
		if( !SV_SplitActive( ) && host.realtime >= split.nexttry )
		{
			split.nexttry = host.realtime + 2.0;

			if( !SV_SplitStart( wii_split_name.string ))
				Con_Printf( "wii_split: can't add player 2 (needs a listen server with maxplayers 2 or more)\n" );
			else VectorClear( split.angles );
		}

		split.active = SV_SplitActive();
		split.mode = mode;
	}

	// the HUD is laid out on the size of the view: redo it when the layout changes
	if( split.drawn_mode != ( split.active ? split.mode : 0 ))
	{
		split.drawn_mode = split.active ? split.mode : 0;
		SCR_VidInit();
	}

	if( !split.active )
		return;

#if XASH_OGC
	{
		float dyaw, dpitch, fmove, smove;
		word buttons;
		int weapon;

		if( OGC_Player2Input( bound( 0.001f, (float)host.realframetime, 0.1f ), &dyaw, &dpitch, &fmove, &smove, &buttons, &weapon ))
		{
			split.angles[YAW] = anglemod( split.angles[YAW] + dyaw );
			split.angles[PITCH] = bound( -89.0f, split.angles[PITCH] + dpitch, 89.0f );

			if( weapon )
				SV_SplitCycleWeapon( weapon );
		}
		else
		{
			fmove = smove = 0.0f;
			buttons = 0;
		}

		SV_SplitSetInput( split.angles, fmove, smove, 0.0f, buttons );
	}
#endif
}

/*
=============
CL_SplitRenderView

draws player 2's view; called after player 1's has been drawn
=============
*/
void CL_SplitRenderView( void )
{
	vec3_t origin, punch, oldorg, oldangles;
	ref_viewpass_t rvp;
	int index;

	if( !split.active || !SV_SplitGetView( origin, punch, &index, NULL, NULL, NULL ))
		return;

	memset( &rvp, 0, sizeof( rvp ));
	CL_SplitViewport( 1, rvp.viewport );
	VectorCopy( origin, rvp.vieworigin );
	VectorAdd( split.angles, punch, rvp.viewangles );
	rvp.viewentity = index;
	rvp.flags = RF_DRAW_WORLD | RF_SPLIT_VIEW | RF_NO_VIEWMODEL;
	CL_SplitFov( 1, cl.local.scr_fov, &rvp.fov_x, &rvp.fov_y );

	// GL_RenderFrame changes these, the 2D code after the views still needs player 1's
	VectorCopy( refState.vieworg, oldorg );
	VectorCopy( refState.viewangles, oldangles );

	GL_RenderFrame( &rvp );

	VectorCopy( oldorg, refState.vieworg );
	VectorCopy( oldangles, refState.viewangles );
}

/*
=============
CL_SplitDrawHUD

dividing line, player 2's crosshair and a minimal status line
=============
*/
void CL_SplitDrawHUD( void )
{
	static const rgba_t white = { 255, 255, 255, 255 };
	float health, armor, frags;
	int x, y, w, h;
	float k = CL_SplitPixelAspect();
	char text[64];

	if( !split.active )
		return;

	// dividing line
	CL_SplitRect( 1, &x, &y, &w, &h );

	if( split.mode == 2 )
		ref.dllFuncs.FillRGBA( kRenderNormal, 0, y - 1, refState.width, 2, 0, 0, 0, 255 );
	else ref.dllFuncs.FillRGBA( kRenderNormal, x - 1, 0, 2, refState.height, 0, 0, 0, 255 );

	if( !SV_SplitGetView( NULL, NULL, NULL, &health, &armor, &frags ))
		return;

	// crosshair: the arms are scaled so they look the same on a stretched 16:9 screen
	{
		const float cx = x + w * 0.5f, cy = y + h * 0.5f;
		const float len = 8.0f, th = 1.0f;

		ref.dllFuncs.FillRGBA( kRenderTransAdd, cx - len / k, cy - th * 0.5f, len * 2.0f / k, th, 0, 255, 0, 255 );
		ref.dllFuncs.FillRGBA( kRenderTransAdd, cx - th / ( 2.0f * k ), cy - len, th / k, len * 2.0f, 0, 255, 0, 255 );
	}

	Q_snprintf( text, sizeof( text ), "HP %d  AR %d  FR %d", (int)health, (int)armor, (int)frags );
	Con_DrawString( x + 8, y + h - 22, text, white );
}
