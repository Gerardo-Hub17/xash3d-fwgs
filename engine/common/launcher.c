/*
launcher.c - direct xash3d launcher
Copyright (C) 2015 Mittorn

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#if XASH_ENABLE_MAIN
#if XASH_OGC
#include <gccore.h>
#include <ogc/system.h>
#include <ogc/video.h>
#endif
#include "build.h"
#include "common.h"
#include "platform/platform.h"

#if XASH_SDLMAIN
#include <SDL.h>
#endif

#ifndef XASH_GAMEDIR
#define XASH_GAMEDIR "valve" // !!! Replace with your default (base) game directory !!!
#endif

#if XASH_OGC
/* Flash de color en pantalla antes de tener devoptabs. */
static void OGC_ColorFlash( u32 color, int vsync_count )
{
    VIDEO_Init();
    GXRModeObj *rmode = VIDEO_GetPreferredMode( NULL );
    if( !rmode ) return;

    VIDEO_Configure( rmode );
    void *xfb = MEM_K0_TO_K1( SYS_AllocateFramebuffer( rmode ));
    if( !xfb ) return;

    VIDEO_ClearFrameBuffer( rmode, xfb, color );
    VIDEO_SetNextFramebuffer( xfb );
    VIDEO_SetBlack( FALSE );
    VIDEO_Flush();
    VIDEO_WaitVSync();
    VIDEO_WaitVSync();

    for( int i = 0; i < vsync_count; i++ )
        VIDEO_WaitVSync();
}
#endif

static int  szArgc;
static char **szArgv;

static void Sys_ChangeGame( const char *progname )
{
	// stub
}

int main( int argc, char **argv )
{
#if XASH_OGC
    /* ============================================================
     * DIAGNÓSTICO: flashes de color para ubicar el crash.
     *   ROJO  -> main() arrancó
     *   VERDE -> llegamos justo antes de OGC_EarlyInit()
     *   NEGRO persistente después de un color -> crashea tras ese punto
     *   NEGRO desde el principio -> crashea ANTES de main()
     * ============================================================ */
    OGC_ColorFlash( COLOR_RED, 60 );

    // bring up the debug console before anything can crash
    OGC_ColorFlash( COLOR_GREEN, 60 );
    OGC_EarlyInit();
#endif

#if XASH_PSVITA
    // inject -dev -console into args if required
    szArgc = PSVita_GetArgv( argc, argv, &szArgv );
#elif XASH_IOS
    IOS_LaunchDialog();
    szArgc = IOS_GetArgs( &szArgv );
#else
    szArgc = argc;
    szArgv = argv;
#endif // XASH_PSVITA
    return Host_Main( szArgc, szArgv, XASH_GAMEDIR, 0, Sys_ChangeGame );
}

#endif // XASH_ENABLE_MAIN
