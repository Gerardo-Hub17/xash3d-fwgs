Copyright (C) 2026

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

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
static vec3_t	sv_split_angles;
static float	sv_split_fmove, sv_split_smove, sv_split_upmove;
static word	sv_split_buttons;
static byte	sv_split_impulse;
static int	sv_split_weapon; // index in sv_split_weapons of the last selection

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

	return true;
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
/*
=============
SV_SplitFrame

called once per server frame, before the physics run: moves player 2
=============
*/
void SV_SplitFrame( void )
{
	int msec;

	if( !SV_SplitActive( ))
		return;

	msec = bound( 1, (int)( sv.frametime * 1000.0 + 0.5 ), 100 );

	SV_FakeClientMove( sv_split_edict, sv_split_angles, sv_split_fmove, sv_split_smove,
		sv_split_upmove, sv_split_buttons, sv_split_impulse, msec );

	sv_split_impulse = 0; // impulses are one shot
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