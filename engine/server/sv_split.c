/*
sv_split.c - split screen, server side (players 2 to 4)
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
The extra local players are fake clients of the listen server (the same kind
of client a bot is): they have an edict and run usercmds, but nothing is sent
to them over the network. Their input comes from the other controllers through
SV_SplitSetInput, and their camera is read straight from the edict, so there is
no network or interpolation delay.

Players are numbered like the screen: 0 is the normal client, 1 to 3 are the
fake ones handled here.
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

#define SPLIT_EXTRA	( SPLIT_MAX_PLAYERS - 1 )
#define SPLIT_WEAPONS	32
#define SPLIT_AMMO	32

typedef struct
{
	edict_t		*edict;

	vec3_t		angles;
	float		fmove, smove, upmove;
	word		buttons;
	byte		impulse;
	int		weapon;		// index in sv_split_weapons of the last selection

	// diagnostics
	unsigned	frames;		// usercmds run
	int		lastmsec;
	unsigned	pushed;		// consecutive frames with move input
	float		moved;		// distance walked in those frames
	double		nextwarn;

	// angles the game asked for (fixangle) that the controller has to adopt
	vec3_t		fixed;
	qboolean	newangles;

	// weapon animation (svc_weaponanim is never delivered to a fake client)
	int		animseq;
	int		animbody;
	unsigned	animcount;	// changes with every animation, even a repeated one

	// viewmodel name -> model index, cached
	string_t	vmname;
	int		vmindex;
	int		vmspawn;	// svs.spawncount the index belongs to

	// what the HUD messages said (CurWeapon, AmmoX, WeaponList)
	int		curweapon;
	int		clip;
	int		ammo[SPLIT_AMMO];
	int		wammo1[SPLIT_WEAPONS];	// weapon id -> primary ammo type
	int		wammo2[SPLIT_WEAPONS];	// weapon id -> secondary ammo type
	char		wname[SPLIT_WEAPONS][32];	// weapon id -> "weapon_9mmhandgun"
	qboolean	flashon;	// flashlight
	int		flashbat;	// 0 to 100

	// the game plays the firing animation of a weapon from a client event, which nobody
	// receives for a fake client: count the events the player causes instead
	unsigned	firecount;
	qboolean	firealt;	// the shot came with the secondary attack button
} sv_splitp_t;

static sv_splitp_t sv_split[SPLIT_EXTRA];

// autoaim strength in percent per player (0 to 100), < 0 = the server's sv_aim
static float sv_split_aim[SPLIT_MAX_PLAYERS] = { -1.0f, -1.0f, -1.0f, -1.0f };

static sv_splitp_t *SV_SplitSlot( int player )
{
	if( player < 1 || player >= SPLIT_MAX_PLAYERS )
		return NULL;

	return &sv_split[player - 1];
}

/*
=============
SV_SplitActive

true while that player exists and is in the game
=============
*/
qboolean SV_SplitActive( int player )
{
	sv_splitp_t *p = SV_SplitSlot( player );
	sv_client_t *cl;

	if( !p || !p->edict || sv.state != ss_active )
		return false;

	cl = SV_ClientFromEdict( p->edict, true );

	return cl != NULL && FBitSet( cl->flags, FCL_FAKECLIENT );
}

edict_t *SV_SplitEdict( int player )
{
	return SV_SplitActive( player ) ? SV_SplitSlot( player )->edict : NULL;
}

// which player an edict is: 0 for the local client, 1-3 for the fake ones, -1 for anyone else
int SV_SplitPlayerOf( const edict_t *ent )
{
	int i;

	if( !ent )
		return -1;

	for( i = 1; i < SPLIT_MAX_PLAYERS; i++ )
	{
		if( SV_SplitSlot( i )->edict == ent )
			return i;
	}

	if( NUM_FOR_EDICT( ent ) == 1 && !FBitSet( ent->v.flags, FL_FAKECLIENT ))
		return 0;

	return -1;
}

/*
=============
SV_SplitStart
=============
*/
qboolean SV_SplitStart( int player, const char *name, const char *model )
{
	sv_splitp_t *p = SV_SplitSlot( player );
	int i;

	if( !p )
		return false;

	if( SV_SplitActive( player ))
		return true;

	p->edict = NULL;

	if( sv.state != ss_active || svs.maxclients < 2 )
		return false;

	p->edict = SV_AddLocalPlayer( name, model, player );

	if( !p->edict )
		return false;

	VectorClear( p->angles );
	p->fmove = p->smove = p->upmove = 0.0f;
	p->buttons = 0;
	p->impulse = 0;
	p->weapon = 1;
	p->animseq = p->animbody = 0;
	p->firecount = 0;
	p->flashon = false;
	p->flashbat = 100;
	p->vmname = 0;
	p->vmindex = 0;
	p->curweapon = 0;
	p->clip = -1;
	p->newangles = false;
	p->frames = 0;
	p->pushed = 0;
	p->moved = 0.0f;
	memset( p->ammo, 0, sizeof( p->ammo ));

	for( i = 0; i < SPLIT_WEAPONS; i++ )
		p->wammo1[i] = p->wammo2[i] = -1;

	return true;
}

/*
=============
SV_SplitStop
=============
*/
void SV_SplitStop( int player )
{
	sv_splitp_t *p = SV_SplitSlot( player );

	if( !p )
		return;

	if( SV_SplitActive( player ))
		SV_DropClient( SV_ClientFromEdict( p->edict, true ), false );

	p->edict = NULL;
}

void SV_SplitStopAll( void )
{
	int i;

	for( i = 1; i < SPLIT_MAX_PLAYERS; i++ )
		SV_SplitStop( i );

	SV_SplitClearScores( );
}

/*
=============
SV_SplitSetInput

the latest controller state of a player
=============
*/
void SV_SplitSetInput( int player, const vec3_t angles, float fmove, float smove, float upmove, word buttons )
{
	sv_splitp_t *p = SV_SplitSlot( player );

	if( !p )
		return;

	VectorCopy( angles, p->angles );
	p->fmove = fmove;
	p->smove = smove;
	p->upmove = upmove;
	p->buttons = buttons;
}

/*
=============
SV_SplitCycleWeapon

dir is +1 / -1; only weapons the player carries (pev->weapons) are used
=============
*/
void SV_SplitCycleWeapon( int player, int dir )
{
	const int count = ARRAYSIZE( sv_split_weapons );
	sv_splitp_t *p = SV_SplitSlot( player );
	sv_client_t *cl;
	char cmd[64];

	if( !p || !SV_SplitActive( player ))
		return;

	cl = SV_ClientFromEdict( p->edict, true );

	for( int i = 1; i <= count; i++ )
	{
		int n = ( p->weapon + dir * i + count * 2 ) % count;

		if( !FBitSet( p->edict->v.weapons, 1 << sv_split_weapons[n].id ))
			continue;

		p->weapon = n;
		Q_snprintf( cmd, sizeof( cmd ), "%s\n", sv_split_weapons[n].name );
		SV_ClientCommandFor( cl, cmd );
		return;
	}
}

/*
=============
SV_SplitOneFrame
=============
*/
static void SV_SplitOneFrame( int player, sv_splitp_t *p )
{
	vec3_t before;
	int msec;

	msec = bound( 1, (int)( sv.frametime * 1000.0 + 0.5 ), 100 );

	VectorCopy( p->edict->v.origin, before );

	// The game sets fixangle on spawn, teleports and so on. A real client
	// acknowledges it (the server sends the angles and clears the flag); a fake
	// one never does, so it would stay set for ever. While it is set the engine
	// ignores the angles of the usercmd: the player would keep looking the way it
	// spawned, shots would fly along that direction and the model would not follow
	// the aim. Take the angles the game asked for and clear the flag.
	if( p->edict->v.fixangle )
	{
		VectorCopy( p->edict->v.angles, p->fixed );
		VectorCopy( p->fixed, p->angles );
		p->newangles = true;
		p->edict->v.fixangle = 0;
	}

	SV_FakeClientMove( p->edict, p->angles, p->fmove, p->smove, p->upmove, p->buttons, p->impulse, msec );

	p->impulse = 0; // impulses are one shot

	// diagnostics: wii_split_info, and a warning when the stick does nothing
	p->frames++;
	p->lastmsec = msec;

	if( p->fmove != 0.0f || p->smove != 0.0f )
	{
		vec3_t d;

		VectorSubtract( p->edict->v.origin, before, d );
		d[2] = 0.0f;
		p->moved += VectorLength( d );
		p->pushed++;

		if( p->pushed >= 120 )
		{
			if( p->moved < 1.0f && host.realtime > p->nextwarn )
			{
				p->nextwarn = host.realtime + 10.0;
				Con_Printf( S_WARN "wii_split: player %d gets move input but does not move\n", player + 1 );
				SV_SplitPrintInfo( player );
			}

			p->pushed = 0;
			p->moved = 0.0f;
		}
	}
	else
	{
		p->pushed = 0;
		p->moved = 0.0f;
	}
}

/*
=============
SV_SplitFrame

called once per server frame, before the physics run: moves the extra players
=============
*/
void SV_SplitFrame( void )
{
	int i;

	for( i = 1; i < SPLIT_MAX_PLAYERS; i++ )
	{
		if( SV_SplitActive( i ))
			SV_SplitOneFrame( i, SV_SplitSlot( i ));
	}
}

/*
=============
SV_SplitTakeAngles

view angles the game forced on a player (spawn, teleport); true once per change
=============
*/
qboolean SV_SplitTakeAngles( int player, vec3_t angles )
{
	sv_splitp_t *p = SV_SplitSlot( player );

	if( !p || !p->newangles )
		return false;

	p->newangles = false;
	VectorCopy( p->fixed, angles );

	return true;
}

/*
=============
SV_SplitSniffScore

keeps the frags and deaths of every player out of the ScoreInfo broadcast, so the
table of a dead extra player can be drawn without the client DLL
=============
*/
static struct
{
	short	frags, deaths;
	qboolean	valid;
} sv_split_score[MAX_CLIENTS];

void SV_SplitSniffScore( sizebuf_t *msg )
{
	const char *name = NULL;
	int i, cmd, size = 0, idx;

	for( i = 1; i < SPLIT_MAX_PLAYERS; i++ )
		if( SV_SplitActive( i )) break;

	if( i >= SPLIT_MAX_PLAYERS )
		return;

	MSG_SeekToBit( msg, 0, SEEK_SET );

	if( MSG_GetNumBitsWritten( msg ) < 16 )
		return;

	cmd = MSG_ReadCmd( msg, NS_SERVER );

	if( cmd <= svc_lastmsg )
		return;

	for( i = 0; i < MAX_USER_MESSAGES; i++ )
	{
		if( svgame.msg[i].number == cmd )
		{
			name = svgame.msg[i].name;
			size = svgame.msg[i].size;
			break;
		}
	}

	if( !name || Q_strcmp( name, "ScoreInfo" ))
		return;

	if( size == -1 )
		MSG_ReadWord( msg );

	if( MSG_GetNumBitsLeft( msg ) < 40 )
		return;

	idx = MSG_ReadByte( msg ) - 1; // client index + 1

	if( idx < 0 || idx >= MAX_CLIENTS )
		return;

	sv_split_score[idx].frags = (short)MSG_ReadShort( msg );
	sv_split_score[idx].deaths = (short)MSG_ReadShort( msg );
	sv_split_score[idx].valid = true;
}

qboolean SV_SplitGetScore( int client, int *frags, int *deaths )
{
	if( client < 0 || client >= MAX_CLIENTS || !sv_split_score[client].valid )
		return false;

	if( frags ) *frags = sv_split_score[client].frags;
	if( deaths ) *deaths = sv_split_score[client].deaths;

	return true;
}

void SV_SplitClearScores( void )
{
	memset( sv_split_score, 0, sizeof( sv_split_score ));
}

/*
=============
SV_SplitSniffMessage

called for every message sent to a single client; keeps what the game tells a
player's HUD (weapon animations, ammo) because nobody reads the messages of a
fake client
=============
*/
void SV_SplitSniffMessage( const edict_t *ent, sizebuf_t *msg )
{
	const int player = SV_SplitPlayerOf( ent );
	sv_splitp_t *p;
	int cmd;

	if( player < 1 || !SV_SplitActive( player ))
		return;

	p = SV_SplitSlot( player );

	MSG_SeekToBit( msg, 0, SEEK_SET );
	cmd = MSG_ReadCmd( msg, NS_SERVER );

	if( cmd == svc_weaponanim )
	{
		p->animseq = MSG_ReadByte( msg );
		p->animbody = MSG_ReadByte( msg );
		p->animcount++;
	}
	else if( cmd > svc_lastmsg )
	{
		const char *name = NULL;
		int i, size = 0;

		for( i = 0; i < MAX_USER_MESSAGES; i++ )
		{
			if( svgame.msg[i].number == cmd )
			{
				name = svgame.msg[i].name;
				size = svgame.msg[i].size;
				break;
			}
		}

		if( name && size == -1 )
			MSG_ReadWord( msg ); // variable length messages carry their size

		if( !name )
		{
		}
		else if( !Q_strcmp( name, "CurWeapon" ))
		{
			const int state = MSG_ReadByte( msg );
			const int id = MSG_ReadByte( msg );
			const int clip = (signed char)MSG_ReadByte( msg );

			// state 0 is the report of a weapon that is not in the hand
			if( state != 0 )
			{
				p->curweapon = id;
				p->clip = clip;
			}
		}
		else if( !Q_strcmp( name, "AmmoX" ))
		{
			const int idx = MSG_ReadByte( msg );
			const int amount = MSG_ReadByte( msg );

			if( idx >= 0 && idx < SPLIT_AMMO )
				p->ammo[idx] = amount;
		}
		else if( !Q_strcmp( name, "WeaponList" ))
		{
			char wname[32];
			int a1, a2, id;

			Q_strncpy( wname, MSG_ReadString( msg ), sizeof( wname )); // name
			a1 = (signed char)MSG_ReadByte( msg );	// primary ammo type
			MSG_ReadByte( msg );			// max
			a2 = (signed char)MSG_ReadByte( msg );	// secondary ammo type
			MSG_ReadByte( msg );			// max
			MSG_ReadByte( msg );			// slot
			MSG_ReadByte( msg );			// position
			id = MSG_ReadByte( msg );

			if( id > 0 && id < SPLIT_WEAPONS )
			{
				p->wammo1[id] = a1;
				p->wammo2[id] = a2;
				Q_strncpy( p->wname[id], wname, sizeof( p->wname[0] ));
			}
		}
		else if( !Q_strcmp( name, "Flashlight" ))
		{
			p->flashon = MSG_ReadByte( msg ) != 0;
			p->flashbat = MSG_ReadByte( msg ) * 100 / 255;
		}
		else if( !Q_strcmp( name, "FlashBat" ))
			p->flashbat = MSG_ReadByte( msg ) * 100 / 255;
	}

	MSG_SeekToBit( msg, 0, SEEK_SET );
}

/*
=============
SV_SplitGetAmmo

weapon the player holds, rounds in the clip (-1 = no clip) and the reserve of
its primary and secondary ammo (-1 = none)
=============
*/
qboolean SV_SplitGetAmmo( int player, int *weapon, int *clip, int *reserve1, int *reserve2 )
{
	sv_splitp_t *p = SV_SplitSlot( player );
	int a1 = -1, a2 = -1, id;

	if( !p || !SV_SplitActive( player ))
		return false;

	id = p->curweapon;

	if( id > 0 && id < SPLIT_WEAPONS )
	{
		a1 = p->wammo1[id];
		a2 = p->wammo2[id];
	}

	if( weapon ) *weapon = id;
	if( clip ) *clip = p->clip;
	if( reserve1 ) *reserve1 = ( a1 >= 0 && a1 < SPLIT_AMMO ) ? p->ammo[a1] : -1;
	if( reserve2 ) *reserve2 = ( a2 >= 0 && a2 < SPLIT_AMMO ) ? p->ammo[a2] : -1;

	return id > 0;
}

/*
=============
SV_SplitGetWeaponInfo

name of the weapon the player holds ("weapon_9mmhandgun"), and whether it uses
primary / secondary ammo
=============
*/
qboolean SV_SplitGetWeaponInfo( int player, char *name, size_t size, qboolean *ammo1, qboolean *ammo2 )
{
	sv_splitp_t *p = SV_SplitSlot( player );
	int id;

	if( !p || !SV_SplitActive( player ))
		return false;

	id = p->curweapon;

	if( id <= 0 || id >= SPLIT_WEAPONS )
		return false;

	if( name ) Q_strncpy( name, p->wname[id], size );
	if( ammo1 ) *ammo1 = p->wammo1[id] > 0;
	if( ammo2 ) *ammo2 = p->wammo2[id] > 0;

	return true;
}

/*
=============
SV_SplitGetFlashlight

flashlight state of a player: on / off and the charge in percent
=============
*/
void SV_SplitGetFlashlight( int player, qboolean *on, int *charge )
{
	sv_splitp_t *p = SV_SplitSlot( player );

	if( on ) *on = ( p && p->flashon );
	if( charge ) *charge = p ? p->flashbat : 100;
}

/*
=============
SV_SplitNoteEvent

called for every event the game plays: remembers the ones a split screen
player's weapon causes, so its view model can play the firing animation
=============
*/
void SV_SplitNoteEvent( const edict_t *invoker, const char *eventname )
{
	const int player = SV_SplitPlayerOf( invoker );
	sv_splitp_t *p;

	if( player < 1 || !SV_SplitActive( player ) || COM_StringEmptyOrNULL( eventname ))
		return;

	p = SV_SplitSlot( player );

	// only the events of a weapon being fired (not the ones that stop a beam, spin a cannon, ...)
	if( !FBitSet( p->buttons, IN_ATTACK|IN_ATTACK2 ))
		return;

	if( Q_stristr( eventname, "stop" ) || Q_stristr( eventname, "spin" ) || Q_stristr( eventname, "decal" ))
		return;

	p->firealt = FBitSet( p->buttons, IN_ATTACK2 ) && !FBitSet( p->buttons, IN_ATTACK );
	p->firecount++;
}

/*
=============
SV_SplitGetFire

how many shots the player has fired so far, and whether the last one was a
secondary attack
=============
*/
unsigned SV_SplitGetFire( int player, qboolean *alt )
{
	sv_splitp_t *p = SV_SplitSlot( player );

	if( !p )
		return 0;

	if( alt ) *alt = p->firealt;

	return p->firecount;
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

first person weapon of a player: model index, sequence, body, and a counter that
changes every time the game restarts the animation
=============
*/
qboolean SV_SplitGetViewModel( int player, int *modelindex, int *sequence, int *body, unsigned *animcount )
{
	sv_splitp_t *p = SV_SplitSlot( player );
	string_t name;

	if( !p || !SV_SplitActive( player ))
		return false;

	name = p->edict->v.viewmodel;

	if( !name )
	{
		p->vmname = 0;
		p->vmindex = 0;
		return false;
	}

	if( name != p->vmname || p->vmspawn != svs.spawncount )
	{
		p->vmname = name;
		p->vmspawn = svs.spawncount;
		p->vmindex = SV_SplitFindModel( svgame.globals->pStringBase + name );
	}

	if( p->vmindex <= 0 )
		return false;

	if( modelindex ) *modelindex = p->vmindex;
	if( sequence ) *sequence = p->animseq;
	if( body ) *body = p->animbody;
	if( animcount ) *animcount = p->animcount;

	return true;
}

/*
=============
SV_SplitGetFov

field of view the game set for a player (crossbow, scope), 0 = the default
=============
*/
float SV_SplitGetFov( int player )
{
	sv_splitp_t *p = SV_SplitSlot( player );

	if( !p || !SV_SplitActive( player ))
		return 0.0f;

	return p->edict->v.fov;
}

/*
=============
SV_SplitGetView

what a player's camera and HUD need
=============
*/
qboolean SV_SplitGetView( int player, vec3_t origin, vec3_t punch, int *index, float *health, float *armor, float *frags )
{
	sv_splitp_t *p = SV_SplitSlot( player );
	const entvars_t *v;

	if( !p || !SV_SplitActive( player ))
		return false;

	v = &p->edict->v;

	if( origin ) VectorAdd( v->origin, v->view_ofs, origin );
	if( punch ) VectorCopy( v->punchangle, punch );

	if( index ) *index = NUM_FOR_EDICT( p->edict );
	if( health ) *health = v->health;
	if( armor ) *armor = v->armorvalue;
	if( frags ) *frags = v->frags;

	return true;
}

/*
=============
SV_SplitGetVelocity

horizontal speed, for the bob of the weapon
=============
*/
float SV_SplitGetSpeed( int player, qboolean *onground )
{
	sv_splitp_t *p = SV_SplitSlot( player );

	if( !p || !SV_SplitActive( player ))
		return 0.0f;

	if( onground )
		*onground = FBitSet( p->edict->v.flags, FL_ONGROUND ) != 0;

	return sqrtf( p->edict->v.velocity[0] * p->edict->v.velocity[0] + p->edict->v.velocity[1] * p->edict->v.velocity[1] );
}

/*
=============
SV_SplitSetAutoaim

autoaim strength of a player in percent (0 = off), < 0 = the server default
=============
*/
void SV_SplitSetAutoaim( int player, float percent )
{
	if( player >= 0 && player < SPLIT_MAX_PLAYERS )
		sv_split_aim[player] = percent;
}

/*
=============
SV_SplitAimThreshold

the cosine the best autoaim target has to beat (1 = never): the default of the
server unless the player picked its own strength
=============
*/
float SV_SplitAimThreshold( const edict_t *ent, float def )
{
	const int player = SV_SplitPlayerOf( ent );

	if( player < 0 || sv_split_aim[player] < 0.0f )
		return def;

	return 1.0f - 0.2f * bound( 0.0f, sv_split_aim[player], 100.0f ) / 100.0f;
}

/*
=============
SV_SplitSeqName

name of a sequence of the model of an edict
=============
*/
static const char *SV_SplitSeqName( const edict_t *e, int seq )
{
	const studiohdr_t *hdr = (const studiohdr_t *)Mod_StudioExtradata( SV_ModelHandle( e->v.modelindex ));

	if( !hdr || seq < 0 || seq >= hdr->numseq )
		return "?";

	return ((const mstudioseqdesc_t *)((const byte *)hdr + hdr->seqindex ))[seq].label;
}

/*
=============
SV_SplitPrintAnim

what the game sets for the third person animation of a player (the client
draws the character from these)
=============
*/
static void SV_SplitPrintAnim( const char *tag, const edict_t *e )
{
	// the sequences the game looks up by name (CBasePlayer::SetAnimation): -1 = the model
	// does not have it and the character falls back to sequence 0
	static const char *names[] = { "ref_aim_crowbar", "ref_shoot_crowbar", "ref_aim_onehanded", "ref_shoot_onehanded", "deep_idle", "die_simple" };
	const studiohdr_t *hdr = (const studiohdr_t *)Mod_StudioExtradata( SV_ModelHandle( e->v.modelindex ));
	char found[160] = "";
	size_t n = 0;

	for( size_t k = 0; k < ARRAYSIZE( names ) && n < sizeof( found ); k++ )
	{
		int idx = -1;

		for( int j = 0; hdr && j < hdr->numseq; j++ )
		{
			if( !Q_stricmp( ((const mstudioseqdesc_t *)((const byte *)hdr + hdr->seqindex ))[j].label, names[k] ))
			{
				idx = j;
				break;
			}
		}

		n += Q_snprintf( found + n, sizeof( found ) - n, "%s%s=%d", k ? " " : "", names[k], idx );
	}

	Con_Printf( "%s: model data %s, %d sequences; %s\n", tag, hdr ? "loaded" : "MISSING", hdr ? hdr->numseq : 0, found );
	Con_Printf( "%s: model %s (index %d) seq %d \"%s\" gait %d \"%s\" frame %.1f framerate %.2f\n",
		tag, SV_GetString( e->v.model ), e->v.modelindex, e->v.sequence, SV_SplitSeqName( e, e->v.sequence ),
		e->v.gaitsequence, SV_SplitSeqName( e, e->v.gaitsequence ), e->v.frame, e->v.framerate );
	Con_Printf( "%s: animtime - sv.time = %.3f s, effects 0x%x, weaponmodel \"%s\", body %d, deadflag %d, health %.0f\n",
		tag, e->v.animtime - sv.time, (unsigned)e->v.effects, SV_GetString( e->v.weaponmodel ), e->v.body,
		e->v.deadflag, e->v.health );
}

/*
=============
SV_SplitPrintInfo
=============
*/
void SV_SplitPrintInfo( int player )
{
	sv_splitp_t *p = SV_SplitSlot( player );
	const edict_t *e;
	sv_client_t *cl;
	int w;

	if( !p || !SV_SplitActive( player ))
	{
		Con_Printf( "wii_split_info: player %d is not active\n", player + 1 );
		return;
	}

	e = p->edict;
	cl = SV_ClientFromEdict( (edict_t *)e, true );
	w = player + 1;

	Con_Printf( "P%d: edict %d state %d health %.0f deadflag %d movetype %d solid %d\n",
		w, NUM_FOR_EDICT( e ), cl ? (int)cl->state : -1, e->v.health, e->v.deadflag, e->v.movetype, e->v.solid );
	Con_Printf( "P%d: origin %.1f %.1f %.1f velocity %.1f %.1f %.1f\n",
		w, e->v.origin[0], e->v.origin[1], e->v.origin[2], e->v.velocity[0], e->v.velocity[1], e->v.velocity[2] );
	Con_Printf( "P%d: v_angle %.1f %.1f flags 0x%x maxspeed %.1f gravity %.2f friction %.2f iuser1 %d\n",
		w, e->v.v_angle[0], e->v.v_angle[1], (unsigned)e->v.flags, e->v.maxspeed, e->v.gravity, e->v.friction, e->v.iuser1 );
	Con_Printf( "P%d: button 0x%x oldbuttons 0x%x impulse %d weapons 0x%x\n",
		w, (unsigned)e->v.button, (unsigned)e->v.oldbuttons, e->v.impulse, e->v.weapons );
	Con_Printf( "P%d: input fmove %.0f smove %.0f buttons 0x%x, %u frames run (last %d ms), moved %.1f of %u pushed frames\n",
		w, p->fmove, p->smove, (unsigned)p->buttons, p->frames, p->lastmsec, p->moved, p->pushed );
	Con_Printf( "P%d: weapon id %d clip %d\n", w, p->curweapon, p->clip );

	{
		char tag[8];

		Q_snprintf( tag, sizeof( tag ), "P%d", w );
		SV_SplitPrintAnim( tag, e );
	}
}

// wii_split_info [player]
void SV_SplitInfo_f( void )
{
	int i;

	if( Cmd_Argc() > 1 )
	{
		SV_SplitPrintInfo( bound( 1, Q_atoi( Cmd_Argv( 1 )) - 1, SPLIT_MAX_PLAYERS - 1 ));
		return;
	}

	// player 1 is a normal client: the same values for comparison
	if( sv.state == ss_active && svs.maxclients > 0 && SV_IsValidEdict( svs.clients[0].edict ))
		SV_SplitPrintAnim( "P1", svs.clients[0].edict );

	for( i = 1; i < SPLIT_MAX_PLAYERS; i++ )
	{
		if( SV_SplitActive( i ))
			SV_SplitPrintInfo( i );
	}
}
