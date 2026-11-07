/*
cl_split.c - split screen (two to four local players)
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
Player 1 is the normal client. The others are fake clients of the listen
server (see sv_split.c) whose cameras are rendered here, in their own part of
the screen, from the server's own entity data.

Layouts: two players share the screen side by side or top and bottom
(wii_split 1 / 2), three get two views on top and one stretched along the
bottom, four get a corner each.
*/

CVAR_DEFINE_AUTO( wii_split, "0", 0, "split screen with other local players (0 = off, 1 = side by side, 2 = top and bottom; only matters for two players)" );
CVAR_DEFINE_AUTO( wii_split_players, "2", 0, "number of players in the split screen (2 to 4)" );
static CVAR_DEFINE_AUTO( wii_split_model1, "gordon", 0, "model of split screen player 2" );
static CVAR_DEFINE_AUTO( wii_split_model2, "barney", 0, "model of split screen player 3" );
static CVAR_DEFINE_AUTO( wii_split_model3, "scientist", 0, "model of split screen player 4" );

typedef struct
{
	vec3_t		angles;		// where the player looks (players 2-4)
	double		nexttry;	// do not retry a failed start every frame
	qboolean	warned;

	// first person weapon
	unsigned	animcount;	// weapon animation counter last seen
	int		vmmodel;	// weapon model last seen
	double		animstart;	// cl.time the current weapon animation started
	float		bobphase;
	float		lag[2];		// the weapon trails behind the camera: yaw, pitch
	float		fpx, fpy;	// filtered pointer, for the lean
	float		px, py;		// pointer inside the view (Nunchuk players)
	qboolean	pointer;
	float		dyaw, dpitch;	// camera change of this frame
} splitp_t;

static struct
{
	qboolean	active;
	int		mode;
	int		count;		// players on the screen (2 to 4)
	int		drawn_layout;	// layout the HUD was last initialised for
	splitp_t	p[SPLIT_MAX_PLAYERS];
} split;

void CL_SplitInit( void )
{
	Cvar_RegisterVariable( &wii_split );
	Cvar_RegisterVariable( &wii_split_players );
	Cvar_RegisterVariable( &wii_split_model1 );
	Cvar_RegisterVariable( &wii_split_model2 );
	Cvar_RegisterVariable( &wii_split_model3 );
	Cmd_AddCommand( "wii_split_info", SV_SplitInfo_f, "show the server side state of the split screen players (optional player number)" );
	CL_SplitUIInit();
}

qboolean CL_SplitActive( void )
{
	return split.active;
}

int CL_SplitCount( void )
{
	return split.active ? split.count : 1;
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

float CL_SplitPixelAspectValue( void )
{
	return CL_SplitPixelAspect();
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

	if( !split.active || player < 0 || player >= split.count )
	{
		*x = *y = 0;
		*w = W;
		*h = H;
		return;
	}

	switch( split.count )
	{
	case 2:
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
		break;

	case 3:
		// two on top, the third one along the whole bottom
		if( player < 2 )
		{
			*y = 0;
			*h = H / 2;
			*w = W / 2;
			*x = player ? *w : 0;
			if( player ) *w = W - *x;
		}
		else
		{
			*x = 0;
			*w = W;
			*y = H / 2;
			*h = H - *y;
		}
		break;

	default:
		// a corner each
		*w = W / 2;
		*h = H / 2;
		*x = ( player & 1 ) ? *w : 0;
		*y = ( player & 2 ) ? *h : 0;
		if( player & 1 ) *w = W - *x;
		if( player & 2 ) *h = H - *y;
		break;
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
CL_SplitViewAspect

shape of a player's view as it looks on the TV (width / height)
=============
*/
float CL_SplitViewAspect( int player )
{
	int x, y, w, h;

	CL_SplitRect( player, &x, &y, &w, &h );

	return (float)w / (float)h * CL_SplitPixelAspect( );
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

once per rendered frame: starts and stops the extra players and reads their
controllers
=============
*/
void CL_SplitFrame( void )
{
	const int mode = (int)wii_split.value;
	const int want = bound( 2, (int)wii_split_players.value, SPLIT_MAX_PLAYERS );
	const qboolean wanted = ( mode == 1 || mode == 2 ) && cls.state == ca_active && SV_Active() &&
		cl.maxclients > 1 && !cl.background && !cls.demoplayback;
	int i, count = 1;

	CL_SplitUIFrame();

	if( !wanted )
	{
		SV_SplitStopAll();
		split.active = false;
	}
	else
	{
		static convar_t *const models[] = { NULL, &wii_split_model1, &wii_split_model2, &wii_split_model3 };

		for( i = 1; i < SPLIT_MAX_PLAYERS; i++ )
		{
			splitp_t *p = &split.p[i];

			if( i >= want )
			{
				if( SV_SplitActive( i ))
					SV_SplitStop( i );
				continue;
			}

			if( !SV_SplitActive( i ) && host.realtime >= p->nexttry )
			{
				char name[32];

				p->nexttry = host.realtime + 2.0;
				Q_snprintf( name, sizeof( name ), "Jugador %d", i + 1 );

				if( !SV_SplitStart( i, name, models[i]->string ))
				{
					if( !p->warned )
						Con_Printf( "wii_split: can't add player %d (needs a listen server with maxplayers %d or more)\n", i + 1, want );
					p->warned = true;
				}
				else
				{
					VectorClear( p->angles );
					p->warned = false;
					p->animstart = 0.0;
					p->lag[0] = p->lag[1] = 0.0f;
					CL_SplitUIPlayerStarted( i );
				}
			}
		}

		// the layout counts the players that are really in the game
		for( i = 1; i < want && SV_SplitActive( i ); i++ )
			count++;

		split.active = count > 1;
		split.count = count;
		split.mode = mode;
	}

	// the HUD is laid out on the size of the view: redo it when the layout changes
	{
		const int layout = split.active ? split.mode * 10 + split.count : 0;

		if( split.drawn_layout != layout )
		{
			split.drawn_layout = layout;
			SCR_VidInit();
		}
	}

	if( !split.active )
		return;

#if XASH_OGC
	{
		const float dt = bound( 0.001f, (float)host.realframetime, 0.1f );

		// player 1's controller is read by the normal input; here only for its own menu
		CL_SplitUIInput( 0, OGC_PortUI( 0, NULL ));

		for( i = 1; i < split.count; i++ )
		{
			splitp_t *p = &split.p[i];
			ogc_psettings_t cfg;
			ogc_pinput_t in;
			vec3_t forced;
			float fmove = 0.0f, smove = 0.0f;
			word buttons = 0;

			CL_SplitUIConfig( i, &cfg.ir_deadzone, &cfg.stick_deadzone, &cfg.stick_sens, &cfg.ir_sens, NULL );

			// the game turned the player (spawn, teleport): start from those angles
			if( SV_SplitTakeAngles( i, forced ))
				VectorCopy( forced, p->angles );

			p->dyaw = p->dpitch = 0.0f;
			p->pointer = false;

			if( OGC_PlayerInput( CL_SplitPortOf( i ), i, dt, &cfg, &in ))
			{
				const qboolean menu = CL_SplitUIInput( i, in.ui );

				if( !menu )
				{
					p->dyaw = in.dyaw;
					p->dpitch = in.dpitch;
					p->angles[YAW] = anglemod( p->angles[YAW] + in.dyaw );
					p->angles[PITCH] = bound( -89.0f, p->angles[PITCH] + in.dpitch, 89.0f );
					fmove = in.fmove;
					smove = in.smove;
					buttons = in.buttons;
					p->px = in.px;
					p->py = in.py;
					p->pointer = in.pointer;

					if( in.weapon )
						SV_SplitCycleWeapon( i, in.weapon );
				}
			}
			else CL_SplitUIInput( i, in.ui ); // a bare remote can still open its menu

			SV_SplitSetInput( i, p->angles, fmove, smove, 0.0f, buttons );
		}
	}
#endif
}

/*
=============
CL_SplitSetupViewModel

fills clgame.viewent with a player's first person weapon (what V_SetupViewModel
and the game's view code do for player 1); false when there is nothing to draw
=============
*/
static qboolean CL_SplitSetupViewModel( int player, const vec3_t origin, const vec3_t viewangles )
{
	splitp_t *p = &split.p[player];
	cl_entity_t *view = &clgame.viewent;
	const float dt = bound( 0.0f, (float)host.realframetime, 0.1f );
	int modelindex, sequence, body;
	unsigned animcount;
	qboolean ground = true;
	vec3_t forward, right, up;
	float speed, s, tx, ty;
	model_t *mod;

	if( !SV_SplitGetViewModel( player, &modelindex, &sequence, &body, &animcount ))
		return false;

	mod = CL_ModelHandle( modelindex );

	if( !mod || mod->type != mod_studio )
		return false;

	// a new animation (or another weapon) starts now
	if( animcount != p->animcount || modelindex != p->vmmodel || p->animstart <= 0.0 )
	{
		p->animcount = animcount;
		p->vmmodel = modelindex;
		p->animstart = cl.time;
	}

	memset( view, 0, sizeof( *view ));
	view->index = view->curstate.number = cl.playernum + 1;
	view->model = mod;
	view->curstate.modelindex = modelindex;
	view->curstate.sequence = sequence;
	view->curstate.body = body;
	view->curstate.framerate = 1.0f;
	view->curstate.animtime = p->animstart;
	view->curstate.rendermode = kRenderNormal;
	view->curstate.renderamt = 255;
	view->latched.prevsequence = sequence;
	view->latched.prevanimtime = p->animstart;
	view->latched.sequencetime = p->animstart - 1.0;

	// the same placement the game's view code gives player 1's weapon
	VectorCopy( origin, view->origin );
	view->origin[2] -= 1.0f;
	view->angles[YAW] = viewangles[YAW];
	view->angles[PITCH] = -viewangles[PITCH];
	view->angles[ROLL] = 0.0f;

	AngleVectors( viewangles, forward, right, up );

	// walking: the weapon bobs
	speed = SV_SplitGetSpeed( player, &ground );
	s = ground ? bound( 0.0f, speed / 320.0f, 1.0f ) : 0.0f;
	p->bobphase += dt * ( 6.0f + 6.0f * s );

	if( s > 0.01f )
	{
		VectorMA( view->origin, sinf( p->bobphase ) * 0.8f * s, right, view->origin );
		VectorMA( view->origin, fabsf( sinf( p->bobphase )) * 0.9f * s - 0.4f * s, up, view->origin );
		VectorMA( view->origin, 0.8f * s, forward, view->origin );
	}

	// turning: the weapon trails a little behind the camera and then catches up
	if( dt > 0.0f )
	{
		const float k = bound( 0.0f, dt * 10.0f, 1.0f );
		const float lag_y = bound( -4.0f, p->dyaw * 0.9f, 4.0f );	// camera turned left (+) -> weapon lags right (-)
		const float lag_p = bound( -4.0f, p->dpitch * 0.9f, 4.0f );

		p->lag[0] += ( -lag_y - p->lag[0] ) * k;
		p->lag[1] += ( -lag_p - p->lag[1] ) * k;
	}

	view->angles[YAW] += p->lag[0];
	view->angles[PITCH] += -p->lag[1];

	// a pointer (Nunchuk) aims the weapon the way player 1's remote does
	if( p->pointer )
	{
		float fx, fy, kk = 1.0f, smooth = Cvar_VariableValue( "wii_gun_smooth" );
		const float sway = Cvar_VariableValue( "wii_ir_gunsway" ), shift = Cvar_VariableValue( "wii_gun_shift" );

		CL_SplitFov( player, cl.local.scr_fov, &fx, &fy );
		tx = tanf( DEG2RAD( fx ) * 0.5f );
		ty = tanf( DEG2RAD( fy ) * 0.5f );

		if( smooth > 0.0f )
			kk = bound( 0.0f, 1.0f - expf( -dt * smooth ), 1.0f );

		p->fpx += ( bound( -1.0f, p->px, 1.0f ) - p->fpx ) * kk;
		p->fpy += ( bound( -1.0f, p->py, 1.0f ) - p->fpy ) * kk;

		view->angles[YAW]   += -RAD2DEG( atanf( p->fpx * tx ));
		view->angles[PITCH] -= RAD2DEG( atanf( p->fpy * ty ));
		view->angles[ROLL]  += -p->fpx * sway;

		if( shift != 0.0f )
		{
			VectorMA( view->origin, p->fpx * shift, right, view->origin );
			VectorMA( view->origin, -p->fpy * shift * 0.6f, up, view->origin );
		}
	}
	else p->fpx = p->fpy = 0.0f;

	VectorCopy( view->angles, view->curstate.angles );
	VectorCopy( view->angles, view->latched.prevangles );

	return true;
}

/*
=============
CL_SplitRenderView

draws the views of the extra players; called after player 1's has been drawn
=============
*/
void CL_SplitRenderView( void )
{
	int player;

	if( !split.active )
		return;

	for( player = 1; player < split.count; player++ )
	{
		vec3_t origin, punch, oldorg, oldangles;
		ref_viewpass_t rvp;
		cl_entity_t savedview;
		float health = 0.0f;
		int index;

		if( !SV_SplitGetView( player, origin, punch, &index, &health, NULL, NULL ))
			continue;

		memset( &rvp, 0, sizeof( rvp ));
		CL_SplitViewport( player, rvp.viewport );
		VectorCopy( origin, rvp.vieworigin );
		VectorAdd( split.p[player].angles, punch, rvp.viewangles );
		rvp.viewentity = index;
		rvp.flags = RF_DRAW_WORLD | RF_SPLIT_VIEW | RF_NO_VIEWMODEL;
		CL_SplitFov( player, cl.local.scr_fov, &rvp.fov_x, &rvp.fov_y );

		// GL_RenderFrame changes these, the 2D code after the views still needs player 1's
		VectorCopy( refState.vieworg, oldorg );
		VectorCopy( refState.viewangles, oldangles );

		// The renderer draws the first person weapon from the engine's viewent, which
		// holds player 1's. Lend it this player's weapon for the length of its view.
		savedview = clgame.viewent;

		if( health > 0.0f && CL_SplitSetupViewModel( player, origin, rvp.viewangles ))
			rvp.flags = ( rvp.flags & ~RF_NO_VIEWMODEL ) | RF_SPLIT_VIEWMODEL;

		GL_RenderFrame( &rvp );

		clgame.viewent = savedview;

		VectorCopy( oldorg, refState.vieworg );
		VectorCopy( oldangles, refState.viewangles );
	}
}

/*
=============
CL_SplitDrawHUD

dividing lines, the HUD of players 2-4 and every player's own menu
=============
*/
void CL_SplitDrawHUD( void )
{
	const int W = refState.width, H = refState.height;
	int i;

	if( !split.active )
		return;

	// the extra views leave the renderer in 3D state: go back to 2D
	ref.dllFuncs.R_Set2DMode( true );

	// dividing lines
	switch( split.count )
	{
	case 2:
		if( split.mode == 2 )
			CL_SplitFill( 0, H / 2 - 1, W, 2, 0, 0, 0, 255 );
		else CL_SplitFill( W / 2 - 1, 0, 2, H, 0, 0, 0, 255 );
		break;
	case 3:
		CL_SplitFill( 0, H / 2 - 1, W, 2, 0, 0, 0, 255 );
		CL_SplitFill( W / 2 - 1, 0, 2, H / 2, 0, 0, 0, 255 );
		break;
	default:
		CL_SplitFill( 0, H / 2 - 1, W, 2, 0, 0, 0, 255 );
		CL_SplitFill( W / 2 - 1, 0, 2, H, 0, 0, 0, 255 );
		break;
	}

	for( i = 0; i < split.count; i++ )
	{
		int x, y, w, h;

		CL_SplitRect( i, &x, &y, &w, &h );
		CL_SplitUIDrawHUD( i, x, y, w, h );
	}
}
