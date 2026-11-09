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
static CVAR_DEFINE_AUTO( wii_split_stats, "0", 0, "show how long each view of the split screen takes to draw, in ms (to see what is slow)" );
static float split_view_ms[SPLIT_MAX_PLAYERS]; // average time of each view
static float split_frame_ms;
static CVAR_DEFINE_AUTO( wii_split_model1, "gordon", 0, "model of split screen player 2" );
static CVAR_DEFINE_AUTO( wii_split_model2, "barney", 0, "model of split screen player 3" );
static CVAR_DEFINE_AUTO( wii_split_model3, "scientist", 0, "model of split screen player 4" );

typedef struct
{
	vec3_t		angles;		// where the camera looks (players 2-4)
	double		nexttry;	// do not retry a failed start every frame
	qboolean	warned;

	// first person weapon
	unsigned	animcount;	// weapon animation counter last seen
	unsigned	firecount;	// shots last seen
	int		vmmodel;	// weapon model last seen
	double		animstart;	// cl.time the current weapon animation started
	qboolean	firing;		// the sequence is a firing animation chosen here, not the game's
	int		fireseq;
	float		bobtime;
	float		bob;
	qboolean	fresh;		// the animation started on this frame (its first events run)
	float		punch;		// pitch kick of the last shot, decays (the game's V_PunchAxis)
	float		fpx, fpy;	// filtered pointer, for the lean
	float		lasthealth;	// health last frame, to shake the controller when it drops

	// controller
	float		px, py;		// pointer inside the view, -1..1 (the last one seen while it points)
	qboolean	pointer;	// the controller has a pointer
	float		roll, tilt;	// twist of the remote
	int		kind;		// OGC_KIND_*
	qboolean	irvalid;
	vec3_t		aim;		// direction of the shots: the camera bent towards the pointer
} splitp_t;

static struct
{
	qboolean	active;
	int		mode;
	int		count;		// players on the screen (2 to 4)
	int		drawn_layout;	// layout the HUD was last initialised for
	splitp_t	p[SPLIT_MAX_PLAYERS];
} split;

static void CL_SplitInput_f( void )
{
	int i;

	if( !split.active )
	{
		Con_Printf( "split screen is not active\n" );
		return;
	}

	for( i = 1; i < split.count; i++ )
	{
		const splitp_t *p = &split.p[i];
		float h = 0.0f, a = 0.0f;

		SV_SplitGetView( i, NULL, NULL, NULL, &h, &a, NULL );
		Con_Printf( "player %d: port %d kind %d pointer %d (ir %d) at %.2f %.2f, roll %.1f tilt %.1f, aim %.1f %.1f, camera %.1f %.1f, shots %u\n",
			i + 1, CL_SplitPortOf( i ), p->kind, p->pointer, p->irvalid, p->px, p->py, p->roll, p->tilt,
			p->aim[PITCH], p->aim[YAW], p->angles[PITCH], p->angles[YAW], SV_SplitGetFire( i, NULL ));
	}
}

void CL_SplitInit( void )
{
	Cvar_RegisterVariable( &wii_split );
	Cvar_RegisterVariable( &wii_split_players );
	Cvar_RegisterVariable( &wii_split_stats );
	Cvar_RegisterVariable( &wii_split_model1 );
	Cvar_RegisterVariable( &wii_split_model2 );
	Cvar_RegisterVariable( &wii_split_model3 );
	Cmd_AddCommand( "wii_split_input", CL_SplitInput_f, "show what the controllers of the extra split screen players report" );
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
static void CL_SplitAimAngles( int player, const vec3_t camera, float px, float py, vec3_t aim, float *moveyaw );

void CL_SplitFrame( void )
{
	const int mode = (int)wii_split.value;
	const int want = bound( 2, (int)wii_split_players.value, SPLIT_MAX_PLAYERS );
	const qboolean wanted = ( mode == 1 || mode == 2 ) && cls.state == ca_active && SV_Active() &&
		cl.maxclients > 1 && !cl.background && !cls.demoplayback;
	int i, count = 1;

	CL_SplitUIFrame();

	// back at the menu: the next map is not a split screen one until a server says so
	if( cls.state == ca_disconnected && !SV_Active() && Cvar_VariableValue( "gx_split_lod" ) != 0.0f )
		Cvar_SetValue( "gx_split_lod", 0.0f );

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
					p->firing = false;
					p->firecount = SV_SplitGetFire( i, NULL );
					p->bobtime = p->bob = p->punch = 0.0f;
					p->fresh = false;
					p->px = p->py = p->fpx = p->fpy = 0.0f;
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
			float fmove = 0.0f, smove = 0.0f, autoaim = 0.0f;
			word buttons = 0;

			CL_SplitUIConfig( i, &cfg.ir_deadzone, &cfg.stick_deadzone, &cfg.stick_sens, &cfg.ir_sens, &autoaim );

			// the game turned the player (spawn, teleport): start from those angles
			if( SV_SplitTakeAngles( i, forced ))
				VectorCopy( forced, p->angles );

			p->roll = p->tilt = 0.0f;

			if( OGC_PlayerInput( CL_SplitPortOf( i ), i, dt, &cfg, &in ))
			{
				const qboolean menu = CL_SplitUIInput( i, in.ui );

				p->kind = in.kind;
				p->pointer = ( in.kind == OGC_KIND_NUNCHUK );
				p->irvalid = in.pointer;

				// like player 1's pointer, the last position stays while the sensor loses the bar
				if( in.pointer )
				{
					p->px = in.px;
					p->py = in.py;
				}

				if( !p->pointer )
					p->px = p->py = 0.0f;

				if( !menu )
				{
					vec3_t eye, aim;
					float dyaw, dpitch, ay = 0.0f, ap = 0.0f, speed = 1.0f, health = 0.0f;
					float moveyaw = 0.0f;
					int self = 0;

					// aim assist: the same as player 1's, with the player's own strength
					if( autoaim > 0.0f && SV_SplitGetView( i, eye, NULL, &self, &health, NULL, NULL ) && health > 0.0f )
					{
						if( p->pointer )
							CL_SplitAimAngles( i, p->angles, p->px, p->py, aim, NULL );
						else VectorCopy( p->angles, aim );

						OGC_PlayerAssist( i, eye, self, aim, autoaim, dt, p->pointer,
							in.dyaw != 0.0f || in.dpitch != 0.0f, &ay, &ap, &speed );
					}

					dyaw = in.dyaw * speed + ay;
					dpitch = in.dpitch * speed + ap;

					p->angles[YAW] = anglemod( p->angles[YAW] + dyaw );
					p->angles[PITCH] = bound( -89.0f, p->angles[PITCH] + dpitch, 89.0f );

					fmove = in.fmove;
					smove = in.smove;
					buttons = in.buttons;
					p->roll = in.roll;
					p->tilt = in.tilt;

					// the shots go where the pointer is, the camera stays put (as for player 1)
					if( p->pointer )
						CL_SplitAimAngles( i, p->angles, p->px, p->py, p->aim, &moveyaw );
					else VectorCopy( p->angles, p->aim );

					// movement is relative to the camera, not to the pointer
					if( moveyaw != 0.0f )
					{
						const float c = cosf( DEG2RAD( moveyaw )), sn = sinf( DEG2RAD( moveyaw ));
						const float fm = fmove, sm = smove;

						fmove = fm * c - sm * sn;
						smove = fm * sn + sm * c;
					}

					if( in.weapon )
						SV_SplitCycleWeapon( i, in.weapon );
				}
				else VectorCopy( p->angles, p->aim );
			}
			else
			{
				CL_SplitUIInput( i, in.ui ); // a bare remote can still open its menu
				p->pointer = false;
				p->px = p->py = 0.0f;
				VectorCopy( p->angles, p->aim );
			}

			SV_SplitSetInput( i, p->aim, fmove, smove, 0.0f, buttons );
		}
	}
#endif
}

/*
=============
CL_SplitAimAngles

The direction the shots of a pointer player take: the camera angles bent towards
the pointer (px, py inside the view, -1..1), what OGC_ApplyPointerToAim does for
player 1. The projection is the one of the player's own view, so the shot lands
under the crosshair. moveyaw gets how far the yaw was turned.
=============
*/
static void CL_SplitAimAngles( int player, const vec3_t camera, float px, float py, vec3_t aim, float *moveyaw )
{
	float fx, fy, tx, ty, flat, dyaw, dpitch;
	vec3_t fwd, right, up, dir;

	VectorCopy( camera, aim );
	aim[ROLL] = 0.0f;

	if( moveyaw )
		*moveyaw = 0.0f;

	CL_SplitFov( player, cl.local.scr_fov, &fx, &fy );
	tx = tanf( DEG2RAD( fx ) * 0.5f ) * bound( -1.0f, px, 1.0f );
	ty = tanf( DEG2RAD( fy ) * 0.5f ) * bound( -1.0f, py, 1.0f );

	AngleVectors( camera, fwd, right, up );
	VectorMA( fwd, tx, right, dir );
	VectorMA( dir, -ty, up, dir );
	VectorNormalize( dir );

	flat = sqrtf( dir[0] * dir[0] + dir[1] * dir[1] );

	// near the poles yaw is ill-defined
	if( flat < 0.02f )
		return;

	dyaw = RAD2DEG( atan2f( dir[1], dir[0] )) - camera[YAW];
	while( dyaw > 180.0f ) dyaw -= 360.0f;
	while( dyaw < -180.0f ) dyaw += 360.0f;
	dpitch = -RAD2DEG( atan2f( dir[2], flat )) - camera[PITCH];

	aim[YAW] = camera[YAW] + dyaw;
	aim[PITCH] = bound( -89.0f, camera[PITCH] + dpitch, 89.0f );

	if( moveyaw )
		*moveyaw = dyaw;
}

// where a player's pointer is inside its view; false for a controller that has none
qboolean CL_SplitPointer( int player, float *px, float *py )
{
	if( !split.active || player < 0 || player >= split.count )
		return false;

	if( player == 0 )
	{
#if XASH_OGC
		if( OGC_AimIsPointer( ))
		{
			float x = 0.0f, y = 0.0f;

			OGC_GetPointer( &x, &y );
			*px = bound( -1.0f, x, 1.0f );
			*py = bound( -1.0f, y, 1.0f );
			return true;
		}
#endif
		return false;
	}

	if( !split.p[player].pointer )
		return false;

	*px = split.p[player].px;
	*py = split.p[player].py;

	return true;
}

static float CL_SplitCvar( const char *name, float def )
{
	const convar_t *var = Cvar_FindVar( name );

	return var ? var->value : def;
}

/*
=============
CL_SplitGunLimit

The weapon follows the pointer by rotating about the eye, and it already sits low
and to the right of the screen, so following the pointer all the way to a corner
throws it out of the picture. Squeezes the angles (left, down: positive towards
those sides, in degrees) with a soft limit that leaves small movements untouched
and never lets the gun go further than wii_gun_edge of the half field of view
(halfx, halfy in degrees), counting wii_gun_home as where the gun rests.
=============
*/
void CL_SplitGunLimit( float *left, float *down, float halfx, float halfy )
{
	const float edge = CL_SplitCvar( "wii_gun_edge", 0.85f );
	const float home = CL_SplitCvar( "wii_gun_home", 0.35f );
	float lim;

	if( edge <= 0.0f || halfx <= 0.0f || halfy <= 0.0f )
		return;

	// it rests to the right, so there is more room to the left
	lim = halfx * bound( 0.1f, edge + ( *left >= 0.0f ? home : -home ), 2.0f );
	*left = lim * tanhf( *left / lim );

	// and it rests low, so there is more room upwards
	lim = halfy * bound( 0.1f, edge + ( *down >= 0.0f ? -home * 1.3f : home * 1.3f ), 2.0f );
	*down = lim * tanhf( *down / lim );
}

/*
=============
CL_SplitGunAim

Points the weapon where the pointer is, always, while its place on the screen is
held by CL_SplitGunLimit (so it never leaves the picture). The angles (left, down,
degrees) are applied in full; the part the limit took off would have moved the gun
across the screen, so it is undone by moving the weapon back along the way: the
gun turns about its own middle (wii_gun_pivot: forward, left, up in its model
space) and ends up pointing at the pointer without sliding off. wii_gun_aim 0
brings back the old behaviour of just stopping the gun short.
=============
*/
void CL_SplitGunAim( cl_entity_t *view, float left, float down, float halfx, float halfy )
{
	const float aim = bound( 0.0f, CL_SplitCvar( "wii_gun_aim", 1.0f ), 1.0f );
	float ll = left, ld = down, fl, fd;
	vec3_t pivot = { 16.0f, -6.0f, -6.0f };
	vec3_t al, af, pl, pf;
	matrix3x4 ml, mf;
	convar_t *cv;

	CL_SplitGunLimit( &ll, &ld, halfx, halfy );

	fl = ll + ( left - ll ) * aim;
	fd = ld + ( down - ld ) * aim;

	if(( cv = Cvar_FindVar( "wii_gun_pivot" )) != NULL && cv->string )
	{
		float px, py, pz;

		if( sscanf( cv->string, "%f %f %f", &px, &py, &pz ) == 3 )
			VectorSet( pivot, px, py, pz );
	}

	// where the middle of the gun is with the limited angles, and with the full ones.
	// The renderer flips the pitch of a model when it builds its matrix, so do the same.
	VectorSet( al, view->angles[PITCH] - ld, view->angles[YAW] + ll, view->angles[ROLL] );
	VectorSet( af, view->angles[PITCH] - fd, view->angles[YAW] + fl, view->angles[ROLL] );

	if( !FBitSet( host.features, ENGINE_COMPENSATE_QUAKE_BUG ))
	{
		al[PITCH] = -al[PITCH];
		af[PITCH] = -af[PITCH];
	}

	Matrix3x4_CreateFromEntity( ml, al, vec3_origin, 1.0f );
	Matrix3x4_CreateFromEntity( mf, af, vec3_origin, 1.0f );
	Matrix3x4_VectorTransform( ml, pivot, pl );
	Matrix3x4_VectorTransform( mf, pivot, pf );

	view->angles[YAW]   += fl;
	view->angles[PITCH] -= fd;

	VectorAdd( view->origin, pl, view->origin );
	VectorSubtract( view->origin, pf, view->origin );
}

/*
=============
CL_SplitPickFireSeq

The game plays the firing animation of a weapon from a client event, which a fake
client never receives. Pick the sequence by its name instead ("fire", "shoot",
"launch", "throw"; "alt..." for the secondary attack); -1 when there is none.
=============
*/
static int CL_SplitPickFireSeq( model_t *mod, qboolean alt )
{
	static const char *skip[] = { "reload", "idle", "draw", "holster", "deploy", "fidget", "spin", "charge",
		"pin", "pull", "empty", "cycle", "stop", "off", "down", "up", "hide" };
	const studiohdr_t *hdr;
	const mstudioseqdesc_t *seq;
	int i, j, first[64], nfirst = 0, any[64], nany = 0, altlist[64], nalt = 0;

	if( !mod || mod->type != mod_studio )
		return -1;

	hdr = (const studiohdr_t *)Mod_StudioExtradata( mod );

	if( !hdr || hdr->numseq <= 0 )
		return -1;

	seq = (const mstudioseqdesc_t *)((const byte *)hdr + hdr->seqindex );

	for( i = 0; i < hdr->numseq; i++ )
	{
		const char *label = seq[i].label;
		const size_t len = Q_strlen( label );
		qboolean bad = false, isalt;

		if( !Q_stristr( label, "fire" ) && !Q_stristr( label, "shoot" ) && !Q_stristr( label, "launch" )
			&& !Q_stristr( label, "throw" ) && !Q_stristr( label, "attack" ))
			continue;

		for( j = 0; j < (int)ARRAYSIZE( skip ); j++ )
		{
			if( Q_stristr( label, skip[j] ))
			{
				bad = true;
				break;
			}
		}

		isalt = Q_stristr( label, "alt" ) || Q_stristr( label, "launch" ) || Q_stristr( label, "secondary" );

		// "altfireon" starts the secondary beam, it contains "on"/"off" only as part of the name
		if( bad && Q_stristr( label, "altfireon" ))
			bad = false;

		if( bad )
			continue;

		if( isalt )
		{
			if( nalt < (int)ARRAYSIZE( altlist ))
				altlist[nalt++] = i;
			continue;
		}

		if( nany < (int)ARRAYSIZE( any ))
			any[nany++] = i;

		// "fire", "fire1", "shoot": the plain one; "fire2", "fire3" are the variants
		if( len == 0 || label[len - 1] < '2' || label[len - 1] > '9' )
		{
			if( nfirst < (int)ARRAYSIZE( first ))
				first[nfirst++] = i;
		}
	}

	if( alt && nalt )
		return altlist[COM_RandomLong( 0, nalt - 1 )];

	if( nfirst )
		return first[COM_RandomLong( 0, nfirst - 1 )];

	if( nany )
		return any[COM_RandomLong( 0, nany - 1 )];

	return nalt ? altlist[0] : -1;
}

/*
=============
CL_SplitKick

the pitch kick the game gives the camera of the player who fires (V_PunchAxis in the
events of the client dll, which a fake client never gets). Negative is up.
=============
*/
static float CL_SplitKick( const char *weapon, qboolean alt )
{
	if( COM_StringEmptyOrNULL( weapon ))
		return -1.0f;

	if( Q_stristr( weapon, "357" ) || Q_stristr( weapon, "python" ))
		return -10.0f;
	if( Q_stristr( weapon, "shotgun" ))
		return alt ? -10.0f : -5.0f;
	if( Q_stristr( weapon, "9mmar" ) || Q_stristr( weapon, "mp5" ))
		return COM_RandomFloat( -2.0f, 2.0f );
	if( Q_stristr( weapon, "hornet" ))
		return COM_RandomFloat( 0.0f, 2.0f );
	if( Q_stristr( weapon, "rpg" ) || Q_stristr( weapon, "egon" ) || Q_stristr( weapon, "crowbar" ) ||
		Q_stristr( weapon, "grenade" ) || Q_stristr( weapon, "satchel" ) || Q_stristr( weapon, "tripmine" ) ||
		Q_stristr( weapon, "snark" ))
		return 0.0f;

	return -2.0f; // 9mm handgun, crossbow, gauss
}

/*
=============
CL_SplitBob

the weapon bob of the game's view code (V_CalcBob of the client dll), from the
player's speed on the ground
=============
*/
static float CL_SplitBob( splitp_t *p, float speed, qboolean ground, float dt )
{
	const float bobcycle = Q_max( 0.05f, CL_SplitCvar( "cl_bobcycle", 0.8f ));
	const float bobup = bound( 0.05f, CL_SplitCvar( "cl_bobup", 0.5f ), 0.95f );
	const float clbob = CL_SplitCvar( "cl_bob", 0.01f );
	float cycle, bob;

	// in the air the last value stays
	if( !ground || dt <= 0.0f )
		return p->bob;

	p->bobtime += dt;
	cycle = p->bobtime - (int)( p->bobtime / bobcycle ) * bobcycle;
	cycle /= bobcycle;

	if( cycle < bobup )
		cycle = M_PI_F * cycle / bobup;
	else cycle = M_PI_F + M_PI_F * ( cycle - bobup ) / ( 1.0f - bobup );

	bob = speed * clbob;
	bob = bob * 0.3f + bob * 0.7f * sinf( cycle );
	bob = Q_min( bob, 4.0f );
	bob = Q_max( bob, -7.0f );

	p->bob = bob;

	return bob;
}

/*
=============
CL_SplitSetupViewModel

fills clgame.viewent with a player's first person weapon the way the game's view
code does for player 1 (V_CalcNormalRefdef of the client dll); false when there
is nothing to draw. angles are the camera's, without the punch of the recoil
=============
*/
static qboolean CL_SplitSetupViewModel( int player, int entindex, const vec3_t origin, const vec3_t angles )
{
	splitp_t *p = &split.p[player];
	cl_entity_t *view = &clgame.viewent;
	const float dt = bound( 0.0f, (float)host.realframetime, 0.1f );
	int modelindex, sequence, body;
	unsigned animcount, firecount;
	qboolean ground = true, alt = false;
	vec3_t forward, right, up;
	float speed, bob;
	model_t *mod;

	if( !SV_SplitGetViewModel( player, &modelindex, &sequence, &body, &animcount ))
		return false;

	mod = CL_ModelHandle( modelindex );

	if( !mod || mod->type != mod_studio )
		return false;

	// a new animation from the game (or another weapon) starts now
	if( animcount != p->animcount || modelindex != p->vmmodel || p->animstart <= 0.0 )
	{
		p->animcount = animcount;
		p->vmmodel = modelindex;
		p->animstart = cl.time;
		p->fresh = true;
		p->firing = false;
	}

	// a shot: the game's own animation for it comes from a client event
	firecount = SV_SplitGetFire( player, &alt );

	if( firecount != p->firecount )
	{
		const int seq = CL_SplitPickFireSeq( mod, alt );
		char wname[32];
		qboolean a1, a2;

		p->firecount = firecount;

		if( seq >= 0 )
		{
			p->firing = true;
			p->fireseq = seq;
			p->animstart = cl.time;
			p->fresh = true;
		}

		// the camera kicks like player 1's does
		wname[0] = 0;
		SV_SplitGetWeaponInfo( player, wname, sizeof( wname ), &a1, &a2 );
		p->punch = CL_SplitKick( wname, alt );

		// and the controller shakes like player 1's does
		OGC_RumblePort( CL_SplitPortOf( player ), 0.7f, 0.12f );
	}

	// the kick wears off the way the game's view code does it
	if( p->punch != 0.0f )
	{
		float len = fabsf( p->punch );

		len -= ( 10.0f + len * 0.5f ) * dt;
		p->punch = len > 0.0f ? ( p->punch < 0.0f ? -len : len ) : 0.0f;
	}

	if( p->firing )
		sequence = p->fireseq;

	memset( view, 0, sizeof( *view ));
	// The renderer copies the attachments of the weapon it draws into the entity it
	// belongs to, and beams that start at one ("idx | 0x1000", like the Gluon Gun's)
	// read them from there. With player 1's number on every lent weapon, player 1's
	// beam started at the weapon of whichever view drew last. Use the player's own.
	view->index = view->curstate.number = ( entindex > 0 && entindex <= cl.maxclients ) ? entindex : cl.playernum + 1;
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
	view->latched.sequencetime = p->fresh ? p->animstart : p->animstart - 1.0; // the events of a new animation
	p->fresh = false;

	// the weapon takes the camera's angles (pitch is inverted for models), no turning lag
	// and no punch: the camera kicks, the weapon does not
	view->angles[YAW] = angles[YAW];
	view->angles[PITCH] = -angles[PITCH];
	view->angles[ROLL] = 0.0f;

	AngleVectors( angles, forward, right, up );

	// walking: the weapon bob of the game
	speed = SV_SplitGetSpeed( player, &ground );
	bob = CL_SplitBob( p, speed, ground, dt );

	VectorCopy( origin, view->origin );
	VectorMA( view->origin, bob * 0.4f, forward, view->origin );
	view->origin[2] += bob;

	// throw in a little tilt
	view->angles[YAW] -= bob * 0.5f;
	view->angles[ROLL] -= bob * 1.0f;
	view->angles[PITCH] -= bob * 0.3f;

	view->origin[2] -= 1.0f;

	// the weapon aims where the pointer is, and turns with the remote, like player 1's
	if( p->pointer )
	{
		float fx, fy, tx, ty, kk = 1.0f;
		const float smooth = CL_SplitCvar( "wii_gun_smooth", 0.0f );
		const float sway = CL_SplitCvar( "wii_ir_gunsway", 0.0f );
		const float shift = CL_SplitCvar( "wii_gun_shift", 0.0f );

		CL_SplitFov( player, cl.local.scr_fov, &fx, &fy );
		tx = tanf( DEG2RAD( fx ) * 0.5f );
		ty = tanf( DEG2RAD( fy ) * 0.5f );

		if( smooth > 0.0f )
			kk = bound( 0.0f, 1.0f - expf( -dt * smooth ), 1.0f );

		p->fpx += ( bound( -1.0f, p->px, 1.0f ) - p->fpx ) * kk;
		p->fpy += ( bound( -1.0f, p->py, 1.0f ) - p->fpy ) * kk;

		if( fabsf( p->fpx ) < 0.001f ) p->fpx = 0.0f;
		if( fabsf( p->fpy ) < 0.001f ) p->fpy = 0.0f;

		CL_SplitGunAim( view, -RAD2DEG( atanf( p->fpx * tx )), RAD2DEG( atanf( p->fpy * ty )),
			RAD2DEG( atanf( tx )), RAD2DEG( atanf( ty )));
		view->angles[PITCH] += p->tilt;
		view->angles[ROLL]  += p->roll - p->fpx * sway;
		view->angles[ROLL]   = bound( -90.0f, view->angles[ROLL], 90.0f );

		if( shift != 0.0f )
		{
			VectorMA( view->origin, p->fpx * shift, right, view->origin );
			VectorMA( view->origin, -p->fpy * shift * 0.6f, up, view->origin );
		}
	}
	else p->fpx = p->fpy = 0.0f;

	view->angles[PITCH] = bound( -89.0f, view->angles[PITCH], 89.0f );

	VectorCopy( view->angles, view->curstate.angles );
	VectorCopy( view->angles, view->latched.prevangles );

	return true;
}

/*
=============
CL_SplitNoteMainTime

how long player 1's view took (told by V_RenderView), for wii_split_stats
=============
*/
void CL_SplitNoteMainTime( double ms )
{
	split_view_ms[0] += ( (float)ms - split_view_ms[0] ) * 0.1f;
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

		// damage taken: same shake as player 1's
		{
			splitp_t *sp = &split.p[player];

			if( sp->lasthealth > 0.0f && health < sp->lasthealth )
			{
				const float lost = bound( 1.0f, sp->lasthealth - health, 50.0f );

				if( health <= 0.0f )
					OGC_RumblePort( CL_SplitPortOf( player ), 1.0f, 0.8f );
				else
					OGC_RumblePort( CL_SplitPortOf( player ), 0.55f + lost * 0.009f, 0.15f + lost * 0.008f );
			}
			sp->lasthealth = health;
		}

		memset( &rvp, 0, sizeof( rvp ));
		CL_SplitViewport( player, rvp.viewport );
		VectorCopy( origin, rvp.vieworigin );
		VectorAdd( split.p[player].angles, punch, rvp.viewangles );
		rvp.viewentity = index;
		rvp.flags = RF_DRAW_WORLD | RF_SPLIT_VIEW | RF_NO_VIEWMODEL | RF_SPLIT_COUNT_BITS( split.count );
		CL_SplitFov( player, cl.local.scr_fov, &rvp.fov_x, &rvp.fov_y );

		// GL_RenderFrame changes these, the 2D code after the views still needs player 1's
		VectorCopy( refState.vieworg, oldorg );
		VectorCopy( refState.viewangles, oldangles );

		// The renderer draws the first person weapon from the engine's viewent, which
		// holds player 1's. Lend it this player's weapon for the length of its view.
		savedview = clgame.viewent;

		if( health > 0.0f && CL_SplitSetupViewModel( player, index, origin, split.p[player].angles ))
		{
			rvp.flags = ( rvp.flags & ~RF_NO_VIEWMODEL ) | RF_SPLIT_VIEWMODEL;

			// like the game's view code: the camera bobs with the weapon, so on screen the
			// weapon only comes and goes instead of moving up and down against a still view
			rvp.vieworigin[2] += split.p[player].bob;
			rvp.viewangles[PITCH] += split.p[player].punch;
		}

		{
			const double t0 = Platform_DoubleTime();

			GL_RenderFrame( &rvp );
			split_view_ms[player] += ( (float)(( Platform_DoubleTime() - t0 ) * 1000.0 ) - split_view_ms[player] ) * 0.1f;
		}

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

	if( wii_split_stats.value != 0.0f )
	{
		char line[96];
		int n = 0;

		split_frame_ms += ( (float)( host.realframetime * 1000.0 ) - split_frame_ms ) * 0.1f;

		for( i = 0; i < split.count; i++ )
			n += Q_snprintf( line + n, sizeof( line ) - n, "%sJ%d %.1f", i ? "  " : "", i + 1, split_view_ms[i] );

		Q_snprintf( line + n, sizeof( line ) - n, "  |  cuadro %.1f ms", split_frame_ms );

		CL_SplitFill( 4, 4, CL_SplitTextWidth( line ) + 8, CL_SplitTextHeight() + 4, 0, 0, 0, 170 );
		CL_SplitDrawText( 8, 6, line, 255, 255, 120, 255 );
	}

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
