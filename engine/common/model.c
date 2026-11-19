/*
model.c - modelloader
Copyright (C) 2007 Uncle Mike

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
#include "platform/platform.h"
#include "mod_local.h"
#include "sprite.h"
#include "xash3d_mathlib.h"
#include "alias.h"
#include "studio.h"
#include "wadfile.h"
#include "world.h"
#include "enginefeatures.h"
#include "client.h"
#include "server.h"

static model_info_t	mod_crcinfo[MAX_MODELS];
static model_t	mod_known[MAX_MODELS];
static int	mod_numknown = 0;
poolhandle_t      com_studiocache;		// cache for submodels
CVAR_DEFINE( mod_studiocache, "r_studiocache", "1", FCVAR_ARCHIVE, "enables studio cache for speedup tracing hitboxes" );
CVAR_DEFINE_AUTO( r_wadtextures, "0", FCVAR_LATCH, "completely ignore textures in the bsp-file if enabled" );
CVAR_DEFINE_AUTO( r_showhull, "0", 0, "draw collision hulls 1-3" );
CVAR_DEFINE_AUTO( r_allow_wad3_luma, "0", FCVAR_LATCH|FCVAR_ARCHIVE, "allow usage of luma textures in wad3 (tilde textures)" );

/*
===============================================================================

			MOD COMMON UTILS

===============================================================================
*/
/*
================
Mod_Modellist_f
================
*/
static void Mod_Modellist_f( void )
{
	int	i, nummodels;
	model_t	*mod;

	Con_Printf( "\n" );
	Con_Printf( "-----------------------------------\n" );

	for( i = nummodels = 0, mod = mod_known; i < mod_numknown; i++, mod++ )
	{
		const char *color_str;

		if( mod->needload == NL_UNREFERENCED )
			continue; // free slot

		switch( mod->type )
		{
		case mod_alias:
			color_str = S_YELLOW;
			break;
		case mod_studio:
			color_str = S_GREEN;
			break;
		case mod_sprite:
			color_str = S_MAGENTA;
			break;
		case mod_brush:
			color_str = mod->name[0] == '*' ? S_CYAN : S_BLUE;
			break;
		default:
			color_str = S_RED;
			break;
		}

		Con_Printf( "%3d:\t%s%s\n" S_DEFAULT, i, color_str, mod->name );
		nummodels++;
	}

	Con_Printf( "-----------------------------------\n" );
	Con_Printf( "%i total models, %i total allocated slots\n", nummodels, mod_numknown );
	Con_Printf( "\n" );
}

static void Mod_UnloadRenderData( model_t *mod )
{
#if !XASH_DEDICATED
	switch( mod->type )
	{
	case mod_sprite:
		Mod_SpriteUnloadTextures( mod->cache.data );
		break;
	default:
		break;
	}

	ref.dllFuncs.Mod_ProcessRenderData( mod, false, NULL, 0 );
#endif
}

/*
================
Mod_FreeUserData
================
*/
static void Mod_FreeUserData( model_t *mod )
{
	// ignore submodels and freed models
	if( mod->needload == NL_UNREFERENCED || mod->name[0] == '*' )
		return;

	if( Host_IsDedicated() )
	{
		if( svgame.physFuncs.Mod_ProcessUserData != NULL )
		{
			// let the server.dll free custom data
			svgame.physFuncs.Mod_ProcessUserData( mod, false, NULL );
		}
	}
	else
	{
		Mod_UnloadRenderData( mod );
	}
}

/*
================
Mod_FreeModel
================
*/
void Mod_FreeModel( model_t *mod )
{
	// already freed?
	if( !mod || mod->needload == NL_UNREFERENCED )
		return;

#if XASH_OGC
	if( mod->type == mod_studio )
	{
		extern void Mod_FreeSeqGroups( model_t *mod );
		Mod_FreeSeqGroups( mod );
	}
#endif

	if( mod->type != mod_brush || mod->name[0] != '*' )
	{
		Mod_FreeUserData( mod );
		Mem_FreePool( &mod->mempool );
	}

	if( mod->type == mod_brush && FBitSet( mod->flags, MODEL_WORLD ) )
	{
		world.version = 0;
		world.shadowdata = NULL;
		world.deluxedata = NULL;

		// data already freed by Mem_FreePool above
		world.hull_models = NULL;
		world.compressed_phs = NULL;
		world.phsofs = NULL;
	}

	memset( mod, 0, sizeof( *mod ));
}

/*
===============================================================================

			MODEL INITIALIZE\SHUTDOWN

===============================================================================
*/
/*
================
Mod_Init
================
*/
void Mod_Init( void )
{
	com_studiocache = Mem_AllocPool( "Studio Cache" );
	Cvar_RegisterVariable( &mod_studiocache );
	Cvar_RegisterVariable( &r_wadtextures );
	Cvar_RegisterVariable( &r_showhull );
	Cvar_RegisterVariable( &r_allow_wad3_luma );

	Cmd_AddCommand( "mapstats", Mod_PrintWorldStats_f, "show stats for currently loaded map" );
	Cmd_AddCommand( "modellist", Mod_Modellist_f, "display loaded models list" );
#if XASH_OGC
	{
		extern convar_t wii_preload_anims;
		extern void Mod_MapCRC_f( void );
		extern convar_t wii_preload_kb, wii_preload_reserve;
		extern convar_t wii_cs_sharemodels, wii_cs_modelreserve, wii_cs_modelfuture, wii_cs_ctmodel, wii_cs_tmodel;
		extern convar_t wii_lazy_models, wii_lazy_kb;
		extern convar_t wii_share_anims, wii_share_anims_kb;
		Cvar_RegisterVariable( &wii_cs_sharemodels );
		Cvar_RegisterVariable( &wii_cs_modelreserve );
		Cvar_RegisterVariable( &wii_cs_modelfuture );
		Cvar_RegisterVariable( &wii_lazy_models );
		Cvar_RegisterVariable( &wii_lazy_kb );
		Cvar_RegisterVariable( &wii_cs_ctmodel );
		Cvar_RegisterVariable( &wii_cs_tmodel );
		Cvar_RegisterVariable( &wii_share_anims );
		Cvar_RegisterVariable( &wii_share_anims_kb );
		Cvar_RegisterVariable( &wii_preload_anims );
		Cvar_RegisterVariable( &wii_preload_kb );
		Cvar_RegisterVariable( &wii_preload_reserve );
		Cmd_AddCommand( "wii_mapcrc", Mod_MapCRC_f, "print the multiplayer CRC of a map, read two different ways" );
	}
#endif

	Mod_ResetStudioAPI ();
	Mod_InitStudioHull ();
}

/*
================
Mod_FreeAll
================
*/
void Mod_FreeAll( void )
{
#if !XASH_DEDICATED
	Mod_ReleaseHullPolygons();
#endif
	for( int i = 0; i < mod_numknown; i++ )
		Mod_FreeModel( &mod_known[i] );
	mod_numknown = 0;
}

/*
================
Mod_ClearUserData
================
*/
void Mod_ClearUserData( void )
{
	for( int i = 0; i < mod_numknown; i++ )
		Mod_FreeUserData( &mod_known[i] );
}

/*
================
Mod_Shutdown
================
*/
void Mod_Shutdown( void )
{
	Mod_FreeAll();
#if XASH_OGC
	{
		extern void Mod_StudioShareShutdown( void );
		Mod_StudioShareShutdown();
	}
#endif
	Mem_FreePool( &com_studiocache );
}

/*
===============================================================================

			MODELS MANAGEMENT

===============================================================================
*/
#if XASH_OGC
/*
==================
Mod_ShareName

Counter-Strike has nine player models (and models/player.mdl, which the server uses for hitboxes)
of about 2 MB each: they have the same skeleton and sequences and differ in the geometry and the
skin. Loading all of them took ~20 MB of the Wii's heap. Their animations are shared (see
Mod_StudioShareAnims in mod_studio.c) so every extra character is cheap and all of them can be
loaded. This is the safety net for when there is not even that memory: a model that is asked for
with too little free memory is loaded as the model of its team (wii_cs_ctmodel / wii_cs_tmodel),
as models/player.mdl is loaded as the CT one. cstrike-psp does this with every model.

wii_cs_sharemodels: 0 = every model on its own (original behaviour), 1 = one per team,
2 = one for everybody, 3 = every model on its own while there are wii_cs_modelreserve KB free
(the decision is made once per model). Only Counter-Strike (gamedir cstrike/czero) and only when
the model that replaces the requested one exists.
==================
*/
CVAR_DEFINE_AUTO( wii_cs_sharemodels, "3", FCVAR_ARCHIVE, "Wii: player models of CS 1.6: 0 = all of them, 1 = one per team, 2 = one for everybody, 3 = all of them while there is memory (wii_cs_modelreserve)" );
CVAR_DEFINE_AUTO( wii_cs_modelreserve, "16384", FCVAR_ARCHIVE, "Wii: free KB that must be left after a player model that stays on its own (wii_cs_sharemodels 3)" );
CVAR_DEFINE_AUTO( wii_cs_modelfuture, "12288", FCVAR_ARCHIVE, "Wii: KB that still load after the player models (client textures and entities, third person weapons...). With wii_mem_budget_kb it decides how many player models stay on their own (wii_cs_sharemodels 3)" );

// KB of heap that the last player model took (measured in Mod_LoadModel): with the animations shared it
// is a fraction of the 2 MB of the file, without them it is all of it, and the policy below follows
size_t mod_player_cost_kb;

extern convar_t wii_mem_budget_kb;
CVAR_DEFINE_AUTO( wii_cs_ctmodel, "gign", FCVAR_ARCHIVE, "Wii: the model that every CT player uses when models are shared (gign, gsg9, sas, urban)" );
CVAR_DEFINE_AUTO( wii_cs_tmodel, "terror", FCVAR_ARCHIVE, "Wii: the model that every terrorist uses when models are shared (terror, leet, arctic, guerilla)" );

extern size_t Mod_FreeMemoryKB( void );

static const char *const mod_cs_ct[] = { "gign", "gsg9", "sas", "urban", "vip", NULL };
static const char *const mod_cs_t[] = { "arctic", "guerilla", "leet", "terror", NULL };

static qboolean Mod_NameInList( const char *name, size_t len, const char *const *list )
{
	for( ; *list; list++ )
	{
		if( Q_strlen( *list ) == len && !Q_strnicmp( *list, name, len ))
			return true;
	}

	return false;
}

static const char *Mod_ShareName( const char *filename, char *out, size_t outsize )
{
	const int level = (int)wii_cs_sharemodels.value;
	static char seen[32][MAX_QPATH];
	static qboolean alias[32];
	static int numseen;
	qboolean ct, useout;
	const char *want;
	int i;

	if( level <= 0 || !filename )
		return filename;

	if( Q_strnicmp( GI->gamefolder, "cstrike", 7 ) && Q_strnicmp( GI->gamefolder, "czero", 5 ))
		return filename;

	if( !Q_stricmp( filename, "models/player.mdl" ))
	{
		ct = true;
	}
	else if( !Q_strnicmp( filename, "models/player/", 14 ))
	{
		// models/player/<name>/<name>.mdl
		const char *dir = filename + 14, *slash = Q_strchr( dir, '/' );
		size_t len;

		if( !slash )
			return filename;

		len = slash - dir;

		if( Q_strnicmp( slash + 1, dir, len ) || Q_stricmp( slash + 1 + len, ".mdl" ))
			return filename;

		if( Mod_NameInList( dir, len, mod_cs_ct ))
			ct = true;
		else if( Mod_NameInList( dir, len, mod_cs_t ))
			ct = false;
		else
			return filename; // custom model, leave it alone
	}
	else
	{
		return filename;
	}

	want = ( ct || level == 2 ) ? wii_cs_ctmodel.string : wii_cs_tmodel.string;

	if( COM_StringEmptyOrNULL( want ))
		return filename;

	Q_snprintf( out, outsize, "models/player/%s/%s.mdl", want, want );

	if( !Q_stricmp( out, filename ) || !FS_FileExists( out, false ))
		return filename;

	for( i = 0; i < numseen; i++ )
	{
		if( !Q_stricmp( seen[i], filename ))
			return alias[i] ? out : filename;
	}

	// no room to remember the decision: a model that is not in the table is never replaced in "auto"
	if( numseen >= ARRAYSIZE( seen ))
		return ( level >= 3 ) ? filename : out;

	// first time this model is asked for: decide now and do not change it in the middle of a level
	if( level >= 3 )
	{
		// a model costs what the last one cost (until there is one, the 2 MB of the file); it stays on its
		// own while the engine would still be inside its budget once everything that loads later is in
		// (wii_cs_modelfuture), and while it leaves the free memory above wii_cs_modelreserve
		const size_t cost = mod_player_cost_kb ? mod_player_cost_kb : 2300;
		const size_t budget = (size_t)Q_max( 0.0f, wii_mem_budget_kb.value );
		const size_t future = (size_t)Q_max( 0.0f, wii_cs_modelfuture.value );
		const size_t reserve = (size_t)Q_max( 0.0f, wii_cs_modelreserve.value );

		useout = ( budget && Mem_HeapUsedKB() + cost + future > budget ) || Mod_FreeMemoryKB() < reserve + cost;
	}
	else useout = true;

	Q_strncpy( seen[numseen], filename, sizeof( seen[0] ));
	alias[numseen++] = useout;

	Con_Printf( "[SHARE] %s %s %s (free %i KB, engine heap %i KB, a model costs %i KB)\n", filename, useout ? "->" : "kept, not",
		out, (int)Mod_FreeMemoryKB(), (int)Mem_HeapUsedKB(), (int)( mod_player_cost_kb ? mod_player_cost_kb : 2300 ));

	return useout ? out : filename;
}
#endif

#if XASH_OGC
// the model is loaded as another one (see above): there is nothing to compare its file with
qboolean Mod_IsShareAlias( const char *name )
{
	char shared[MAX_QPATH];

	return Mod_ShareName( name, shared, sizeof( shared )) != name;
}
#endif

/*
==================
Mod_FindName

never return NULL
==================
*/
model_t *Mod_FindName( const char *filename, qboolean trackCRC )
{
	char	modname[MAX_QPATH];
	model_t	*mod;
	int	i;

#if XASH_OGC
	{
		char shared[MAX_QPATH];

		filename = Mod_ShareName( filename, shared, sizeof( shared ));
		Q_strncpy( modname, filename, sizeof( modname ));
	}
#else
	Q_strncpy( modname, filename, sizeof( modname ));
#endif

	// search the currently loaded models
	for( i = 0, mod = mod_known; i < mod_numknown; i++, mod++ )
	{
		if( !Q_stricmp( mod->name, modname ))
		{
			if( mod->mempool || mod->name[0] == '*' )
				mod->needload = NL_PRESENT;
			else
				mod->needload = NL_NEEDS_LOADED;

			return mod;
		}
	}

	// find a free model slot spot
	for( i = 0, mod = mod_known; i < mod_numknown; i++, mod++ )
	{
		if( mod->needload == NL_UNREFERENCED )
			break; // this is a valid spot
	}

	if( i == mod_numknown )
	{
		if( mod_numknown == MAX_MODELS )
			Host_Error( "MAX_MODELS limit exceeded (%d)\n", MAX_MODELS );
		mod_numknown++;
	}

	// copy name, so model loader can find model file
	Q_strncpy( mod->name, modname, sizeof( mod->name ));
	if( trackCRC ) mod_crcinfo[i].flags = FCRC_SHOULD_CHECKSUM;
	else mod_crcinfo[i].flags = 0;
	mod->needload = NL_NEEDS_LOADED;
	mod_crcinfo[i].initialCRC = 0;

	return mod;
}

/*
==================
Mod_LoadModel

Loads a model into the cache
==================
*/
static model_t *Mod_LoadModel( model_t *mod, qboolean crash )
{
	char		tempname[MAX_QPATH];
	fs_offset_t		length = 0;
	qboolean		loaded, loaded2 = false;
	uint32_t		diskCRC = 0;

	if( !mod )
	{
		Host_Error( "%s: mod == NULL\n", __func__ );
		return NULL;
	}

	// check if already loaded (or inline bmodel)
	if( mod->mempool || mod->name[0] == '*' )
	{
		mod->needload = NL_PRESENT;
#if XASH_OGC
		{
			// el pool de secuencias se vacia al cambiar de nivel: volver a precargarlas
			extern void Mod_RefreshSeqGroups( model_t *mod );
			Mod_RefreshSeqGroups( mod );
		}
#endif
		return mod;
	}

	if( mod->needload != NL_NEEDS_LOADED )
	{
		Host_Error( "%s: trying to load model not marked for loading (%d)\n", __func__, mod->needload );
		return NULL;
	}

	// store modelname to show error
	Q_strncpy( tempname, mod->name, sizeof( tempname ));
	COM_FixSlashes( tempname );

#if XASH_OGC
	const size_t heap0 = Mem_HeapUsedKB();
#endif

	byte *buf = FS_LoadFile( tempname, &length, false );

	if( !buf || length < sizeof( uint ))
	{
		memset( mod, 0, sizeof( model_t ));

		if( crash ) Host_Error( "Could not load model %s from disk\n", tempname );
		else Con_Printf( S_ERROR "Could not load model %s from disk\n", tempname );

		return NULL;
	}

	Con_Reportf( "loading %s\n", mod->name );
	mod->needload = NL_PRESENT;
	mod->type = mod_bad;

	// the CRC is the one of the file as it is on the disk: the loaders swap the buffer in place on a
	// big endian CPU (Wii), the CRC of the buffer afterwards is not the one of the file and the
	// consistency check of the servers (CS 1.6) rejected every model that was loaded
	if( FBitSet( mod_crcinfo[mod - mod_known].flags, FCRC_SHOULD_CHECKSUM ))
	{
		CRC32_Init( &diskCRC );
		CRC32_ProcessBuffer( &diskCRC, buf, length );
		diskCRC = CRC32_Final( diskCRC );
	}

	// call the apropriate loader
	switch( *(uint *)buf )
	{
	case LittleLong( IDSTUDIOHEADER ):
		Mod_LoadStudioModel( mod, buf, length, &loaded );
		break;
	case LittleLong( IDSPRITEHEADER ):
		Mod_LoadSpriteModel( mod, buf, length, &loaded );
		break;
	case LittleLong( IDALIASHEADER ):
		Mod_LoadAliasModel( mod, buf, &loaded );
		break;
	case LittleLong( Q1BSP_VERSION ):
	case LittleLong( HLBSP_VERSION ):
	case LittleLong( QBSP2_VERSION ):
		Mod_LoadBrushModel( mod, buf, length, &loaded );
		break;
	default:
		Mem_Free( buf );
		if( crash ) Host_Error( "%s has unknown format\n", tempname );
		else Con_Printf( S_ERROR "%s has unknown format\n", tempname );
		return NULL;
	}

	if( loaded )
	{
		if( world.loading )
			SetBits( mod->flags, MODEL_WORLD ); // mark worldmodel

		if( Host_IsDedicated() )
		{
			if( svgame.physFuncs.Mod_ProcessUserData != NULL )
			{
				// let the server.dll load custom data
				svgame.physFuncs.Mod_ProcessUserData( mod, true, buf );
			}
			loaded2 = true;
		}
#if !XASH_DEDICATED
		else
		{
			loaded2 = ref.dllFuncs.Mod_ProcessRenderData( mod, true, buf, length );
		}
#endif
	}

	if( mod->type == mod_alias )
	{
		aliashdr_t *hdr = mod->cache.data;
		if( hdr ) // clean up temporary pointer after passing the alias model to the renderer
			hdr->pposeverts = NULL;
	}

	if( !loaded || !loaded2 )
	{
		Mod_FreeModel( mod );
		Mem_Free( buf );

		if( crash ) Host_Error( "Could not load model %s\n", tempname );
		else Con_Printf( S_ERROR "Could not load model %s\n", tempname );

		return NULL;
	}

	model_info_t *p = &mod_crcinfo[mod - mod_known];
	mod->needload = NL_PRESENT;

	if( FBitSet( p->flags, FCRC_SHOULD_CHECKSUM ))
	{
		const uint32_t currentCRC = diskCRC;

		if( FBitSet( p->flags, FCRC_CHECKSUM_DONE ))
		{
			if( currentCRC != p->initialCRC )
				Host_Error( "%s has a bad checksum\n", tempname );
		}
		else
		{
			SetBits( p->flags, FCRC_CHECKSUM_DONE );
			p->initialCRC = currentCRC;
		}
	}
	Mem_Free( buf );

#if XASH_OGC
	// what a player model really costs (see Mod_ShareName): the heap after minus the heap before, file buffer gone
	if( !Q_strnicmp( mod->name, "models/player", 13 ))
	{
		const size_t heap1 = Mem_HeapUsedKB();

		mod_player_cost_kb = ( heap1 > heap0 ) ? Q_max( heap1 - heap0, (size_t)64 ) : 64;
		Con_Printf( "[MEM] %s took %i KB of heap\n", mod->name, (int)mod_player_cost_kb );
	}
#endif

	return mod;
}

/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
model_t *Mod_ForName( const char *name, qboolean crash, qboolean trackCRC )
{
    model_t *mod;

    if( COM_StringEmptyOrNULL( name ))
        return NULL;

    mod = Mod_FindName( name, trackCRC );

    if( !mod )
        return NULL;

#if XASH_OGC
    double t0 = Platform_DoubleTime();
    model_t *ret = Mod_LoadModel( mod, crash );

    OGC_ProfAdd( 10, Platform_DoubleTime() - t0 );
    OGC_ProfNote( 'M', mod->name, Platform_DoubleTime() - t0 );
    return ret;
#else
    return Mod_LoadModel( mod, crash );
#endif
}

#if XASH_OGC
/*
==================
Weapon models on demand (wii_lazy_models)

Counter-Strike has about thirty weapons and the game precaches the first person (v_), third person
(p_) and shield models of all of them on every map: 10 to 13 MB of the heap of the Wii for models of
which a player sees two or three at a time. With wii_lazy_models the server and the client only
register them (name, slot and CRC flag: Mod_LazyRegister) and the model is loaded when it is drawn
(Mod_LazyTouch, from CL_WiiLazyModel). The models that are loaded stay up to wii_lazy_kb KB of heap:
after each load the ones that were drawn the longest time ago are freed (the slot and the name stay,
so they come back when they are needed again). The server never loads them: it only needs the index.
==================
*/
CVAR_DEFINE_AUTO( wii_lazy_models, "1", FCVAR_ARCHIVE, "Wii: weapon models of CS 1.6 (v_, p_ and shields) are loaded when they are drawn and the ones that are not used are freed (0 = all of them at the start)" );
CVAR_DEFINE_AUTO( wii_lazy_kb, "3072", FCVAR_ARCHIVE, "Wii: KB of heap that the weapon models loaded on demand may use before the ones that were drawn the longest time ago are freed (wii_lazy_models)" );

#define MOD_LAZY_MAX	48	// loaded weapon models that are tracked: more than can be on the screen
#define MOD_LAZY_HOT	3	// frames in which a model that was drawn is not freed (it may still be in use)

static struct
{
	model_t	*mod;
	uint	frame;	// host.framecount of the last time that it was asked for
	size_t	kb;	// KB of heap that it took when it was loaded
} mod_lazy[MOD_LAZY_MAX];

static qboolean Mod_IsCounterStrike( void )
{
	return !Q_strnicmp( GI->gamefolder, "cstrike", 7 ) || !Q_strnicmp( GI->gamefolder, "czero", 5 );
}

// the name of a model that is drawn by the weapons of the players and nothing else
static qboolean Mod_IsLazyName( const char *name )
{
	if( !name || Q_strnicmp( name, "models/", 7 ))
		return false;

	name += 7;

	if(( name[0] == 'v' || name[0] == 'V' || name[0] == 'p' || name[0] == 'P' ) && name[1] == '_' )
		return true;

	return !Q_strnicmp( name, "shield/", 7 );
}

/*
==================
Mod_LazyWanted

true if the model is going to be loaded when it is drawn instead of when it is precached. A file that
does not exist takes the normal way (with its error).
==================
*/
qboolean Mod_LazyWanted( const char *name )
{
	if( wii_lazy_models.value <= 0.0f || !Mod_IsLazyName( name ) || !Mod_IsCounterStrike( ))
		return false;

	return FS_FileExists( name, false ) != 0;
}

/*
==================
Mod_LazyRegister

the model has a slot, a name and a CRC flag but it is not loaded
==================
*/
model_t *Mod_LazyRegister( const char *name )
{
	model_t *mod = Mod_FindName( name, true );

	// never loaded: a zeroed slot looks like a brush model (type 0) to the code that scans the models
	if( mod && !mod->mempool && mod->name[0] != '*' )
		mod->type = mod_bad;

	return mod;
}

static int Mod_LazyFind( const model_t *mod )
{
	for( int i = 0; i < MOD_LAZY_MAX; i++ )
	{
		if( mod_lazy[i].mod == mod )
			return i;
	}

	return -1;
}

// the slot of the table that still holds a loaded weapon model, else it is released
static qboolean Mod_LazyLive( int i )
{
	const model_t *mod = mod_lazy[i].mod;

	if( !mod )
		return false;

	if( mod->mempool && Mod_IsLazyName( mod->name ))
		return true;

	mod_lazy[i].mod = NULL; // freed with the level, or its slot was reused
	return false;
}

// frees the heap of a model but not its slot, its name or its CRC: it is registered again
static void Mod_LazyUnload( int i )
{
	model_t *mod = mod_lazy[i].mod;
	const int kb = (int)mod_lazy[i].kb;
	char name[MAX_QPATH];

	mod_lazy[i].mod = NULL; // first: the print below may need memory, and the allocator may call Mod_LazyFlush

	Q_strncpy( name, mod->name, sizeof( name ));
	Mod_FreeModel( mod );
	Q_strncpy( mod->name, name, sizeof( mod->name ));
	mod->needload = NL_NEEDS_LOADED;
	mod->type = mod_bad;

	Con_Printf( "[LAZY] free %s (%i KB)\n", name, kb );
}

// the model that was drawn the longest time ago and is not in use now, -1 if all of them are
static int Mod_LazyOldest( const model_t *keep )
{
	int oldest = -1;

	for( int i = 0; i < MOD_LAZY_MAX; i++ )
	{
		if( !Mod_LazyLive( i ) || mod_lazy[i].mod == keep )
			continue;

		if( host.framecount - mod_lazy[i].frame < MOD_LAZY_HOT )
			continue;

		if( oldest < 0 || mod_lazy[i].frame < mod_lazy[oldest].frame )
			oldest = i;
	}

	return oldest;
}

// keeps the heap of the weapon models under wii_lazy_kb by freeing the ones that are not used
static void Mod_LazyTrim( const model_t *keep )
{
	const size_t budget = (size_t)Q_max( 256.0f, wii_lazy_kb.value );

	for( ;; )
	{
		size_t used = 0;
		int oldest;

		for( int i = 0; i < MOD_LAZY_MAX; i++ )
		{
			if( Mod_LazyLive( i ))
				used += mod_lazy[i].kb;
		}

		if( used <= budget || ( oldest = Mod_LazyOldest( keep )) < 0 )
			break;

		Mod_LazyUnload( oldest );
	}
}

/*
==================
Mod_LazyFlush

frees every weapon model that is not in use: the last resort before an allocation fails. Returns
the KB that were freed.
==================
*/
size_t Mod_LazyFlush( void )
{
	size_t freed = 0;
	int i;

	while(( i = Mod_LazyOldest( NULL )) >= 0 )
	{
		freed += mod_lazy[i].kb;
		Mod_LazyUnload( i );
	}

	return freed;
}

// a free entry of the table: the oldest model is freed if there is none
static int Mod_LazySlot( void )
{
	int i, oldest;

	for( i = 0; i < MOD_LAZY_MAX; i++ )
	{
		if( !Mod_LazyLive( i ))
			return i;
	}

	if(( oldest = Mod_LazyOldest( NULL )) < 0 )
		return -1;

	Mod_LazyUnload( oldest );
	return oldest;
}

/*
==================
Mod_LazyTouch

the model is going to be drawn: loads it if it is not loaded and marks it as in use. NULL if it
cannot be loaded (its slot is then free: the caller forgets the pointer).
==================
*/
model_t *Mod_LazyTouch( model_t *mod )
{
	char name[MAX_QPATH];
	size_t heap0, kb;
	double t0;
	int i;

	// the other games load their weapons as always: only a model that Mod_LazyRegister left unloaded
	// (CS) is loaded here, and the ones that are loaded join the table
	if( !mod || mod->name[0] == '*' || !Mod_IsLazyName( mod->name ) || !Mod_IsCounterStrike( ))
		return mod;

	if( mod->mempool )
	{
		// a model that was loaded some other way joins the table
		if(( i = Mod_LazyFind( mod )) < 0 && ( i = Mod_LazySlot( )) >= 0 )
		{
			mod_lazy[i].mod = mod;
			mod_lazy[i].kb = 0;
		}

		if( i >= 0 )
			mod_lazy[i].frame = host.framecount;

		return mod;
	}

	if( mod->needload != NL_NEEDS_LOADED )
		return NULL; // freed with the level

	Q_strncpy( name, mod->name, sizeof( name ));
	heap0 = Mem_HeapUsedKB();
	t0 = Platform_DoubleTime();

	if( !Mod_LoadModel( mod, false ))
	{
		Con_Printf( S_ERROR "[LAZY] %s cannot be loaded\n", name );
		return NULL;
	}

	kb = Mem_HeapUsedKB();
	kb = ( kb > heap0 ) ? Q_max( kb - heap0, (size_t)16 ) : 16;

	if(( i = Mod_LazySlot( )) >= 0 )
	{
		mod_lazy[i].mod = mod;
		mod_lazy[i].frame = host.framecount;
		mod_lazy[i].kb = kb;
	}

	Con_Printf( "[LAZY] load %s: %i KB, %i ms\n", name, (int)kb, (int)(( Platform_DoubleTime() - t0 ) * 1000.0 ));
	Mod_LazyTrim( mod );

	return mod;
}
#endif

/*
==================
Mod_PurgeStudioCache

free studio cache on change level
==================
*/
static void Mod_PurgeStudioCache( void )
{
	// refresh hull data
	SetBits( r_showhull.flags, FCVAR_CHANGED );
#if !XASH_DEDICATED
	Mod_ReleaseHullPolygons();
#endif
	// release previois map
	Mod_FreeModel( mod_known );	// world is stuck on slot #0 always

	// we should release all the world submodels
	// and clear studio sequences
	for( int i = 1; i < mod_numknown; i++ )
	{
		if( mod_known[i].needload == NL_UNREFERENCED )
			continue;

		if( mod_known[i].type == mod_studio )
			mod_known[i].submodels = NULL;

		if( mod_known[i].name[0] == '*' )
			Mod_FreeModel( &mod_known[i] );
		else
			mod_known[i].needload = NL_FREE_UNUSED;
	}

	Mem_EmptyPool( com_studiocache );
	Mod_ClearStudioCache();
#if XASH_OGC
	{
		extern void Mod_ResetPreloadBudget( void );
		Mod_ResetPreloadBudget();
	}
#endif
}

/*
==================
Mod_LoadWorld

Loads in the map and all submodels
==================
*/
model_t *Mod_LoadWorld( const char *name, qboolean preload )
{
	// already loaded?
	if( !Q_stricmp( mod_known->name, name ))
		return mod_known;

	// free sequence files on studiomodels
	Mod_PurgeStudioCache();

	// load the newmap
	world.loading = true;
	model_t *pworld = Mod_FindName( name, false );
	if( preload ) Mod_LoadModel( pworld, true );
	world.loading = false;

	ASSERT( pworld == mod_known );

	return pworld;
}

/*
==================
Mod_FreeUnused

Purge all unused models
==================
*/
void Mod_FreeUnused( void )
{
	model_t	*mod;
	int	i;

	// never tries to release worldmodel
	for( i = 1, mod = &mod_known[1]; i < mod_numknown; i++, mod++ )
	{
		if( mod->needload == NL_FREE_UNUSED )
			Mod_FreeModel( mod );
	}
}

/*
===============================================================================

			MODEL ROUTINES

===============================================================================
*/
/*
===============
Mod_Calloc

===============
*/
void *GAME_EXPORT Mod_Calloc( int number, size_t size )
{
	if( number <= 0 || size <= 0 )
		return NULL;

	cache_user_t *cu = (cache_user_t *)Mem_Calloc( com_studiocache, sizeof( cache_user_t ) + number * size );
	cu->data = (void *)cu; // make sure that cu->data is not NULL

	return cu;
}

/*
===============
Mod_CacheCheck

===============
*/
void *GAME_EXPORT Mod_CacheCheck( cache_user_t *c )
{
	if( !c->data )
		return NULL;

	if( !Mem_IsAllocatedExt( com_studiocache, c->data ))
		return NULL;

	return c->data;
}

/*
===============
Mod_LoadCacheFile

===============
*/
void GAME_EXPORT Mod_LoadCacheFile( const char *filename, cache_user_t *cu )
{
	char	modname[MAX_QPATH];
	fs_offset_t	size;

	Assert( cu != NULL );

	if( COM_StringEmptyOrNULL( filename ))
		return;

	Q_strncpy( modname, filename, sizeof( modname ));
	COM_FixSlashes( modname );

	byte *buf = FS_LoadFile( modname, &size, false );
	if( !buf || !size ) Host_Error( "LoadCacheFile: ^1can't load %s^7\n", filename );
	cu->data = Mem_Malloc( com_studiocache, size );
	memcpy( cu->data, buf, size );
	Mem_Free( buf );

	// this handles when studio model renderer tries to load sequence files on it's own
	// which is what they always do in HLSDK
#if XASH_BIG_ENDIAN
	if( size >= sizeof( int ) && LittleLong( IDSEQGRPHEADER ) == *(uint *)cu->data )
	{
		studiohdr_t *phdr = (studiohdr_t *)REF_GET_PARM( PARM_GET_STUDIO_HDR, 0 );
		if( !phdr )
			return;

		mstudioseqdesc_t *pseq = (mstudioseqdesc_t *)((byte *)phdr + phdr->seqindex );

		for( int i = 0; i < phdr->numseq; i++ )
		{
			if( pseq[i].seqgroup == 0 )
				continue;

			mstudioseqgroup_t *pgrp = (mstudioseqgroup_t *)((byte *)phdr + phdr->seqgroupindex ) + pseq[i].seqgroup;

			// assuming filename passes seqgroup's name
			if( !Q_stricmp( pgrp->name, filename ))
				Mod_SwapStudioSeqGroupAnims( phdr, &pseq[i], (byte *)cu->data, (size_t)size );
		}
	}
#endif
}

/*
==================
Mod_ValidateCRC

==================
*/
qboolean Mod_ValidateCRC( const char *name, uint32_t crc )
{
	model_t *mod;
	model_info_t *p;

#if XASH_OGC
	// a model that is loaded as another one (Mod_ShareName) is never loaded under its own name:
	// there is no CRC of it to compare, and the one of the replacement is not its CRC
	if( Mod_IsShareAlias( name ))
		return true;
#endif

	mod = Mod_FindName( name, true );
	p = &mod_crcinfo[mod - mod_known];

	if( !FBitSet( p->flags, FCRC_CHECKSUM_DONE ))
		return true;
	if( p->initialCRC == crc )
		return true;
	return false;
}

/*
==================
Mod_NeedCRC

==================
*/
void Mod_NeedCRC( const char *name, qboolean needCRC )
{
	model_t *mod = Mod_FindName( name, true );
	model_info_t *p = &mod_crcinfo[mod - mod_known];

	if( needCRC ) SetBits( p->flags, FCRC_SHOULD_CHECKSUM );
	else ClearBits( p->flags, FCRC_SHOULD_CHECKSUM );
}

#if XASH_LLVM_LIBFUZZER

static const uint8_t *fuzz_data;
static size_t fuzz_size;

static byte *Fuzz_LoadFile( const char *path, fs_offset_t *filesizeptr, qboolean gamedironly )
{
	byte *buf = Mem_Malloc( host.mempool, fuzz_size );
	memcpy( buf, fuzz_data, fuzz_size );
	*filesizeptr = fuzz_size;
	return buf;
}

int EXPORT Fuzz_Mod_LoadModel( const uint8_t *Data, size_t Size );
int EXPORT Fuzz_Mod_LoadModel( const uint8_t *Data, size_t Size )
{
	model_t mod = { .name = "test", .needload = NL_NEEDS_LOADED };

	Memory_Init();

	host.type = HOST_DEDICATED;
	host.mempool = Mem_AllocPool( "fuzzing pool" );
	fuzz_data = Data;
	fuzz_size = Size;
	refState.draw_surfaces = NULL;

	g_fsapi.LoadFile = Fuzz_LoadFile;

	if( Mod_LoadModel( &mod, false ) && mod.mempool )
		Mem_FreePool( &mod.mempool );

	Mem_FreePool( &host.mempool );

	return 0;
}

#endif // XASH_LLVM_LIBFUZZER
