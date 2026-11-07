/*
sv_split.c - local second player for split screen
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
#include "server.h"

/*
The second local player is a fake client of the listen server (the same kind
of client a bot is): it has an edict and runs usercmds, but nothing is sent to
it over the network. Its input comes from the second controller through
SV_SplitSetInput, and its camera is read straight from the edict, so it has no
network or interpolation delay.
*/

// HL weapon ids (dlls/weapons.h) in the order the D-pad cycles them
static const struct
{
	int		id;
	const char	*name;
} sv_split_weapons[] =
{
	{ 1,  "weapon_crowbar" },
	{ 2,  "weapon_9mmhandgun" },
	{ 3,  "weapon_357" },
	{ 4,  "weapon_9mmAR" },
	{ 7,  "weapon_shotgun" },
	{ 6,  "weapon_crossbow" },
	{ 8,  "weapon_rpg" },
	{ 9,  "weapon_gauss" },
	{ 10, "weapon_egon" },
	{ 11, "weapon_hornetgun" },
	{ 12, "weapon_handgrenade" },
	{ 14, "weapon_satchel" },
	{ 13, "weapon_tripmine" },
	{ 15, "weapon_snark" },
};

edict_t *sv_split_edict;

static vec3_t	sv_split_angles;
static float	sv_split_fmove, sv_split_smove, sv_split_upmove;
static word	sv_split_buttons;
static byte	sv_split_impulse;
static int	sv_split_weapon; // index in sv_split_weapons of the last selection

// diagnostics
static unsigned	sv_split_frames;	// usercmds run for player 2
static int	sv_split_lastmsec;
static unsigned	sv_split_pushed;	// consecutive frames with move input
static float	sv_split_moved;		// distance walked in those frames
static double	sv_split_nextwarn;

// angles the game asked for (fixangle) that the controller has to adopt
static vec3_t	sv_split_fixed;
static qboolean	sv_split_newangles;

// weapon animation player 2 is playing (svc_weaponanim is never delivered to a fake client)
static int	sv_split_animseq;
static int	sv_split_animbody;
static unsigned	sv_split_animcount;	// changes with every animation, even a repeated one

// viewmodel name -> model index, cached
static string_t	sv_split_vmname;
static int	sv_split_vmindex;
static int	sv_split_vmspawn;	// svs.spawncount the index belongs to

/*
=============
SV_SplitActive

true while the second player exists and is in the game
=============
*/
qboolean SV_SplitActive( void )
{
	sv_client_t *cl;

	if( !sv_split_edict || sv.state != ss_active )
		return false;

	cl = SV_ClientFromEdict( sv_split_edict, true );

	return cl != NULL && FBitSet( cl->flags, FCL_FAKECLIENT );
}

/*
=============
SV_SplitStart
=============
*/
qboolean SV_SplitStart( const char *name )
{
	if( SV_SplitActive( ))
		return true;

	sv_split_edict = NULL;

	if( sv.state != ss_active || svs.maxclients < 2 )
		return false;

	sv_split_edict = SV_AddLocalPlayer( name );

	if( !sv_split_edict )
		return false;

	VectorClear( sv_split_angles );
	sv_split_fmove = sv_split_smove = sv_split_upmove = 0.0f;
	sv_split_buttons = 0;
	sv_split_impulse = 0;
	sv_split_weapon = 1;
	sv_split_animseq = sv_split_animbody = 0;
	sv_split_vmname = 0;
	sv_split_vmindex = 0;

	return true;
}

/*
=============
SV_SplitStop
=============
*/
void SV_SplitStop( void )
{
	if( SV_SplitActive( ))
		SV_DropClient( SV_ClientFromEdict( sv_split_edict, true ), false );

	sv_split_edict = NULL;
}

/*
=============
SV_SplitSetInput

the latest controller state of player 2
=============
*/
void SV_SplitSetInput( const vec3_t angles, float fmove, float smove, float upmove, word buttons )
{
	VectorCopy( angles, sv_split_angles );
	sv_split_fmove = fmove;
	sv_split_smove = smove;
	sv_split_upmove = upmove;
	sv_split_buttons = buttons;
}

/*
=============
SV_SplitCycleWeapon

dir is +1 / -1; only weapons the player carries (pev->weapons) are used
=============
*/
void SV_SplitCycleWeapon( int dir )
{
	const int count = ARRAYSIZE( sv_split_weapons );
	sv_client_t *cl;
	char cmd[64];

	if( !SV_SplitActive( ))
		return;

	cl = SV_ClientFromEdict( sv_split_edict, true );

	for( int i = 1; i <= count; i++ )
	{
		int n = ( sv_split_weapon + dir * i + count * 2 ) % count;

		if( !FBitSet( sv_split_edict->v.weapons, 1 << sv_split_weapons[n].id ))
			continue;

		sv_split_weapon = n;
		Q_snprintf( cmd, sizeof( cmd ), "%s\n", sv_split_weapons[n].name );
		SV_ClientCommandFor( cl, cmd );
		return;
	}
}

/*
=============
SV_SplitFrame

called once per server frame, before the physics run: moves player 2
=============
*/
void SV_SplitFrame( void )
{
	vec3_t before;
	int msec;

	if( !SV_SplitActive( ))
		return;

	msec = bound( 1, (int)( sv.frametime * 1000.0 + 0.5 ), 100 );

	VectorCopy( sv_split_edict->v.origin, before );

	// The game sets fixangle on spawn, teleports and so on. A real client
	// acknowledges it (the server sends the angles and clears the flag); a fake
	// one never does, so it would stay set for ever. While it is set the engine
	// ignores the angles of the usercmd: the player would keep looking the way it
	// spawned, shots would fly along that direction and the model would not follow
	// the aim. Take the angles the game asked for and clear the flag.
	if( sv_split_edict->v.fixangle )
	{
		VectorCopy( sv_split_edict->v.angles, sv_split_fixed );
		VectorCopy( sv_split_fixed, sv_split_angles );
		sv_split_newangles = true;
		sv_split_edict->v.fixangle = 0;
	}

	SV_FakeClientMove( sv_split_edict, sv_split_angles, sv_split_fmove, sv_split_smove,
		sv_split_upmove, sv_split_buttons, sv_split_impulse, msec );

	sv_split_impulse = 0; // impulses are one shot

	// diagnostics: wii_split_info, and a warning when the stick does nothing
	sv_split_frames++;
	sv_split_lastmsec = msec;

	if( sv_split_fmove != 0.0f || sv_split_smove != 0.0f )
	{
		vec3_t d;

		VectorSubtract( sv_split_edict->v.origin, before, d );
		d[2] = 0.0f;
		sv_split_moved += VectorLength( d );
		sv_split_pushed++;

		if( sv_split_pushed >= 120 )
		{
			if( sv_split_moved < 1.0f && host.realtime > sv_split_nextwarn )
			{
				sv_split_nextwarn = host.realtime + 10.0;
				Con_Printf( S_WARN "wii_split: player 2 gets move input but does not move\n" );
				SV_SplitInfo_f( );
			}

			sv_split_pushed = 0;
			sv_split_moved = 0.0f;
		}
	}
	else
	{
		sv_split_pushed = 0;
		sv_split_moved = 0.0f;
	}
}

/*
=============
SV_SplitTakeAngles

view angles the game forced on player 2 (spawn, teleport); true once per change
=============
*/
qboolean SV_SplitTakeAngles( vec3_t angles )
{
	if( !sv_split_newangles )
		return false;

	sv_split_newangles = false;
	VectorCopy( sv_split_fixed, angles );

	return true;
}

/*
=============
SV_SplitSniffMessage

called for every message sent to a single client; keeps the weapon animations
the game sends to player 2
=============
*/
void SV_SplitSniffMessage( const edict_t *ent, sizebuf_t *msg )
{
	if( !sv_split_edict || ent != sv_split_edict || !SV_SplitActive( ))
		return;

	MSG_SeekToBit( msg, 0, SEEK_SET );

	if( MSG_ReadCmd( msg, NS_SERVER ) == svc_weaponanim )
	{
		sv_split_animseq = MSG_ReadByte( msg );
		sv_split_animbody = MSG_ReadByte( msg );
		sv_split_animcount++;
	}

	MSG_SeekToBit( msg, 0, SEEK_SET );
}

/*
=============
SV_SplitFindModel

index of an already precached model (SV_ModelIndex would register a missing one)
=============
*/
static int SV_SplitFindModel( const char *filename )
{
	char name[MAX_QPATH];
	int i;

	if( COM_StringEmptyOrNULL( filename ))
		return 0;

	if( *filename == '\\' || *filename == '/' )
		filename++;

	Q_strncpy( name, filename, sizeof( name ));
	COM_FixSlashes( name );

	for( i = 1; i < MAX_MODELS && sv.model_precache[i][0]; i++ )
	{
		if( !Q_stricmp( sv.model_precache[i], name ))
			return i;
	}

	return 0;
}

/*
=============
SV_SplitGetViewModel

first person weapon of player 2: model index, sequence, body, and a counter that
changes every time the game restarts the animation
=============
*/
qboolean SV_SplitGetViewModel( int *modelindex, int *sequence, int *body, unsigned *animcount )
{
	string_t name;

	if( !SV_SplitActive( ))
		return false;

	name = sv_split_edict->v.viewmodel;

	if( !name )
	{
		sv_split_vmname = 0;
		sv_split_vmindex = 0;
		return false;
	}

	if( name != sv_split_vmname || sv_split_vmspawn != svs.spawncount )
	{
		sv_split_vmname = name;
		sv_split_vmspawn = svs.spawncount;
		sv_split_vmindex = SV_SplitFindModel( svgame.globals->pStringBase + name );
	}

	if( sv_split_vmindex <= 0 )
		return false;

	if( modelindex ) *modelindex = sv_split_vmindex;
	if( sequence ) *sequence = sv_split_animseq;
	if( body ) *body = sv_split_animbody;
	if( animcount ) *animcount = sv_split_animcount;

	return true;
}

/*
=============
SV_SplitInfo_f

wii_split_info: what the server knows about player 2 (for chasing bugs)
=============
*/
void SV_SplitInfo_f( void )
{
	edict_t *e = sv_split_edict;
	sv_client_t *cl;

	if( !SV_SplitActive( ))
	{
		Con_Printf( "wii_split_info: player 2 is not active\n" );
		return;
	}

	cl = SV_ClientFromEdict( e, true );

	Con_Printf( "P2: edict %d state %d health %.0f deadflag %d movetype %d solid %d\n",
		NUM_FOR_EDICT( e ), cl ? (int)cl->state : -1, e->v.health, e->v.deadflag, e->v.movetype, e->v.solid );
	Con_Printf( "P2: origin %.1f %.1f %.1f velocity %.1f %.1f %.1f\n",
		e->v.origin[0], e->v.origin[1], e->v.origin[2], e->v.velocity[0], e->v.velocity[1], e->v.velocity[2] );
	Con_Printf( "P2: v_angle %.1f %.1f flags 0x%x maxspeed %.1f gravity %.2f friction %.2f iuser1 %d\n",
		e->v.v_angle[0], e->v.v_angle[1], (unsigned)e->v.flags, e->v.maxspeed, e->v.gravity, e->v.friction, e->v.iuser1 );
	Con_Printf( "P2: button 0x%x oldbuttons 0x%x impulse %d weapons 0x%x\n",
		(unsigned)e->v.button, (unsigned)e->v.oldbuttons, e->v.impulse, e->v.weapons );
	Con_Printf( "P2: input fmove %.0f smove %.0f buttons 0x%x, %u frames run (last %d ms), moved %.1f of %u pushed frames\n",
		sv_split_fmove, sv_split_smove, (unsigned)sv_split_buttons, sv_split_frames, sv_split_lastmsec,
		sv_split_moved, sv_split_pushed );
}

/*
=============
SV_SplitGetView

what the renderer needs to draw player 2's view; false while not available
=============
*/
qboolean SV_SplitGetView( vec3_t origin, vec3_t punch, int *index, float *health, float *armor, float *frags )
{
	const entvars_t *v;

	if( !SV_SplitActive( ))
		return false;

	v = &sv_split_edict->v;

	if( origin ) VectorAdd( v->origin, v->view_ofs, origin );
	if( punch ) VectorCopy( v->punchangle, punch );

	if( index ) *index = NUM_FOR_EDICT( sv_split_edict );
	if( health ) *health = v->health;
	if( armor ) *armor = v->armorvalue;
	if( frags ) *frags = v->frags;

	return true;
}
