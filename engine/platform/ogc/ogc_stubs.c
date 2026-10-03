/*
ogc_stubs.c - Stubs para símbolos que vid_sdl2.c proveía.
Necesarios porque excluimos vid_sdl2.c del build de Wii (ver CMakeLists),
pero el engine todavía referencia estas funciones desde otros archivos.
*/

#include "platform/platform.h"
#include "common.h"

#if XASH_VIDEO == VIDEO_OGC

/* === GL API (esperado por ref_common / cl_view) === */

void *GL_GetProcAddress( const char *name )
{
    (void)name;
    return NULL;
}

void GL_UpdateSwapInterval( void )
{
    /* GX está bloqueado a la tasa de refresco del VI, no hay nada que hacer. */
}

extern void GX_Present( void );

void GL_SwapBuffers( void )
{
    GX_Present();
}

int GL_GetAttribute( int attr, int *val )
{
    (void)attr;
    (void)val;
    return 0;
}

int GL_SetAttribute( int attr, int val )
{
    (void)attr;
    (void)val;
    return 0;
}

int GL_SetAttributes( int attr, int val )   /* alias por si algún módulo usa el plural */
{
    (void)attr;
    (void)val;
    return 0;
}

/* === Platform helpers (esperado por host / sys / in_gyro) === */

void Platform_Minimize_f( void )
{
    /* Wii no tiene window manager. */
}

platform_orientation_t Platform_GetDisplayOrientation( void )
{
    /* Wii siempre está en landscape (640x480 o 320x240 horizontales). */
    return (platform_orientation_t)0;
}

ref_window_type_t R_GetWindowHandle( void **handle, ref_window_type_t type )
{
    (void)type;
    if( handle )
        *handle = NULL;
    return (ref_window_type_t)0;
}

void VID_SaveWindowSize( int width, int height )
{
    (void)width;
    (void)height;
}

#endif /* XASH_VIDEO == VIDEO_OGC */
